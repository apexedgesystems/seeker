# Memory Diagnostics Module

**Namespace:** `seeker::memory`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive memory telemetry for real-time and performance-critical systems. This module provides 6 focused components for monitoring page sizes, memory usage, NUMA topology, hugepage allocation, memory locking capabilities, and ECC/EDAC memory error detection.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [PageSizes](#pagesizes) - Base and hugepage sizes
   - [MemoryStats](#memorystats) - RAM/swap usage and VM policies
   - [NumaTopology](#numatopology) - NUMA nodes, CPUs, and distances
   - [HugepageStatus](#hugepagestatus) - Hugepage allocation state
   - [MemoryLocking](#memorylocking) - mlock limits and capabilities
   - [EdacStatus](#edacstatus) - ECC memory error detection
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT Memory Validation](#example-rt-memory-validation)

---

## Overview

The memory diagnostics module answers these questions for RT and HPC systems:

| Question                                                 | Module           |
| -------------------------------------------------------- | ---------------- |
| What page sizes are available (base, 2M, 1G)?            | `PageSizes`      |
| What is current RAM/swap usage and availability?         | `MemoryStats`    |
| What are the VM policies (swappiness, THP, overcommit)?  | `MemoryStats`    |
| What is the NUMA topology and inter-node distances?      | `NumaTopology`   |
| Which CPUs belong to which NUMA node?                    | `NumaTopology`   |
| How many hugepages are allocated/free per size?          | `HugepageStatus` |
| What is the per-NUMA node hugepage distribution?         | `HugepageStatus` |
| Can I mlock the memory I need for RT?                    | `MemoryLocking`  |
| Do I have CAP_IPC_LOCK for unlimited mlock?              | `MemoryLocking`  |
| Are there ECC memory errors (correctable/uncorrectable)? | `EdacStatus`     |
| Is ECC enabled and how many memory controllers?          | `EdacStatus`     |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/memory/inc/PageSizes.hpp"
#include "src/memory/inc/MemoryStats.hpp"
#include "src/memory/inc/NumaTopology.hpp"
#include "src/memory/inc/HugepageStatus.hpp"
#include "src/memory/inc/MemoryLocking.hpp"
#include "src/memory/inc/EdacStatus.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::memory;

// Static system info (query once at startup)
auto pages = getPageSizes();              // Base page and hugepage sizes
auto numa = getNumaTopology();            // NUMA nodes, CPUs, distances

// Dynamic state (query periodically)
auto stats = getMemoryStats();            // RAM/swap usage, VM policies
auto hugepages = getHugepageStatus();     // Hugepage allocation counts
auto mlock = getMemoryLockingStatus();    // mlock limits and capabilities
auto edac = getEdacStatus();              // ECC memory error counts
```

### RT Memory Validation Pattern

```cpp
using namespace seeker::memory;

// Check if RT memory requirements can be met
auto mlock = getMemoryLockingStatus();
constexpr std::uint64_t REQUIRED_BYTES = 512 * 1024 * 1024;  // 512 MiB

if (mlock.isUnlimited()) {
  // CAP_IPC_LOCK or root - can lock any amount
} else if (mlock.canLock(REQUIRED_BYTES)) {
  // Within limits - proceed with mlock
} else {
  // Insufficient limits - warn user
}

// Check for memory errors (critical for radiation environments)
auto edac = getEdacStatus();
if (edac.hasCriticalErrors()) {
  // Uncorrectable errors - memory hardware failure!
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
 * @brief Collect memory locking status from /proc and capabilities.
 * @return Populated MemoryLockingStatus.
 * @note RT-safe: Bounded file reads, no allocation.
 */
[[nodiscard]] MemoryLockingStatus getMemoryLockingStatus() noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, THP_STRING_SIZE> thpEnabled{};

// Instead of std::vector (allocates)
NumaNodeInfo nodes[MAX_NUMA_NODES]{};
HugepageSizeStatus sizes[HP_MAX_SIZES]{};
std::array<MemoryController, EDAC_MAX_MC> controllers{};
```

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto numa = getNumaTopology();
if (numa.nodeCount == 0) {
  // NUMA info not available - handle gracefully
}

auto stats = getMemoryStats();
if (stats.swappiness < 0) {
  // Swappiness unavailable (containerized environment, etc.)
}

auto edac = getEdacStatus();
if (!edac.edacSupported) {
  // No ECC memory or EDAC kernel module not loaded
}
```

---

## Module Reference

---

### PageSizes

**Header:** `PageSizes.hpp`
**Purpose:** Query base page size and available hugepage sizes.

#### Key Types

```cpp
/// Maximum number of distinct hugepage sizes supported.
inline constexpr std::size_t MAX_HUGEPAGE_SIZES = 8;

struct PageSizes {
  std::uint64_t basePageBytes{0};                ///< Base page size (typically 4096)
  std::uint64_t hugeSizes[MAX_HUGEPAGE_SIZES]{}; ///< Available hugepage sizes (bytes)
  std::size_t hugeSizeCount{0};                  ///< Valid entries in hugeSizes[]

  bool hasHugePageSize(std::uint64_t sizeBytes) const noexcept;
  bool hasHugePages() const noexcept;
  std::uint64_t largestHugePageSize() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query page sizes (RT-safe: bounded syscall + directory scan)
[[nodiscard]] PageSizes getPageSizes() noexcept;

/// Format bytes as human-readable string (NOT RT-safe)
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);
```

#### Usage

```cpp
using namespace seeker::memory;

auto pages = getPageSizes();

fmt::print("Base page: {} bytes\n", pages.basePageBytes);

if (pages.hasHugePageSize(2 * 1024 * 1024)) {
  // 2 MiB hugepages available
}

if (pages.hasHugePageSize(1024 * 1024 * 1024)) {
  // 1 GiB hugepages available (requires boot-time allocation)
}
```

#### Data Sources

- `getpagesize(2)` - Base page size
- `/sys/kernel/mm/hugepages/hugepages-*kB` - Available hugepage sizes

---

### MemoryStats

**Header:** `MemoryStats.hpp`
**Purpose:** System-wide memory usage and VM policy settings.

#### Key Types

```cpp
inline constexpr std::size_t THP_STRING_SIZE = 64;

struct MemoryStats {
  // RAM usage (from /proc/meminfo)
  std::uint64_t totalBytes{0};      ///< MemTotal
  std::uint64_t freeBytes{0};       ///< MemFree
  std::uint64_t availableBytes{0};  ///< MemAvailable
  std::uint64_t buffersBytes{0};    ///< Buffers
  std::uint64_t cachedBytes{0};     ///< Cached + SReclaimable

  // Swap usage
  std::uint64_t swapTotalBytes{0};  ///< SwapTotal
  std::uint64_t swapFreeBytes{0};   ///< SwapFree

  // VM policies (-1 if unavailable)
  int swappiness{-1};       ///< 0-100, tendency to swap
  int zoneReclaimMode{-1};  ///< NUMA zone reclaim policy
  int overcommitMemory{-1}; ///< 0=heuristic, 1=always, 2=never

  // Transparent Huge Pages
  std::array<char, THP_STRING_SIZE> thpEnabled{};  ///< e.g., "[always] madvise never"
  std::array<char, THP_STRING_SIZE> thpDefrag{};   ///< THP defrag policy

  // Computed values
  std::uint64_t usedBytes() const noexcept;
  std::uint64_t swapUsedBytes() const noexcept;
  double utilizationPercent() const noexcept;
  double swapUtilizationPercent() const noexcept;
  bool isTHPEnabled() const noexcept;
  bool isSwappinessLow() const noexcept;  // <= 10
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect memory statistics (RT-safe: bounded file reads)
[[nodiscard]] MemoryStats getMemoryStats() noexcept;
```

#### Usage

```cpp
using namespace seeker::memory;

auto stats = getMemoryStats();

fmt::print("RAM: {:.1f}% used ({} / {} bytes)\n",
           stats.utilizationPercent(),
           stats.usedBytes(),
           stats.totalBytes);

// Check RT-relevant policies
if (stats.swappiness > 10) {
  fmt::print("Warning: swappiness={} may cause jitter\n", stats.swappiness);
}

if (stats.isTHPEnabled()) {
  fmt::print("Warning: THP enabled, may cause latency spikes\n");
}
```

#### Data Sources

- `/proc/meminfo` - RAM and swap totals
- `/proc/sys/vm/swappiness` - Swap tendency
- `/proc/sys/vm/zone_reclaim_mode` - NUMA zone reclaim
- `/proc/sys/vm/overcommit_memory` - Overcommit policy
- `/sys/kernel/mm/transparent_hugepage/enabled` - THP mode
- `/sys/kernel/mm/transparent_hugepage/defrag` - THP defrag

---

### NumaTopology

**Header:** `NumaTopology.hpp`
**Purpose:** NUMA node layout, per-node memory/CPUs, and inter-node distances.

#### Key Types

```cpp
inline constexpr std::size_t MAX_NUMA_NODES = 64;
inline constexpr std::size_t MAX_CPUS_PER_NODE = 256;
inline constexpr std::uint8_t NUMA_DISTANCE_INVALID = 255;
inline constexpr std::uint8_t NUMA_DISTANCE_LOCAL = 10;

struct NumaNodeInfo {
  int nodeId{-1};                    ///< NUMA node ID (0-based)
  std::uint64_t totalBytes{0};       ///< Total memory on this node
  std::uint64_t freeBytes{0};        ///< Free memory on this node
  int cpuIds[MAX_CPUS_PER_NODE]{};   ///< CPUs belonging to this node
  std::size_t cpuCount{0};           ///< Valid entries in cpuIds[]

  std::uint64_t usedBytes() const noexcept;
  bool hasCpu(int cpuId) const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct NumaTopology {
  NumaNodeInfo nodes[MAX_NUMA_NODES]{};
  std::size_t nodeCount{0};

  /// Distance matrix: distance[from][to] = relative latency
  /// Values: 10 = local, 20-40 typical remote, 255 = invalid
  std::uint8_t distance[MAX_NUMA_NODES][MAX_NUMA_NODES]{};

  bool isNuma() const noexcept;  // nodeCount > 1
  std::uint64_t totalMemoryBytes() const noexcept;
  std::uint64_t freeMemoryBytes() const noexcept;
  int findNodeForCpu(int cpuId) const noexcept;
  std::uint8_t getDistance(std::size_t from, std::size_t to) const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect NUMA topology (NOT RT-safe: directory scanning)
[[nodiscard]] NumaTopology getNumaTopology() noexcept;
```

#### Usage

```cpp
using namespace seeker::memory;

auto numa = getNumaTopology();

if (numa.isNuma()) {
  fmt::print("NUMA system: {} nodes\n", numa.nodeCount);

  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    const auto& node = numa.nodes[i];
    fmt::print("Node {}: {} CPUs, {} bytes memory\n",
               node.nodeId, node.cpuCount, node.totalBytes);
  }

  // Check inter-node latency
  auto dist = numa.getDistance(0, 1);
  fmt::print("Distance node 0 -> 1: {}\n", dist);
} else {
  fmt::print("UMA system (single NUMA node)\n");
}
```

#### Data Sources

- `/sys/devices/system/node/nodeN/meminfo` - Per-node memory
- `/sys/devices/system/node/nodeN/cpulist` - Per-node CPUs
- `/sys/devices/system/node/nodeN/distance` - NUMA distances

---

### HugepageStatus

**Header:** `HugepageStatus.hpp`
**Purpose:** Hugepage allocation counts per size and per NUMA node.

#### Key Types

```cpp
inline constexpr std::size_t HP_MAX_SIZES = 8;
inline constexpr std::size_t HP_MAX_NUMA_NODES = 64;

struct HugepageSizeStatus {
  std::uint64_t pageSize{0};   ///< Page size in bytes
  std::uint64_t total{0};      ///< nr_hugepages: configured
  std::uint64_t free{0};       ///< free_hugepages: available
  std::uint64_t reserved{0};   ///< resv_hugepages: reserved
  std::uint64_t surplus{0};    ///< surplus_hugepages: over-allocated

  std::uint64_t used() const noexcept;       // total + surplus - free
  std::uint64_t totalBytes() const noexcept;
  std::uint64_t freeBytes() const noexcept;
  std::uint64_t usedBytes() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct HugepageNodeStatus {
  int nodeId{-1};
  std::uint64_t total{0};
  std::uint64_t free{0};
  std::uint64_t surplus{0};
};

struct HugepageStatus {
  HugepageSizeStatus sizes[HP_MAX_SIZES]{};
  std::size_t sizeCount{0};

  /// Per-NUMA node allocation [sizeIdx][nodeIdx]
  HugepageNodeStatus perNode[HP_MAX_SIZES][HP_MAX_NUMA_NODES]{};
  std::size_t nodeCount{0};

  bool hasHugepages() const noexcept;
  std::uint64_t totalBytes() const noexcept;
  std::uint64_t freeBytes() const noexcept;
  std::uint64_t usedBytes() const noexcept;
  const HugepageSizeStatus* findSize(std::uint64_t pageSize) const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect hugepage status (NOT RT-safe: directory scanning)
[[nodiscard]] HugepageStatus getHugepageStatus() noexcept;
```

#### Usage

```cpp
using namespace seeker::memory;

auto hp = getHugepageStatus();

if (!hp.hasHugepages()) {
  fmt::print("No hugepages configured\n");
  return;
}

for (std::size_t i = 0; i < hp.sizeCount; ++i) {
  const auto& s = hp.sizes[i];
  fmt::print("{} bytes: {} total, {} free, {} used\n",
             s.pageSize, s.total, s.free, s.used());
}

// Check specific size
constexpr std::uint64_t SIZE_2M = 2 * 1024 * 1024;
if (const auto* s = hp.findSize(SIZE_2M)) {
  if (s->free > 0) {
    fmt::print("{} 2MiB hugepages available\n", s->free);
  }
}
```

#### Data Sources

- `/sys/kernel/mm/hugepages/hugepages-NkB/nr_hugepages` - Total configured
- `/sys/kernel/mm/hugepages/hugepages-NkB/free_hugepages` - Currently free
- `/sys/kernel/mm/hugepages/hugepages-NkB/resv_hugepages` - Reserved
- `/sys/kernel/mm/hugepages/hugepages-NkB/surplus_hugepages` - Surplus
- `/sys/devices/system/node/nodeN/hugepages/hugepages-NkB/` - Per-NUMA stats

---

### MemoryLocking

**Header:** `MemoryLocking.hpp`
**Purpose:** Validate mlock capabilities for RT memory pinning.

#### Key Types

```cpp
inline constexpr std::uint64_t MLOCK_UNLIMITED = static_cast<std::uint64_t>(-1);

struct MemoryLockingStatus {
  std::uint64_t softLimitBytes{0};     ///< RLIMIT_MEMLOCK soft limit
  std::uint64_t hardLimitBytes{0};     ///< RLIMIT_MEMLOCK hard limit
  std::uint64_t currentLockedBytes{0}; ///< VmLck from /proc/self/status
  bool hasCapIpcLock{false};           ///< CAP_IPC_LOCK capability
  bool isRoot{false};                  ///< Running as uid 0

  bool isUnlimited() const noexcept;   // CAP_IPC_LOCK, root, or unlimited limit
  bool canLock(std::uint64_t bytes) const noexcept;
  std::uint64_t availableBytes() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct MlockallStatus {
  bool canLockCurrent{false};   ///< MCL_CURRENT would succeed
  bool canLockFuture{false};    ///< MCL_FUTURE would succeed
  bool isCurrentlyLocked{false}; ///< mlockall() already active
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Collect mlock limits and capabilities (RT-safe: bounded reads)
[[nodiscard]] MemoryLockingStatus getMemoryLockingStatus() noexcept;

/// Check mlockall() capability (RT-safe)
[[nodiscard]] MlockallStatus getMlockallStatus() noexcept;

/// Check CAP_IPC_LOCK capability (RT-safe: single syscall)
[[nodiscard]] bool hasCapIpcLock() noexcept;
```

#### Usage

```cpp
using namespace seeker::memory;

auto ml = getMemoryLockingStatus();

// Check if we can lock RT buffers
constexpr std::uint64_t RT_BUFFER_SIZE = 256 * 1024 * 1024;  // 256 MiB

if (ml.isUnlimited()) {
  fmt::print("Unlimited mlock - RT ready\n");
} else if (ml.canLock(RT_BUFFER_SIZE)) {
  fmt::print("Can lock {} bytes (limit: {})\n",
             RT_BUFFER_SIZE, ml.softLimitBytes);
} else {
  fmt::print("Cannot lock {} bytes (available: {})\n",
             RT_BUFFER_SIZE, ml.availableBytes());
  fmt::print("Recommendation: Grant CAP_IPC_LOCK or increase RLIMIT_MEMLOCK\n");
}

// Check mlockall() capability
auto mla = getMlockallStatus();
if (mla.canLockCurrent && mla.canLockFuture) {
  fmt::print("mlockall(MCL_CURRENT | MCL_FUTURE) will succeed\n");
}
```

#### Data Sources

- `getrlimit(RLIMIT_MEMLOCK)` - Soft/hard limits
- `/proc/self/status` (VmLck) - Currently locked bytes
- `capget(2)` - CAP_IPC_LOCK check
- `getuid(2)` - Root check

---

### EdacStatus

**Header:** `EdacStatus.hpp`
**Purpose:** ECC memory error detection via Linux EDAC subsystem - critical for radiation environments.

#### Overview

The EDAC (Error Detection And Correction) subsystem monitors ECC memory for:

- **Correctable Errors (CE):** Single-bit errors corrected by ECC - memory still functional
- **Uncorrectable Errors (UE):** Multi-bit errors that cannot be corrected - data corruption

This is essential for:

- Spacecraft and satellite systems (radiation-induced bit flips)
- High-altitude aviation systems
- High-reliability server environments
- Any system where memory integrity is critical

#### Key Types

```cpp
inline constexpr std::size_t EDAC_MAX_MC = 8;       ///< Max memory controllers
inline constexpr std::size_t EDAC_MAX_CSROW = 32;   ///< Max chip-select rows
inline constexpr std::size_t EDAC_MAX_DIMM = 32;    ///< Max DIMMs
inline constexpr std::size_t EDAC_LABEL_SIZE = 32;  ///< Label string size
inline constexpr std::size_t EDAC_TYPE_SIZE = 64;   ///< Type string size

struct MemoryController {
  std::array<char, EDAC_LABEL_SIZE> name{};     ///< mc0, mc1, etc.
  std::array<char, EDAC_TYPE_SIZE> mcType{};    ///< EDAC driver type
  std::array<char, EDAC_LABEL_SIZE> edacMode{}; ///< SECDED, S4ECD4ED, etc.
  std::array<char, EDAC_TYPE_SIZE> memType{};   ///< DDR4, DDR5, etc.
  std::size_t sizeMb{0};                        ///< Total size in MB
  std::uint64_t ceCount{0};                     ///< Correctable errors
  std::uint64_t ceNoInfoCount{0};               ///< CE with no location info
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors
  std::uint64_t ueNoInfoCount{0};               ///< UE with no location info
  std::size_t csrowCount{0};                    ///< Number of chip-select rows
  std::int32_t mcIndex{-1};                     ///< Controller index

  bool hasErrors() const noexcept;
  bool hasCriticalErrors() const noexcept;  // UE > 0
};

struct CsRow {
  std::array<char, EDAC_LABEL_SIZE> label{};    ///< Row label
  std::uint32_t mcIndex{0};                     ///< Parent controller
  std::uint32_t csrowIndex{0};                  ///< Row index
  std::uint64_t ceCount{0};                     ///< Correctable errors
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors
  std::size_t sizeMb{0};                        ///< Size in MB
  std::array<char, EDAC_LABEL_SIZE> memType{};  ///< Memory type
  std::array<char, EDAC_LABEL_SIZE> edacMode{}; ///< EDAC mode
};

struct DimmInfo {
  std::array<char, EDAC_LABEL_SIZE> label{};    ///< DIMM label
  std::array<char, EDAC_LABEL_SIZE> location{}; ///< Physical slot
  std::uint32_t mcIndex{0};                     ///< Parent controller
  std::uint32_t dimmIndex{0};                   ///< DIMM index
  std::uint64_t ceCount{0};                     ///< Correctable errors
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors
  std::size_t sizeMb{0};                        ///< Size in MB
  std::array<char, EDAC_LABEL_SIZE> memType{};  ///< Memory type
};

struct EdacStatus {
  std::array<MemoryController, EDAC_MAX_MC> controllers{};
  std::size_t mcCount{0};

  std::array<CsRow, EDAC_MAX_CSROW> csrows{};
  std::size_t csrowCount{0};

  std::array<DimmInfo, EDAC_MAX_DIMM> dimms{};
  std::size_t dimmCount{0};

  std::uint64_t totalCeCount{0};    ///< Total correctable errors
  std::uint64_t totalUeCount{0};    ///< Total uncorrectable errors

  bool edacSupported{false};        ///< EDAC subsystem present
  bool eccEnabled{false};           ///< ECC actually enabled
  std::uint64_t pollIntervalMs{0};  ///< EDAC polling interval

  std::int64_t lastCeTime{0};      ///< Timestamp of most recent CE (Unix epoch, 0 if unavailable)
  std::int64_t lastUeTime{0};      ///< Timestamp of most recent UE (Unix epoch, 0 if unavailable)

  bool hasErrors() const noexcept;
  bool hasCriticalErrors() const noexcept;  // UE > 0
  const MemoryController* findController(std::int32_t mcIndex) const noexcept;
  std::string toString() const;  // NOT RT-safe
  std::string toJson() const;    // NOT RT-safe
};
```

#### API

```cpp
/// Collect EDAC status (RT-safe: bounded sysfs reads)
[[nodiscard]] EdacStatus getEdacStatus() noexcept;

/// Check if EDAC subsystem is available (RT-safe: single stat)
[[nodiscard]] bool isEdacSupported() noexcept;
```

#### Usage

```cpp
using namespace seeker::memory;

auto edac = getEdacStatus();

if (!edac.edacSupported) {
  fmt::print("EDAC not available (no ECC memory or module not loaded)\n");
  return;
}

fmt::print("ECC Memory Status:\n");
fmt::print("  Controllers: {}\n", edac.mcCount);
fmt::print("  Correctable errors: {}\n", edac.totalCeCount);
fmt::print("  Uncorrectable errors: {}\n", edac.totalUeCount);

// Critical check for radiation environments
if (edac.hasCriticalErrors()) {
  fmt::print("CRITICAL: Uncorrectable memory errors detected!\n");
  fmt::print("Memory hardware failure - replace faulty DIMMs!\n");
}

// Monitor correctable errors (may indicate failing DIMM)
if (edac.totalCeCount > 100) {
  fmt::print("Warning: High correctable error count - monitor closely\n");
}

// Per-controller details
for (std::size_t i = 0; i < edac.mcCount; ++i) {
  const auto& mc = edac.controllers[i];
  fmt::print("  {}: {} CE={} UE={}\n",
             mc.name.data(), mc.mcType.data(), mc.ceCount, mc.ueCount);
}
```

#### Data Sources

- `/sys/devices/system/edac/mc/` - Memory controller presence
- `/sys/devices/system/edac/mc/mcN/ce_count` - Correctable error count
- `/sys/devices/system/edac/mc/mcN/ue_count` - Uncorrectable error count
- `/sys/devices/system/edac/mc/mcN/mc_name` - Controller type
- `/sys/devices/system/edac/mc/mcN/size_mb` - Controller memory size
- `/sys/devices/system/edac/mc/mcN/csrowN/` - Chip-select row info
- `/sys/devices/system/edac/mc/mcN/dimmN/` - DIMM-level info

#### Interpretation Guide

| Condition                | Meaning                                 | Action                                     |
| ------------------------ | --------------------------------------- | ------------------------------------------ |
| `edacSupported == false` | No ECC memory or EDAC module not loaded | Load edac_mc module or use ECC RAM         |
| `eccEnabled == false`    | EDAC present but no controllers         | Check BIOS ECC settings                    |
| `totalCeCount > 0`       | Correctable errors (soft errors)        | Monitor trend, memory still functional     |
| `totalCeCount > 100`     | High CE count                           | May indicate failing DIMM                  |
| `totalUeCount > 0`       | **CRITICAL:** Uncorrectable errors      | Data corruption possible, replace hardware |

---

## Common Patterns

### Startup Validation

Query static info once at startup, validate RT requirements:

```cpp
using namespace seeker::memory;

void validateMemoryConfig() {
  // Static checks (run once)
  auto pages = getPageSizes();
  auto numa = getNumaTopology();
  auto ml = getMemoryLockingStatus();
  auto edac = getEdacStatus();

  // Verify hugepages available
  if (!pages.hasHugePages()) {
    fmt::print("Warning: No hugepage support\n");
  }

  // Verify mlock capability
  if (!ml.isUnlimited() && ml.softLimitBytes < 64 * 1024 * 1024) {
    fmt::print("Warning: mlock limit only {} bytes\n", ml.softLimitBytes);
  }

  // Check ECC status (critical for radiation environments)
  if (edac.edacSupported && edac.hasCriticalErrors()) {
    fmt::print("CRITICAL: Memory hardware errors detected!\n");
  }

  // Log NUMA topology
  if (numa.isNuma()) {
    fmt::print("NUMA system: {} nodes\n", numa.nodeCount);
  }
}
```

### Periodic Monitoring

Poll dynamic state periodically:

```cpp
using namespace seeker::memory;

void monitorMemory() {
  auto stats = getMemoryStats();
  auto hp = getHugepageStatus();
  auto edac = getEdacStatus();

  // Check memory pressure
  if (stats.utilizationPercent() > 90.0) {
    fmt::print("Warning: Memory utilization at {:.1f}%\n",
               stats.utilizationPercent());
  }

  // Check hugepage availability
  if (hp.hasHugepages() && hp.freeBytes() == 0) {
    fmt::print("Warning: No free hugepages\n");
  }

  // Check for new memory errors
  if (edac.edacSupported && edac.hasErrors()) {
    fmt::print("Memory errors: CE={} UE={}\n",
               edac.totalCeCount, edac.totalUeCount);
  }
}
```

### NUMA-Aware Allocation

Use NUMA info for memory placement decisions:

```cpp
using namespace seeker::memory;

int selectNodeForCpu(int targetCpu) {
  auto numa = getNumaTopology();
  return numa.findNodeForCpu(targetCpu);
}

void checkNumaLocality(int cpu1, int cpu2) {
  auto numa = getNumaTopology();

  int node1 = numa.findNodeForCpu(cpu1);
  int node2 = numa.findNodeForCpu(cpu2);

  if (node1 != node2) {
    auto dist = numa.getDistance(node1, node2);
    fmt::print("CPUs {} and {} on different NUMA nodes (distance: {})\n",
               cpu1, cpu2, dist);
  }
}
```

---

## Real-Time Considerations

### RT-Safe Functions

These can be called from RT threads:

- `getPageSizes()` - Bounded syscall + directory scan
- `getMemoryStats()` - Bounded file reads
- `getMemoryLockingStatus()` - Bounded syscalls and file reads
- `getMlockallStatus()` - Based on getMemoryLockingStatus()
- `hasCapIpcLock()` - Single syscall
- `getEdacStatus()` - Bounded sysfs reads
- `isEdacSupported()` - Single stat() call

### NOT RT-Safe Functions

Call these from setup/monitoring threads only:

- `getNumaTopology()` - Directory scanning, multiple file reads
- `getHugepageStatus()` - Directory scanning, multiple file reads
- All `toString()` and `toJson()` methods - String allocation

### Recommended RT Memory Configuration

For best RT performance, verify these with the diagnostics:

1. **Hugepages:** Pre-allocate sufficient 2MiB or 1GiB hugepages at boot
2. **mlock:** Unlimited via CAP_IPC_LOCK or sufficient RLIMIT_MEMLOCK
3. **THP:** Disabled (`never`) or opt-in (`madvise`) to avoid compaction stalls
4. **Swappiness:** Low value (0-10) to prevent swap activity
5. **Overcommit:** Mode 0 (heuristic) or 2 (never) to prevent OOM surprises
6. **NUMA:** Pin RT threads and their memory to the same NUMA node
7. **ECC:** Monitor for errors, especially in radiation environments

### Memory Locking Checklist

Before calling `mlockall()` or `mlock()`:

```cpp
using namespace seeker::memory;

bool canProceedWithMlock(std::uint64_t requiredBytes) {
  auto ml = getMemoryLockingStatus();

  if (ml.isUnlimited()) {
    return true;  // CAP_IPC_LOCK or root
  }

  if (!ml.canLock(requiredBytes)) {
    fmt::print(stderr,
               "Error: Cannot lock {} bytes (available: {})\n"
               "Solutions:\n"
               "  1. Grant CAP_IPC_LOCK: setcap cap_ipc_lock+ep <binary>\n"
               "  2. Increase limit in /etc/security/limits.conf:\n"
               "     <user> - memlock unlimited\n"
               "  3. Run as root (not recommended)\n",
               requiredBytes, ml.availableBytes());
    return false;
  }

  return true;
}
```

---

## CLI Tools

The memory domain includes 3 command-line tools: `mem-info`, `mem-rtcheck`, `mem-numa`.

See: `tools/cpp/memory/README.md` for detailed tool usage.

---

## Example: RT Memory Validation

```cpp
#include "src/memory/inc/EdacStatus.hpp"
#include "src/memory/inc/HugepageStatus.hpp"
#include "src/memory/inc/MemoryLocking.hpp"
#include "src/memory/inc/MemoryStats.hpp"
#include "src/memory/inc/NumaTopology.hpp"
#include "src/memory/inc/PageSizes.hpp"

#include <fmt/core.h>

using namespace seeker::memory;

int main() {
  fmt::print("=== RT Memory Validation ===\n\n");

  // 1. Page Sizes
  auto pages = getPageSizes();
  fmt::print("Page Sizes:\n");
  fmt::print("  Base: {} bytes\n", pages.basePageBytes);
  fmt::print("  Hugepages: {}\n", pages.hasHugePages() ? "available" : "none");

  // 2. Memory Stats
  auto stats = getMemoryStats();
  fmt::print("\nMemory:\n");
  fmt::print("  Total: {} bytes\n", stats.totalBytes);
  fmt::print("  Available: {} bytes ({:.1f}%)\n",
             stats.availableBytes,
             100.0 * stats.availableBytes / stats.totalBytes);

  // 3. VM Policy Checks
  fmt::print("\nVM Policies:\n");

  if (stats.swappiness > 10) {
    fmt::print("  [WARN] Swappiness: {} (recommend <= 10)\n", stats.swappiness);
  } else {
    fmt::print("  [OK] Swappiness: {}\n", stats.swappiness);
  }

  if (stats.isTHPEnabled()) {
    fmt::print("  [WARN] THP enabled (recommend madvise or never)\n");
  } else {
    fmt::print("  [OK] THP: {}\n", stats.thpEnabled.data());
  }

  // 4. Hugepage Status
  auto hp = getHugepageStatus();
  fmt::print("\nHugepages:\n");
  if (hp.hasHugepages()) {
    fmt::print("  Total: {} bytes\n", hp.totalBytes());
    fmt::print("  Free: {} bytes\n", hp.freeBytes());
  } else {
    fmt::print("  [WARN] No hugepages configured\n");
  }

  // 5. Memory Locking
  auto ml = getMemoryLockingStatus();
  fmt::print("\nMemory Locking:\n");
  if (ml.isUnlimited()) {
    fmt::print("  [OK] Unlimited mlock\n");
  } else {
    fmt::print("  [WARN] Limited to {} bytes\n", ml.softLimitBytes);
    fmt::print("  Recommendation: Grant CAP_IPC_LOCK\n");
  }

  // 6. NUMA Topology
  auto numa = getNumaTopology();
  fmt::print("\nNUMA:\n");
  if (numa.isNuma()) {
    fmt::print("  {} nodes, {} total memory\n",
               numa.nodeCount, numa.totalMemoryBytes());
  } else {
    fmt::print("  UMA system (single node)\n");
  }

  // 7. ECC Memory Status
  auto edac = getEdacStatus();
  fmt::print("\nECC/EDAC:\n");
  if (!edac.edacSupported) {
    fmt::print("  [SKIP] EDAC not available\n");
  } else if (edac.hasCriticalErrors()) {
    fmt::print("  [FAIL] Uncorrectable errors: {} - REPLACE MEMORY!\n",
               edac.totalUeCount);
  } else if (edac.totalCeCount > 100) {
    fmt::print("  [WARN] High correctable error count: {}\n", edac.totalCeCount);
  } else {
    fmt::print("  [OK] ECC enabled, {} controllers, {} CE, {} UE\n",
               edac.mcCount, edac.totalCeCount, edac.totalUeCount);
  }

  return 0;
}
```

---

## See Also

- `seeker::cpu` - CPU telemetry (topology, frequency, utilization, IRQs)
- `seeker::gpu` - GPU telemetry (CUDA, NVML, PCIe)
- `seeker::timing` - Clock sources, timer latency
- `seeker::system` - Containers, RT readiness, drivers
