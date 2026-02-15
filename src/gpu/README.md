# GPU Diagnostics Module

**Namespace:** `seeker::gpu`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive GPU telemetry for real-time and performance-critical systems. This module provides 6 focused components for monitoring GPU topology, telemetry, memory status, driver configuration, PCIe links, and process isolation, plus 1 CUDA-only component for GPU benchmarking (7 total).

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [GpuTopology](#gputopology) - Device enumeration, SM architecture, capabilities
   - [GpuTelemetry](#gputelemetry) - Temperature, power, clocks, throttling
   - [GpuMemoryStatus](#gpumemorystatus) - Capacity, usage, ECC, retired pages
   - [GpuDriverStatus](#gpudriverstatus) - Driver versions, persistence, compute mode
   - [PcieStatus](#pciestatus) - PCIe link width, generation, NUMA affinity
   - [GpuIsolation](#gpuisolation) - MIG, MPS, process enumeration
   - [GpuBench](#gpubench) - Transfer bandwidth, launch latency, allocation timing (CUDA-only)
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: GPU RT Readiness Check](#example-gpu-rt-readiness-check)

---

## Overview

The GPU diagnostics module answers these questions for RT systems:

| Question                                       | Module            |
| ---------------------------------------------- | ----------------- |
| How many GPUs are present and what type?       | `GpuTopology`     |
| What is the SM count and compute capability?   | `GpuTopology`     |
| What CUDA cores and memory are available?      | `GpuTopology`     |
| What is the GPU temperature and power draw?    | `GpuTelemetry`    |
| Are clocks throttling due to thermal/power?    | `GpuTelemetry`    |
| What is the GPU utilization?                   | `GpuTelemetry`    |
| How much GPU memory is used/free?              | `GpuMemoryStatus` |
| Is ECC enabled and are there any errors?       | `GpuMemoryStatus` |
| Are there retired memory pages?                | `GpuMemoryStatus` |
| What driver and CUDA versions are installed?   | `GpuDriverStatus` |
| Is persistence mode enabled?                   | `GpuDriverStatus` |
| Is the GPU in exclusive compute mode?          | `GpuDriverStatus` |
| What PCIe generation/width is the GPU running? | `PcieStatus`      |
| Is the GPU running at maximum PCIe link speed? | `PcieStatus`      |
| Which NUMA node is the GPU attached to?        | `PcieStatus`      |
| Is MIG mode enabled? How many instances?       | `GpuIsolation`    |
| Is MPS (Multi-Process Service) active?         | `GpuIsolation`    |
| What processes are using the GPU?              | `GpuIsolation`    |
| What is the H2D/D2H transfer bandwidth?        | `GpuBench`        |
| What is the kernel launch overhead?            | `GpuBench`        |
| How fast is GPU memory allocation?             | `GpuBench`        |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/gpu/inc/GpuTelemetry.hpp"
#include "src/gpu/inc/GpuMemoryStatus.hpp"
#include "src/gpu/inc/GpuDriverStatus.hpp"
#include "src/gpu/inc/PcieStatus.hpp"
#include "src/gpu/inc/GpuIsolation.hpp"
#include "src/gpu/inc/GpuBench.cuh"       // CUDA-only
```

### One-Shot Queries

```cpp
using namespace seeker::gpu;

// Static system info (query once at startup)
auto topo = getGpuTopology();           // All GPUs, capabilities
auto device = getGpuDevice(0);          // Single device topology

// Dynamic state (query periodically)
auto telem = getGpuTelemetry(0);        // Temperature, power, clocks
auto mem = getGpuMemoryStatus(0);       // Memory usage, ECC
auto drv = getGpuDriverStatus(0);       // Driver config
auto pcie = getPcieStatus(0);           // PCIe link status
auto iso = getGpuIsolation(0);          // Process isolation

// All-device queries
auto allTelem = getAllGpuTelemetry();
auto allMem = getAllGpuMemoryStatus();
auto allDrv = getAllGpuDriverStatus();
auto allPcie = getAllPcieStatus();
auto allIso = getAllGpuIsolation();

// Benchmarks (CUDA-only)
auto bench = runGpuBench(0);            // Single GPU benchmark
auto allBench = runAllGpuBench();       // All GPUs benchmark
```

### RT Readiness Check

```cpp
using namespace seeker::gpu;

auto drv = getGpuDriverStatus(0);
if (drv.isRtReady()) {
  // Persistence mode + exclusive compute mode
  fmt::print("GPU 0 is RT-ready\n");
} else {
  if (!drv.persistenceMode) {
    fmt::print("Enable persistence: nvidia-smi -pm 1\n");
  }
  if (drv.computeMode != ComputeMode::ExclusiveProcess) {
    fmt::print("Set exclusive mode: nvidia-smi -c EXCLUSIVE_PROCESS\n");
  }
}
```

### Throttling Detection

```cpp
using namespace seeker::gpu;

auto telem = getGpuTelemetry(0);
if (telem.isThrottling()) {
  if (telem.throttleReasons.isThermalThrottling()) {
    fmt::print("GPU 0: Thermal throttling at {} C\n", telem.temperatureC);
  }
  if (telem.throttleReasons.isPowerThrottling()) {
    fmt::print("GPU 0: Power throttling at {} mW\n", telem.powerMilliwatts);
  }
}
```

### Memory Health Check

```cpp
using namespace seeker::gpu;

auto mem = getGpuMemoryStatus(0);
if (!mem.isHealthy()) {
  if (mem.eccErrors.hasUncorrected()) {
    fmt::print("GPU 0: Uncorrected ECC errors detected!\n");
  }
  if (mem.retiredPages.total() > 0) {
    fmt::print("GPU 0: {} retired pages\n", mem.retiredPages.total());
  }
}
```

### PCIe Link Validation

```cpp
using namespace seeker::gpu;

auto pcie = getPcieStatus(0);
if (!pcie.isAtMaxLink()) {
  fmt::print("GPU 0: Running at x{} Gen{}, max is x{} Gen{}\n",
             pcie.currentWidth, static_cast<int>(pcie.currentGen),
             pcie.maxWidth, static_cast<int>(pcie.maxGen));
}
if (pcie.numaNode >= 0) {
  fmt::print("GPU 0: NUMA node {}\n", pcie.numaNode);
}
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
 * @brief Query telemetry for a specific GPU.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated telemetry; defaults on failure.
 * @note RT-safe for single device query (no allocation beyond result).
 */
[[nodiscard]] GpuTelemetry getGpuTelemetry(int deviceIndex) noexcept;
```

### Data Structure Strategy

GPU structs use `std::string` and `std::vector` for variable-length data from NVML/sysfs
(device names, UUIDs, driver versions, process lists). Size constants are provided for
callers that need bounded buffers:

```cpp
inline constexpr std::size_t GPU_NAME_SIZE = 256;
inline constexpr std::size_t GPU_UUID_SIZE = 48;
inline constexpr std::size_t PCI_BDF_SIZE = 16;
```

### Graceful Degradation

All functions are `noexcept`. Missing drivers, failed queries, or unavailable features result in zeroed/default fields, not exceptions:

```cpp
auto telem = getGpuTelemetry(0);
if (telem.deviceIndex < 0) {
  // Query failed - no GPU or driver not loaded
}

auto mem = getGpuMemoryStatus(0);
if (!mem.eccSupported) {
  // Consumer GPU without ECC - eccErrors will be zeroed
}
```

---

## Module Reference

---

### GpuTopology

**Header:** `GpuTopology.hpp`
**Purpose:** GPU device enumeration, SM architecture, and capability discovery.

#### Key Types

```cpp
enum class GpuVendor : int {
  Unknown = 0,
  Nvidia = 1,
  Amd = 2,
  Intel = 3
};

struct GpuDevice {
  // Identity
  int deviceIndex{-1};
  std::string name;
  std::string uuid;
  GpuVendor vendor{GpuVendor::Unknown};

  // Architecture (NVIDIA-specific)
  int smMajor{0};              // Compute capability major
  int smMinor{0};              // Compute capability minor
  int smCount{0};              // Number of SMs
  int coresPerSm{0};           // CUDA cores per SM
  int cudaCores{0};            // Total CUDA cores (smCount * coresPerSm)

  // Execution limits
  int warpSize{0};
  int maxThreadsPerBlock{0};
  int maxThreadsPerSm{0};
  int maxBlocksPerSm{0};

  // Register/shared memory limits
  int regsPerBlock{0};
  int regsPerSm{0};
  std::size_t sharedMemPerBlock{0};
  std::size_t sharedMemPerSm{0};

  // Memory
  std::uint64_t totalMemoryBytes{0};
  int memoryBusWidth{0};
  int l2CacheBytes{0};

  // PCI topology
  std::string pciBdf;
  int pciDomain{0}, pciBus{0}, pciDevice{0}, pciFunction{0};

  // Capabilities
  bool unifiedAddressing{false};
  bool managedMemory{false};
  bool concurrentKernels{false};
  bool asyncEngines{false};

  std::string computeCapability() const;  // e.g., "8.9"
  std::string toString() const;           // NOT RT-safe
};

struct GpuTopology {
  int deviceCount{0};
  int nvidiaCount{0};
  int amdCount{0};
  int intelCount{0};
  std::vector<GpuDevice> devices;

  bool hasGpu() const noexcept;
  bool hasCuda() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query single GPU device (NOT RT-safe)
[[nodiscard]] GpuDevice getGpuDevice(int deviceIndex) noexcept;

/// Query all GPUs (NOT RT-safe)
[[nodiscard]] GpuTopology getGpuTopology() noexcept;

/// Convert vendor to string (RT-safe)
[[nodiscard]] const char* toString(GpuVendor vendor) noexcept;
```

#### Data Sources

- CUDA runtime API (`cudaGetDeviceCount`, `cudaGetDeviceProperties`)
- NVML for additional details
- sysfs fallback for non-NVIDIA GPUs

---

### GpuTelemetry

**Header:** `GpuTelemetry.hpp`
**Purpose:** Real-time GPU telemetry including temperature, power, clocks, utilization, and throttling.

#### Key Types

```cpp
struct ThrottleReasons {
  bool gpuIdle{false};
  bool applicationClocks{false};
  bool swPowerCap{false};
  bool hwSlowdown{false};
  bool syncBoost{false};
  bool swThermal{false};
  bool hwThermal{false};
  bool hwPowerBrake{false};
  bool displayClocks{false};

  bool isThrottling() const noexcept;
  bool isThermalThrottling() const noexcept;
  bool isPowerThrottling() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpuTelemetry {
  int deviceIndex{-1};
  std::string name;

  // Temperature (Celsius)
  int temperatureC{0};
  int temperatureSlowdownC{0};
  int temperatureShutdownC{0};
  int temperatureMemoryC{0};

  // Power (milliwatts)
  std::uint32_t powerMilliwatts{0};
  std::uint32_t powerLimitMilliwatts{0};
  std::uint32_t powerDefaultMilliwatts{0};
  std::uint32_t powerMaxMilliwatts{0};

  // Clocks (MHz)
  int smClockMHz{0};
  int smClockMaxMHz{0};
  int memClockMHz{0};
  int memClockMaxMHz{0};
  int graphicsClockMHz{0};
  int videoClockMHz{0};

  // Performance state
  int perfState{0};  // P0-P15, 0=max

  // Throttling
  ThrottleReasons throttleReasons;

  // Utilization (percent, 0-100)
  int gpuUtilization{0};
  int memoryUtilization{0};
  int encoderUtilization{0};
  int decoderUtilization{0};

  // Fan
  int fanSpeedPercent{-1};  // -1 if passive/unavailable

  bool isMaxPerformance() const noexcept;
  bool isThrottling() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query telemetry for single GPU (RT-safe for single device)
[[nodiscard]] GpuTelemetry getGpuTelemetry(int deviceIndex) noexcept;

/// Query telemetry for all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<GpuTelemetry> getAllGpuTelemetry() noexcept;
```

#### Data Sources

- NVML (`nvmlDeviceGetTemperature`, `nvmlDeviceGetPowerUsage`, etc.)
- hwmon fallback for non-NVIDIA GPUs

---

### GpuMemoryStatus

**Header:** `GpuMemoryStatus.hpp`
**Purpose:** GPU memory capacity, usage, ECC status, and retired page tracking.

#### Key Types

```cpp
struct EccErrorCounts {
  std::uint64_t correctedVolatile{0};    // Since boot
  std::uint64_t uncorrectedVolatile{0};  // Since boot
  std::uint64_t correctedAggregate{0};   // Lifetime
  std::uint64_t uncorrectedAggregate{0}; // Lifetime

  bool hasUncorrected() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct RetiredPages {
  int singleBitEcc{0};
  int doubleBitEcc{0};
  bool pendingRetire{false};
  bool pendingRemapping{false};

  int total() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpuMemoryStatus {
  int deviceIndex{-1};

  // Capacity
  std::uint64_t totalBytes{0};
  std::uint64_t freeBytes{0};
  std::uint64_t usedBytes{0};

  // Memory topology
  int memoryBusWidth{0};
  int memoryClockMHz{0};
  int memoryClockMaxMHz{0};

  // ECC
  bool eccSupported{false};
  bool eccEnabled{false};
  EccErrorCounts eccErrors;
  RetiredPages retiredPages;

  // BAR
  std::uint64_t bar1Total{0};
  std::uint64_t bar1Used{0};

  double utilizationPercent() const noexcept;
  bool isHealthy() const noexcept;  // No uncorrected errors
  std::string toString() const;     // NOT RT-safe
};
```

#### API

```cpp
/// Query memory status for single GPU (NOT RT-safe)
[[nodiscard]] GpuMemoryStatus getGpuMemoryStatus(int deviceIndex) noexcept;

/// Query memory status for all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<GpuMemoryStatus> getAllGpuMemoryStatus() noexcept;
```

#### Data Sources

- NVML (`nvmlDeviceGetMemoryInfo`, `nvmlDeviceGetMemoryErrorCounter`)
- CUDA runtime (`cudaMemGetInfo`)

---

### GpuDriverStatus

**Header:** `GpuDriverStatus.hpp`
**Purpose:** GPU driver configuration, versions, and RT-readiness settings.

#### Key Types

```cpp
enum class ComputeMode : int {
  Default = 0,           // Multiple contexts allowed
  ExclusiveThread = 1,   // One context per thread (deprecated)
  Prohibited = 2,        // No CUDA contexts allowed
  ExclusiveProcess = 3   // One context per process (recommended for RT)
};

struct GpuDriverStatus {
  int deviceIndex{-1};
  std::string name;

  // Driver versions
  std::string driverVersion;      // e.g., "535.104.05"
  int cudaDriverVersion{0};       // e.g., 12040 = 12.4
  int cudaRuntimeVersion{0};
  std::string nvmlVersion;

  // Configuration
  bool persistenceMode{false};    // GPU stays initialized
  ComputeMode computeMode{ComputeMode::Default};
  bool accountingEnabled{false};

  // Environment
  std::string cudaVisibleDevices;
  std::string driverModelCurrent;

  // Firmware versions
  std::string inforomImageVersion;
  std::string inforomOemVersion;
  std::string vbiosVersion;

  bool versionsCompatible() const noexcept;  // driver >= runtime
  bool isRtReady() const noexcept;           // persistence + exclusive
  static std::string formatCudaVersion(int version);
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query driver status for single GPU (RT-safe for single device)
[[nodiscard]] GpuDriverStatus getGpuDriverStatus(int deviceIndex) noexcept;

/// Query driver status for all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<GpuDriverStatus> getAllGpuDriverStatus() noexcept;

/// Get system-wide CUDA info (NOT RT-safe)
[[nodiscard]] GpuDriverStatus getSystemGpuDriverInfo() noexcept;

/// Convert compute mode to string (RT-safe)
[[nodiscard]] const char* toString(ComputeMode mode) noexcept;
```

#### Data Sources

- NVML (`nvmlDeviceGetPersistenceMode`, `nvmlDeviceGetComputeMode`)
- CUDA runtime (`cudaDriverGetVersion`, `cudaRuntimeGetVersion`)

---

### PcieStatus

**Header:** `PcieStatus.hpp`
**Purpose:** PCIe link status, bandwidth, and NUMA topology for GPUs.

#### Key Types

```cpp
enum class PcieGeneration : int {
  Unknown = 0,
  Gen1 = 1,  // 2.5 GT/s
  Gen2 = 2,  // 5.0 GT/s
  Gen3 = 3,  // 8.0 GT/s
  Gen4 = 4,  // 16.0 GT/s
  Gen5 = 5,  // 32.0 GT/s
  Gen6 = 6   // 64.0 GT/s
};

struct PcieStatus {
  int deviceIndex{-1};

  // PCI address
  std::string bdf;  // e.g., "0000:65:00.0"
  int domain{0}, bus{0}, device{0}, function{0};

  // Current link
  int currentWidth{0};
  std::string currentSpeed;
  PcieGeneration currentGen{PcieGeneration::Unknown};

  // Maximum capability
  int maxWidth{0};
  std::string maxSpeed;
  PcieGeneration maxGen{PcieGeneration::Unknown};

  // NUMA
  int numaNode{-1};

  // Error counters
  std::uint64_t replayCount{0};
  std::uint64_t replayRollover{0};

  // Throughput (KB/s)
  int txThroughputKBps{0};
  int rxThroughputKBps{0};

  bool isAtMaxLink() const noexcept;
  int theoreticalBandwidthMBps() const noexcept;
  int currentBandwidthMBps() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query PCIe status by device index (RT-safe)
[[nodiscard]] PcieStatus getPcieStatus(int deviceIndex) noexcept;

/// Query PCIe status by BDF string (RT-safe)
[[nodiscard]] PcieStatus getPcieStatusByBdf(const std::string& bdf) noexcept;

/// Query PCIe status for all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<PcieStatus> getAllPcieStatus() noexcept;

/// Get bandwidth per lane (RT-safe)
[[nodiscard]] int pcieBandwidthPerLaneMBps(PcieGeneration gen) noexcept;

/// Parse speed string to generation (RT-safe)
[[nodiscard]] PcieGeneration parsePcieGeneration(const std::string& speed) noexcept;
```

#### Data Sources

- sysfs (`/sys/bus/pci/devices/<bdf>/current_link_speed`)
- NVML (`nvmlDeviceGetPcieThroughput`)

---

### GpuIsolation

**Header:** `GpuIsolation.hpp`
**Purpose:** Multi-tenancy features including MIG, MPS, and process enumeration.

#### Key Types

```cpp
struct MigInstance {
  int index{-1};
  std::string name;
  std::string uuid;
  int smCount{0};
  std::uint64_t memoryBytes{0};
  int computeInstanceCount{0};

  std::string toString() const;  // NOT RT-safe
};

struct GpuProcess {
  std::uint32_t pid{0};
  std::string name;
  std::uint64_t usedMemoryBytes{0};

  enum class Type : int {
    Unknown = 0,
    Compute = 1,
    Graphics = 2
  };
  Type type{Type::Unknown};

  std::string toString() const;  // NOT RT-safe
};

struct GpuIsolation {
  int deviceIndex{-1};
  std::string name;

  // Compute mode
  enum class ComputeMode : int {
    Default = 0,
    ExclusiveThread = 1,
    Prohibited = 2,
    ExclusiveProcess = 3
  };
  ComputeMode computeMode{ComputeMode::Default};

  // MIG (Multi-Instance GPU)
  bool migModeSupported{false};
  bool migModeEnabled{false};
  std::vector<MigInstance> migInstances;

  // MPS (Multi-Process Service)
  bool mpsSupported{false};
  bool mpsServerActive{false};

  // Processes
  int computeProcessCount{0};
  int graphicsProcessCount{0};
  std::vector<GpuProcess> processes;

  bool isExclusive() const noexcept;
  bool isRtIsolated() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query isolation status for single GPU (NOT RT-safe)
[[nodiscard]] GpuIsolation getGpuIsolation(int deviceIndex) noexcept;

/// Query isolation status for all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<GpuIsolation> getAllGpuIsolation() noexcept;
```

#### Data Sources

- NVML (`nvmlDeviceGetMigMode`, `nvmlDeviceGetComputeRunningProcesses`)

---

### GpuBench

**Header:** `GpuBench.cuh`
**Purpose:** GPU benchmarks including transfer bandwidth, kernel launch latency, and memory allocation timing.
**Requires:** CUDA (feature-guarded by `COMPAT_CUDA_AVAILABLE`)

#### Key Types

```cpp
struct BandwidthResult {
  double bandwidthMiBps{0.0};       // Bandwidth in MiB/s
  double latencyUs{0.0};            // Average latency per transfer (us)
  int iterations{0};                // Number of iterations measured
  std::size_t transferSizeBytes{0}; // Bytes per transfer

  std::string toString() const;
};

struct GpuBenchResult {
  int deviceIndex{-1};
  std::string name;

  // Transfer benchmarks (pinned memory)
  BandwidthResult h2d;         // Host-to-Device transfer
  BandwidthResult d2h;         // Device-to-Host transfer
  BandwidthResult d2d;         // Device-to-Device copy

  // Pageable transfer benchmarks
  BandwidthResult h2dPageable; // H2D with pageable host memory
  BandwidthResult d2hPageable; // D2H with pageable host memory

  // Kernel launch
  double launchOverheadUs{0.0}; // Empty kernel launch overhead (us)
  int launchIterations{0};      // Iterations for launch measurement

  // Memory allocation
  double deviceAllocUs{0.0};   // cudaMalloc average time (us)
  double pinnedAllocUs{0.0};   // cudaMallocHost average time (us)
  double deviceFreeUs{0.0};    // cudaFree average time (us)
  double pinnedFreeUs{0.0};    // cudaFreeHost average time (us)

  // Occupancy
  int maxActiveBlocksPerSm{0}; // Max active blocks (empty kernel)
  int maxActiveWarpsPerSm{0};  // Max active warps

  // Stream operations
  double streamCreateUs{0.0};  // Stream creation time (us)
  double streamSyncUs{0.0};    // Empty stream sync time (us)
  double eventCreateUs{0.0};   // Event creation time (us)

  // Benchmark metadata
  std::chrono::milliseconds budgetMs{0}; // Time budget used
  bool completed{false};                 // All benchmarks completed

  std::string toString() const;  // NOT RT-safe
};

struct BenchmarkOptions {
  std::chrono::milliseconds budget{1000};     // Total time budget
  std::size_t transferSize{64 * 1024 * 1024}; // Transfer size (bytes)
  int launchIterations{10000};                // Iterations for launch overhead
  int allocIterations{100};                   // Iterations for allocation timing

  bool skipPageable{false};   // Skip pageable memory benchmarks
  bool skipAllocation{false}; // Skip allocation benchmarks
  bool skipStreams{false};    // Skip stream/event benchmarks
};
```

#### API

```cpp
/// Run GPU benchmarks on a specific device (NOT RT-safe)
[[nodiscard]] GpuBenchResult runGpuBench(int deviceIndex,
                                         const BenchmarkOptions& options = {}) noexcept;

/// Run GPU benchmarks with time budget (NOT RT-safe, simplified API)
[[nodiscard]] GpuBenchResult runGpuBench(int deviceIndex,
                                         std::chrono::milliseconds budget) noexcept;

/// Run benchmarks on all GPUs (NOT RT-safe)
[[nodiscard]] std::vector<GpuBenchResult>
runAllGpuBench(const BenchmarkOptions& options = {}) noexcept;
```

#### Data Sources

- CUDA runtime (pinned/pageable transfers, kernel launch, allocation)
- CUDA occupancy API (`cudaOccupancyMaxActiveBlocksPerMultiprocessor`)

---

## Common Patterns

### Query Once vs. Query Periodically

**Query once at startup:**

```cpp
auto topo = getGpuTopology();       // Hardware doesn't change
auto pcie = getPcieStatus(0);       // Link usually stable
```

**Query periodically:**

```cpp
auto telem = getGpuTelemetry(0);    // Temperature/clocks change
auto mem = getGpuMemoryStatus(0);   // Usage changes
auto iso = getGpuIsolation(0);      // Processes come and go
```

### Single Device vs. All Devices

For targeted monitoring, use single-device queries:

```cpp
// More efficient for single GPU
auto telem = getGpuTelemetry(0);
auto mem = getGpuMemoryStatus(0);
```

For system-wide views, use all-device queries:

```cpp
// Returns vectors for iteration
auto allTelem = getAllGpuTelemetry();
for (const auto& t : allTelem) {
  fmt::print("GPU {}: {} C\n", t.deviceIndex, t.temperatureC);
}
```

---

## Real-Time Considerations

### RT-Safe Functions (call from any thread)

- `getGpuTelemetry(int)` - Single NVML call
- `getGpuDriverStatus(int)` - Minimal allocation
- `getPcieStatus(int)` - sysfs reads
- `getPcieStatusByBdf(const std::string&)` - sysfs reads
- `pcieBandwidthPerLaneMBps()` - Pure computation
- `parsePcieGeneration()` - String parsing
- `toString(GpuVendor)` - Returns static string
- `toString(ComputeMode)` - Returns static string
- All struct helper methods (`isThrottling()`, `isHealthy()`, etc.)

### NOT RT-Safe Functions (call from initialization thread)

- `getGpuTopology()` - CUDA enumeration, allocates
- `getGpuDevice(int)` - CUDA query, allocates
- `getAllGpuTelemetry()` - Allocates vector
- `getAllGpuMemoryStatus()` - Allocates vector
- `getAllGpuDriverStatus()` - Allocates vector
- `getAllPcieStatus()` - Allocates vector
- `getGpuIsolation(int)` - Allocates vectors for processes
- `getAllGpuIsolation()` - Allocates vectors
- `getGpuMemoryStatus(int)` - NVML queries, allocates
- `getSystemGpuDriverInfo()` - System-wide CUDA query
- `runGpuBench()` - Active benchmarks with allocation
- `runAllGpuBench()` - Active benchmarks with allocation
- All `toString()` methods - String allocation

### Recommended RT GPU Configuration

For best RT performance, verify these with the diagnostics:

1. **Persistence mode:** Enabled (`nvidia-smi -pm 1`)
2. **Compute mode:** Exclusive process (`nvidia-smi -c EXCLUSIVE_PROCESS`)
3. **ECC:** Enabled for mission-critical workloads
4. **Throttling:** None (check thermal and power)
5. **PCIe link:** Running at maximum (x16 Gen4)
6. **NUMA affinity:** Pin RT threads to same node as GPU
7. **Process isolation:** No competing processes

### nvidia-smi RT Configuration Commands

```bash
# Enable persistence mode (reduces initialization latency)
sudo nvidia-smi -pm 1

# Set exclusive process mode (prevents context sharing)
sudo nvidia-smi -c EXCLUSIVE_PROCESS

# Enable ECC (datacenter GPUs only)
sudo nvidia-smi -e 1

# Set power limit (if power throttling)
sudo nvidia-smi -pl 350
```

---

## CLI Tools

The GPU domain includes 4 command-line tools: `gpu-info`, `gpu-stat`, `gpu-rtcheck`, and `gpu-bench` (CUDA-only).

See: `tools/cpp/gpu/README.md` for detailed tool usage.

---

## Example: GPU RT Readiness Check

```cpp
#include "src/gpu/inc/GpuDriverStatus.hpp"
#include "src/gpu/inc/GpuIsolation.hpp"
#include "src/gpu/inc/GpuMemoryStatus.hpp"
#include "src/gpu/inc/GpuTelemetry.hpp"
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/gpu/inc/PcieStatus.hpp"

#include <fmt/core.h>

using namespace seeker::gpu;

int main() {
  fmt::print("=== GPU RT Readiness Check ===\n\n");

  // 1. Enumerate GPUs
  auto topo = getGpuTopology();
  if (topo.deviceCount == 0) {
    fmt::print("No GPUs detected.\n");
    return 1;
  }
  fmt::print("GPUs detected: {} ({} NVIDIA)\n", topo.deviceCount, topo.nvidiaCount);

  for (const auto& dev : topo.devices) {
    fmt::print("\n--- GPU {} ---\n", dev.deviceIndex);
    fmt::print("Name: {}\n", dev.name);
    fmt::print("Compute: SM {} ({} cores)\n", dev.computeCapability(), dev.cudaCores);

    // 2. Driver configuration
    auto drv = getGpuDriverStatus(dev.deviceIndex);
    fmt::print("\nDriver Configuration:\n");
    fmt::print("  Driver version: {}\n", drv.driverVersion);
    fmt::print("  CUDA version: {}\n", GpuDriverStatus::formatCudaVersion(drv.cudaDriverVersion));
    fmt::print("  Persistence mode: {}\n", drv.persistenceMode ? "enabled" : "DISABLED");
    fmt::print("  Compute mode: {}\n", toString(drv.computeMode));
    fmt::print("  RT ready: {}\n", drv.isRtReady() ? "YES" : "NO");

    // 3. Thermal status
    auto telem = getGpuTelemetry(dev.deviceIndex);
    fmt::print("\nThermal Status:\n");
    fmt::print("  Temperature: {} C\n", telem.temperatureC);
    fmt::print("  Power: {:.1f} W / {:.0f} W\n",
               telem.powerMilliwatts / 1000.0,
               telem.powerLimitMilliwatts / 1000.0);
    fmt::print("  Throttling: {}\n", telem.isThrottling() ? telem.throttleReasons.toString() : "none");

    // 4. Memory health
    auto mem = getGpuMemoryStatus(dev.deviceIndex);
    fmt::print("\nMemory Health:\n");
    fmt::print("  Usage: {:.1f}%%\n", mem.utilizationPercent());
    if (mem.eccSupported) {
      fmt::print("  ECC: {}\n", mem.eccEnabled ? "enabled" : "disabled");
      if (mem.eccErrors.hasUncorrected()) {
        fmt::print("  ECC ERRORS: {} uncorrected!\n", mem.eccErrors.uncorrectedAggregate);
      }
    }
    fmt::print("  Healthy: {}\n", mem.isHealthy() ? "yes" : "NO");

    // 5. PCIe link
    auto pcie = getPcieStatus(dev.deviceIndex);
    fmt::print("\nPCIe Link:\n");
    fmt::print("  Current: x{} Gen{}\n", pcie.currentWidth, static_cast<int>(pcie.currentGen));
    fmt::print("  Maximum: x{} Gen{}\n", pcie.maxWidth, static_cast<int>(pcie.maxGen));
    fmt::print("  At max: {}\n", pcie.isAtMaxLink() ? "yes" : "NO");
    if (pcie.numaNode >= 0) {
      fmt::print("  NUMA node: {}\n", pcie.numaNode);
    }

    // 6. Process isolation
    auto iso = getGpuIsolation(dev.deviceIndex);
    fmt::print("\nIsolation:\n");
    fmt::print("  Compute processes: {}\n", iso.computeProcessCount);
    fmt::print("  Graphics processes: {}\n", iso.graphicsProcessCount);
    fmt::print("  RT isolated: {}\n", iso.isRtIsolated() ? "yes" : "NO");

    // Summary
    fmt::print("\n=== Summary ===\n");
    bool ready = drv.isRtReady() && !telem.isThrottling() && mem.isHealthy() && pcie.isAtMaxLink();
    fmt::print("RT Ready: {}\n", ready ? "YES" : "NO - review above");

    if (!ready) {
      fmt::print("\nRecommendations:\n");
      if (!drv.persistenceMode) {
        fmt::print("  - Enable persistence: nvidia-smi -pm 1\n");
      }
      if (drv.computeMode != ComputeMode::ExclusiveProcess) {
        fmt::print("  - Set exclusive mode: nvidia-smi -c EXCLUSIVE_PROCESS\n");
      }
      if (telem.throttleReasons.isThermalThrottling()) {
        fmt::print("  - Improve cooling (thermal throttling)\n");
      }
      if (telem.throttleReasons.isPowerThrottling()) {
        fmt::print("  - Increase power limit: nvidia-smi -pl <watts>\n");
      }
      if (!pcie.isAtMaxLink()) {
        fmt::print("  - Check PCIe slot (running below max)\n");
      }
    }
  }

  return 0;
}
```

---

## See Also

- `seeker::cpu` - CPU telemetry (topology, frequency, utilization, IRQs, isolation)
- `seeker::memory` - Memory telemetry (pages, NUMA, hugepages, mlock, ECC/EDAC)
- `seeker::storage` - Storage telemetry (block devices, I/O stats, benchmarks)
- `seeker::network` - Network telemetry (interfaces, IRQ affinity, traffic, ethtool)
- `seeker::timing` - Timing telemetry (clocksource, timer config, PTP, RTC)
- `seeker::device` - Device telemetry (serial, I2C, SPI, CAN, GPIO)
- `seeker::system` - System telemetry (kernel, limits, capabilities, containers)
