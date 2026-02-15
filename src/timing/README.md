# Timing Diagnostics Module

**Namespace:** `seeker::timing`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive timing telemetry for real-time and performance-critical systems. This module provides 6 focused components for monitoring clocksources, timer configuration, time synchronization, sleep jitter, PTP hardware clocks, and hardware RTCs.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [ClockSource](#clocksource) - Clocksource and timer resolution
   - [TimerConfig](#timerconfig) - Timer slack and tickless configuration
   - [TimeSyncStatus](#timesyncstatus) - NTP/PTP synchronization status
   - [LatencyBench](#latencybench) - Sleep jitter benchmarks
   - [PtpStatus](#ptpstatus) - PTP hardware clock capabilities
   - [RtcStatus](#rtcstatus) - Hardware RTC status and drift
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT Timing Validation](#example-rt-timing-validation)

---

## Overview

The timing diagnostics module answers these questions for RT systems:

| Question                                          | Module           |
| ------------------------------------------------- | ---------------- |
| What clocksource is active (TSC, HPET, acpi_pm)?  | `ClockSource`    |
| What is the timer resolution for each clock type? | `ClockSource`    |
| Are high-resolution timers enabled?               | `ClockSource`    |
| What is the current process timer_slack?          | `TimerConfig`    |
| Are tickless (nohz_full) CPUs configured?         | `TimerConfig`    |
| Is this a PREEMPT_RT kernel?                      | `TimerConfig`    |
| What NTP/PTP sync daemon is running?              | `TimeSyncStatus` |
| Are PTP hardware clocks available?                | `TimeSyncStatus` |
| Is the kernel clock synchronized?                 | `TimeSyncStatus` |
| What is the actual sleep jitter on this system?   | `LatencyBench`   |
| What are the p99/max latencies for sleep calls?   | `LatencyBench`   |
| What PTP capabilities does my hardware support?   | `PtpStatus`      |
| Which NIC is bound to which PTP clock?            | `PtpStatus`      |
| Is the hardware RTC healthy and accurate?         | `RtcStatus`      |
| Has the RTC drifted from system time?             | `RtcStatus`      |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/timing/inc/ClockSource.hpp"
#include "src/timing/inc/TimerConfig.hpp"
#include "src/timing/inc/TimeSyncStatus.hpp"
#include "src/timing/inc/LatencyBench.hpp"
#include "src/timing/inc/PtpStatus.hpp"
#include "src/timing/inc/RtcStatus.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::timing;

// Static system info (query once at startup)
auto clock = getClockSource();           // Clocksource and resolutions
auto config = getTimerConfig();          // Timer slack, nohz_full, PREEMPT_RT

// Dynamic state (query periodically)
auto sync = getTimeSyncStatus();         // NTP/PTP status, kernel time state
auto kernel = getKernelTimeStatus();     // Just kernel sync status (RT-safe)

// Hardware clock details
auto ptp = getPtpStatus();               // PTP hardware capabilities
auto rtc = getRtcStatus();               // Hardware RTC status and drift
```

### Timer Slack Optimization

```cpp
using namespace seeker::timing;

// Check and optimize timer slack at startup
auto config = getTimerConfig();
if (config.hasDefaultSlack()) {
  // Default ~50us slack adds jitter to sleep calls
  setTimerSlackNs(1);  // Minimize to 1ns
}
```

### PTP Clock Selection

```cpp
using namespace seeker::timing;

auto ptp = getPtpStatus();
if (ptp.clockCount > 0) {
  // Find best clock for RT workloads
  const auto* best = ptp.getBestClock();
  if (best != nullptr) {
    fmt::print("Best PTP clock: {} (score: {}/100)\n",
               best->device.data(), best->rtScore());
  }
}
```

### RTC Health Check

```cpp
using namespace seeker::timing;

auto rtc = getRtcStatus();
if (rtc.rtcSupported && rtc.deviceCount > 0) {
  const auto* sysRtc = rtc.getSystemRtc();
  if (sysRtc != nullptr) {
    fmt::print("RTC health: {}\n", sysRtc->healthString());
    if (!sysRtc->time.isDriftAcceptable()) {
      fmt::print("Warning: RTC drift {} sec - run hwclock --systohc\n",
                 sysRtc->time.driftSeconds);
    }
  }
}
```

### Latency Benchmarking

```cpp
using namespace seeker::timing;

// Quick benchmark (250ms, 1ms sleep target)
auto stats = measureLatency(std::chrono::milliseconds{250});

// RT characterization (with priority elevation)
BenchConfig config;
config.budget = std::chrono::milliseconds{2000};
config.sleepTarget = std::chrono::microseconds{100};
config.useAbsoluteTime = true;
config.rtPriority = 90;  // SCHED_FIFO 90
auto rtStats = measureLatency(config);
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
 * @brief Query clocksource and timer resolution.
 * @return Populated ClockSource with current settings and resolutions.
 * @note RT-safe: Bounded syscalls and file reads, fixed-size output.
 */
[[nodiscard]] ClockSource getClockSource() noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, CLOCKSOURCE_NAME_SIZE> current{};

// Instead of std::vector (allocates)
std::array<std::array<char, CLOCKSOURCE_NAME_SIZE>, MAX_CLOCKSOURCES> available{};

// Fixed bitset for CPU tracking
std::bitset<MAX_NOHZ_CPUS> nohzFullCpus{};
```

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto config = getTimerConfig();
if (!config.slackQuerySucceeded) {
  // prctl query failed - handle gracefully
}

auto sync = getTimeSyncStatus();
if (!sync.kernel.querySucceeded) {
  // adjtimex failed (containerized environment, etc.)
}

auto ptp = getPtpStatus();
for (std::size_t i = 0; i < ptp.clockCount; ++i) {
  if (!ptp.clocks[i].capsQuerySucceeded) {
    // ioctl failed - check permissions on /dev/ptpN
  }
}
```

---

## Module Reference

---

### ClockSource

**Header:** `ClockSource.hpp`
**Purpose:** Query kernel clocksource and timer resolution for all clock types.

#### Key Types

```cpp
inline constexpr std::size_t CLOCKSOURCE_NAME_SIZE = 32;
inline constexpr std::size_t MAX_CLOCKSOURCES = 8;

struct ClockResolution {
  std::int64_t resolutionNs{0};  ///< clock_getres() result in nanoseconds
  bool available{false};          ///< True if clock type is accessible

  bool isHighRes() const noexcept;  // <= 1 microsecond
  bool isCoarse() const noexcept;   // > 1 millisecond
};

struct ClockSource {
  std::array<char, CLOCKSOURCE_NAME_SIZE> current{};  ///< Active clocksource
  std::array<std::array<char, CLOCKSOURCE_NAME_SIZE>, MAX_CLOCKSOURCES> available{};
  std::size_t availableCount{0};

  // Timer resolutions for all clock types
  ClockResolution monotonic{};        ///< CLOCK_MONOTONIC
  ClockResolution monotonicRaw{};     ///< CLOCK_MONOTONIC_RAW
  ClockResolution monotonicCoarse{};  ///< CLOCK_MONOTONIC_COARSE
  ClockResolution realtime{};         ///< CLOCK_REALTIME
  ClockResolution realtimeCoarse{};   ///< CLOCK_REALTIME_COARSE
  ClockResolution boottime{};         ///< CLOCK_BOOTTIME

  bool isTsc() const noexcept;
  bool isHpet() const noexcept;
  bool isAcpiPm() const noexcept;
  bool hasHighResTimers() const noexcept;
  bool hasClockSource(const char* name) const noexcept;
  int rtScore() const noexcept;       // 0-100 RT suitability
  std::string toString() const;       // NOT RT-safe
};
```

#### API

```cpp
/// Query clocksource and resolutions (RT-safe)
[[nodiscard]] ClockSource getClockSource() noexcept;

/// Get resolution for specific clock ID (RT-safe)
[[nodiscard]] std::int64_t getClockResolutionNs(int clockId) noexcept;
```

#### Usage

```cpp
using namespace seeker::timing;

auto cs = getClockSource();

if (!cs.isTsc()) {
  fmt::print("Warning: Not using TSC clocksource ({})\n", cs.current.data());
}

if (!cs.hasHighResTimers()) {
  fmt::print("Warning: High-res timers not enabled\n");
}

fmt::print("MONOTONIC resolution: {} ns\n", cs.monotonic.resolutionNs);
fmt::print("RT Score: {}/100\n", cs.rtScore());
```

#### Data Sources

- `/sys/devices/system/clocksource/clocksource0/current_clocksource`
- `/sys/devices/system/clocksource/clocksource0/available_clocksource`
- `clock_getres(2)` for all clock types

---

### TimerConfig

**Header:** `TimerConfig.hpp`
**Purpose:** Query and set timer slack, tickless configuration, and RT kernel detection.

#### Key Types

```cpp
inline constexpr std::size_t MAX_NOHZ_CPUS = 256;
inline constexpr std::uint64_t DEFAULT_TIMER_SLACK_NS = 50'000;

struct TimerConfig {
  // Process timer slack
  std::uint64_t timerSlackNs{0};
  bool slackQuerySucceeded{false};

  // High-res timer status
  bool highResTimersEnabled{false};

  // Tickless configuration
  bool nohzFullEnabled{false};
  std::bitset<MAX_NOHZ_CPUS> nohzFullCpus{};
  std::size_t nohzFullCount{0};

  // Additional kernel parameters
  bool nohzIdleEnabled{false};
  bool preemptRtEnabled{false};

  bool hasMinimalSlack() const noexcept;   // <= 1ns
  bool hasDefaultSlack() const noexcept;   // ~50us
  bool isNohzFullCpu(std::size_t cpuId) const noexcept;
  bool isOptimalForRt() const noexcept;
  int rtScore() const noexcept;            // 0-100 RT suitability
  std::string toString() const;            // NOT RT-safe
};
```

#### API

```cpp
/// Query timer configuration (RT-safe)
[[nodiscard]] TimerConfig getTimerConfig() noexcept;

/// Get current process timer slack (RT-safe)
[[nodiscard]] std::uint64_t getTimerSlackNs() noexcept;

/// Set timer slack for current process (RT-safe)
[[nodiscard]] bool setTimerSlackNs(std::uint64_t slackNs) noexcept;

/// Check if PREEMPT_RT kernel is running (RT-safe)
[[nodiscard]] bool isPreemptRtKernel() noexcept;
```

#### Usage

```cpp
using namespace seeker::timing;

auto cfg = getTimerConfig();

// Optimize timer slack for RT
if (!cfg.hasMinimalSlack()) {
  setTimerSlackNs(1);
}

// Check for tickless CPUs
if (cfg.nohzFullCount > 0) {
  fmt::print("Tickless CPUs available: {}\n", cfg.nohzFullCount);
}
```

#### Data Sources

- `prctl(PR_GET_TIMERSLACK)` - Timer slack value
- `/proc/cmdline` - nohz_full=, PREEMPT_RT detection
- `/sys/devices/system/cpu/nohz_full` - Tickless CPU mask
- `clock_getres(CLOCK_MONOTONIC)` - High-res timer detection
- `/sys/kernel/realtime` - PREEMPT_RT detection

---

### TimeSyncStatus

**Header:** `TimeSyncStatus.hpp`
**Purpose:** Query time synchronization status including NTP/PTP daemons, PTP hardware, and kernel time state.

#### Key Types

```cpp
inline constexpr std::size_t MAX_PTP_DEVICES = 8;
inline constexpr std::size_t PTP_NAME_SIZE = 16;
inline constexpr std::size_t PTP_CLOCK_NAME_SIZE = 64;

struct PtpDevice {
  std::array<char, PTP_NAME_SIZE> name{};        ///< Device name (e.g., "ptp0")
  std::array<char, PTP_CLOCK_NAME_SIZE> clock{};  ///< Clock identity/name
  std::int64_t maxAdjPpb{0};                      ///< Maximum adjustment (ppb)
  int ppsAvailable{-1};                           ///< PPS output (1=yes, 0=no, -1=unknown)

  bool isValid() const noexcept;
};

struct KernelTimeStatus {
  bool synced{false};           ///< TIME_OK from adjtimex
  bool pll{false};              ///< PLL mode active
  bool ppsFreq{false};          ///< PPS frequency discipline
  bool ppsTime{false};          ///< PPS time discipline
  bool freqHold{false};         ///< Frequency hold mode

  std::int64_t offsetUs{0};     ///< Time offset in microseconds
  std::int64_t freqPpb{0};      ///< Frequency adjustment in ppb
  std::int64_t maxErrorUs{0};   ///< Maximum error estimate
  std::int64_t estErrorUs{0};   ///< Estimated error

  int clockState{0};            ///< Clock state from adjtimex return value
  bool querySucceeded{false};   ///< True if adjtimex query succeeded

  bool isWellSynced() const noexcept;      // synced && low error
  const char* qualityString() const noexcept;  // "excellent"/"good"/"fair"/"poor"
};

struct TimeSyncStatus {
  // Detected sync daemons
  bool chronyDetected{false};
  bool ntpdDetected{false};
  bool systemdTimesyncDetected{false};
  bool ptpLinuxDetected{false};

  // PTP hardware
  PtpDevice ptpDevices[MAX_PTP_DEVICES]{};
  std::size_t ptpDeviceCount{0};

  // Kernel time state
  KernelTimeStatus kernel{};

  bool hasAnySyncDaemon() const noexcept;
  bool hasPtpHardware() const noexcept;
  const char* primarySyncMethod() const noexcept;
  int rtScore() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
/// Query full sync status (NOT RT-safe: directory scanning)
[[nodiscard]] TimeSyncStatus getTimeSyncStatus() noexcept;

/// Query just kernel time status (RT-safe: single adjtimex call)
[[nodiscard]] KernelTimeStatus getKernelTimeStatus() noexcept;

/// Check if a specific sync daemon is running (NOT RT-safe: file checks)
[[nodiscard]] bool isSyncDaemonRunning(const char* daemon) noexcept;
```

#### Data Sources

- `/run/chrony/`, `/var/run/chrony/` - chrony presence
- `/var/lib/ntp/`, `/run/ntpd.pid` - ntpd presence
- `/run/systemd/timesync/` - systemd-timesyncd presence
- `/run/ptp4l*` - linuxptp presence
- `/sys/class/ptp/ptp*` - PTP device enumeration
- `adjtimex(2)` - Kernel time state

---

### LatencyBench

**Header:** `LatencyBench.hpp`
**Purpose:** Measure timer overhead and sleep jitter with configurable parameters.

#### Key Types

```cpp
inline constexpr std::size_t MAX_LATENCY_SAMPLES = 8192;
inline constexpr std::chrono::milliseconds MIN_BENCH_BUDGET{50};

struct LatencyStats {
  std::size_t sampleCount{0};

  // Timer overhead
  double nowOverheadNs{0.0};    ///< steady_clock::now() call overhead

  // Sleep statistics (actual sleep durations)
  double targetNs{0.0};         ///< Requested sleep duration
  double minNs{0.0};            ///< Minimum observed sleep
  double maxNs{0.0};            ///< Maximum observed sleep
  double meanNs{0.0};           ///< Mean sleep duration
  double medianNs{0.0};         ///< Median (p50) sleep duration
  double p90Ns{0.0};            ///< 90th percentile
  double p95Ns{0.0};            ///< 95th percentile
  double p99Ns{0.0};            ///< 99th percentile
  double p999Ns{0.0};           ///< 99.9th percentile
  double stdDevNs{0.0};         ///< Standard deviation

  // Measurement metadata
  bool usedAbsoluteTime{false}; ///< True if TIMER_ABSTIME was used
  bool usedRtPriority{false};   ///< True if RT priority was elevated
  int rtPriorityUsed{0};        ///< SCHED_FIFO priority (0 = not elevated)

  // Jitter helpers (actual - target)
  double jitterMeanNs() const noexcept;   // mean - target
  double jitterP95Ns() const noexcept;    // p95 - target
  double jitterP99Ns() const noexcept;    // p99 - target
  double jitterMaxNs() const noexcept;    // max - target
  double undershootNs() const noexcept;   // target - min
  bool isGoodForRt() const noexcept;      // p99 jitter < 100us
  int rtScore() const noexcept;           // 0-100
  std::string toString() const;           // NOT RT-safe
};

struct BenchConfig {
  std::chrono::milliseconds budget{250};
  std::chrono::microseconds sleepTarget{1000};
  bool useAbsoluteTime{false};   ///< Use TIMER_ABSTIME
  int rtPriority{0};             ///< 0=no change, 1-99=SCHED_FIFO

  static BenchConfig quick() noexcept;
  static BenchConfig thorough() noexcept;
  static BenchConfig rtCharacterization() noexcept;
};
```

#### API

```cpp
/// Configurable benchmark (NOT RT-safe)
[[nodiscard]] LatencyStats measureLatency(const BenchConfig& config) noexcept;

/// Quick benchmark with default settings (NOT RT-safe)
[[nodiscard]] LatencyStats measureLatency(std::chrono::milliseconds budget) noexcept;

/// Measure clock_gettime overhead (RT-safe after warmup)
[[nodiscard]] double measureNowOverhead(std::size_t iterations = 10000) noexcept;
```

---

### PtpStatus

**Header:** `PtpStatus.hpp`
**Purpose:** Query PTP (Precision Time Protocol) hardware clock capabilities and network interface bindings.

#### Key Types

```cpp
inline constexpr std::size_t PTP_MAX_CLOCKS = 8;
inline constexpr std::size_t PTP_DEVICE_NAME_SIZE = 16;
inline constexpr std::size_t PTP_CLOCK_DRIVER_NAME_SIZE = 64;
inline constexpr std::size_t PTP_IFACE_NAME_SIZE = 16;

struct PtpClockCaps {
  std::int32_t maxAdjPpb{0};      ///< Maximum frequency adjustment (ppb)
  std::int32_t nAlarm{0};         ///< Number of programmable alarms
  std::int32_t nExtTs{0};         ///< Number of external timestamp channels
  std::int32_t nPerOut{0};        ///< Number of periodic output channels
  std::int32_t nPins{0};          ///< Number of programmable pins
  bool pps{false};                ///< PPS (pulse-per-second) output support
  bool crossTimestamp{false};     ///< Cross-timestamp support (PHC <-> system)
  bool adjustPhase{false};        ///< Phase adjustment support
  std::int32_t maxAdjPhaseNs{0};  ///< Maximum phase adjustment (ns)

  bool hasExtTimestamp() const noexcept;
  bool hasPeriodicOutput() const noexcept;
  bool hasHighPrecisionSync() const noexcept;  // crossTimestamp && pps
};

struct PtpClock {
  std::array<char, PTP_DEVICE_NAME_SIZE> device{};           ///< e.g., "ptp0"
  std::array<char, PTP_CLOCK_DRIVER_NAME_SIZE> clockName{};  ///< Driver name
  std::int32_t index{-1};                                    ///< PTP index
  std::int32_t phcIndex{-1};                                 ///< PHC index for binding

  PtpClockCaps caps{};
  bool capsQuerySucceeded{false};  ///< True if ioctl succeeded

  std::array<char, PTP_IFACE_NAME_SIZE> boundInterface{};  ///< e.g., "eth0"
  bool hasBoundInterface{false};

  bool isValid() const noexcept;
  int rtScore() const noexcept;  // 0-100 RT suitability
};

struct PtpStatus {
  std::array<PtpClock, PTP_MAX_CLOCKS> clocks{};
  std::size_t clockCount{0};

  bool ptpSupported{false};      ///< PTP subsystem available
  bool hasHardwareClock{false};  ///< At least one hardware PTP clock present

  const PtpClock* findByDevice(const char* device) const noexcept;
  const PtpClock* findByIndex(std::int32_t index) const noexcept;
  const PtpClock* findByInterface(const char* iface) const noexcept;
  const PtpClock* getBestClock() const noexcept;  // Highest rtScore
  int rtScore() const noexcept;
  std::string toString() const;
  std::string toJson() const;
};
```

#### API

```cpp
/// Query PTP subsystem status (NOT RT-safe: directory scan, ioctls)
[[nodiscard]] PtpStatus getPtpStatus() noexcept;

/// Query specific PTP clock capabilities (NOT RT-safe: ioctl)
[[nodiscard]] PtpClockCaps getPtpClockCaps(const char* device) noexcept;

/// Check if PTP is supported (RT-safe: single stat() call)
[[nodiscard]] bool isPtpSupported() noexcept;

/// Get PHC index for a network interface (RT-safe: single file read)
[[nodiscard]] std::int32_t getPhcIndexForInterface(const char* iface) noexcept;
```

#### Usage

```cpp
using namespace seeker::timing;

auto ptp = getPtpStatus();

fmt::print("PTP Clocks: {}\n", ptp.clockCount);
for (std::size_t i = 0; i < ptp.clockCount; ++i) {
  const auto& clk = ptp.clocks[i];
  fmt::print("  {}: {} (max adj: {} ppb)\n",
             clk.device.data(), clk.clockName.data(), clk.caps.maxAdjPpb);
  if (clk.caps.pps) {
    fmt::print("    - PPS output available\n");
  }
  if (clk.hasBoundInterface) {
    fmt::print("    - Bound to interface: {}\n", clk.boundInterface.data());
  }
}

// Find best clock for RT
const auto* best = ptp.getBestClock();
if (best != nullptr) {
  fmt::print("Recommended: {} (score: {}/100)\n", best->device.data(), best->rtScore());
}
```

#### Data Sources

- `/sys/class/ptp/` - PTP clock enumeration
- `/dev/ptp*` - PTP_CLOCK_GETCAPS ioctl for capabilities
- `/sys/class/ptp/ptpN/clock_name` - Clock identity
- `/sys/class/net/<iface>/device/ptp/` - Interface-to-PTP binding

---

### RtcStatus

**Header:** `RtcStatus.hpp`
**Purpose:** Query hardware Real-Time Clock (RTC) status, capabilities, and drift from system time.

#### Key Types

```cpp
inline constexpr std::size_t RTC_MAX_DEVICES = 4;
inline constexpr std::size_t RTC_DEVICE_NAME_SIZE = 16;
inline constexpr std::size_t RTC_DRIVER_NAME_SIZE = 64;

struct RtcCapabilities {
  bool hasAlarm{false};       ///< Supports alarm interrupts
  bool hasPeriodicIrq{false}; ///< Supports periodic interrupts
  bool hasUpdateIrq{false};   ///< Supports update-complete interrupts
  bool hasWakeAlarm{false};   ///< Supports wake-from-suspend via alarm
  bool hasBattery{false};     ///< Battery-backed (inferred)
  std::int32_t irqFreqMin{0}; ///< Minimum IRQ frequency (if periodic IRQ)
  std::int32_t irqFreqMax{0}; ///< Maximum IRQ frequency (if periodic IRQ)

  bool canWakeFromSuspend() const noexcept;
};

struct RtcTime {
  std::int32_t year{0};           ///< Full year (e.g., 2024)
  std::int32_t month{0};          ///< 1-12
  std::int32_t day{0};            ///< 1-31
  std::int32_t hour{0};           ///< 0-23
  std::int32_t minute{0};         ///< 0-59
  std::int32_t second{0};         ///< 0-59
  std::int64_t epochSeconds{0};   ///< Unix epoch seconds
  std::int64_t systemEpochSec{0}; ///< System time at query (for drift calc)
  std::int64_t driftSeconds{0};   ///< RTC - system time (positive = RTC ahead)
  bool querySucceeded{false};

  bool isValid() const noexcept;
  bool isDriftAcceptable() const noexcept;  // |drift| <= 5 sec
  std::int64_t absDrift() const noexcept;
};

struct RtcAlarm {
  bool enabled{false};            ///< Alarm is set and enabled
  bool pending{false};            ///< Alarm has fired but not cleared
  std::int64_t alarmEpoch{0};     ///< Alarm time (Unix epoch), 0 if not set
  std::int64_t secondsUntil{0};   ///< Seconds until alarm fires (negative = past)
  bool querySucceeded{false};

  bool isFutureAlarm() const noexcept;
};

struct RtcDevice {
  std::array<char, RTC_DEVICE_NAME_SIZE> device{};    ///< e.g., "rtc0"
  std::array<char, RTC_DRIVER_NAME_SIZE> name{};      ///< Driver/chip name
  std::array<char, RTC_DRIVER_NAME_SIZE> hctosys{};   ///< "1" if system clock set from this RTC
  std::int32_t index{-1};

  RtcCapabilities caps{};
  RtcTime time{};
  RtcAlarm alarm{};

  bool isSystemRtc{false};             ///< True if hctosys or rtc0

  bool isValid() const noexcept;
  const char* healthString() const noexcept;  // "healthy"/"drifted"/"invalid"/"unreadable"
};

struct RtcStatus {
  std::array<RtcDevice, RTC_MAX_DEVICES> devices{};
  std::size_t deviceCount{0};

  bool rtcSupported{false};          ///< RTC subsystem available
  bool hasHardwareRtc{false};        ///< At least one hardware RTC present
  bool hasWakeCapable{false};        ///< At least one device can wake from suspend
  std::int32_t systemRtcIndex{-1};   ///< Index of system RTC, -1 if not determined

  const RtcDevice* findByName(const char* name) const noexcept;
  const RtcDevice* findByIndex(std::int32_t index) const noexcept;
  const RtcDevice* getSystemRtc() const noexcept;
  std::int64_t maxDriftSeconds() const noexcept;
  bool allDriftAcceptable() const noexcept;
  std::string toString() const;
  std::string toJson() const;
};
```

#### API

```cpp
/// Query RTC subsystem status (NOT RT-safe: directory scan)
[[nodiscard]] RtcStatus getRtcStatus() noexcept;

/// Query time from specific RTC (NOT RT-safe: sysfs read)
[[nodiscard]] RtcTime getRtcTime(const char* device) noexcept;

/// Query alarm from specific RTC (NOT RT-safe: sysfs read)
[[nodiscard]] RtcAlarm getRtcAlarm(const char* device) noexcept;

/// Check if RTC is supported (RT-safe: single stat() call)
[[nodiscard]] bool isRtcSupported() noexcept;
```

#### Usage

```cpp
using namespace seeker::timing;

auto rtc = getRtcStatus();

fmt::print("RTC Devices: {}\n", rtc.deviceCount);
for (std::size_t i = 0; i < rtc.deviceCount; ++i) {
  const auto& dev = rtc.devices[i];
  fmt::print("  {}{}: {}\n",
             dev.device.data(),
             dev.isSystemRtc ? " [system]" : "",
             dev.name.data());
  fmt::print("    Health: {}\n", dev.healthString());

  if (dev.time.querySucceeded && dev.time.isValid()) {
    fmt::print("    Time: {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}\n",
               dev.time.year, dev.time.month, dev.time.day,
               dev.time.hour, dev.time.minute, dev.time.second);
    fmt::print("    Drift: {} sec {}\n",
               dev.time.driftSeconds,
               dev.time.isDriftAcceptable() ? "[OK]" : "[HIGH]");
  }

  if (dev.caps.hasWakeAlarm) {
    fmt::print("    Wake-from-suspend: supported\n");
  }
}

// Check overall health
if (!rtc.allDriftAcceptable()) {
  fmt::print("Warning: RTC drift exceeds threshold - run hwclock --systohc\n");
}
```

#### Data Sources

- `/sys/class/rtc/` - RTC device enumeration
- `/sys/class/rtc/rtcN/name` - Driver/chip name
- `/sys/class/rtc/rtcN/hctosys` - System clock source flag
- `/sys/class/rtc/rtcN/time`, `/date` - Current RTC time
- `/sys/class/rtc/rtcN/wakealarm` - Wake alarm setting

---

## Common Patterns

### Query Once vs. Query Periodically

**Query once at startup:**

```cpp
auto cs = getClockSource();      // Clocksource rarely changes
auto cfg = getTimerConfig();     // Timer config is mostly static
auto ptp = getPtpStatus();       // PTP hardware doesn't change
```

**Query periodically:**

```cpp
auto sync = getTimeSyncStatus(); // Sync state changes
auto kernel = getKernelTimeStatus();  // Offset/error evolve
auto rtc = getRtcStatus();       // Drift changes over time
```

### RT-Safe vs. NOT RT-Safe

Functions with filesystem enumeration or dynamic allocation are NOT RT-safe:

```cpp
// NOT RT-safe - enumeration
auto ptp = getPtpStatus();
auto rtc = getRtcStatus();

// RT-safe - targeted queries
bool supported = isPtpSupported();
bool rtcOk = isRtcSupported();
std::int32_t phcIdx = getPhcIndexForInterface("eth0");
```

---

## Real-Time Considerations

### RT-Safe Functions (call from any thread)

- `getClockSource()` - Bounded sysfs reads, single syscall
- `getTimerConfig()` - Bounded file reads, prctl
- `getKernelTimeStatus()` - Single adjtimex() call
- `getTimerSlackNs()` - Single prctl call
- `isPtpSupported()` - Single stat() call
- `isRtcSupported()` - Single stat() call
- `isPreemptRtKernel()` - Single file check
- `getPhcIndexForInterface()` - Single sysfs read
- `measureNowOverhead()` - CPU-bound timing loop
- All struct helper methods (`rtScore()`, `isValid()`, etc.)

### NOT RT-Safe Functions (call from initialization thread)

- `getTimeSyncStatus()` - Directory scanning, file reads
- `getPtpStatus()` - Directory scanning, ioctls
- `getRtcStatus()` - Directory scanning
- `isSyncDaemonRunning()` - File existence checks
- `measureLatency()` - Active benchmark, may allocate, may change scheduling
- All `toString()` / `toJson()` methods - String allocation

### Recommended RT Timing Configuration

For best RT performance, verify these with the diagnostics:

1. **Clocksource:** TSC (lowest overhead, most accurate)
2. **High-res timers:** Enabled (resolution <= 1us)
3. **Timer slack:** Minimal (1ns) via `prctl(PR_SET_TIMERSLACK, 1)`
4. **Tickless:** nohz_full= on RT cores to eliminate timer interrupts
5. **PREEMPT_RT:** For hard RT requirements
6. **Time sync:** chrony or PTP for distributed systems
7. **PTP hardware:** Use NIC with PTP support for precision timing
8. **RTC health:** Verify RTC drift is acceptable (<5 sec)

### Timer Slack Explained

Timer slack allows the kernel to coalesce timer wakeups to save power:

```
Requested wakeup: T
Actual wakeup:    T to T+slack

Default slack: ~50us
Minimal slack: 1ns
```

For RT applications, always call `setTimerSlackNs(1)` at startup:

```cpp
#include <sys/prctl.h>

int main() {
  // First thing in main()
  prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
  // ...
}
```

---

## CLI Tools

The timing domain includes 4 command-line tools: `timing-info`, `timing-rtcheck`, `timing-bench`, `timing-sync`.

See: `tools/cpp/timing/README.md` for detailed tool usage.

---

## Example: RT Timing Validation

```cpp
#include "src/timing/inc/ClockSource.hpp"
#include "src/timing/inc/LatencyBench.hpp"
#include "src/timing/inc/PtpStatus.hpp"
#include "src/timing/inc/RtcStatus.hpp"
#include "src/timing/inc/TimerConfig.hpp"
#include "src/timing/inc/TimeSyncStatus.hpp"

#include <fmt/core.h>

using namespace seeker::timing;

int main() {
  fmt::print("=== RT Timing Validation ===\n\n");

  // 1. Clocksource
  auto cs = getClockSource();
  fmt::print("Clocksource: {}", cs.current.data());
  if (cs.isTsc()) {
    fmt::print(" [optimal]\n");
  } else {
    fmt::print(" [suboptimal - prefer TSC]\n");
  }

  // 2. Timer Resolution
  fmt::print("MONOTONIC resolution: {} ns", cs.monotonic.resolutionNs);
  if (cs.hasHighResTimers()) {
    fmt::print(" [high-res]\n");
  } else {
    fmt::print(" [COARSE - enable CONFIG_HIGH_RES_TIMERS]\n");
  }

  // 3. Timer Slack
  auto cfg = getTimerConfig();
  fmt::print("Timer slack: {} ns", cfg.timerSlackNs);
  if (cfg.hasMinimalSlack()) {
    fmt::print(" [optimal]\n");
  } else {
    fmt::print(" [optimizing...]\n");
    setTimerSlackNs(1);
  }

  // 4. Tickless CPUs
  fmt::print("nohz_full CPUs: ");
  if (cfg.nohzFullCount > 0) {
    fmt::print("{}\n", cfg.nohzFullCount);
  } else {
    fmt::print("none (add nohz_full= to kernel cmdline)\n");
  }

  // 5. PREEMPT_RT
  fmt::print("PREEMPT_RT: {}\n", cfg.preemptRtEnabled ? "yes" : "no");

  // 6. Time Sync
  auto sync = getTimeSyncStatus();
  fmt::print("Time sync: {}\n", sync.primarySyncMethod());
  if (sync.kernel.querySucceeded) {
    fmt::print("Kernel clock: {} ({})\n",
               sync.kernel.synced ? "synced" : "not synced",
               sync.kernel.qualityString());
  }

  // 7. PTP Hardware
  auto ptp = getPtpStatus();
  fmt::print("PTP clocks: {}\n", ptp.clockCount);
  if (ptp.clockCount > 0) {
    const auto* best = ptp.getBestClock();
    if (best != nullptr) {
      fmt::print("  Best for RT: {} (score: {}/100)\n",
                 best->device.data(), best->rtScore());
    }
  }

  // 8. RTC Health
  auto rtc = getRtcStatus();
  if (rtc.rtcSupported && rtc.deviceCount > 0) {
    const auto* sysRtc = rtc.getSystemRtc();
    if (sysRtc != nullptr) {
      fmt::print("RTC health: {} (drift: {} sec)\n",
                 sysRtc->healthString(), sysRtc->time.driftSeconds);
    }
  }

  // 9. Quick jitter test
  fmt::print("\nRunning jitter benchmark (500ms)...\n");
  auto stats = measureLatency(std::chrono::milliseconds{500});

  fmt::print("  Samples: {}\n", stats.sampleCount);
  fmt::print("  p99 jitter: {:.1f} us\n", stats.jitterP99Ns() / 1000.0);
  fmt::print("  Max jitter: {:.1f} us\n", stats.jitterMaxNs() / 1000.0);
  fmt::print("  RT Score: {}/100", stats.rtScore());
  if (stats.isGoodForRt()) {
    fmt::print(" [GOOD]\n");
  } else {
    fmt::print(" [NEEDS TUNING]\n");
  }

  // Summary
  fmt::print("\n=== Summary ===\n");
  fmt::print("Clock Source Score: {}/100\n", cs.rtScore());
  fmt::print("Timer Config Score: {}/100\n", cfg.rtScore());
  fmt::print("Time Sync Score:    {}/100\n", sync.rtScore());
  fmt::print("PTP Score:          {}/100\n", ptp.rtScore());
  fmt::print("Jitter Score:       {}/100\n", stats.rtScore());

  return 0;
}
```

---

## See Also

- `seeker::cpu` - CPU telemetry (topology, frequency, utilization, IRQs, isolation)
- `seeker::memory` - Memory telemetry (pages, NUMA, hugepages, mlock, ECC/EDAC)
- `seeker::storage` - Storage telemetry (block devices, I/O stats, benchmarks)
- `seeker::network` - Network telemetry (interfaces, IRQ affinity, traffic, ethtool)
- `seeker::system` - System telemetry (kernel, limits, capabilities, containers, drivers)
