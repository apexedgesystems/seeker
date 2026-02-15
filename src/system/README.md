# System Diagnostics Module

**Namespace:** `seeker::system`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive system-level diagnostics for real-time and performance-critical applications. This module provides 11 focused components for monitoring kernel configuration, process limits, capabilities, container environments, kernel modules, virtualization, RT scheduling, watchdog devices, IPC resources, security status, and file descriptor usage.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [KernelInfo](#kernelinfo) - Kernel version and preemption model
   - [ProcessLimits](#processlimits) - Resource limits (rlimits)
   - [CapabilityStatus](#capabilitystatus) - Linux capabilities
   - [ContainerLimits](#containerlimits) - Container detection and cgroup limits
   - [DriverInfo](#driverinfo) - Kernel module inventory
   - [VirtualizationInfo](#virtualizationinfo) - VM/container environment detection
   - [RtSchedConfig](#rtschedconfig) - RT scheduling configuration
   - [WatchdogStatus](#watchdogstatus) - Hardware/software watchdog status
   - [IpcStatus](#ipcstatus) - IPC resource status (shm, sem, mqueue)
   - [SecurityStatus](#securitystatus) - Linux Security Module (LSM) status
   - [FileDescriptorStatus](#filedescriptorstatus) - File descriptor usage and limits
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT System Validation](#example-rt-system-validation)

---

## Overview

The system diagnostics module answers these questions for RT and HPC systems:

| Question                                              | Module                 |
| ----------------------------------------------------- | ---------------------- |
| What kernel version and preemption model are running? | `KernelInfo`           |
| Is this a PREEMPT_RT kernel?                          | `KernelInfo`           |
| Are RT-relevant cmdline flags set (isolcpus, nohz)?   | `KernelInfo`           |
| Can this process use RT scheduling (SCHED_FIFO/RR)?   | `CapabilityStatus`     |
| Can this process lock memory (mlock)?                 | `CapabilityStatus`     |
| What are the RTPRIO, MEMLOCK, NOFILE limits?          | `ProcessLimits`        |
| Is the process running in a container?                | `ContainerLimits`      |
| What CPU/memory limits does the container impose?     | `ContainerLimits`      |
| Is the NVIDIA driver loaded? Which version?           | `DriverInfo`           |
| Is the kernel tainted by out-of-tree modules?         | `DriverInfo`           |
| Are we running in a VM or container? Which type?      | `VirtualizationInfo`   |
| Is RT bandwidth throttling enabled?                   | `RtSchedConfig`        |
| Is autogroup scheduling enabled (bad for RT)?         | `RtSchedConfig`        |
| Is a hardware watchdog available and configured?      | `WatchdogStatus`       |
| Are IPC resources (shm, sem, mqueue) near limits?     | `IpcStatus`            |
| Is SELinux or AppArmor enforcing?                     | `SecurityStatus`       |
| What LSMs are active on this system?                  | `SecurityStatus`       |
| How many file descriptors are open?                   | `FileDescriptorStatus` |
| Are we near process or system FD limits?              | `FileDescriptorStatus` |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/system/inc/KernelInfo.hpp"
#include "src/system/inc/ProcessLimits.hpp"
#include "src/system/inc/CapabilityStatus.hpp"
#include "src/system/inc/ContainerLimits.hpp"
#include "src/system/inc/DriverInfo.hpp"
#include "src/system/inc/VirtualizationInfo.hpp"
#include "src/system/inc/RtSchedConfig.hpp"
#include "src/system/inc/WatchdogStatus.hpp"
#include "src/system/inc/IpcStatus.hpp"
#include "src/system/inc/SecurityStatus.hpp"
#include "src/system/inc/FileDescriptorStatus.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::system;

// Static system info (query once at startup)
auto kernel = getKernelInfo();              // Kernel version, preemption, cmdline
auto drivers = getDriverInventory();        // Loaded kernel modules
auto virt = getVirtualizationInfo();        // VM/container environment
auto sched = getRtSchedConfig();            // RT scheduler configuration
auto security = getSecurityStatus();        // LSM status (SELinux, AppArmor)

// Process-specific state (query at startup or after privilege changes)
auto caps = getCapabilityStatus();          // Linux capabilities
auto limits = getProcessLimits();           // Resource limits (rlimits)

// Container environment (query once at startup)
auto container = getContainerLimits();      // Container detection and cgroups

// Hardware status (query at startup or periodically)
auto watchdog = getWatchdogStatus();        // Watchdog device enumeration
auto ipc = getIpcStatus();                  // IPC resource usage
auto fds = getFileDescriptorStatus();       // File descriptor usage
```

### RT Capability Check Pattern

```cpp
using namespace seeker::system;

// Comprehensive RT readiness check
auto caps = getCapabilityStatus();
auto limits = getProcessLimits();
auto sched = getRtSchedConfig();
auto virt = getVirtualizationInfo();

bool canRunRt = caps.canUseRtScheduling() && limits.canUseRtScheduling();
bool canLockMem = caps.canLockMemory() && limits.hasUnlimitedMemlock();
bool rtSchedOk = sched.isRtFriendly();
bool envOk = virt.isRtSuitable();

if (!canRunRt) {
  fmt::print("Cannot use RT scheduling: need CAP_SYS_NICE or root\n");
}
if (!canLockMem) {
  fmt::print("Cannot lock unlimited memory: need CAP_IPC_LOCK or root\n");
}
if (!rtSchedOk) {
  fmt::print("RT scheduler config suboptimal (score: {})\n", sched.rtScore());
}
if (!envOk) {
  fmt::print("Environment not ideal for RT (score: {}%)\n", virt.rtSuitability);
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
 * @brief Collect kernel information from /proc and /sys.
 * @return Populated KernelInfo structure.
 * @note RT-safe: Bounded file reads, fixed-size output, no allocation.
 */
[[nodiscard]] KernelInfo getKernelInfo() noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, KERNEL_RELEASE_SIZE> release{};  // 128
std::array<char, CONTAINER_RUNTIME_SIZE> runtime{};  // 32

// Instead of std::vector (allocates)
DriverEntry entries[MAX_DRIVER_ENTRIES]{};
WatchdogDevice devices[MAX_WATCHDOG_DEVICES]{};
```

This allows query functions to be RT-safe where possible.

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto kernel = getKernelInfo();
if (kernel.preempt == PreemptModel::UNKNOWN) {
  // Could not determine preemption model - handle gracefully
}

auto container = getContainerLimits();
if (!container.detected) {
  // Not running in a container - all limits will be defaults
}

auto watchdog = getWatchdogStatus();
if (!watchdog.hasWatchdog()) {
  // No watchdog devices found - not an error
}
```

---

## Module Reference

---

### KernelInfo

**Header:** `KernelInfo.hpp`
**Purpose:** Query kernel version, preemption model, and RT-relevant cmdline flags.

#### Constants

```cpp
inline constexpr std::size_t KERNEL_RELEASE_SIZE = 128;
inline constexpr std::size_t KERNEL_VERSION_SIZE = 256;
inline constexpr std::size_t PREEMPT_MODEL_SIZE = 32;
```

#### Key Types

```cpp
enum class PreemptModel : std::uint8_t {
  UNKNOWN = 0,
  NONE,        // No preemption (server kernels)
  VOLUNTARY,   // Voluntary preemption points
  PREEMPT,     // Full preemption (desktop kernels)
  PREEMPT_RT,  // PREEMPT_RT patchset (RT kernels)
};

struct KernelInfo {
  // Kernel identification
  std::array<char, KERNEL_RELEASE_SIZE> release{};   // e.g., "6.1.0-rt5-amd64"
  std::array<char, KERNEL_VERSION_SIZE> version{};    // Full /proc/version string

  // Preemption model
  PreemptModel preempt{PreemptModel::UNKNOWN};
  std::array<char, PREEMPT_MODEL_SIZE> preemptStr{};  // Raw preempt model string
  bool rtPreemptPatched{false};                        // CONFIG_PREEMPT_RT=y detected

  // RT cmdline flags
  bool nohzFull{false};      // nohz_full= present
  bool isolCpus{false};      // isolcpus= present
  bool rcuNocbs{false};      // rcu_nocbs= present
  bool skewTick{false};      // skew_tick=1 present
  bool tscReliable{false};   // tsc=reliable present
  bool cstateLimit{false};   // processor.max_cstate or intel_idle.max_cstate
  bool idlePoll{false};      // idle=poll present

  // Taint status
  int taintMask{0};
  bool tainted{false};

  // Convenience methods
  [[nodiscard]] bool isRtKernel() const noexcept;
  [[nodiscard]] bool isPreemptRt() const noexcept;
  [[nodiscard]] bool hasRtCmdlineFlags() const noexcept;
  [[nodiscard]] const char* preemptModelStr() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query kernel information (RT-safe)
[[nodiscard]] KernelInfo getKernelInfo() noexcept;

/// Convert PreemptModel to string (RT-safe)
[[nodiscard]] const char* toString(PreemptModel model) noexcept;
```

---

### ProcessLimits

**Header:** `ProcessLimits.hpp`
**Purpose:** Query process resource limits (rlimits) for RT validation.

#### Constants

```cpp
inline constexpr std::uint64_t RLIMIT_UNLIMITED_VALUE = static_cast<std::uint64_t>(-1);
```

#### Key Types

```cpp
struct RlimitValue {
  std::uint64_t soft{0};
  std::uint64_t hard{0};
  bool unlimited{false};

  [[nodiscard]] bool canIncreaseTo(std::uint64_t value) const noexcept;
  [[nodiscard]] bool hasAtLeast(std::uint64_t value) const noexcept;
};

struct ProcessLimits {
  // RT Scheduling Limits
  RlimitValue rtprio{};        // RLIMIT_RTPRIO - max RT priority
  RlimitValue rttime{};        // RLIMIT_RTTIME - max RT CPU time
  RlimitValue nice{};          // RLIMIT_NICE - nice ceiling

  // Memory Limits
  RlimitValue memlock{};       // RLIMIT_MEMLOCK - max locked memory
  RlimitValue addressSpace{};  // RLIMIT_AS - max virtual memory
  RlimitValue dataSegment{};   // RLIMIT_DATA - max data segment
  RlimitValue stack{};         // RLIMIT_STACK - stack size

  // File/Process Limits
  RlimitValue nofile{};        // RLIMIT_NOFILE - max open files
  RlimitValue nproc{};         // RLIMIT_NPROC - max processes
  RlimitValue core{};          // RLIMIT_CORE - max core dump size
  RlimitValue msgqueue{};      // RLIMIT_MSGQUEUE - POSIX mqueue bytes

  // Convenience methods
  [[nodiscard]] int rtprioMax() const noexcept;
  [[nodiscard]] bool hasUnlimitedMemlock() const noexcept;
  [[nodiscard]] bool canUseRtPriority(int priority) const noexcept;
  [[nodiscard]] bool canUseRtScheduling() const noexcept;
  [[nodiscard]] bool canLockMemory(std::uint64_t bytes) const noexcept;
  [[nodiscard]] std::string toString() const;      // NOT RT-safe
  [[nodiscard]] std::string toRtSummary() const;   // NOT RT-safe
};
```

#### API

```cpp
/// Query process limits (RT-safe)
[[nodiscard]] ProcessLimits getProcessLimits() noexcept;

/// Query a single resource limit (RT-safe)
[[nodiscard]] RlimitValue getRlimit(int resource) noexcept;

/// Format limit value for display (NOT RT-safe)
[[nodiscard]] std::string formatLimit(std::uint64_t value, bool isBytes = false);
```

---

### CapabilityStatus

**Header:** `CapabilityStatus.hpp`
**Purpose:** Query Linux capabilities for RT and privileged operations.

#### Constants

```cpp
inline constexpr int CAP_SYS_NICE_BIT = 23;
inline constexpr int CAP_IPC_LOCK_BIT = 14;
inline constexpr int CAP_SYS_RAWIO_BIT = 17;
inline constexpr int CAP_SYS_RESOURCE_BIT = 24;
inline constexpr int CAP_SYS_ADMIN_BIT = 21;
inline constexpr int CAP_NET_ADMIN_BIT = 12;
inline constexpr int CAP_NET_RAW_BIT = 13;
inline constexpr int CAP_SYS_PTRACE_BIT = 19;
```

#### Key Types

```cpp
struct CapabilityStatus {
  // RT-Relevant Capabilities
  bool sysNice{false};         // CAP_SYS_NICE - RT scheduling
  bool ipcLock{false};         // CAP_IPC_LOCK - memory locking
  bool sysRawio{false};        // CAP_SYS_RAWIO - raw I/O
  bool sysResource{false};     // CAP_SYS_RESOURCE - override limits

  // Administrative Capabilities
  bool sysAdmin{false};        // CAP_SYS_ADMIN - admin operations
  bool netAdmin{false};        // CAP_NET_ADMIN - network admin
  bool netRaw{false};          // CAP_NET_RAW - raw sockets
  bool sysPtrace{false};       // CAP_SYS_PTRACE - ptrace

  // Process State
  bool isRoot{false};          // Running as UID 0

  // Raw Capability Masks (first 64 bits)
  std::uint64_t effective{0};
  std::uint64_t permitted{0};
  std::uint64_t inheritable{0};

  // Convenience methods
  [[nodiscard]] bool canUseRtScheduling() const noexcept;  // root or CAP_SYS_NICE
  [[nodiscard]] bool canLockMemory() const noexcept;       // root or CAP_IPC_LOCK
  [[nodiscard]] bool isPrivileged() const noexcept;        // root or CAP_SYS_ADMIN
  [[nodiscard]] bool hasCapability(int capBit) const noexcept;
  [[nodiscard]] std::string toString() const;              // NOT RT-safe
  [[nodiscard]] std::string toRtSummary() const;           // NOT RT-safe
};
```

#### API

```cpp
/// Query capability status (RT-safe)
[[nodiscard]] CapabilityStatus getCapabilityStatus() noexcept;

/// Check for a specific capability (RT-safe)
[[nodiscard]] bool hasCapability(int capBit) noexcept;

/// Check if running as root (RT-safe)
[[nodiscard]] bool isRunningAsRoot() noexcept;

/// Get human-readable capability name (RT-safe)
[[nodiscard]] const char* capabilityName(int capBit) noexcept;
```

---

### ContainerLimits

**Header:** `ContainerLimits.hpp`
**Purpose:** Detect container environment and query cgroup limits.

#### Constants

```cpp
inline constexpr std::size_t CPUSET_STRING_SIZE = 128;
inline constexpr std::size_t CONTAINER_ID_SIZE = 80;
inline constexpr std::size_t CONTAINER_RUNTIME_SIZE = 32;
inline constexpr std::int64_t LIMIT_UNLIMITED = -1;
```

#### Key Types

```cpp
enum class CgroupVersion : std::uint8_t {
  UNKNOWN = 0,
  V1,
  V2,
  HYBRID,
};

struct ContainerLimits {
  // Container Detection
  bool detected{false};
  std::array<char, CONTAINER_ID_SIZE> containerId{};    // 80 chars
  std::array<char, CONTAINER_RUNTIME_SIZE> runtime{};   // 32 chars

  // cgroup Info
  CgroupVersion cgroupVersion{CgroupVersion::UNKNOWN};

  // CPU limits
  std::int64_t cpuQuotaUs{LIMIT_UNLIMITED};             // -1 = unlimited
  std::int64_t cpuPeriodUs{LIMIT_UNLIMITED};             // -1 = unknown
  std::array<char, CPUSET_STRING_SIZE> cpusetCpus{};     // Allowed CPUs

  // Memory limits
  std::int64_t memMaxBytes{LIMIT_UNLIMITED};             // -1 = unlimited
  std::int64_t memCurrentBytes{LIMIT_UNLIMITED};         // -1 = unknown
  std::int64_t swapMaxBytes{LIMIT_UNLIMITED};            // -1 = unlimited

  // PID limits
  std::int64_t pidsMax{LIMIT_UNLIMITED};
  std::int64_t pidsCurrent{LIMIT_UNLIMITED};             // -1 = unknown

  // Convenience methods
  [[nodiscard]] double cpuQuotaPercent() const noexcept;
  [[nodiscard]] bool hasCpuLimit() const noexcept;
  [[nodiscard]] bool hasMemoryLimit() const noexcept;
  [[nodiscard]] bool hasPidLimit() const noexcept;
  [[nodiscard]] bool hasCpusetLimit() const noexcept;
  [[nodiscard]] std::string toString() const;             // NOT RT-safe
};
```

#### API

```cpp
/// Query container limits (RT-safe)
[[nodiscard]] ContainerLimits getContainerLimits() noexcept;

/// Simple container detection (RT-safe)
[[nodiscard]] bool isRunningInContainer() noexcept;

/// Detect cgroup version (RT-safe)
[[nodiscard]] CgroupVersion detectCgroupVersion() noexcept;

/// Convert CgroupVersion to string (RT-safe)
[[nodiscard]] const char* toString(CgroupVersion version) noexcept;
```

---

### DriverInfo

**Header:** `DriverInfo.hpp`
**Purpose:** Query loaded kernel modules and GPU driver status.

#### Constants

```cpp
inline constexpr std::size_t MAX_DRIVER_ENTRIES = 512;
inline constexpr std::size_t DRIVER_NAME_SIZE = 64;
inline constexpr std::size_t DRIVER_VERSION_SIZE = 64;
inline constexpr std::size_t DRIVER_STATE_SIZE = 16;
inline constexpr std::size_t MAX_DRIVER_DEPS = 16;
inline constexpr std::size_t MAX_ASSESSMENT_NOTES = 8;
inline constexpr std::size_t ASSESSMENT_NOTE_SIZE = 256;
```

#### Key Types

```cpp
struct DriverEntry {
  std::array<char, DRIVER_NAME_SIZE> name{};
  std::array<char, DRIVER_VERSION_SIZE> version{};
  std::array<char, DRIVER_VERSION_SIZE> srcVersion{};
  std::array<char, DRIVER_STATE_SIZE> state{};    // "Live", "Loading", "Unloading"
  std::int32_t useCount{0};                       // Reference count (number of users)
  std::size_t sizeBytes{0};                       // Module size in bytes
  std::array<char, DRIVER_NAME_SIZE> deps[MAX_DRIVER_DEPS]{};
  std::size_t depCount{0};

  [[nodiscard]] bool isNamed(const char* targetName) const noexcept;
  [[nodiscard]] std::string toString() const;     // NOT RT-safe
};

struct DriverInventory {
  DriverEntry entries[MAX_DRIVER_ENTRIES]{};
  std::size_t entryCount{0};
  std::int32_t taintMask{0};
  bool tainted{false};

  [[nodiscard]] const DriverEntry* find(const char* name) const noexcept;
  [[nodiscard]] bool isLoaded(const char* name) const noexcept;
  [[nodiscard]] bool hasNvidiaDriver() const noexcept;
  [[nodiscard]] std::string toString() const;          // NOT RT-safe
  [[nodiscard]] std::string toBriefSummary() const;    // NOT RT-safe
};

struct DriverAssessment {
  bool nvidiaLoaded{false};
  bool nvmlHeaderAvailable{false};
  bool nvmlRuntimePresent{false};
  bool nouveauLoaded{false};
  bool i915Loaded{false};
  bool amdgpuLoaded{false};

  std::array<char, ASSESSMENT_NOTE_SIZE> notes[MAX_ASSESSMENT_NOTES]{};
  std::size_t noteCount{0};

  void addNote(const char* note) noexcept;
  [[nodiscard]] std::string toString() const;          // NOT RT-safe
};
```

#### API

```cpp
/// Query driver inventory (NOT RT-safe - unbounded iteration)
[[nodiscard]] DriverInventory getDriverInventory() noexcept;

/// Assess driver compatibility (NOT RT-safe - may dlopen)
[[nodiscard]] DriverAssessment assessDrivers(const DriverInventory& inv) noexcept;

/// Quick check if NVIDIA driver is loaded (NOT RT-safe)
[[nodiscard]] bool isNvidiaDriverLoaded() noexcept;

/// Quick check if NVML runtime is available (NOT RT-safe)
[[nodiscard]] bool isNvmlRuntimeAvailable() noexcept;
```

---

### VirtualizationInfo

**Header:** `VirtualizationInfo.hpp`
**Purpose:** Detect virtualization environment (VM, container, bare metal).

#### Constants

```cpp
inline constexpr std::size_t VIRT_TYPE_SIZE = 32;
inline constexpr std::size_t VIRT_NAME_SIZE = 64;
inline constexpr std::size_t VIRT_PRODUCT_SIZE = 128;
```

#### Key Types

```cpp
enum class VirtType : std::uint8_t {
  NONE = 0,      // Bare metal
  VM,            // Virtual machine
  CONTAINER,     // Container
  UNKNOWN,
};

enum class Hypervisor : std::uint8_t {
  NONE = 0,
  KVM,
  VMWARE,
  VIRTUALBOX,
  HYPERV,
  XEN,
  PARALLELS,
  BHYVE,
  QNX,
  ACRN,
  POWERVM,
  ZVM,
  AWS_NITRO,
  GOOGLE_COMPUTE,
  AZURE,
  OTHER,
};

enum class ContainerRuntime : std::uint8_t {
  NONE = 0,
  DOCKER,
  PODMAN,
  LXC,
  SYSTEMD_NSPAWN,
  RKT,
  OPENVZ,
  WSL,
  OTHER,
};

struct VirtualizationInfo {
  // Classification
  VirtType type{VirtType::NONE};
  Hypervisor hypervisor{Hypervisor::NONE};
  ContainerRuntime containerRuntime{ContainerRuntime::NONE};

  // Identification Strings
  std::array<char, VIRT_NAME_SIZE> hypervisorName{};     // 64 chars
  std::array<char, VIRT_NAME_SIZE> containerName{};      // 64 chars
  std::array<char, VIRT_PRODUCT_SIZE> productName{};     // 128 chars
  std::array<char, VIRT_PRODUCT_SIZE> manufacturer{};    // 128 chars
  std::array<char, VIRT_NAME_SIZE> biosVendor{};         // 64 chars

  // Detection Flags
  bool cpuidHypervisor{false};      // CPUID hypervisor bit set
  bool dmiVirtual{false};           // DMI indicates virtual
  bool containerIndicators{false};  // Container markers found
  bool nested{false};               // Nested virtualization
  bool paravirt{false};             // Paravirtualization detected

  // RT Impact Assessment
  int confidence{0};            // Detection confidence (0-100)
  int rtSuitability{0};         // RT suitability score (0-100)

  // Convenience methods
  [[nodiscard]] bool isBareMetal() const noexcept;
  [[nodiscard]] bool isVirtualMachine() const noexcept;
  [[nodiscard]] bool isContainer() const noexcept;
  [[nodiscard]] bool isVirtualized() const noexcept;
  [[nodiscard]] bool isCloud() const noexcept;
  [[nodiscard]] bool isRtSuitable() const noexcept;
  [[nodiscard]] const char* description() const noexcept;  // RT-safe
  [[nodiscard]] std::string toString() const;              // NOT RT-safe
};
```

#### API

```cpp
/// Query virtualization info (RT-safe)
[[nodiscard]] VirtualizationInfo getVirtualizationInfo() noexcept;

/// Quick checks (RT-safe)
[[nodiscard]] bool isVirtualized() noexcept;
[[nodiscard]] bool isContainerized() noexcept;

/// Convert enums to strings (RT-safe)
[[nodiscard]] const char* toString(VirtType type) noexcept;
[[nodiscard]] const char* toString(Hypervisor hv) noexcept;
[[nodiscard]] const char* toString(ContainerRuntime rt) noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto virt = getVirtualizationInfo();

if (virt.isBareMetal()) {
  fmt::print("Running on bare metal (optimal for RT)\n");
} else if (virt.isVirtualMachine()) {
  fmt::print("Running in VM: {} (RT score: {}%)\n",
             toString(virt.hypervisor), virt.rtSuitability);
  if (virt.nested) {
    fmt::print("WARNING: Nested virtualization detected\n");
  }
} else if (virt.isContainer()) {
  fmt::print("Running in container: {}\n", toString(virt.containerRuntime));
}
```

---

### RtSchedConfig

**Header:** `RtSchedConfig.hpp`
**Purpose:** Query RT scheduling configuration and kernel tunables.

#### Constants

```cpp
inline constexpr std::int64_t DEFAULT_RT_PERIOD_US = 1000000;   // 1 second
inline constexpr std::int64_t DEFAULT_RT_RUNTIME_US = 950000;   // 950ms = 95% of period
inline constexpr std::int64_t RT_RUNTIME_UNLIMITED = -1;
inline constexpr std::size_t SCHED_NAME_SIZE = 32;
```

#### Key Types

```cpp
struct RtBandwidth {
  std::int64_t periodUs{DEFAULT_RT_PERIOD_US};    // sched_rt_period_us (default 1s)
  std::int64_t runtimeUs{DEFAULT_RT_RUNTIME_US};  // sched_rt_runtime_us (default 950ms)
  bool valid{false};

  [[nodiscard]] bool isUnlimited() const noexcept;        // runtimeUs == -1
  [[nodiscard]] double bandwidthPercent() const noexcept;  // (runtime/period) * 100
  [[nodiscard]] bool isRtFriendly() const noexcept;        // >= 90% or unlimited
  [[nodiscard]] std::int64_t quotaUs() const noexcept;     // Remaining quota per period
  [[nodiscard]] std::string toString() const;              // NOT RT-safe
};

struct SchedTunables {
  std::uint64_t minGranularityNs{0};
  std::uint64_t wakeupGranularityNs{0};
  std::uint64_t migrationCostNs{0};
  std::uint64_t latencyNs{0};
  std::uint32_t nrMigrate{0};
  bool childRunsFirst{false};
  bool autogroupEnabled{false};             // Bad for RT if true
  bool valid{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct RtSchedConfig {
  RtBandwidth bandwidth{};
  SchedTunables tunables{};

  // Kernel Config
  bool hasRtGroupSched{false};     // CONFIG_RT_GROUP_SCHED
  bool hasCfsBandwidth{false};     // CONFIG_CFS_BANDWIDTH
  bool hasSchedDeadline{false};    // SCHED_DEADLINE support
  bool timerMigration{false};

  // RT Statistics
  std::uint32_t rtTasksRunnable{0};
  std::uint64_t rtThrottleCount{0};

  // Convenience methods
  [[nodiscard]] bool isRtFriendly() const noexcept;
  [[nodiscard]] int rtScore() const noexcept;           // 0-100
  [[nodiscard]] bool hasUnlimitedRt() const noexcept;
  [[nodiscard]] bool hasAutogroupDisabled() const noexcept;
  [[nodiscard]] std::string toString() const;           // NOT RT-safe
};
```

#### API

```cpp
/// Query RT scheduler config (RT-safe)
[[nodiscard]] RtSchedConfig getRtSchedConfig() noexcept;

/// Query RT bandwidth only (RT-safe)
[[nodiscard]] RtBandwidth getRtBandwidth() noexcept;

/// Query scheduler tunables (RT-safe)
[[nodiscard]] SchedTunables getSchedTunables() noexcept;

/// Quick checks (RT-safe)
[[nodiscard]] bool isRtThrottlingDisabled() noexcept;
[[nodiscard]] double getRtBandwidthPercent() noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto sched = getRtSchedConfig();

if (!sched.bandwidth.isUnlimited()) {
  fmt::print("RT bandwidth: {:.1f}% ({} us / {} us)\n",
             sched.bandwidth.bandwidthPercent(),
             sched.bandwidth.runtimeUs,
             sched.bandwidth.periodUs);
  fmt::print("WARNING: RT tasks may be throttled\n");
}

if (sched.tunables.autogroupEnabled) {
  fmt::print("WARNING: Autogroup enabled - interferes with RT priority\n");
  fmt::print("Fix: echo 0 > /proc/sys/kernel/sched_autogroup_enabled\n");
}

fmt::print("RT Score: {}/100\n", sched.rtScore());
```

---

### WatchdogStatus

**Header:** `WatchdogStatus.hpp`
**Purpose:** Query hardware and software watchdog device status.

#### Constants

```cpp
inline constexpr std::size_t MAX_WATCHDOG_DEVICES = 8;
inline constexpr std::size_t WATCHDOG_IDENTITY_SIZE = 64;
inline constexpr std::size_t WATCHDOG_PATH_SIZE = 32;
inline constexpr std::size_t WATCHDOG_GOVERNOR_SIZE = 32;
```

#### Key Types

```cpp
struct WatchdogCapabilities {
  bool settimeout{false};       // WDIOF_SETTIMEOUT
  bool magicclose{false};       // WDIOF_MAGICCLOSE
  bool pretimeout{false};       // WDIOF_PRETIMEOUT
  bool keepaliveping{false};    // WDIOF_KEEPALIVEPING
  bool alarmonly{false};        // WDIOF_ALARMONLY
  bool powerover{false};        // WDIOF_POWEROVER
  bool fanfault{false};         // WDIOF_FANFAULT
  bool externPowerFault{false}; // WDIOF_EXTERN1
  bool overheat{false};         // WDIOF_OVERHEAT

  std::uint32_t raw{0};         // Raw bitmask

  [[nodiscard]] bool hasAny() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct WatchdogDevice {
  std::uint32_t index{0};
  std::array<char, WATCHDOG_PATH_SIZE> devicePath{};
  std::array<char, WATCHDOG_IDENTITY_SIZE> identity{};

  std::uint32_t timeout{0};
  std::uint32_t minTimeout{0};
  std::uint32_t maxTimeout{0};
  std::uint32_t pretimeout{0};
  std::uint32_t timeleft{0};
  std::uint32_t bootstatus{0};

  WatchdogCapabilities capabilities{};
  std::array<char, WATCHDOG_GOVERNOR_SIZE> pretimeoutGovernor{};

  bool valid{false};            // Device state successfully read
  bool active{false};           // Currently running (armed)
  bool nowayout{false};         // Cannot be stopped once started

  // Convenience methods
  [[nodiscard]] bool isPrimary() const noexcept;
  [[nodiscard]] bool canSetTimeout() const noexcept;
  [[nodiscard]] bool hasPretimeout() const noexcept;
  [[nodiscard]] bool isRtSuitable() const noexcept;
  [[nodiscard]] std::string toString() const;   // NOT RT-safe
};

struct WatchdogStatus {
  WatchdogDevice devices[MAX_WATCHDOG_DEVICES]{};
  std::size_t deviceCount{0};
  bool softdogLoaded{false};
  bool hasHardwareWatchdog{false};

  // Convenience methods
  [[nodiscard]] const WatchdogDevice* find(std::uint32_t index) const noexcept;
  [[nodiscard]] const WatchdogDevice* primary() const noexcept;
  [[nodiscard]] bool hasWatchdog() const noexcept;
  [[nodiscard]] bool anyActive() const noexcept;
  [[nodiscard]] const WatchdogDevice* findRtSuitable() const noexcept;
  [[nodiscard]] std::string toString() const;   // NOT RT-safe
};
```

#### API

```cpp
/// Query all watchdog devices (RT-safe)
[[nodiscard]] WatchdogStatus getWatchdogStatus() noexcept;

/// Query single device (RT-safe)
[[nodiscard]] WatchdogDevice getWatchdogDevice(std::uint32_t index) noexcept;

/// Check if softdog module is loaded (RT-safe)
[[nodiscard]] bool isSoftdogLoaded() noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto wd = getWatchdogStatus();

if (!wd.hasWatchdog()) {
  fmt::print("WARNING: No watchdog device available\n");
  fmt::print("Consider loading softdog: modprobe softdog\n");
} else {
  auto* primary = wd.primary();
  fmt::print("Watchdog: {} (timeout {}-{}s)\n",
             primary->identity.data(),
             primary->minTimeout,
             primary->maxTimeout);

  if (primary->active) {
    fmt::print("Status: ACTIVE, {} seconds remaining\n", primary->timeleft);
  }
}
```

---

### IpcStatus

**Header:** `IpcStatus.hpp`
**Purpose:** Query System V and POSIX IPC resource usage and limits.

#### Constants

```cpp
inline constexpr std::size_t MAX_IPC_ENTRIES = 64;
inline constexpr std::size_t IPC_KEY_SIZE = 32;
inline constexpr std::size_t MQUEUE_NAME_SIZE = 64;
```

#### Key Types

```cpp
struct ShmLimits {
  std::uint64_t shmmax{0};    // Max segment size (bytes)
  std::uint64_t shmall{0};    // Max total pages
  std::uint32_t shmmni{0};    // Max segments
  std::uint32_t shmmin{1};    // Min segment size (always 1)
  std::uint64_t pageSize{4096};
  bool valid{false};

  [[nodiscard]] std::uint64_t maxTotalBytes() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct SemLimits {
  std::uint32_t semmsl{0};    // Max semaphores per array
  std::uint32_t semmns{0};    // Max semaphores system-wide
  std::uint32_t semopm{0};    // Max ops per semop call
  std::uint32_t semmni{0};    // Max semaphore arrays
  bool valid{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct MsgLimits {
  std::uint64_t msgmax{0};    // Max message size (bytes)
  std::uint64_t msgmnb{0};    // Max bytes per queue
  std::uint32_t msgmni{0};    // Max queues
  bool valid{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct PosixMqLimits {
  std::uint32_t queuesMax{0};    // Max queues per user
  std::uint32_t msgMax{0};       // Max messages per queue
  std::uint64_t msgsizeMax{0};   // Max message size (bytes) - uint64_t
  bool valid{false};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct ShmSegment {
  std::int32_t shmid{-1};
  std::int32_t key{0};
  std::uint64_t size{0};
  std::uint32_t nattch{0};
  std::uint32_t uid{0};
  std::uint32_t gid{0};
  std::uint32_t mode{0};
  bool markedForDeletion{false};

  [[nodiscard]] bool canAttach(std::uint32_t processUid) const noexcept;
};

struct ShmStatus {
  ShmLimits limits{};
  ShmSegment segments[MAX_IPC_ENTRIES]{};
  std::size_t segmentCount{0};
  std::uint64_t totalBytes{0};

  [[nodiscard]] bool isNearSegmentLimit() const noexcept;
  [[nodiscard]] bool isNearMemoryLimit() const noexcept;
  [[nodiscard]] const ShmSegment* find(std::int32_t shmid) const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct SemStatus {
  SemLimits limits{};
  std::uint32_t arraysInUse{0};
  std::uint32_t semsInUse{0};

  [[nodiscard]] bool isNearArrayLimit() const noexcept;
  [[nodiscard]] bool isNearSemLimit() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct MsgStatus {
  MsgLimits limits{};
  std::uint32_t queuesInUse{0};
  std::uint64_t totalMessages{0};
  std::uint64_t totalBytes{0};

  [[nodiscard]] bool isNearQueueLimit() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct PosixMqStatus {
  PosixMqLimits limits{};
  std::uint32_t queuesInUse{0};

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct IpcStatus {
  ShmStatus shm{};
  SemStatus sem{};
  MsgStatus msg{};
  PosixMqStatus posixMq{};

  [[nodiscard]] bool isNearAnyLimit() const noexcept;
  [[nodiscard]] int rtScore() const noexcept;            // 0-100
  [[nodiscard]] std::string toString() const;            // NOT RT-safe
};
```

#### API

```cpp
/// Query complete IPC status (RT-safe)
[[nodiscard]] IpcStatus getIpcStatus() noexcept;

/// Query individual subsystems (RT-safe)
[[nodiscard]] ShmStatus getShmStatus() noexcept;
[[nodiscard]] SemStatus getSemStatus() noexcept;
[[nodiscard]] MsgStatus getMsgStatus() noexcept;
[[nodiscard]] PosixMqStatus getPosixMqStatus() noexcept;

/// Query limits only (RT-safe)
[[nodiscard]] ShmLimits getShmLimits() noexcept;
[[nodiscard]] SemLimits getSemLimits() noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto ipc = getIpcStatus();

fmt::print("Shared Memory: {}/{} segments, {} bytes\n",
           ipc.shm.segmentCount, ipc.shm.limits.shmmni, ipc.shm.totalBytes);
fmt::print("Semaphores: {}/{} arrays\n",
           ipc.sem.arraysInUse, ipc.sem.limits.semmni);
fmt::print("Message Queues: {}/{}\n",
           ipc.msg.queuesInUse, ipc.msg.limits.msgmni);

if (ipc.isNearAnyLimit()) {
  fmt::print("WARNING: Approaching IPC resource limits\n");
}
```

---

### SecurityStatus

**Header:** `SecurityStatus.hpp`
**Purpose:** Query Linux Security Module (LSM) status including SELinux, AppArmor, and other security features.

#### Constants

```cpp
inline constexpr std::size_t LSM_NAME_SIZE = 32;
inline constexpr std::size_t SECURITY_CONTEXT_SIZE = 256;
inline constexpr std::size_t MAX_LSMS = 8;
```

#### Key Types

```cpp
enum class SelinuxMode : std::uint8_t {
  NOT_PRESENT = 0,
  DISABLED = 1,
  PERMISSIVE = 2,
  ENFORCING = 3,
};

enum class ApparmorMode : std::uint8_t {
  NOT_PRESENT = 0,
  DISABLED = 1,
  ENABLED = 2,
};

struct SelinuxStatus {
  SelinuxMode mode = SelinuxMode::NOT_PRESENT;
  bool mcsEnabled = false;
  bool mlsEnabled = false;
  bool booleansPending = false;

  std::array<char, LSM_NAME_SIZE> policyType{};             // e.g., "targeted", "mls"
  std::array<char, SECURITY_CONTEXT_SIZE> currentContext{};  // Current process context

  std::uint32_t policyVersion = 0;
  std::uint32_t denialCount = 0;       // AVC denial count

  [[nodiscard]] bool isActive() const noexcept;
  [[nodiscard]] bool isEnforcing() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct ApparmorStatus {
  ApparmorMode mode = ApparmorMode::NOT_PRESENT;
  std::uint32_t profilesLoaded = 0;
  std::uint32_t profilesEnforce = 0;
  std::uint32_t profilesComplain = 0;

  [[nodiscard]] bool isActive() const noexcept;
  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct LsmInfo {
  std::array<char, LSM_NAME_SIZE> name{};
  bool active = false;

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct SecurityStatus {
  SelinuxStatus selinux{};
  ApparmorStatus apparmor{};

  std::array<LsmInfo, MAX_LSMS> lsms{};
  std::size_t lsmCount = 0;

  bool seccompAvailable = false;
  bool landLockAvailable = false;
  bool yamaPtrace = false;

  [[nodiscard]] bool hasEnforcement() const noexcept;
  [[nodiscard]] std::string activeLsmList() const;     // NOT noexcept, NOT RT-safe
  [[nodiscard]] std::string toString() const;          // NOT RT-safe
};
```

#### API

```cpp
/// Query complete security status (NOT RT-safe - may allocate)
[[nodiscard]] SecurityStatus getSecurityStatus() noexcept;

/// Query individual subsystems (partially RT-safe)
[[nodiscard]] SelinuxStatus getSelinuxStatus() noexcept;
[[nodiscard]] ApparmorStatus getApparmorStatus() noexcept;

/// Quick checks (RT-safe)
[[nodiscard]] bool selinuxAvailable() noexcept;
[[nodiscard]] bool apparmorAvailable() noexcept;

/// Convert enums to strings (RT-safe)
[[nodiscard]] const char* toString(SelinuxMode mode) noexcept;
[[nodiscard]] const char* toString(ApparmorMode mode) noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto sec = getSecurityStatus();

fmt::print("SELinux: {}\n", toString(sec.selinux.mode));
fmt::print("AppArmor: {}\n", toString(sec.apparmor.mode));
fmt::print("Seccomp: {}\n", sec.seccompAvailable ? "available" : "not available");
fmt::print("Active LSMs: {}\n", sec.activeLsmList());

if (sec.hasEnforcement()) {
  fmt::print("Note: Security enforcement active - may affect RT behavior\n");
}
```

---

### FileDescriptorStatus

**Header:** `FileDescriptorStatus.hpp`
**Purpose:** Query file descriptor usage and limits for process and system.

#### Constants

```cpp
inline constexpr std::size_t FD_PATH_SIZE = 256;
inline constexpr std::size_t MAX_FD_TYPES = 16;
```

#### Key Types

```cpp
enum class FdType : std::uint8_t {
  UNKNOWN = 0,
  REGULAR = 1,
  DIRECTORY = 2,
  PIPE = 3,
  SOCKET = 4,
  DEVICE = 5,
  EVENTFD = 6,
  TIMERFD = 7,
  SIGNALFD = 8,
  EPOLL = 9,
  INOTIFY = 10,
  ANON_INODE = 11,
};

struct FdTypeCount {
  FdType type = FdType::UNKNOWN;
  std::uint32_t count = 0;

  [[nodiscard]] std::string toString() const;  // NOT RT-safe
};

struct ProcessFdStatus {
  std::uint32_t openCount = 0;        // Currently open FDs
  std::uint64_t softLimit = 0;        // RLIMIT_NOFILE soft limit
  std::uint64_t hardLimit = 0;        // RLIMIT_NOFILE hard limit

  std::array<FdTypeCount, MAX_FD_TYPES> byType{};
  std::size_t typeCount = 0;

  std::uint32_t highestFd = 0;        // Highest FD number in use

  [[nodiscard]] std::uint64_t available() const noexcept;
  [[nodiscard]] double utilizationPercent() const noexcept;
  [[nodiscard]] bool isCritical() const noexcept;     // >90% usage
  [[nodiscard]] bool isElevated() const noexcept;     // >75% usage
  [[nodiscard]] std::uint32_t countByType(FdType type) const noexcept;
  [[nodiscard]] std::string toString() const;         // NOT RT-safe
};

struct SystemFdStatus {
  std::uint64_t allocated = 0;        // Currently allocated FDs
  std::uint64_t free = 0;             // Free FD slots in kernel
  std::uint64_t maximum = 0;          // fs.file-max
  std::uint64_t nrOpen = 0;           // fs.nr_open (per-process max)
  std::uint64_t inodeMax = 0;         // fs.inode-max (if available)

  [[nodiscard]] std::uint64_t available() const noexcept;
  [[nodiscard]] double utilizationPercent() const noexcept;
  [[nodiscard]] bool isCritical() const noexcept;
  [[nodiscard]] std::string toString() const;         // NOT RT-safe
};

struct FileDescriptorStatus {
  ProcessFdStatus process{};
  SystemFdStatus system{};

  [[nodiscard]] bool anyCritical() const noexcept;
  [[nodiscard]] std::string toString() const;         // NOT RT-safe
};
```

#### API

```cpp
/// Query complete FD status (NOT RT-safe - iterates /proc/self/fd)
[[nodiscard]] FileDescriptorStatus getFileDescriptorStatus() noexcept;

/// Query individual components
[[nodiscard]] ProcessFdStatus getProcessFdStatus() noexcept;   // NOT RT-safe
[[nodiscard]] SystemFdStatus getSystemFdStatus() noexcept;     // RT-safe

/// Quick queries
[[nodiscard]] std::uint32_t getOpenFdCount() noexcept;         // NOT RT-safe
[[nodiscard]] std::uint64_t getFdSoftLimit() noexcept;         // RT-safe
[[nodiscard]] std::uint64_t getFdHardLimit() noexcept;         // RT-safe

/// Convert FdType to string (RT-safe)
[[nodiscard]] const char* toString(FdType type) noexcept;
```

#### Usage

```cpp
using namespace seeker::system;

auto fds = getFileDescriptorStatus();

fmt::print("Process FDs: {}/{} ({:.1f}%)\n",
           fds.process.openCount,
           fds.process.softLimit,
           fds.process.utilizationPercent());

fmt::print("System FDs: {}/{} ({:.1f}%)\n",
           fds.system.allocated,
           fds.system.maximum,
           fds.system.utilizationPercent());

if (fds.process.isCritical()) {
  fmt::print("WARNING: Process FD usage critical!\n");
  fmt::print("  Recommendation: ulimit -n {}\n", fds.process.hardLimit);
}

if (fds.process.isElevated()) {
  fmt::print("Note: FD usage elevated - monitor for leaks\n");
}
```

---

## Common Patterns

### Startup Validation

```cpp
using namespace seeker::system;

bool validateSystem() {
  auto caps = getCapabilityStatus();
  auto limits = getProcessLimits();
  auto sched = getRtSchedConfig();
  auto virt = getVirtualizationInfo();

  bool ok = true;

  if (!caps.canUseRtScheduling()) {
    fmt::print("ERROR: No RT scheduling capability\n");
    ok = false;
  }

  if (!sched.bandwidth.isUnlimited() && sched.bandwidth.bandwidthPercent() < 90) {
    fmt::print("WARNING: RT bandwidth may cause throttling\n");
  }

  if (sched.tunables.autogroupEnabled) {
    fmt::print("WARNING: Autogroup interferes with RT priority\n");
  }

  if (!virt.isBareMetal()) {
    fmt::print("INFO: Running in {} (RT score {}%)\n",
               virt.isContainer() ? "container" : "VM",
               virt.rtSuitability);
  }

  return ok;
}
```

### Safety-Critical System Check

```cpp
using namespace seeker::system;

void validateSafetyCritical() {
  auto wd = getWatchdogStatus();
  auto kernel = getKernelInfo();

  // Require watchdog for safety-critical systems
  if (!wd.hasWatchdog()) {
    throw std::runtime_error("No watchdog device - required for safety");
  }

  auto* dev = wd.findRtSuitable();
  if (dev == nullptr) {
    throw std::runtime_error("No RT-suitable watchdog found");
  }

  // Require untainted kernel
  if (kernel.tainted) {
    fmt::print("WARNING: Kernel tainted (mask={:#x})\n", kernel.taintMask);
  }
}
```

---

## Real-Time Considerations

### RT-Safe vs NOT RT-Safe

| Module               | Query Function              | RT-Safety   |
| -------------------- | --------------------------- | ----------- |
| KernelInfo           | `getKernelInfo()`           | RT-safe     |
| ProcessLimits        | `getProcessLimits()`        | RT-safe     |
| CapabilityStatus     | `getCapabilityStatus()`     | RT-safe     |
| ContainerLimits      | `getContainerLimits()`      | RT-safe     |
| DriverInfo           | `getDriverInventory()`      | NOT RT-safe |
| VirtualizationInfo   | `getVirtualizationInfo()`   | RT-safe     |
| RtSchedConfig        | `getRtSchedConfig()`        | RT-safe     |
| WatchdogStatus       | `getWatchdogStatus()`       | RT-safe     |
| IpcStatus            | `getIpcStatus()`            | RT-safe     |
| SecurityStatus       | `getSecurityStatus()`       | NOT RT-safe |
| FileDescriptorStatus | `getFileDescriptorStatus()` | NOT RT-safe |

**RT-safe functions** can be called from RT threads (e.g., for periodic health checks).

**NOT RT-safe functions** (DriverInfo, SecurityStatus, FileDescriptorStatus) should only be called from non-RT contexts due to unbounded iteration over `/proc/modules`, `/sys/module/`, or `/proc/self/fd/`, or potential string allocation.

### Best Practices

1. **Query at startup:** Collect system info before entering RT sections
2. **Cache results:** Kernel info, capabilities, and limits rarely change at runtime
3. **Periodic checks:** For long-running processes, periodically verify container limits and IPC usage
4. **Graceful handling:** Always check return values; missing capabilities should warn, not crash
5. **Watchdog integration:** For safety-critical systems, verify watchdog availability early

---

## CLI Tools

The system domain includes 4 command-line tools: `sys-info`, `sys-rtcheck`, `sys-limits`, `sys-drivers`.

See: `tools/cpp/system/README.md` for detailed tool usage.

---

## Example: RT System Validation

```cpp
#include "src/system/inc/CapabilityStatus.hpp"
#include "src/system/inc/ContainerLimits.hpp"
#include "src/system/inc/IpcStatus.hpp"
#include "src/system/inc/KernelInfo.hpp"
#include "src/system/inc/ProcessLimits.hpp"
#include "src/system/inc/RtSchedConfig.hpp"
#include "src/system/inc/VirtualizationInfo.hpp"
#include "src/system/inc/WatchdogStatus.hpp"

#include <fmt/core.h>
#include <stdexcept>

using namespace seeker::system;

void validateRtEnvironment() {
  fmt::print("=== RT Environment Validation ===\n\n");

  // 1. Kernel
  auto kernel = getKernelInfo();
  fmt::print("Kernel: {} ({})\n", kernel.release.data(), kernel.preemptModelStr());

  // 2. Virtualization
  auto virt = getVirtualizationInfo();
  if (virt.isBareMetal()) {
    fmt::print("Environment: Bare metal (optimal)\n");
  } else {
    fmt::print("Environment: {} (RT score {}%)\n",
               virt.isContainer() ? "Container" : "VM",
               virt.rtSuitability);
  }

  // 3. RT Scheduler
  auto sched = getRtSchedConfig();
  fmt::print("RT Bandwidth: {}\n",
             sched.bandwidth.isUnlimited() ? "unlimited" :
             fmt::format("{:.0f}%", sched.bandwidth.bandwidthPercent()));
  fmt::print("RT Score: {}/100\n\n", sched.rtScore());

  // 4. Capabilities
  auto caps = getCapabilityStatus();
  if (!caps.canUseRtScheduling()) {
    throw std::runtime_error(
        "Cannot use RT scheduling. Fix: run as root or "
        "'setcap cap_sys_nice+ep <binary>'");
  }

  // 5. Limits
  auto limits = getProcessLimits();
  if (limits.rtprioMax() < 99) {
    fmt::print("WARNING: RTPRIO limited to {}\n", limits.rtprioMax());
  }

  // 6. Watchdog (for safety-critical)
  auto wd = getWatchdogStatus();
  if (wd.hasWatchdog()) {
    fmt::print("Watchdog: {} available\n", wd.primary()->identity.data());
  }

  // 7. IPC headroom
  auto ipc = getIpcStatus();
  if (ipc.isNearAnyLimit()) {
    fmt::print("WARNING: Near IPC resource limits\n");
  }

  fmt::print("\n=== Validation Complete ===\n");
}

int main() {
  try {
    validateRtEnvironment();
    return 0;
  } catch (const std::exception& e) {
    fmt::print(stderr, "FATAL: {}\n", e.what());
    return 1;
  }
}
```

---

## See Also

- `seeker::cpu` - CPU topology, frequency, isolation, interrupts
- `seeker::memory` - Memory stats, NUMA, hugepages, mlock, EDAC
- `seeker::storage` - Block devices, I/O schedulers, benchmarks
- `seeker::network` - Network interfaces, socket buffers, ethtool
- `seeker::timing` - Clock sources, timer latency, time sync, PTP, RTC
