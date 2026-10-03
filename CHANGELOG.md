# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## v1.1.0

### Added

- aarch64 CPU identification in `CpuFeatures`: a `CpuArch` tag; NEON, SVE,
  SVE2, dot product, I8MM, BF16, AES, PMULL, SHA1/SHA2/SHA3/SHA512, CRC32 and
  LSE flags from `getauxval(AT_HWCAP/AT_HWCAP2)` with a `/proc/cpuinfo`
  fallback; MIDR implementer and part numbers; vendor and brand strings from
  MIDR name tables (`armImplementerName()`, `armPartName()`)
- NVIDIA GPU enumeration through NVML in `GpuTopology` (name, UUID, compute
  capability, memory, PCI address) when the GPU module is not compiled as CUDA
- `PcieStatus::hasLinkInfo()` to tell a GPU without PCIe link attributes
  (integrated GPUs) from a degraded link
- `BlockDevice::isMmc()` and `BlockDeviceList::countMmc()`; SD cards and eMMC
  report device type `SD/eMMC`
- `KernelInfo::preemptDynamic`; the active mode of a PREEMPT_DYNAMIC kernel is
  read from debugfs or the `preempt=` boot parameter
- cpu-info and cpu-snapshot print aarch64 features and an `arch` JSON field
- `SYNC_INTERVAL_BYTES` (4 MiB) in `StorageBench`: synced benchmark writes call
  `fdatasync` at this interval, so no sync flushes more than 4 MiB

### Changed

- `InterfaceInfo::hasLink()` follows the operational state alone; a link
  speed is not required (Wi-Fi reports none)
- Only Ethernet-framed (including Wi-Fi) and InfiniBand links count as
  physical NICs (`isNicLinkType()`); CAN and raw-IP interfaces do not
- `GpuTopology` lists NVIDIA devices first (NVML ordinals), then PCI display
  controllers (class 0x03) from sysfs ordered by DRM card number, without
  duplicates; PCI addresses use the sysfs form (`0000:01:00.0`)
- `PcieStatus` device indices resolve through `GpuTopology`;
  `getAllPcieStatus()` returns one entry per topology device, and
  `getPcieStatus()` is documented as not RT-safe (it enumerates the topology)
- `getCpuFeatures()` stays RT-safe on x86; on aarch64 it reads /proc/cpuinfo
  and is meant for startup queries
- `GpuDriverStatus::cudaDriverVersion` comes from NVML when the module is not
  compiled as CUDA
- `CpuTopology` groups logical CPUs into cores by the kernel's sibling lists
- cpu-rtcheck reports device IRQs on RT cores as a rate over its sample window
  and skips per-CPU sources with non-numeric names (local timer, IPIs) and the
  ARM arch_timer; idle-state advice and the invariant TSC check depend on the
  architecture
- timing-rtcheck accepts the ARM `arch_sys_counter` clocksource and exits 0, 1
  or 2 (pass, warnings, failures) like the other rtcheck tools
- sys-rtcheck grades RT scheduling and memory locking by `RLIMIT_RTPRIO` and
  `RLIMIT_MEMLOCK` when no capability grants them, and grades PREEMPT_DYNAMIC
  kernels by their active mode
- mem-rtcheck skips the hugepage check on kernels without hugepage support;
  storage-rtcheck warns when the only storage is SD/eMMC
- gpu-info, gpu-stat and gpu-rtcheck follow the topology ordinals; NVML checks
  are skipped for non-NVIDIA GPUs, the PCIe check is skipped without link
  attributes, and a device with no applicable check gets verdict `UNKNOWN`
- cpu-info shows threads per core and the frequency range across mixed core
  types
- `StorageBench` time budget: `timeBudgetSec` bounds each benchmark's wall time,
  setup and every sync included, to the budget plus one I/O and one sync of at
  most `SYNC_INTERVAL_BYTES` (a suite run to five times that). The
  sequential-read, random-read and random-write benchmarks write their file
  within half the budget and run on what was written, so `dataSize` is an upper
  bound. storage-bench `--budget` and `--size` follow the same contract
- Sequential write with `useFsync` syncs every `SYNC_INTERVAL_BYTES` inside the
  measured time, so its throughput is the rate at which data is written and
  synced in 4 MiB steps, sync time included. That reads lower than writing
  everything and syncing once at the end: about 10% lower on an SD card and
  15 to 25% lower on an NVMe drive (ext4) at the default size
- The sequential-read benchmark syncs only its own file and asks the kernel to
  drop that file's cached pages before reading (best effort), so its throughput
  is the device's where the kernel honours the request (not on tmpfs)

### Fixed

- aarch64 systems reported no CPU vendor, brand or SIMD features
- Platform and virtual DRM devices (vc4, v3d, host1x, evdi) were listed as
  GPUs, and NVIDIA GPUs were missed unless the module was compiled as CUDA
- PCIe status was empty unless the module was compiled as CUDA, and an
  integrated GPU's placeholder link was graded as degraded
- SD cards and eMMC were counted as SSDs
- PREEMPT_DYNAMIC kernels were graded as fully preemptible
- Systems that report core_id 0 for every core were counted as one core
- gpu-stat printed `%%` instead of `%`
- Storage benchmarks overran their time budget by seconds on slow or busy
  disks: sequential write synced everything it had written after the deadline,
  and the read and random benchmarks wrote and synced their whole file before
  the budget started
- The sequential-read benchmark called `sync()`, flushing every filesystem on
  the machine while dropping no cached pages
- The storage-bench README promised a ~10 second quick run, privileges for
  `--direct` and large sequential I/O; the sequential benchmarks issue 4 KiB
  operations

---

## v1.0.1

### Fixed

- `cpp_tools` aggregate target scoped with `PROJECT_NAME` to avoid collisions
  in multi-project builds
- Test and benchmark subdirectories guarded behind `PROJECT_IS_TOP_LEVEL` to
  prevent CTest registration and coverage manifest pollution when consumed via
  FetchContent

---

## v1.0.0

Initial release. System diagnostics library with hardware introspection and
RT-safety annotations covering CPU, memory, storage, network, timing, device,
GPU, and system domains.
