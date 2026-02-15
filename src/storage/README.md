# Storage Diagnostics Module

**Namespace:** `seeker::storage`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive storage telemetry for real-time and performance-critical systems. This module provides 5 focused components for monitoring block devices, mount configuration, I/O schedulers, I/O statistics, and storage performance characteristics.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [BlockDeviceInfo](#blockdeviceinfo) - Block device enumeration and properties
   - [MountInfo](#mountinfo) - Mount table and filesystem options
   - [IoScheduler](#ioscheduler) - I/O scheduler configuration
   - [IoStats](#iostats) - I/O statistics with snapshot+delta
   - [StorageBench](#storagebench) - Bounded filesystem benchmarks
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT Storage Validation](#example-rt-storage-validation)

---

## Overview

The storage diagnostics module answers these questions for RT and HPC systems:

| Question                                              | Module            |
| ----------------------------------------------------- | ----------------- |
| What block devices are available (NVMe, SSD, HDD)?    | `BlockDeviceInfo` |
| What are device properties (size, sector sizes)?      | `BlockDeviceInfo` |
| Does the device support TRIM/discard?                 | `BlockDeviceInfo` |
| What filesystems are mounted and where?               | `MountInfo`       |
| What mount options are in effect (noatime, barriers)? | `MountInfo`       |
| What I/O scheduler is configured?                     | `IoScheduler`     |
| Is the scheduler RT-friendly?                         | `IoScheduler`     |
| What is the queue depth and read-ahead configuration? | `IoScheduler`     |
| What is the current I/O throughput and latency?       | `IoStats`         |
| What is device utilization and queue depth?           | `IoStats`         |
| What is the actual fsync latency on this storage?     | `StorageBench`    |
| What throughput can this device sustain?              | `StorageBench`    |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/storage/inc/MountInfo.hpp"
#include "src/storage/inc/IoScheduler.hpp"
#include "src/storage/inc/IoStats.hpp"
#include "src/storage/inc/StorageBench.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::storage;

// Static system info (query once at startup)
auto devices = getBlockDevices();             // All block devices
auto device = getBlockDevice("nvme0n1");      // Specific device
auto mounts = getMountTable();                // All mounts
auto mount = getMountForPath("/data");        // Mount containing path

// Dynamic state (query periodically)
auto sched = getIoSchedulerConfig("nvme0n1"); // Scheduler config
```

### Snapshot + Delta Pattern

For I/O statistics (throughput, IOPS, latency):

```cpp
using namespace seeker::storage;

// Take two snapshots with a delay
auto before = getIoStatsSnapshot("nvme0n1");
std::this_thread::sleep_for(std::chrono::seconds(1));
auto after = getIoStatsSnapshot("nvme0n1");

// Compute delta (IOPS, throughput, latency)
auto delta = computeIoStatsDelta(before, after);

// Now delta.readIops, delta.writeBytesPerSec, delta.avgWriteLatencyMs are populated
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
 * @brief Get properties for a specific block device.
 * @param name Device name (e.g., "nvme0n1", "sda").
 * @return Device properties (zeroed if device not found).
 * @note RT-safe: Bounded file reads from /sys/block/<name>/.
 */
[[nodiscard]] BlockDevice getBlockDevice(const char* name) noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, DEVICE_NAME_SIZE> name{};
std::array<char, MODEL_STRING_SIZE> model{};

// Instead of std::vector (allocates)
BlockDevice devices[MAX_BLOCK_DEVICES]{};
MountEntry mounts[MAX_MOUNTS]{};
```

This allows snapshot functions to be RT-safe.

### Snapshot + Delta Separation

For counter-based metrics (IoStats), we separate:

1. **Snapshot** - Raw counters (RT-safe, cheap)
2. **Delta computation** - Rates and averages (RT-safe, pure function)
3. **toString()** - Human-readable output (NOT RT-safe, allocates)

This lets RT code collect snapshots in the hot path and defer formatting to cold paths.

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto device = getBlockDevice("nonexistent");
if (device.sizeBytes == 0) {
  // Device not found - handle gracefully
}

auto sched = getIoSchedulerConfig("nvme0n1");
if (sched.current[0] == '\0') {
  // Scheduler info not available
}
```

---

## Module Reference

---

### BlockDeviceInfo

**Header:** `BlockDeviceInfo.hpp`
**Purpose:** Enumerate block devices and query hardware properties.

#### Key Types

```cpp
inline constexpr std::size_t DEVICE_NAME_SIZE = 32;
inline constexpr std::size_t MODEL_STRING_SIZE = 64;
inline constexpr std::size_t MAX_BLOCK_DEVICES = 64;

struct BlockDevice {
  std::array<char, DEVICE_NAME_SIZE> name{};    ///< e.g., "nvme0n1", "sda"
  std::array<char, MODEL_STRING_SIZE> model{};  ///< Device model string
  std::array<char, MODEL_STRING_SIZE> vendor{}; ///< Vendor string

  std::uint64_t sizeBytes{0};         ///< Total capacity
  std::uint32_t logicalBlockSize{0};  ///< Logical sector size (typically 512)
  std::uint32_t physicalBlockSize{0}; ///< Physical sector size (512 or 4096)
  std::uint32_t minIoSize{0};         ///< Minimum I/O size for optimal performance
  std::uint32_t optimalIoSize{0};     ///< Optimal I/O size (0 if unknown)

  bool rotational{false};  ///< true for HDD, false for SSD/NVMe
  bool removable{false};   ///< Removable media (USB, SD)
  bool hasTrim{false};     ///< Supports TRIM/discard

  // Type helpers
  [[nodiscard]] bool isNvme() const noexcept;
  [[nodiscard]] bool isSsd() const noexcept;
  [[nodiscard]] bool isHdd() const noexcept;
  [[nodiscard]] bool isAdvancedFormat() const noexcept;   // 4K physical sectors
  [[nodiscard]] const char* deviceType() const noexcept;  // "NVMe", "SSD", "HDD", "Unknown"
  [[nodiscard]] std::string toString() const;             // NOT RT-safe: allocates
};

struct BlockDeviceList {
  BlockDevice devices[MAX_BLOCK_DEVICES]{};
  std::size_t count{0};

  [[nodiscard]] const BlockDevice* find(const char* name) const noexcept;
  [[nodiscard]] std::size_t countNvme() const noexcept;
  [[nodiscard]] std::size_t countSsd() const noexcept;
  [[nodiscard]] std::size_t countHdd() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe: allocates
};
```

#### API

```cpp
/// Enumerate all block devices (NOT RT-safe: directory iteration)
[[nodiscard]] BlockDeviceList getBlockDevices() noexcept;

/// Get specific device properties (RT-safe: bounded file reads)
[[nodiscard]] BlockDevice getBlockDevice(const char* name) noexcept;

/// Format bytes as human-readable (NOT RT-safe: allocates string)
[[nodiscard]] std::string formatCapacity(std::uint64_t bytes);
```

#### Usage

```cpp
using namespace seeker::storage;

auto devices = getBlockDevices();
for (std::size_t i = 0; i < devices.count; ++i) {
  const auto& dev = devices.devices[i];
  fmt::print("{}: {} {} [{}]\n",
             dev.name.data(), dev.vendor.data(),
             dev.model.data(), dev.deviceType());
}

// Check specific device
auto nvme = getBlockDevice("nvme0n1");
if (nvme.isNvme() && nvme.hasTrim) {
  // NVMe with TRIM support - optimal for RT
}
```

#### Data Sources

- `/sys/block/` - Device enumeration
- `/sys/block/<dev>/size` - Capacity in 512-byte sectors
- `/sys/block/<dev>/queue/rotational` - Device type (0=SSD/NVMe, 1=HDD)
- `/sys/block/<dev>/queue/discard_max_bytes` - TRIM support
- `/sys/block/<dev>/queue/logical_block_size` - Logical sector size
- `/sys/block/<dev>/queue/physical_block_size` - Physical sector size
- `/sys/block/<dev>/device/model` - Model string
- `/sys/block/<dev>/device/vendor` - Vendor string

---

### MountInfo

**Header:** `MountInfo.hpp`
**Purpose:** Query mounted filesystems and mount options.

#### Key Types

```cpp
inline constexpr std::size_t PATH_SIZE = 256;
inline constexpr std::size_t FSTYPE_SIZE = 32;
inline constexpr std::size_t MOUNT_OPTIONS_SIZE = 512;
inline constexpr std::size_t MAX_MOUNTS = 128;
inline constexpr std::size_t MOUNT_DEVICE_NAME_SIZE = 32;

struct MountEntry {
  std::array<char, PATH_SIZE> mountPoint{};           ///< Mount path (e.g., "/", "/home")
  std::array<char, PATH_SIZE> device{};               ///< Device path (e.g., /dev/nvme0n1p2)
  std::array<char, MOUNT_DEVICE_NAME_SIZE> devName{}; ///< Base device name (e.g., "nvme0n1")
  std::array<char, FSTYPE_SIZE> fsType{};             ///< Filesystem type (ext4, xfs, etc.)
  std::array<char, MOUNT_OPTIONS_SIZE> options{};     ///< Mount options string

  // Filesystem type helpers
  [[nodiscard]] bool isBlockDevice() const noexcept;   // /dev/* device
  [[nodiscard]] bool isNetworkFs() const noexcept;     // NFS, CIFS, etc.
  [[nodiscard]] bool isTmpFs() const noexcept;         // tmpfs, ramfs

  // Option helpers
  [[nodiscard]] bool isReadOnly() const noexcept;      // ro option
  [[nodiscard]] bool hasNoAtime() const noexcept;      // noatime option
  [[nodiscard]] bool hasNoDirAtime() const noexcept;   // nodiratime option
  [[nodiscard]] bool hasRelAtime() const noexcept;     // relatime option
  [[nodiscard]] bool hasNoBarrier() const noexcept;    // nobarrier/barrier=0 option
  [[nodiscard]] bool isSync() const noexcept;          // sync option

  // ext4-specific
  [[nodiscard]] const char* ext4DataMode() const noexcept; // ordered/journal/writeback

  [[nodiscard]] std::string toString() const;  // NOT RT-safe: allocates
};

struct MountTable {
  MountEntry mounts[MAX_MOUNTS]{};
  std::size_t count{0};

  [[nodiscard]] const MountEntry* findByMountPoint(const char* path) const noexcept;
  [[nodiscard]] const MountEntry* findForPath(const char* path) const noexcept;
  [[nodiscard]] const MountEntry* findByDevice(const char* devName) const noexcept;
  [[nodiscard]] std::size_t countBlockDevices() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe: allocates
};
```

#### API

```cpp
/// Get all mounts (NOT RT-safe: file parsing)
[[nodiscard]] MountTable getMountTable() noexcept;

/// Find mount containing a path (RT-safe: bounded scan)
[[nodiscard]] MountEntry getMountForPath(const char* path) noexcept;
```

#### Usage

```cpp
using namespace seeker::storage;

auto mounts = getMountTable();

// Find mount for a specific path
auto mount = getMountForPath("/home/user/data");
if (mount.isBlockDevice()) {
  fmt::print("{} mounted on {} ({})\n",
             mount.device.data(),
             mount.mountPoint.data(),
             mount.fsType.data());

  if (!mount.hasNoAtime()) {
    fmt::print("  Warning: atime updates enabled (adds I/O overhead)\n");
  }
}
```

#### Data Sources

- `/proc/mounts` - Mount table

---

### IoScheduler

**Header:** `IoScheduler.hpp`
**Purpose:** Query and assess I/O scheduler configuration for RT suitability.

#### Key Types

```cpp
inline constexpr std::size_t SCHEDULER_NAME_SIZE = 32;
inline constexpr std::size_t MAX_SCHEDULERS = 8;
inline constexpr std::size_t SCHED_DEVICE_NAME_SIZE = 32;

struct IoSchedulerConfig {
  std::array<char, SCHED_DEVICE_NAME_SIZE> device{};                 ///< Device name
  std::array<char, SCHEDULER_NAME_SIZE> current{};                   ///< Active scheduler
  std::array<char, SCHEDULER_NAME_SIZE> available[MAX_SCHEDULERS]{}; ///< Available schedulers
  std::size_t availableCount{0};

  // Queue parameters
  std::int32_t nrRequests{-1};   ///< Queue depth (nr_requests)
  std::int32_t readAheadKb{-1};  ///< Read-ahead in KB
  std::int32_t maxSectorsKb{-1}; ///< Max request size
  std::int32_t rqAffinity{-1};   ///< Request CPU affinity
  std::int32_t noMerges{-1};     ///< 0=merge, 1=nomerge, 2=try-nomerge

  bool iostatsEnabled{false};    ///< I/O stats collection enabled
  bool addRandom{false};         ///< Contribute to entropy pool

  // RT assessment helpers
  [[nodiscard]] bool isNoneScheduler() const noexcept;      // "none" (bypass)
  [[nodiscard]] bool isMqDeadline() const noexcept;         // "mq-deadline"
  [[nodiscard]] bool isRtFriendly() const noexcept;         // none or mq-deadline
  [[nodiscard]] bool isReadAheadLow() const noexcept;       // read-ahead <= 128 KB
  [[nodiscard]] bool hasScheduler(const char* name) const noexcept; // check availability
  [[nodiscard]] int rtScore() const noexcept;               // 0-100 RT suitability score
  [[nodiscard]] std::string toString() const;               // NOT RT-safe: allocates
  [[nodiscard]] std::string rtAssessment() const;           // NOT RT-safe: allocates
};
```

#### RT Score Calculation

The `rtScore()` method returns 0-100 based on:

| Factor       | Points | Best Value                     |
| ------------ | ------ | ------------------------------ |
| Scheduler    | 50     | none (NVMe), mq-deadline (HDD) |
| Read-ahead   | 20     | 0 KB (disabled)                |
| Merge policy | 15     | noMerges=1 or 2                |
| Queue depth  | 15     | <= 32                          |

#### API

```cpp
/// Get scheduler config for device (RT-safe: bounded file reads)
[[nodiscard]] IoSchedulerConfig getIoSchedulerConfig(const char* device) noexcept;
```

#### Usage

```cpp
using namespace seeker::storage;

auto sched = getIoSchedulerConfig("nvme0n1");

fmt::print("Scheduler: {} (RT score: {}/100)\n",
           sched.current.data(), sched.rtScore());

if (!sched.isRtFriendly()) {
  fmt::print("  Recommendation: echo none > /sys/block/{}/queue/scheduler\n",
             sched.device.data());
}

if (sched.nrRequests > 128) {
  fmt::print("  Warning: High queue depth ({}) may increase latency variance\n",
             sched.nrRequests);
}
```

#### Data Sources

- `/sys/block/<dev>/queue/scheduler` - Current and available schedulers
- `/sys/block/<dev>/queue/nr_requests` - Queue depth
- `/sys/block/<dev>/queue/read_ahead_kb` - Read-ahead size
- `/sys/block/<dev>/queue/max_sectors_kb` - Max request size
- `/sys/block/<dev>/queue/rq_affinity` - Request affinity
- `/sys/block/<dev>/queue/nomerges` - Merge policy
- `/sys/block/<dev>/queue/iostats` - Stats collection
- `/sys/block/<dev>/queue/add_random` - Entropy contribution

---

### IoStats

**Header:** `IoStats.hpp`
**Purpose:** Capture I/O statistics using snapshot + delta pattern.

#### Key Types

```cpp
inline constexpr std::size_t IOSTAT_DEVICE_NAME_SIZE = 32;

/// Raw I/O counters from /sys/block/<dev>/stat (cumulative since boot).
struct IoCounters {
  std::uint64_t readOps{0};       ///< Completed read operations
  std::uint64_t readMerges{0};    ///< Read requests merged
  std::uint64_t readSectors{0};   ///< Sectors read (512-byte units)
  std::uint64_t readTimeMs{0};    ///< Time spent reading (ms)

  std::uint64_t writeOps{0};      ///< Completed write operations
  std::uint64_t writeMerges{0};   ///< Write requests merged
  std::uint64_t writeSectors{0};  ///< Sectors written
  std::uint64_t writeTimeMs{0};   ///< Time spent writing (ms)

  std::uint64_t ioInFlight{0};    ///< Currently in-flight I/O (snapshot)

  std::uint64_t ioTimeMs{0};         ///< Total time spent doing I/O (ms)
  std::uint64_t weightedIoTimeMs{0}; ///< Weighted I/O time (for queue depth)

  // Discard counters (kernel 4.18+)
  std::uint64_t discardOps{0};      ///< TRIM/discard operations
  std::uint64_t discardMerges{0};   ///< Discard requests merged
  std::uint64_t discardSectors{0};  ///< Sectors discarded
  std::uint64_t discardTimeMs{0};   ///< Time spent discarding

  // Flush counters (kernel 5.5+)
  std::uint64_t flushOps{0};     ///< Flush operations
  std::uint64_t flushTimeMs{0};  ///< Time spent flushing

  // Helpers
  [[nodiscard]] std::uint64_t readBytes() const noexcept;   // sectors * 512
  [[nodiscard]] std::uint64_t writeBytes() const noexcept;  // sectors * 512
  [[nodiscard]] std::uint64_t totalOps() const noexcept;    // read + write ops
  [[nodiscard]] std::uint64_t totalBytes() const noexcept;  // read + write bytes
};

struct IoStatsSnapshot {
  std::array<char, IOSTAT_DEVICE_NAME_SIZE> device{};
  IoCounters counters{};         ///< Raw cumulative counters
  std::uint64_t timestampNs{0};  ///< CLOCK_MONOTONIC nanoseconds

  [[nodiscard]] std::string toString() const;  // NOT RT-safe: allocates
};

struct IoStatsDelta {
  std::array<char, IOSTAT_DEVICE_NAME_SIZE> device{};
  double intervalSec{0.0};           ///< Measurement interval (seconds)

  // IOPS (operations per second)
  double readIops{0.0};
  double writeIops{0.0};
  double totalIops{0.0};

  // Throughput (bytes per second)
  double readBytesPerSec{0.0};
  double writeBytesPerSec{0.0};
  double totalBytesPerSec{0.0};

  // Latency (ms per op)
  double avgReadLatencyMs{0.0};
  double avgWriteLatencyMs{0.0};

  // Utilization
  double utilizationPct{0.0};   ///< 0-100%
  double avgQueueDepth{0.0};

  // Merge efficiency
  double readMergesPct{0.0};
  double writeMergesPct{0.0};

  // Discard stats (if available)
  double discardIops{0.0};
  double discardBytesPerSec{0.0};

  // Helpers
  [[nodiscard]] bool isIdle() const noexcept;
  [[nodiscard]] bool isHighUtilization() const noexcept;  // >80%
  [[nodiscard]] std::string formatThroughput() const;     // NOT RT-safe: allocates
  [[nodiscard]] std::string toString() const;             // NOT RT-safe: allocates
};
```

#### API

```cpp
/// Capture I/O counters (RT-safe: single file read)
[[nodiscard]] IoStatsSnapshot getIoStatsSnapshot(const char* device) noexcept;

/// Compute rates from two snapshots (RT-safe: pure computation)
[[nodiscard]] IoStatsDelta computeIoStatsDelta(
    const IoStatsSnapshot& before,
    const IoStatsSnapshot& after) noexcept;
```

#### Usage

```cpp
using namespace seeker::storage;

// Monitor I/O over 1 second
auto snap1 = getIoStatsSnapshot("nvme0n1");
std::this_thread::sleep_for(std::chrono::seconds(1));
auto snap2 = getIoStatsSnapshot("nvme0n1");

auto delta = computeIoStatsDelta(snap1, snap2);

fmt::print("IOPS: {:.0f} read, {:.0f} write\n",
           delta.readIops, delta.writeIops);
fmt::print("Throughput: {:.1f} MB/s read, {:.1f} MB/s write\n",
           delta.readBytesPerSec / 1e6,
           delta.writeBytesPerSec / 1e6);
fmt::print("Latency: {:.2f}ms read, {:.2f}ms write\n",
           delta.avgReadLatencyMs, delta.avgWriteLatencyMs);
fmt::print("Utilization: {:.1f}%\n", delta.utilizationPct);
```

#### Data Sources

- `/sys/block/<dev>/stat` - I/O counters (13-17 fields depending on kernel)

---

### StorageBench

**Header:** `StorageBench.hpp`
**Purpose:** Bounded filesystem micro-benchmarks for performance characterization.

> **Warning:** NOT RT-safe. These benchmarks perform active I/O and should only be run during system characterization, not in RT paths.

#### Key Types

```cpp
inline constexpr std::size_t BENCH_PATH_SIZE = 256;
inline constexpr std::size_t DEFAULT_IO_SIZE = 4096;
inline constexpr std::size_t DEFAULT_DATA_SIZE = 64 * 1024 * 1024;  // 64 MiB
inline constexpr std::size_t DEFAULT_ITERATIONS = 1000;
inline constexpr double MAX_BENCH_TIME_SEC = 30.0;

struct BenchConfig {
  std::array<char, BENCH_PATH_SIZE> directory{};
  std::size_t ioSize{DEFAULT_IO_SIZE};       ///< I/O operation size
  std::size_t dataSize{DEFAULT_DATA_SIZE};   ///< Total data for throughput tests
  std::size_t iterations{DEFAULT_ITERATIONS}; ///< Iterations for latency tests
  double timeBudgetSec{MAX_BENCH_TIME_SEC};   ///< Max time per benchmark
  bool useDirectIo{false};  ///< O_DIRECT to bypass page cache
  bool useFsync{true};      ///< fsync after writes

  void setDirectory(const char* path) noexcept;
  [[nodiscard]] bool isValid() const noexcept;
};

struct BenchResult {
  bool success{false};
  double elapsedSec{0.0};
  std::size_t operations{0};
  std::size_t bytesTransferred{0};

  // Throughput (for seq read/write)
  double throughputBytesPerSec{0.0};

  // Latency stats (for fsync, random I/O)
  double avgLatencyUs{0.0};
  double minLatencyUs{0.0};
  double maxLatencyUs{0.0};
  double p99LatencyUs{0.0};

  [[nodiscard]] std::string formatThroughput() const;  // NOT RT-safe: allocates
  [[nodiscard]] std::string toString() const;          // NOT RT-safe: allocates
};

struct BenchSuite {
  BenchResult seqWrite;
  BenchResult seqRead;
  BenchResult fsyncLatency;
  BenchResult randRead;
  BenchResult randWrite;

  [[nodiscard]] bool allSuccess() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe: allocates
};
```

#### API

```cpp
/// Run individual benchmarks (NOT RT-safe: active I/O)
[[nodiscard]] BenchResult runSeqWriteBench(const BenchConfig& config) noexcept;
[[nodiscard]] BenchResult runSeqReadBench(const BenchConfig& config) noexcept;
[[nodiscard]] BenchResult runFsyncBench(const BenchConfig& config) noexcept;
[[nodiscard]] BenchResult runRandReadBench(const BenchConfig& config) noexcept;
[[nodiscard]] BenchResult runRandWriteBench(const BenchConfig& config) noexcept;

/// Run all benchmarks (NOT RT-safe)
[[nodiscard]] BenchSuite runBenchSuite(const BenchConfig& config) noexcept;
```

#### Usage

```cpp
using namespace seeker::storage;

BenchConfig config;
config.setDirectory("/tmp");
config.dataSize = 64 * 1024 * 1024;  // 64 MiB
config.iterations = 100;

auto suite = runBenchSuite(config);

if (suite.allSuccess()) {
  fmt::print("Sequential Write: {:.1f} MB/s\n",
             suite.seqWrite.throughputBytesPerSec / 1e6);
  fmt::print("Sequential Read:  {:.1f} MB/s\n",
             suite.seqRead.throughputBytesPerSec / 1e6);
  fmt::print("fsync p99:        {:.2f} us\n",
             suite.fsyncLatency.p99LatencyUs);
}
```

#### Benchmark Details

| Benchmark        | What it Measures                          | Key Metric       |
| ---------------- | ----------------------------------------- | ---------------- |
| Sequential Write | Sustained write throughput                | MB/s             |
| Sequential Read  | Sustained read throughput (may hit cache) | MB/s             |
| fsync Latency    | Time to persist data to storage           | p99 latency (us) |
| Random Read 4K   | Small random read latency                 | avg latency (us) |
| Random Write 4K  | Small random write latency                | avg latency (us) |

---

## Common Patterns

### Device Type Detection

```cpp
using namespace seeker::storage;

auto devices = getBlockDevices();

for (std::size_t i = 0; i < devices.count; ++i) {
  const auto& dev = devices.devices[i];

  if (dev.isNvme()) {
    // NVMe - use "none" scheduler
  } else if (dev.isSsd()) {
    // SATA SSD - "none" or "mq-deadline"
  } else if (dev.isHdd()) {
    // HDD - "mq-deadline" recommended
  }
}
```

### Finding the Right Mount

```cpp
using namespace seeker::storage;

void checkDataPath(const char* dataPath) {
  auto mount = getMountForPath(dataPath);

  if (!mount.isBlockDevice()) {
    fmt::print("Warning: {} is not on a block device\n", dataPath);
    return;
  }

  if (!mount.hasNoAtime() && !mount.hasRelAtime()) {
    fmt::print("Warning: atime updates enabled on {}\n", mount.mountPoint.data());
  }

  if (std::strcmp(mount.fsType.data(), "ext4") == 0) {
    const char* mode = mount.ext4DataMode();
    if (std::strcmp(mode, "journal") == 0) {
      fmt::print("Warning: ext4 data=journal mode has high overhead\n");
    }
  }
}
```

### Continuous I/O Monitoring

```cpp
using namespace seeker::storage;

void monitorIo(const char* device, int seconds) {
  auto prev = getIoStatsSnapshot(device);

  for (int i = 0; i < seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto curr = getIoStatsSnapshot(device);
    auto delta = computeIoStatsDelta(prev, curr);

    fmt::print("[{}s] IOPS: {:.0f}r/{:.0f}w  Util: {:.1f}%\n",
               i + 1, delta.readIops, delta.writeIops,
               delta.utilizationPct);

    prev = curr;
  }
}
```

---

## Real-Time Considerations

### RT-Safe Functions

These can be called from RT threads:

- `getBlockDevice()` - Bounded file reads for single device
- `getMountForPath()` - Bounded file read, single-pass scan
- `getIoSchedulerConfig()` - Bounded file reads
- `getIoStatsSnapshot()` - Single file read
- `computeIoStatsDelta()` - Pure computation, no I/O

### NOT RT-Safe Functions

Call these from setup/monitoring threads only:

- `getBlockDevices()` - Directory iteration
- `getMountTable()` - File parsing
- `runBenchSuite()` and all benchmark functions - Active I/O
- All `toString()`, `formatThroughput()`, and `formatCapacity()` functions - String allocation

### Recommended RT Storage Configuration

For best RT performance, verify these with the diagnostics:

1. **Device Type:** NVMe or SSD preferred; HDDs have unpredictable seek latency
2. **Scheduler:** `none` for NVMe, `mq-deadline` for SATA SSD/HDD
3. **Queue Depth:** Lower values (32-64) reduce latency variance
4. **Read-ahead:** Disable (0 KB) for random I/O workloads
5. **Mount Options:** `noatime` or `relatime` to reduce metadata I/O
6. **Filesystem:** ext4 with `data=ordered` (default) is good; avoid `data=journal`
7. **fsync Latency:** Characterize with StorageBench before committing to RT guarantees

### Storage Latency Checklist

```cpp
using namespace seeker::storage;

bool validateStorageForRt(const char* device, const char* mountPath) {
  // 1. Check device type
  auto dev = getBlockDevice(device);
  if (dev.isHdd()) {
    fmt::print("Warning: HDD has unpredictable latency\n");
  }

  // 2. Check scheduler
  auto sched = getIoSchedulerConfig(device);
  if (!sched.isRtFriendly()) {
    fmt::print("Warning: Scheduler '{}' not optimal for RT\n",
               sched.current.data());
  }

  // 3. Check mount options
  auto mount = getMountForPath(mountPath);
  if (!mount.hasNoAtime() && !mount.hasRelAtime()) {
    fmt::print("Warning: atime updates add I/O overhead\n");
  }

  // 4. Characterize fsync latency
  BenchConfig config;
  config.setDirectory(mountPath);
  config.iterations = 100;

  auto fsync = runFsyncBench(config);
  if (fsync.p99LatencyUs > 10000) {  // 10ms
    fmt::print("Warning: fsync p99 latency is {:.1f}ms\n",
               fsync.p99LatencyUs / 1000.0);
  }

  return sched.rtScore() >= 70;
}
```

---

## CLI Tools

The storage domain includes 4 command-line tools: `storage-info`, `storage-rtcheck`, `storage-iostat`, `storage-bench`.

See: `tools/cpp/storage/README.md` for detailed tool usage.

---

## Example: RT Storage Validation

```cpp
#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/storage/inc/IoScheduler.hpp"
#include "src/storage/inc/IoStats.hpp"
#include "src/storage/inc/MountInfo.hpp"
#include "src/storage/inc/StorageBench.hpp"

#include <chrono>
#include <fmt/core.h>
#include <thread>

using namespace seeker::storage;

int main() {
  fmt::print("=== RT Storage Validation ===\n\n");

  // 1. Enumerate devices
  auto devices = getBlockDevices();
  fmt::print("Block Devices: {} found\n", devices.count);
  fmt::print("  NVMe: {}, SSD: {}, HDD: {}\n",
             devices.countNvme(), devices.countSsd(), devices.countHdd());

  // 2. Check each device's RT suitability
  fmt::print("\nRT Assessment:\n");
  for (std::size_t i = 0; i < devices.count; ++i) {
    const auto& dev = devices.devices[i];
    auto sched = getIoSchedulerConfig(dev.name.data());

    fmt::print("  {}: {} scheduler={} score={}/100\n",
               dev.name.data(), dev.deviceType(),
               sched.current.data(), sched.rtScore());

    if (!sched.isRtFriendly()) {
      fmt::print("    -> Consider: echo {} > /sys/block/{}/queue/scheduler\n",
                 dev.isNvme() ? "none" : "mq-deadline",
                 dev.name.data());
    }
  }

  // 3. Check data directory mount
  const char* dataDir = "/tmp";
  auto mount = getMountForPath(dataDir);
  fmt::print("\nMount for {}:\n", dataDir);
  fmt::print("  Device: {} ({})\n", mount.device.data(), mount.fsType.data());
  fmt::print("  noatime: {}, relatime: {}\n",
             mount.hasNoAtime() ? "yes" : "no",
             mount.hasRelAtime() ? "yes" : "no");

  // 4. Quick I/O sample
  if (devices.count > 0) {
    const char* testDev = devices.devices[0].name.data();
    fmt::print("\nI/O Sample ({}, 1 second):\n", testDev);

    auto snap1 = getIoStatsSnapshot(testDev);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto snap2 = getIoStatsSnapshot(testDev);
    auto delta = computeIoStatsDelta(snap1, snap2);

    fmt::print("  IOPS: {:.0f}r/{:.0f}w\n", delta.readIops, delta.writeIops);
    fmt::print("  Latency: {:.2f}ms r / {:.2f}ms w\n",
               delta.avgReadLatencyMs, delta.avgWriteLatencyMs);
    fmt::print("  Utilization: {:.1f}%%\n", delta.utilizationPct);
  }

  // 5. Quick fsync benchmark
  fmt::print("\nfsync Benchmark (quick):\n");
  BenchConfig config;
  config.setDirectory(dataDir);
  config.iterations = 50;

  auto fsync = runFsyncBench(config);
  if (fsync.success) {
    fmt::print("  avg: {:.1f}us, p99: {:.1f}us, max: {:.1f}us\n",
               fsync.avgLatencyUs, fsync.p99LatencyUs, fsync.maxLatencyUs);
  } else {
    fmt::print("  FAILED\n");
  }

  return 0;
}
```

---

## See Also

- `seeker::cpu` - CPU telemetry (topology, frequency, utilization, IRQs)
- `seeker::memory` - Memory topology, page sizes, hugepages, NUMA
- `seeker::gpu` - GPU telemetry (CUDA, NVML, PCIe)
- `seeker::timing` - Clock sources, timer latency
