# Seeker

**Namespace:** `seeker`
**Platform:** Linux-only
**C++ Standard:** C++23

System diagnostics library for real-time and performance-critical Linux systems.
Provides 57 modules across 8 domains for system inspection, monitoring, and RT
readiness validation.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Design Principles](#2-design-principles)
3. [Domains](#3-domains)
4. [CLI Tools](#4-cli-tools)
5. [Building](#5-building)
6. [Platform Support](#6-platform-support)
7. [Testing](#7-testing)
8. [Requirements](#8-requirements)
9. [Project Structure](#9-project-structure)
10. [See Also](#10-see-also)

---

## 1. Quick Start

```cpp
#include "src/cpu/inc/CpuTopology.hpp"
#include "src/system/inc/KernelInfo.hpp"
using namespace seeker;

auto topo = cpu::getCpuTopology();
auto ki   = system::getKernelInfo();
fmt::print("CPUs: {} logical, {} physical (PREEMPT_RT: {})\n",
           topo.logicalCount, topo.coreCount, ki.isPreemptRt);
```

### Build and Run (Docker)

```bash
make compose-debug
make compose-testp
```

### Build Without Docker

```bash
cmake --preset native-linux-debug
cmake --build --preset native-linux-debug
ctest --test-dir build/native-linux-debug
```

### Install as Library

```bash
make install
```

Consumers use `find_package(seeker)`:

```cmake
find_package(seeker REQUIRED)
target_link_libraries(my_app PRIVATE seeker::cpu seeker::timing)
```

The install tree contains headers, shared libraries, CMake config, and an `.env`
file under `build/native-linux-release/install/`.

---

## 2. Design Principles

### No Exceptions

All APIs are `noexcept`. Failures result in zeroed/default fields, not exceptions:

```cpp
auto topo = seeker::cpu::getCpuTopology();
if (topo.coreCount == 0) {
  // Query failed - handle gracefully
}
```

### RT-Safety Annotations

Every public function documents its RT-safety:

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for RT threads        |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

### Fixed-Size Data Structures

Critical structs use fixed-size arrays to avoid heap allocation in RT paths:

```cpp
std::array<char, CPU_MODEL_SIZE> model{};
std::bitset<MAX_CPUS> cpuMask{};
```

### Graceful Degradation

Missing files, failed syscalls, or unavailable features result in safe defaults:

```cpp
auto freq = seeker::cpu::getCpuFrequencySummary();
// If cpufreq not available: currentMHz[i] = 0, governor = ""
```

### RT-Safe Usage Patterns

Query static system info once at initialization:

```cpp
// Cache at startup (NOT RT-safe)
static const auto TOPO = seeker::cpu::getCpuTopology();
static const auto FEATURES = seeker::cpu::getCpuFeatures();
```

Use RT-safe accessors in RT paths:

```cpp
// RT-safe: bounded syscalls, no allocation
auto telem = seeker::gpu::getGpuTelemetry(0);
if (telem.isThrottling()) { /* handle */ }
```

Recommended RT settings validated by the `*-rtcheck` tools:

| Domain  | Setting             | Command                                      |
| ------- | ------------------- | -------------------------------------------- |
| CPU     | isolcpus boot param | `isolcpus=2-7 nohz_full=2-7`                 |
| Memory  | Hugepages           | `hugeadm --create-global-mounts`             |
| Storage | I/O scheduler       | `echo none > /sys/block/sda/queue/scheduler` |
| Network | IRQ affinity        | `irqbalance --banirq`                        |
| Timing  | Timer slack         | `prctl(PR_SET_TIMERSLACK, 1)`                |
| GPU     | Persistence mode    | `nvidia-smi -pm 1`                           |
| GPU     | Exclusive compute   | `nvidia-smi -c EXCLUSIVE_PROCESS`            |

---

## 3. Domains

| Domain  | Namespace         | Modules | Documentation                   |
| ------- | ----------------- | ------- | ------------------------------- |
| CPU     | `seeker::cpu`     | 11      | [README](src/cpu/README.md)     |
| Memory  | `seeker::memory`  | 6       | [README](src/memory/README.md)  |
| Storage | `seeker::storage` | 5       | [README](src/storage/README.md) |
| Network | `seeker::network` | 6       | [README](src/network/README.md) |
| Timing  | `seeker::timing`  | 6       | [README](src/timing/README.md)  |
| Device  | `seeker::device`  | 5       | [README](src/device/README.md)  |
| System  | `seeker::system`  | 11      | [README](src/system/README.md)  |
| GPU     | `seeker::gpu`     | 7       | [README](src/gpu/README.md)     |

### CPU

Topology, features, frequency scaling, thermal status, per-core utilization,
IRQ/softirq statistics, CPU isolation, idle states, and affinity.

**Modules:** CpuTopology, CpuFeatures, CpuFreq, ThermalStatus, CpuStats,
CpuUtilization, IrqStats, SoftirqStats, CpuIsolation, CpuIdle, Affinity

```cpp
auto topo = seeker::cpu::getCpuTopology();
fmt::print("CPUs: {} logical, {} physical cores\n",
           topo.logicalCount, topo.coreCount);
```

### Memory

NUMA topology, hugepages, memory locking, page sizes, memory statistics, and
ECC/EDAC status.

**Modules:** NumaTopology, HugepageStatus, MemoryLocking, PageSizes,
MemoryStats, EdacStatus

```cpp
auto hp = seeker::memory::getHugepageStatus();
if (hp.hasConfiguredHugepages()) {
  fmt::print("Hugepages: {} x {}\n", hp.freePages, hp.pageSizeBytes);
}
```

### Storage

Block device info, I/O schedulers, I/O statistics, mount info, and storage
benchmarks.

**Modules:** BlockDeviceInfo, IoScheduler, IoStats, MountInfo, StorageBench

```cpp
auto sched = seeker::storage::getIoScheduler("sda");
fmt::print("Scheduler: {} (RT: {})\n", sched.current.data(), sched.isRtOptimal());
```

### Network

Interface info, traffic statistics, IRQ affinity, socket buffer configuration,
ethtool capabilities, and loopback benchmarks.

**Modules:** InterfaceInfo, InterfaceStats, NetworkIsolation,
SocketBufferConfig, EthtoolInfo, LoopbackBench

```cpp
auto iface = seeker::network::getInterfaceInfo("eth0");
fmt::print("{}: {} Mbps, {}\n", iface.name.data(), iface.speedMbps,
           iface.isUp ? "UP" : "DOWN");
```

### Timing

Clocksources, timer configuration, time synchronization, PTP hardware, RTC
status, and latency benchmarks.

**Modules:** ClockSource, TimerConfig, TimeSyncStatus, LatencyBench,
PtpStatus, RtcStatus

```cpp
auto cs = seeker::timing::getClockSource();
if (cs.isTsc()) { fmt::print("TSC clocksource (optimal)\n"); }
```

### Device

Serial ports, I2C buses, SPI buses, CAN buses, and GPIO for embedded and
industrial systems.

**Modules:** SerialPortInfo, I2cBusInfo, SpiBusInfo, CanBusInfo, GpioInfo

```cpp
auto can = seeker::device::getCanBusInfo();
fmt::print("CAN interfaces: {}\n", can.interfaceCount);
```

### System

Kernel info, process limits, capabilities, container limits, drivers,
virtualization, RT scheduling, watchdogs, IPC, security, and file descriptors.

**Modules:** KernelInfo, ProcessLimits, CapabilityStatus, ContainerLimits,
DriverInfo, VirtualizationInfo, RtSchedConfig, WatchdogStatus, IpcStatus,
SecurityStatus, FileDescriptorStatus

```cpp
auto ki = seeker::system::getKernelInfo();
fmt::print("Kernel: {} (PREEMPT_RT: {})\n", ki.version.data(), ki.isPreemptRt);
```

### GPU

Topology, telemetry, memory, driver configuration, PCIe links, and process
isolation. Primary support for NVIDIA via NVML; sysfs fallback for AMD/Intel.

**Modules:** GpuTopology, GpuTelemetry, GpuMemoryStatus, GpuDriverStatus,
PcieStatus, GpuIsolation

```cpp
auto drv = seeker::gpu::getGpuDriverStatus(0);
if (drv.isRtReady()) { fmt::print("GPU 0: RT-ready\n"); }
```

### Shared Utilities

Internal helpers used by all domains:

```cpp
#include "src/helpers/inc/Format.hpp"   // bytesBinary, frequencyHz, count
#include "src/helpers/inc/Files.hpp"    // readFileToBuffer, pathExists
#include "src/helpers/inc/Strings.hpp"  // startsWith, endsWith, trim
```

---

## 4. CLI Tools

33 C++ command-line tools plus 1 CUDA-only tool, organized by domain. Build
with `make tools` or `make compose-tools`. Each domain's tool directory contains
a README with detailed usage.

| Domain  | Count | Tools                                                                                                  | Docs                                  |
| ------- | ----- | ------------------------------------------------------------------------------------------------------ | ------------------------------------- |
| CPU     | 7     | `cpu-info`, `cpu-rtcheck`, `cpu-affinity`, `cpu-corestat`, `cpu-irqmap`, `cpu-snapshot`, `cpu-thermal` | [README](tools/cpp/cpu/README.md)     |
| Memory  | 3     | `mem-info`, `mem-rtcheck`, `mem-numa`                                                                  | [README](tools/cpp/memory/README.md)  |
| Storage | 4     | `storage-info`, `storage-rtcheck`, `storage-bench`, `storage-iostat`                                   | [README](tools/cpp/storage/README.md) |
| Network | 3     | `net-info`, `net-rtcheck`, `net-stat`                                                                  | [README](tools/cpp/network/README.md) |
| Timing  | 4     | `timing-info`, `timing-rtcheck`, `timing-bench`, `timing-sync`                                         | [README](tools/cpp/timing/README.md)  |
| System  | 4     | `sys-info`, `sys-rtcheck`, `sys-drivers`, `sys-limits`                                                 | [README](tools/cpp/system/README.md)  |
| Device  | 5     | `device-info`, `device-rtcheck`, `device-serial`, `device-i2c`, `device-can`                           | [README](tools/cpp/device/README.md)  |
| GPU     | 3+1   | `gpu-info`, `gpu-stat`, `gpu-rtcheck`, `gpu-bench` (CUDA only)                                         | [README](tools/cpp/gpu/README.md)     |

Tools are installed to `<prefix>/bin/` via `make install`.

---

## 5. Building

Run `make help` for the full list of targets.

### Docker (Recommended)

```bash
make compose-debug          # Native debug via dev-cuda container
make compose-release        # Native release
make compose-test           # Run tests
make compose-testp          # Run tests (parallel + timing serial)
make compose-coverage       # Code coverage report
make compose-format         # Auto-format code
make compose-static         # Static analysis (scan-build)
make compose-asan           # AddressSanitizer
make compose-tsan           # ThreadSanitizer
make compose-ubsan          # UBSanitizer
make compose-tools          # Build all CLI tools
```

### Native

```bash
make debug                  # Configure + build debug
make release                # Configure + build release
make test                   # Run all tests
make format                 # Auto-format code
make coverage               # Code coverage report
```

### Cross-Compilation

```bash
make compose-jetson-debug   # Jetson (aarch64 + CUDA) via dev-jetson
make compose-jetson-release
make compose-rpi-debug      # Raspberry Pi (aarch64) via dev-rpi
make compose-rpi-release
make compose-riscv-debug    # RISC-V 64 via dev-riscv64
make compose-riscv-release
```

### Artifact Packaging

Build release artifacts for all platforms and package into tarballs:

```bash
make artifacts
ls output/
# seeker-1.0.0-x86_64-linux.tar.gz
# seeker-1.0.0-x86_64-linux-cuda.tar.gz
# seeker-1.0.0-aarch64-jetson.tar.gz
# seeker-1.0.0-aarch64-rpi.tar.gz
# seeker-1.0.0-riscv64-linux.tar.gz
```

Each tarball contains `lib/`, `include/`, `lib/cmake/seeker/`, `bin/`, and an
`.env` file for `LD_LIBRARY_PATH` setup.

---

## 6. Platform Support

| Platform               | Library | GPU (NVML) | Pre-built Artifact             |
| ---------------------- | ------- | ---------- | ------------------------------ |
| x86_64 Linux           | Full    | Yes        | `seeker-*-x86_64-linux[-cuda]` |
| Jetson (aarch64)       | Full    | Yes        | `seeker-*-aarch64-jetson`      |
| Raspberry Pi (aarch64) | Full    | No         | `seeker-*-aarch64-rpi`         |
| RISC-V 64              | Full    | No         | `seeker-*-riscv64-linux`       |

GPU modules compile on all platforms but NVML telemetry requires an NVIDIA GPU
with the proprietary driver. Without NVML, GPU queries return safe defaults.

---

## 7. Testing

```bash
# Build and run all tests (Docker)
make compose-debug
make compose-testp

# Run specific domain
make compose-test CTEST_ARGS="-L cpu"

# Code coverage
make compose-coverage

# Sanitizers
make compose-asan           # AddressSanitizer
make compose-tsan           # ThreadSanitizer
make compose-ubsan          # UBSanitizer
```

| Domain  | Test Target         | Tests |
| ------- | ------------------- | ----- |
| CPU     | `TestSeekerCpu`     | 212   |
| Memory  | `TestSeekerMemory`  | 182   |
| Storage | `TestSeekerStorage` | 132   |
| Network | `TestSeekerNetwork` | 221   |
| Timing  | `TestSeekerTiming`  | 226   |
| System  | `TestSeekerSystem`  | 342   |
| Device  | `TestSeekerDevice`  | 335   |
| GPU     | `TestSeekerGpu`     | 143   |

Some tests may be skipped on systems without NUMA, specific hardware, or
elevated privileges.

---

## 8. Requirements

**Required:**

- C++23 compiler (Clang 21 recommended, GCC 13+ for cross-compilation)
- CMake 3.24+
- Linux kernel 4.x+

**Auto-fetched (via CMake FetchContent):**

- fmt 11.1.4
- GoogleTest 1.16.0

**Optional:**

- CUDA toolkit 12+ (GPU modules, `gpu-bench` tool)
- NVML (GPU telemetry)

---

## 9. Project Structure

```
seeker/
  CMakeLists.txt              Root project (version, presets, CUDA detection)
  CMakePresets.json            Build presets (native, Jetson, RPi, RISC-V)
  ExternalDependencies.cmake   Third-party deps (fmt, GoogleTest)
  Makefile                    Build entry point (make help for full list)
  docker-compose.yml          Dev containers (CPU, CUDA, cross-compile)
  cmake/
    seeker/                   CMake infrastructure (targets, testing, coverage)
    toolchains/               Cross-compilation toolchains (aarch64, riscv64)
  docker/
    base/                     Base image (LLVM 21, build tools, ccache)
    toolchain/                Cross-compiler sysroot images
    dev/                      Development shells (CPU, CUDA, Jetson, RPi, RISC-V)
    builder/                  CI artifact builders (one per platform)
    final.Dockerfile          Artifact packaging (collects all builders)
  mk/                         Make modules (build, test, docker, coverage, ...)
  src/
    cpu/                      CPU diagnostics library + tests
    memory/                   Memory diagnostics library + tests
    storage/                  Storage diagnostics library + tests
    network/                  Network diagnostics library + tests
    timing/                   Timing diagnostics library + tests
    device/                   Device diagnostics library + tests
    system/                   System diagnostics library + tests
    gpu/                      GPU diagnostics library + tests
    helpers/                  Shared utilities (files, strings, formatting)
  tools/
    cpp/                      34 C++ CLI tools organized by domain
```

---

## 10. See Also

**Domain documentation:**

- [CPU](src/cpu/README.md) -- [Memory](src/memory/README.md) -- [Storage](src/storage/README.md) -- [Network](src/network/README.md)
- [Timing](src/timing/README.md) -- [Device](src/device/README.md) -- [System](src/system/README.md) -- [GPU](src/gpu/README.md)

**Tool documentation:**

- [CPU Tools](tools/cpp/cpu/README.md) -- [Memory Tools](tools/cpp/memory/README.md) -- [Storage Tools](tools/cpp/storage/README.md) -- [Network Tools](tools/cpp/network/README.md)
- [Timing Tools](tools/cpp/timing/README.md) -- [Device Tools](tools/cpp/device/README.md) -- [System Tools](tools/cpp/system/README.md) -- [GPU Tools](tools/cpp/gpu/README.md)
