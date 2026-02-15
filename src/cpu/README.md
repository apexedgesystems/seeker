# CPU Diagnostics Module

**Namespace:** `seeker::cpu`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive CPU telemetry for real-time and performance-critical systems. This module provides 11 focused components for monitoring CPU state, topology, utilization, interrupts, isolation, and thermal conditions.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [Affinity](#affinity) - Thread CPU pinning
   - [CpuFeatures](#cpufeatures) - ISA capability detection
   - [CpuFreq](#cpufreq) - Frequency and governor state
   - [CpuIdle](#cpuidle) - C-state residency
   - [CpuIsolation](#cpuisolation) - RT isolation configuration
   - [CpuStats](#cpustats) - Basic system health
   - [CpuTopology](#cputopology) - Socket/core/thread layout
   - [CpuUtilization](#cpuutilization) - Per-core busy/idle percentages
   - [IrqStats](#irqstats) - Hardware interrupt distribution
   - [SoftirqStats](#softirqstats) - Software interrupt distribution
   - [ThermalStatus](#thermalstatus) - Temperature and throttling
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT System Health Check](#example-rt-system-health-check)

---

## Overview

The CPU diagnostics module answers these questions for RT and HPC systems:

| Question                                                            | Module           |
| ------------------------------------------------------------------- | ---------------- |
| Which cores can my thread run on?                                   | `Affinity`       |
| What's the CPU topology (sockets, cores, caches)?                   | `CpuTopology`    |
| What ISA extensions are available (AVX, etc.)?                      | `CpuFeatures`    |
| What's the current clock speed and governor?                        | `CpuFreq`        |
| Is the CPU throttling (thermal or power)?                           | `ThermalStatus`  |
| Basic system health (RAM, load, uptime)?                            | `CpuStats`       |
| Which cores are busy right now?                                     | `CpuUtilization` |
| Are hardware interrupts hitting my RT cores?                        | `IrqStats`       |
| Are software interrupts causing jitter?                             | `SoftirqStats`   |
| Are deep C-states adding wake latency?                              | `CpuIdle`        |
| Are my RT cores properly isolated (isolcpus, nohz_full, rcu_nocbs)? | `CpuIsolation`   |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/cpu/inc/Affinity.hpp"
#include "src/cpu/inc/CpuFeatures.hpp"
#include "src/cpu/inc/CpuFreq.hpp"
#include "src/cpu/inc/CpuIdle.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/cpu/inc/CpuStats.hpp"
#include "src/cpu/inc/CpuTopology.hpp"
#include "src/cpu/inc/CpuUtilization.hpp"
#include "src/cpu/inc/IrqStats.hpp"
#include "src/cpu/inc/SoftirqStats.hpp"
#include "src/cpu/inc/ThermalStatus.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::cpu;

// Static system info (query once at startup)
auto topo = getCpuTopology();           // Sockets, cores, caches
auto feat = getCpuFeatures();           // ISA flags
auto affinity = getCurrentThreadAffinity();  // Current thread's allowed CPUs
auto isolation = getCpuIsolationConfig();    // isolcpus, nohz_full, rcu_nocbs

// Dynamic state (query periodically)
auto stats = getCpuStats();             // RAM, load, uptime
auto freq = getCpuFrequencySummary();   // Per-core MHz and governors
auto thermal = getThermalStatus();      // Temps, throttling, power limits
```

### Snapshot + Delta Pattern

For rate-based metrics (utilization, IRQs, softirqs, C-states):

```cpp
using namespace seeker::cpu;

// Take two snapshots with a delay
auto before = getCpuUtilizationSnapshot();
std::this_thread::sleep_for(std::chrono::milliseconds(100));
auto after = getCpuUtilizationSnapshot();

// Compute delta (percentages, rates)
auto delta = computeUtilizationDelta(before, after);

// Now delta.aggregate.idle gives you idle % over that 100ms window
```

---

## Design Principles

### RT-Safety Annotations

Every public function documents its RT-safety:

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for RT threads        |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

Example from header:

```cpp
/**
 * @brief Capture current CPU time counters from /proc/stat.
 * @note RT-safe: Single file read, no allocation, bounded parsing.
 */
[[nodiscard]] CpuUtilizationSnapshot getCpuUtilizationSnapshot() noexcept;
```

### Data Structure Strategy

Structs use a mix of fixed-size arrays (for RT-safe snapshots) and `std::vector` (where the data size varies with hardware). Modules that need RT-safe snapshot capture (CpuUtilization, IrqStats, SoftirqStats, CpuIdle) use fixed-size arrays with compile-time maximums. Modules that run during setup (CpuTopology, CpuFreq, ThermalStatus) use `std::vector` since they are NOT RT-safe anyway.

```cpp
// Fixed-size for RT-safe snapshots:
CpuTimeCounters perCore[MAX_CPUS]{};    // CpuUtilization
IrqLineStats lines[IRQ_MAX_LINES]{};    // IrqStats

// Vector-based for setup-time queries:
std::vector<CoreInfo> cores{};           // CpuTopology
std::vector<TemperatureSensor> sensors{}; // ThermalStatus
```

### Snapshot + Delta Separation

For counter-based metrics, we separate:

1. **Snapshot** - Raw counters (RT-safe, cheap)
2. **Delta computation** - Percentages/rates (RT-safe, pure function)
3. **toString()** - Human-readable output (NOT RT-safe, allocates)

This lets RT code collect snapshots in the hot path and defer formatting to cold paths.

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto snap = getCpuIdleSnapshot();
if (snap.cpuCount == 0) {
  // cpuidle not available on this system - handle gracefully
}
```

---

## Module Reference

---

### Affinity

**Header:** `Affinity.hpp`
**Purpose:** Query and set CPU affinity for the current thread.

#### Key Types

```cpp
/// Maximum supported CPU count (covers most systems; matches common CPU_SETSIZE).
inline constexpr std::size_t MAX_CPUS = 1024;

/// Set of logical CPU IDs
struct CpuSet {
  std::bitset<MAX_CPUS> mask{};

  [[nodiscard]] bool test(std::size_t cpuId) const noexcept;
  void set(std::size_t cpuId) noexcept;
  void clear(std::size_t cpuId) noexcept;
  void reset() noexcept;              // Clear all CPUs
  [[nodiscard]] std::size_t count() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::string toString() const;       // NOT RT-safe
};

enum class AffinityStatus : unsigned char {
  OK = 0,
  INVALID_ARGUMENT,
  SYSCALL_FAILED,
};

/// Human-readable status string.
/// @note NOT RT-safe: Returns static string pointer (safe) but intended for logging.
[[nodiscard]] const char* toString(AffinityStatus status) noexcept;
```

#### API

```cpp
/// Get current thread's CPU affinity (RT-safe)
[[nodiscard]] CpuSet getCurrentThreadAffinity() noexcept;

/// Set current thread's CPU affinity (RT-safe)
[[nodiscard]] AffinityStatus setCurrentThreadAffinity(const CpuSet& set) noexcept;

/// Get configured CPU count on the system (RT-safe)
[[nodiscard]] std::size_t getConfiguredCpuCount() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

// Check which CPUs this thread can run on
CpuSet allowed = getCurrentThreadAffinity();
fmt::print("Allowed CPUs: {}\n", allowed.toString());

// Pin to CPU 3 only
CpuSet pinned;
pinned.set(3);
if (setCurrentThreadAffinity(pinned) == AffinityStatus::OK) {
  // Now running exclusively on CPU 3
}
```

---

### CpuFeatures

**Header:** `CpuFeatures.hpp`
**Purpose:** Detect ISA extensions via CPUID (x86/x86_64).

#### Key Types

```cpp
/// Maximum vendor string length (12 chars from CPUID + null).
inline constexpr std::size_t VENDOR_STRING_SIZE = 13;

/// Maximum brand string length (48 chars from CPUID + null).
inline constexpr std::size_t BRAND_STRING_SIZE = 49;

struct CpuFeatures {
  // SIMD: SSE family
  bool sse{false};
  bool sse2{false};
  bool sse3{false};
  bool ssse3{false};
  bool sse41{false};
  bool sse42{false};

  // SIMD: AVX family
  bool avx{false};
  bool avx2{false};
  bool avx512f{false};
  bool avx512dq{false};
  bool avx512cd{false};
  bool avx512bw{false};
  bool avx512vl{false};

  // Math and bit manipulation
  bool fma{false};
  bool bmi1{false};
  bool bmi2{false};

  // Cryptography
  bool aes{false};
  bool sha{false};

  // Misc
  bool popcnt{false};
  bool rdrand{false};       // RDRAND instruction available
  bool rdseed{false};       // RDSEED instruction available
  bool invariantTsc{false}; // Invariant TSC (reliable for timing)

  // Identification (fixed-size, RT-safe)
  std::array<char, VENDOR_STRING_SIZE> vendor{}; // e.g., "GenuineIntel", "AuthenticAMD"
  std::array<char, BRAND_STRING_SIZE> brand{};   // Full model string if available

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query CPU features via CPUID (RT-safe: no I/O, just CPUID instructions)
[[nodiscard]] CpuFeatures getCpuFeatures() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto feat = getCpuFeatures();

if (feat.avx2) {
  // Use AVX2-optimized code path
} else if (feat.sse42) {
  // Fall back to SSE4.2 path
}

if (!feat.invariantTsc) {
  // Warning: TSC may not be reliable for timing on this CPU
}
```

---

### CpuFreq

**Header:** `CpuFreq.hpp`
**Purpose:** Per-core frequency and scaling governor information.

#### Key Types

```cpp
/// Maximum governor string length (covers all known governors + null).
inline constexpr std::size_t GOVERNOR_STRING_SIZE = 24;

struct CoreFrequency {
  int cpuId{-1};
  std::array<char, GOVERNOR_STRING_SIZE> governor{}; // "performance", "powersave", "schedutil"
  std::int64_t minKHz{0};
  std::int64_t maxKHz{0};
  std::int64_t curKHz{0};
  bool turboAvailable{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct CpuFrequencySummary {
  std::vector<CoreFrequency> cores{}; // One entry per logical CPU

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect per-core cpufreq data (NOT RT-safe: allocates vector, scans sysfs)
[[nodiscard]] CpuFrequencySummary getCpuFrequencySummary() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto freq = getCpuFrequencySummary();

for (const auto& c : freq.cores) {
  // Check for performance governor (recommended for RT)
  if (std::strcmp(c.governor.data(), "performance") != 0) {
    fmt::print("Warning: CPU {} using {} governor\n", c.cpuId, c.governor.data());
  }

  // Check if running at max frequency
  if (c.curKHz < c.maxKHz * 0.9) {
    fmt::print("CPU {} running below max: {} / {} kHz\n",
               c.cpuId, c.curKHz, c.maxKHz);
  }
}
```

---

### CpuIdle

**Header:** `CpuIdle.hpp`
**Purpose:** C-state residency and idle state configuration.

#### Why It Matters for RT

Deep C-states (C3, C6, etc.) save power but add wake latency. For RT systems, you often want to disable deep states or monitor their usage.

#### Key Types

```cpp
/// Maximum C-states per CPU.
inline constexpr std::size_t IDLE_MAX_STATES = 16;

/// Maximum CPUs for idle tracking.
inline constexpr std::size_t IDLE_MAX_CPUS = 256;

/// Maximum state name length.
inline constexpr std::size_t IDLE_NAME_SIZE = 32;

/// Maximum state description length.
inline constexpr std::size_t IDLE_DESC_SIZE = 64;

struct CStateInfo {
  std::array<char, IDLE_NAME_SIZE> name{}; // "POLL", "C1", "C1E", "C6"
  std::array<char, IDLE_DESC_SIZE> desc{}; // "MWAIT 0x00", etc.
  std::uint32_t latencyUs{0};              // Exit latency
  std::uint32_t residencyUs{0};            // Target residency
  std::uint64_t usageCount{0};             // Times entered
  std::uint64_t timeUs{0};                 // Total time in state
  bool disabled{false};                    // Administratively disabled
};

struct CpuIdleStats {
  int cpuId{-1};
  CStateInfo states[IDLE_MAX_STATES]{};
  std::size_t stateCount{0};

  [[nodiscard]] std::uint64_t totalIdleTimeUs() const noexcept;
  [[nodiscard]] int deepestEnabledState() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct CpuIdleSnapshot {
  CpuIdleStats perCpu[IDLE_MAX_CPUS]{};
  std::size_t cpuCount{0};
  std::uint64_t timestampNs{0};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct CpuIdleDelta {
  // Per-CPU, per-state deltas
  std::uint64_t usageDelta[IDLE_MAX_CPUS][IDLE_MAX_STATES]{};
  std::uint64_t timeDeltaUs[IDLE_MAX_CPUS][IDLE_MAX_STATES]{};
  std::size_t stateCount[IDLE_MAX_CPUS]{};
  std::size_t cpuCount{0};
  std::uint64_t intervalNs{0};

  [[nodiscard]] double residencyPercent(std::size_t cpuId, std::size_t stateIdx) const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Capture C-state stats (NOT RT-safe: scans sysfs directories)
[[nodiscard]] CpuIdleSnapshot getCpuIdleSnapshot() noexcept;

/// Compute delta (RT-safe: pure computation)
[[nodiscard]] CpuIdleDelta computeCpuIdleDelta(const CpuIdleSnapshot& before,
                                               const CpuIdleSnapshot& after) noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto snap = getCpuIdleSnapshot();

for (std::size_t i = 0; i < snap.cpuCount; ++i) {
  const auto& cpu = snap.perCpu[i];

  // Check for deep states with high latency
  for (std::size_t s = 0; s < cpu.stateCount; ++s) {
    const auto& state = cpu.states[s];
    if (state.latencyUs > 100 && !state.disabled) {
      fmt::print("Warning: CPU {} has {} enabled ({}us latency)\n",
                 cpu.cpuId, state.name.data(), state.latencyUs);
    }
  }
}
```

---

### CpuIsolation

**Header:** `CpuIsolation.hpp`
**Purpose:** Query kernel CPU isolation configuration for RT systems.

#### Why It Matters for RT

Proper CPU isolation requires coordinated kernel boot parameters:

- `isolcpus` - Exclude CPUs from general scheduler
- `nohz_full` - Disable timer ticks when single task running
- `rcu_nocbs` - Offload RCU callbacks to other CPUs

Missing any of these on RT cores can cause unexpected jitter.

#### Key Types

```cpp
/// Maximum kernel command line length to capture.
inline constexpr std::size_t CMDLINE_MAX_SIZE = 4096;

struct CpuIsolationConfig {
  CpuSet isolcpus{};   // CPUs isolated from scheduler
  CpuSet nohzFull{};   // Tickless CPUs
  CpuSet rcuNocbs{};   // RCU callback offload CPUs

  bool isolcpusManaged{false};  // True if managed_irq specified
  bool nohzFullAll{false};      // True if nohz_full=all

  [[nodiscard]] bool isFullyIsolated(std::size_t cpuId) const noexcept;
  [[nodiscard]] bool hasAnyIsolation() const noexcept;
  [[nodiscard]] CpuSet getFullyIsolatedCpus() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct IsolationValidation {
  CpuSet missingIsolcpus{};  // Requested CPUs not in isolcpus
  CpuSet missingNohzFull{};  // Requested CPUs not in nohz_full
  CpuSet missingRcuNocbs{};  // Requested CPUs not in rcu_nocbs

  [[nodiscard]] bool isValid() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query isolation configuration (RT-safe: bounded file reads)
[[nodiscard]] CpuIsolationConfig getCpuIsolationConfig() noexcept;

/// Validate RT CPUs have full isolation (RT-safe: pure computation)
[[nodiscard]] IsolationValidation validateIsolation(
    const CpuIsolationConfig& config,
    const CpuSet& rtCpus) noexcept;

/// Parse kernel CPU list format (RT-safe)
[[nodiscard]] CpuSet parseCpuList(const char* cpuList) noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

// Check system isolation at startup
auto isolation = getCpuIsolationConfig();
fmt::print("{}", isolation.toString());

// Verify RT cores are properly isolated
CpuSet rtCpus;
rtCpus.set(2);
rtCpus.set(3);

auto validation = validateIsolation(isolation, rtCpus);
if (!validation.isValid()) {
  fmt::print("WARNING: RT cores not fully isolated!\n");
  fmt::print("{}", validation.toString());
}

// Check if specific CPU has all isolation features
if (isolation.isFullyIsolated(3)) {
  fmt::print("CPU 3 is fully isolated for RT\n");
}

// Parse CPU list from command line or config file
CpuSet userCpus = parseCpuList("0,2-4,6");
// Results in CPUs: 0, 2, 3, 4, 6

// Supported formats:
//   "3"       -> single CPU
//   "2-5"     -> range (inclusive)
//   "0,2,4"   -> list
//   "0,2-4,6" -> mixed
```

---

### CpuStats

**Header:** `CpuStats.hpp`
**Purpose:** Basic system health - CPU count, RAM, swap, load averages, uptime.

#### Key Types

```cpp
/// Maximum CPU model string length.
inline constexpr std::size_t CPU_MODEL_STRING_SIZE = 128;

/// Maximum kernel version string length.
inline constexpr std::size_t KERNEL_VERSION_STRING_SIZE = 256;

struct SysinfoData {
  std::uint64_t totalRamBytes{0};
  std::uint64_t freeRamBytes{0};
  std::uint64_t totalSwapBytes{0};
  std::uint64_t freeSwapBytes{0};
  std::uint64_t uptimeSeconds{0};
  int processCount{0};
  double load1{0.0};
  double load5{0.0};
  double load15{0.0};
};

struct KernelVersionData {
  std::array<char, KERNEL_VERSION_STRING_SIZE> version{};
};

struct CpuInfoData {
  std::array<char, CPU_MODEL_STRING_SIZE> model{};
  long frequencyMhz{0};  // Rounded MHz; 0 if unavailable
};

struct MeminfoData {
  std::uint64_t availableBytes{0}; // MemAvailable; 0 if key absent
  bool hasAvailable{false};        // True if MemAvailable was present
};

struct CpuCountData {
  int count{0};  // Logical CPU count (>= 1)
};

struct CpuStats {
  CpuCountData cpuCount{};    // Logical CPU count
  KernelVersionData kernel{}; // Kernel version
  CpuInfoData cpuInfo{};      // CPU model and frequency
  SysinfoData sysinfo{};      // RAM, swap, uptime, load
  MeminfoData meminfo{};      // MemAvailable

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
// Individual readers (varying RT-safety)
[[nodiscard]] SysinfoData readSysinfo() noexcept;        // RT-safe
[[nodiscard]] CpuCountData readCpuCount() noexcept;      // RT-safe
[[nodiscard]] KernelVersionData readKernelVersion() noexcept;  // RT-safe
[[nodiscard]] MeminfoData readMeminfo() noexcept;        // RT-safe
[[nodiscard]] CpuInfoData readCpuInfo() noexcept;        // NOT RT-safe

// Aggregate (NOT RT-safe)
[[nodiscard]] CpuStats getCpuStats() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto stats = getCpuStats();

// Check system load
if (stats.sysinfo.load1 > stats.cpuCount.count) {
  fmt::print("System overloaded: load {} > {} CPUs\n",
             stats.sysinfo.load1, stats.cpuCount.count);
}

// Check available memory
if (stats.meminfo.hasAvailable) {
  double availPct = 100.0 * stats.meminfo.availableBytes / stats.sysinfo.totalRamBytes;
  if (availPct < 10.0) {
    fmt::print("Low memory: {:.1f}% available\n", availPct);
  }
}
```

---

### CpuTopology

**Header:** `CpuTopology.hpp`
**Purpose:** Socket/core/thread layout, NUMA nodes, cache hierarchy.

#### Key Types

```cpp
/// Maximum cache type/policy string length.
inline constexpr std::size_t CACHE_STRING_SIZE = 16;

struct CacheInfo {
  int level{0};                                 // 1, 2, 3
  std::array<char, CACHE_STRING_SIZE> type{};   // "Data", "Instruction", "Unified"
  std::uint64_t sizeBytes{0};
  std::uint64_t lineBytes{0};
  int associativity{0};                         // Ways of associativity; 0 if unknown
  std::array<char, CACHE_STRING_SIZE> policy{};  // Write policy if known

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct ThreadInfo {
  int cpuId{-1};     // Linux logical CPU id (0-based)
  int coreId{-1};    // Physical core id within package
  int packageId{-1}; // Socket/package id
  int numaNode{-1};  // NUMA node id (-1 if unknown)
};

struct CoreInfo {
  int coreId{-1};
  int packageId{-1};
  int numaNode{-1};                    // NUMA node id (-1 if unknown)
  std::vector<int> threadCpuIds{};     // Sibling logical CPU ids (HT/SMT)
  std::vector<CacheInfo> caches{};     // Per-core caches (L1/L2)

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct CpuTopology {
  int packages{0};                         // Socket count
  int physicalCores{0};                    // Total physical cores
  int logicalCpus{0};                      // Total threads (with HT)
  int numaNodes{0};                        // NUMA node count (0 if unknown)
  std::vector<CoreInfo> cores{};           // Per-physical-core details
  std::vector<CacheInfo> sharedCaches{};   // Package-level shared caches (L3+)

  [[nodiscard]] int threadsPerCore() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect topology (NOT RT-safe: allocates vectors, scans sysfs)
[[nodiscard]] CpuTopology getCpuTopology() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto topo = getCpuTopology();

fmt::print("System: {} socket(s), {} cores, {} threads\n",
           topo.packages, topo.physicalCores, topo.logicalCpus);

// Check for hyperthreading
if (topo.threadsPerCore() > 1) {
  fmt::print("Hyperthreading enabled ({} threads/core)\n", topo.threadsPerCore());
  // For RT, you may want to disable HT or pin to physical cores only
}

// Find L3 cache size
for (const auto& cache : topo.sharedCaches) {
  if (cache.level == 3) {
    fmt::print("L3 cache: {} bytes\n", cache.sizeBytes);
  }
}
```

---

### CpuUtilization

**Header:** `CpuUtilization.hpp`
**Purpose:** Per-core CPU utilization percentages (user, system, idle, etc.).

#### Key Types

```cpp
struct CpuTimeCounters {
  std::uint64_t user{0};
  std::uint64_t nice{0};
  std::uint64_t system{0};
  std::uint64_t idle{0};
  std::uint64_t iowait{0};
  std::uint64_t irq{0};
  std::uint64_t softirq{0};
  std::uint64_t steal{0};
  std::uint64_t guest{0};
  std::uint64_t guestNice{0};

  [[nodiscard]] std::uint64_t total() const noexcept;
  [[nodiscard]] std::uint64_t active() const noexcept;  // total - idle - iowait
};

struct CpuUtilizationSnapshot {
  CpuTimeCounters aggregate;              // All CPUs combined
  CpuTimeCounters perCore[MAX_CPUS];      // Per-core counters (indexed by CPU id)
  std::size_t coreCount{0};
  std::uint64_t timestampNs{0};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct CpuUtilizationPercent {
  double user{0.0};
  double nice{0.0};
  double system{0.0};
  double idle{0.0};
  double iowait{0.0};
  double irq{0.0};
  double softirq{0.0};
  double steal{0.0};
  double guest{0.0};
  double guestNice{0.0};

  [[nodiscard]] double active() const noexcept;  // Sum of non-idle components
};

struct CpuUtilizationDelta {
  CpuUtilizationPercent aggregate;
  CpuUtilizationPercent perCore[MAX_CPUS];
  std::size_t coreCount{0};
  std::uint64_t intervalNs{0};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Capture raw counters (RT-safe: single file read)
[[nodiscard]] CpuUtilizationSnapshot getCpuUtilizationSnapshot() noexcept;

/// Compute percentages (RT-safe: pure computation)
[[nodiscard]] CpuUtilizationDelta computeUtilizationDelta(
    const CpuUtilizationSnapshot& before,
    const CpuUtilizationSnapshot& after) noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

// Measure utilization over 100ms
auto before = getCpuUtilizationSnapshot();
std::this_thread::sleep_for(std::chrono::milliseconds(100));
auto after = getCpuUtilizationSnapshot();

auto delta = computeUtilizationDelta(before, after);

// Check if isolated cores are truly idle
int rtCore = 3;  // Assume CPU 3 is isolated for RT
if (delta.perCore[rtCore].active() > 1.0) {
  fmt::print("Warning: RT core {} is {:.1f}% active!\n",
             rtCore, delta.perCore[rtCore].active());
}

// Check for high IRQ/softirq overhead
for (std::size_t i = 0; i < delta.coreCount; ++i) {
  double irqPct = delta.perCore[i].irq + delta.perCore[i].softirq;
  if (irqPct > 5.0) {
    fmt::print("CPU {}: {:.1f}% in IRQ handling\n", i, irqPct);
  }
}
```

---

### IrqStats

**Header:** `IrqStats.hpp`
**Purpose:** Per-core hardware interrupt counts by IRQ line.

#### Why It Matters for RT

Hardware interrupts can cause jitter. RT systems often configure IRQ affinity to keep interrupts off RT cores. This module lets you verify that configuration.

#### Key Types

```cpp
/// Maximum supported CPUs for per-core IRQ tracking.
inline constexpr std::size_t IRQ_MAX_CPUS = 256;

/// Maximum number of IRQ lines to track.
inline constexpr std::size_t IRQ_MAX_LINES = 512;

/// Maximum IRQ name length.
inline constexpr std::size_t IRQ_NAME_SIZE = 32;

/// Maximum IRQ description length.
inline constexpr std::size_t IRQ_DESC_SIZE = 64;

struct IrqLineStats {
  std::array<char, IRQ_NAME_SIZE> name{}; // IRQ number or name ("0", "NMI", "LOC")
  std::array<char, IRQ_DESC_SIZE> desc{}; // Description ("timer", "eth0")
  std::uint64_t perCore[IRQ_MAX_CPUS]{};
  std::uint64_t total{0};

  [[nodiscard]] std::string toString(std::size_t coreCount) const;  // NOT RT-safe
};

struct IrqSnapshot {
  IrqLineStats lines[IRQ_MAX_LINES]{};
  std::size_t lineCount{0};
  std::size_t coreCount{0};
  std::uint64_t timestampNs{0};

  [[nodiscard]] std::uint64_t totalForCore(std::size_t core) const noexcept;
  [[nodiscard]] std::uint64_t totalAllCores() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct IrqDelta {
  std::array<char, IRQ_NAME_SIZE> names[IRQ_MAX_LINES]{};    // IRQ names (copied)
  std::uint64_t perCoreDelta[IRQ_MAX_LINES][IRQ_MAX_CPUS]{}; // Per-IRQ, per-core deltas
  std::uint64_t lineTotals[IRQ_MAX_LINES]{};                 // Per-IRQ total deltas
  std::size_t lineCount{0};
  std::size_t coreCount{0};
  std::uint64_t intervalNs{0};

  [[nodiscard]] std::uint64_t totalForCore(std::size_t core) const noexcept;
  [[nodiscard]] double rateForCore(std::size_t core) const noexcept;  // IRQs/second
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Capture IRQ counts (RT-safe: single file read)
[[nodiscard]] IrqSnapshot getIrqSnapshot() noexcept;

/// Compute delta (RT-safe: pure computation)
[[nodiscard]] IrqDelta computeIrqDelta(
    const IrqSnapshot& before,
    const IrqSnapshot& after) noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto before = getIrqSnapshot();
std::this_thread::sleep_for(std::chrono::seconds(1));
auto after = getIrqSnapshot();

auto delta = computeIrqDelta(before, after);

// Check isolated cores for IRQ activity
int rtCore = 3;
double rate = delta.rateForCore(rtCore);
if (rate > 10.0) {  // More than 10 IRQs/sec
  fmt::print("Warning: {} IRQs/sec hitting RT core {}\n", rate, rtCore);
}

// Find which IRQs are hitting that core
for (std::size_t i = 0; i < delta.lineCount; ++i) {
  if (delta.perCoreDelta[i][rtCore] > 0) {
    fmt::print("  {} IRQs from {}\n",
               delta.perCoreDelta[i][rtCore],
               delta.names[i].data());
  }
}
```

---

### SoftirqStats

**Header:** `SoftirqStats.hpp`
**Purpose:** Per-core software interrupt counts by type.

#### Softirq Types

| Type       | Meaning                |
| ---------- | ---------------------- |
| `HI`       | High-priority tasklets |
| `TIMER`    | Timer tick processing  |
| `NET_TX`   | Network transmit       |
| `NET_RX`   | Network receive        |
| `BLOCK`    | Block device I/O       |
| `IRQ_POLL` | IRQ polling            |
| `TASKLET`  | Regular tasklets       |
| `SCHED`    | Scheduler              |
| `HRTIMER`  | High-resolution timers |
| `RCU`      | Read-copy-update       |

#### Key Types

```cpp
/// Maximum CPUs for softirq tracking.
inline constexpr std::size_t SOFTIRQ_MAX_CPUS = 256;

/// Maximum softirq types.
inline constexpr std::size_t SOFTIRQ_MAX_TYPES = 16;

/// Maximum softirq type name length.
inline constexpr std::size_t SOFTIRQ_NAME_SIZE = 16;

enum class SoftirqType : std::uint8_t {
  HI = 0, TIMER, NET_TX, NET_RX, BLOCK, IRQ_POLL,
  TASKLET, SCHED, HRTIMER, RCU, UNKNOWN
};

/// Convert softirq type to string.
[[nodiscard]] const char* softirqTypeName(SoftirqType type) noexcept;

struct SoftirqTypeStats {
  std::array<char, SOFTIRQ_NAME_SIZE> name{};
  SoftirqType type{SoftirqType::UNKNOWN};
  std::uint64_t perCore[SOFTIRQ_MAX_CPUS]{};
  std::uint64_t total{0};
};

struct SoftirqSnapshot {
  SoftirqTypeStats types[SOFTIRQ_MAX_TYPES]{};
  std::size_t typeCount{0};
  std::size_t cpuCount{0};
  std::uint64_t timestampNs{0};

  [[nodiscard]] std::uint64_t totalForCpu(std::size_t cpu) const noexcept;
  [[nodiscard]] const SoftirqTypeStats* getType(SoftirqType type) const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct SoftirqDelta {
  std::array<char, SOFTIRQ_NAME_SIZE> names[SOFTIRQ_MAX_TYPES]{};
  SoftirqType typeEnums[SOFTIRQ_MAX_TYPES]{};
  std::uint64_t perCoreDelta[SOFTIRQ_MAX_TYPES][SOFTIRQ_MAX_CPUS]{};
  std::uint64_t typeTotals[SOFTIRQ_MAX_TYPES]{};
  std::size_t typeCount{0};
  std::size_t cpuCount{0};
  std::uint64_t intervalNs{0};

  [[nodiscard]] std::uint64_t totalForCpu(std::size_t cpu) const noexcept;
  [[nodiscard]] double rateForCpu(std::size_t cpu) const noexcept;
  [[nodiscard]] double rateForType(SoftirqType type) const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Capture softirq counts (RT-safe: single file read)
[[nodiscard]] SoftirqSnapshot getSoftirqSnapshot() noexcept;

/// Compute delta (RT-safe: pure computation)
[[nodiscard]] SoftirqDelta computeSoftirqDelta(
    const SoftirqSnapshot& before,
    const SoftirqSnapshot& after) noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto before = getSoftirqSnapshot();
std::this_thread::sleep_for(std::chrono::seconds(1));
auto after = getSoftirqSnapshot();

auto delta = computeSoftirqDelta(before, after);

// Check for network softirq storms
double netRxRate = delta.rateForType(SoftirqType::NET_RX);
if (netRxRate > 100000) {
  fmt::print("High NET_RX rate: {:.0f}/s\n", netRxRate);
}

// Check per-CPU softirq load
for (std::size_t i = 0; i < delta.cpuCount; ++i) {
  double rate = delta.rateForCpu(i);
  if (rate > 50000) {
    fmt::print("CPU {} handling {:.0f} softirqs/s\n", i, rate);
  }
}
```

---

### ThermalStatus

**Header:** `ThermalStatus.hpp`
**Purpose:** CPU temperatures, throttling indicators, and RAPL power limits.

#### Key Types

```cpp
/// Maximum sensor/domain name length.
inline constexpr std::size_t THERMAL_NAME_SIZE = 32;

struct TemperatureSensor {
  std::array<char, THERMAL_NAME_SIZE> name{};  // "Package id 0", "Core 0", "Tctl"
  double tempCelsius{0.0};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct PowerLimit {
  std::array<char, THERMAL_NAME_SIZE> domain{};  // "package-0", "core", "dram"
  double watts{0.0};
  bool enforced{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct ThrottleHints {
  bool powerLimit{false};  // Power limit throttling active
  bool thermal{false};     // Thermal throttling active
  bool current{false};     // Electrical current limit
};

struct ThermalStatus {
  std::vector<TemperatureSensor> sensors{};  // All detected temperature sensors
  std::vector<PowerLimit> powerLimits{};     // RAPL power limits (Intel)
  ThrottleHints throttling{};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect thermal status (NOT RT-safe: allocates vectors, scans sysfs/hwmon)
[[nodiscard]] ThermalStatus getThermalStatus() noexcept;
```

#### Usage

```cpp
using namespace seeker::cpu;

auto thermal = getThermalStatus();

// Check for thermal throttling
if (thermal.throttling.thermal) {
  fmt::print("WARNING: CPU is thermally throttling!\n");
}

// Check temperatures
for (const auto& sensor : thermal.sensors) {
  if (sensor.tempCelsius > 80.0) {
    fmt::print("High temp: {} = {:.1f}C\n",
               sensor.name.data(),
               sensor.tempCelsius);
  }
}

// Check power limits
for (const auto& limit : thermal.powerLimits) {
  fmt::print("Power limit {}: {:.1f}W {}\n",
             limit.domain.data(),
             limit.watts,
             limit.enforced ? "(enforced)" : "");
}
```

---

## Common Patterns

### Periodic Monitoring Loop

```cpp
using namespace seeker::cpu;

void monitoringThread() {
  auto prevUtil = getCpuUtilizationSnapshot();
  auto prevIrq = getIrqSnapshot();

  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto currUtil = getCpuUtilizationSnapshot();
    auto currIrq = getIrqSnapshot();

    auto utilDelta = computeUtilizationDelta(prevUtil, currUtil);
    auto irqDelta = computeIrqDelta(prevIrq, currIrq);

    // Log or alert on anomalies
    checkUtilization(utilDelta);
    checkIrqDistribution(irqDelta);

    prevUtil = currUtil;
    prevIrq = currIrq;
  }
}
```

### Startup System Check

```cpp
using namespace seeker::cpu;

bool validateSystemForRt() {
  bool ok = true;

  // Check topology
  auto topo = getCpuTopology();
  if (topo.logicalCpus < 4) {
    log("Warning: Only {} CPUs available", topo.logicalCpus);
    ok = false;
  }

  // Check for performance governor
  auto freq = getCpuFrequencySummary();
  for (const auto& core : freq.cores) {
    if (std::strcmp(core.governor.data(), "performance") != 0) {
      log("Warning: CPU {} not in performance mode", core.cpuId);
      ok = false;
    }
  }

  // Check for deep C-states
  auto idle = getCpuIdleSnapshot();
  for (std::size_t i = 0; i < idle.cpuCount; ++i) {
    for (std::size_t s = 0; s < idle.perCpu[i].stateCount; ++s) {
      const auto& state = idle.perCpu[i].states[s];
      if (state.latencyUs > 100 && !state.disabled) {
        log("Warning: CPU {} has high-latency C-state enabled", i);
        ok = false;
      }
    }
  }

  return ok;
}
```

---

## Real-Time Considerations

### RT-Safe Functions

These can be called from RT threads:

- `getCurrentThreadAffinity()`
- `setCurrentThreadAffinity()`
- `getConfiguredCpuCount()`
- `getCpuFeatures()`
- `getCpuIsolationConfig()`
- `validateIsolation()`
- `parseCpuList()`
- `getCpuUtilizationSnapshot()`
- `computeUtilizationDelta()`
- `getIrqSnapshot()`
- `computeIrqDelta()`
- `getSoftirqSnapshot()`
- `computeSoftirqDelta()`
- `computeCpuIdleDelta()`
- `readSysinfo()`
- `readCpuCount()`
- `readKernelVersion()`
- `readMeminfo()`

### NOT RT-Safe Functions

Call these from setup/monitoring threads only:

- `getCpuTopology()` - Directory scanning, allocates vectors
- `getCpuFrequencySummary()` - Directory scanning, allocates vector
- `getCpuIdleSnapshot()` - Directory scanning
- `getThermalStatus()` - Directory scanning, allocates vectors
- `getCpuStats()` - Multiple file reads
- `readCpuInfo()` - File size scales with core count
- All `toString()` methods - String allocation

### Recommended RT System Configuration

For best RT performance, verify these with the diagnostics:

1. **Governor:** `performance` (not `powersave` or `schedutil`)
2. **C-states:** Deep states (C3+) disabled or latency < 10us
3. **IRQ affinity:** No IRQs on RT cores
4. **CPU isolation:** RT cores have 0% utilization when idle
5. **Hyperthreading:** Consider disabling or pinning to physical cores

---

## Example: RT System Health Check

```cpp
#include "src/cpu/inc/Affinity.hpp"
#include "src/cpu/inc/CpuFreq.hpp"
#include "src/cpu/inc/CpuIdle.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/cpu/inc/CpuTopology.hpp"
#include "src/cpu/inc/CpuUtilization.hpp"
#include "src/cpu/inc/IrqStats.hpp"
#include "src/cpu/inc/ThermalStatus.hpp"

#include <fmt/core.h>
#include <thread>
#include <chrono>

using namespace seeker::cpu;

int main() {
  fmt::print("=== RT System Health Check ===\n\n");

  // 1. Topology
  auto topo = getCpuTopology();
  fmt::print("Topology: {} sockets, {} cores, {} threads\n",
             topo.packages, topo.physicalCores, topo.logicalCpus);

  // 2. CPU Isolation
  auto isolation = getCpuIsolationConfig();
  fmt::print("\nIsolation:\n");
  fmt::print("  isolcpus:  {}\n", isolation.isolcpus.toString());
  fmt::print("  nohz_full: {}\n", isolation.nohzFull.toString());
  fmt::print("  rcu_nocbs: {}\n", isolation.rcuNocbs.toString());
  fmt::print("  fully isolated: {}\n", isolation.getFullyIsolatedCpus().toString());

  // 3. Frequency/Governor
  auto freq = getCpuFrequencySummary();
  fmt::print("\nFrequency/Governor:\n");
  for (const auto& c : freq.cores) {
    fmt::print("  CPU {}: {} @ {} MHz\n",
               c.cpuId,
               c.governor.data(),
               c.curKHz / 1000);
  }

  // 4. Thermal
  auto thermal = getThermalStatus();
  fmt::print("\nThermal: ");
  if (thermal.throttling.thermal || thermal.throttling.powerLimit) {
    fmt::print("THROTTLING ACTIVE!\n");
  } else {
    fmt::print("OK\n");
  }

  // 5. Utilization (1 second sample)
  fmt::print("\nMeasuring utilization...\n");
  auto utilBefore = getCpuUtilizationSnapshot();
  auto irqBefore = getIrqSnapshot();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  auto utilAfter = getCpuUtilizationSnapshot();
  auto irqAfter = getIrqSnapshot();

  auto utilDelta = computeUtilizationDelta(utilBefore, utilAfter);
  auto irqDelta = computeIrqDelta(irqBefore, irqAfter);

  fmt::print("\nPer-core utilization:\n");
  for (std::size_t i = 0; i < utilDelta.coreCount; ++i) {
    fmt::print("  CPU {}: {:.1f}% active, {:.1f}% idle, {} IRQs/s\n",
               i,
               utilDelta.perCore[i].active(),
               utilDelta.perCore[i].idle,
               irqDelta.rateForCore(i));
  }

  return 0;
}
```

---

## CLI Tools

The CPU domain includes 7 command-line tools: `cpu-info`, `cpu-rtcheck`, `cpu-corestat`, `cpu-irqmap`, `cpu-thermal`, `cpu-affinity`, `cpu-snapshot`.

See: `tools/cpp/cpu/README.md` for detailed tool usage.

---

## See Also

- `seeker::gpu` - GPU telemetry (CUDA, NVML, PCIe)
- `seeker::memory` - Memory topology, page sizes
- `seeker::timing` - Clock sources, timer latency
- `seeker::system` - Containers, RT readiness, drivers
