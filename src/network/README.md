# Network Diagnostics Module

**Namespace:** `seeker::network`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive network telemetry for real-time and performance-critical systems. This module provides 6 focused components for monitoring network interfaces, traffic statistics, socket buffer configuration, NIC IRQ affinity, NIC driver tuning (ethtool), and loopback latency benchmarking.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [InterfaceInfo](#interfaceinfo) - NIC link status and properties
   - [InterfaceStats](#interfacestats) - Traffic counters and rates
   - [SocketBufferConfig](#socketbufferconfig) - System-wide buffer tuning
   - [NetworkIsolation](#networkisolation) - NIC IRQ affinity analysis
   - [EthtoolInfo](#ethtoolinfo) - Ring buffers, coalescing, offloads
   - [LoopbackBench](#loopbackbench) - Network stack latency measurement
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: RT Network Validation](#example-rt-network-validation)

---

## Overview

The network diagnostics module answers these questions for RT and HPC systems:

| Question                                            | Module               |
| --------------------------------------------------- | -------------------- |
| What NICs are present and what's their link status? | `InterfaceInfo`      |
| What's the speed, duplex, MTU, and driver?          | `InterfaceInfo`      |
| How many RX/TX queues does each NIC have?           | `InterfaceInfo`      |
| What's the current throughput and packet rate?      | `InterfaceStats`     |
| Are packets being dropped or errored?               | `InterfaceStats`     |
| What are the socket buffer limits?                  | `SocketBufferConfig` |
| Is busy polling enabled for low latency?            | `SocketBufferConfig` |
| Are NIC IRQs hitting my RT cores?                   | `NetworkIsolation`   |
| What are the ring buffer and coalescing settings?   | `EthtoolInfo`        |
| Is adaptive interrupt coalescing enabled?           | `EthtoolInfo`        |
| Which offloads are active (TSO, GRO, LRO)?          | `EthtoolInfo`        |
| What's the baseline network stack latency?          | `LoopbackBench`      |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/network/inc/InterfaceInfo.hpp"
#include "src/network/inc/InterfaceStats.hpp"
#include "src/network/inc/SocketBufferConfig.hpp"
#include "src/network/inc/NetworkIsolation.hpp"
#include "src/network/inc/EthtoolInfo.hpp"
#include "src/network/inc/LoopbackBench.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::network;

// Static system info (query once at startup)
auto interfaces = getAllInterfaces();         // All NICs
auto physical = getPhysicalInterfaces();      // Physical NICs only
auto bufConfig = getSocketBufferConfig();     // Buffer limits, TCP tunables
auto netIso = getNetworkIsolation();          // NIC IRQ affinities
auto ethtoolList = getAllEthtoolInfo();       // Ring buffers, coalescing, offloads

// Single interface query (RT-safe)
auto eth0 = getInterfaceInfo("eth0");         // Specific NIC info
auto counters = getInterfaceCounters("eth0"); // Raw traffic counters
auto ethInfo = getEthtoolInfo("eth0");        // Ethtool settings for one NIC
```

### Snapshot + Delta Pattern

For rate-based metrics (throughput, packet rates):

```cpp
using namespace seeker::network;

// Take two snapshots with a delay
auto before = getInterfaceStatsSnapshot();
std::this_thread::sleep_for(std::chrono::milliseconds(100));
auto after = getInterfaceStatsSnapshot();

// Compute delta (rates in bytes/sec, packets/sec)
auto delta = computeStatsDelta(before, after);

// Access rates for specific interface
if (const auto* rates = delta.find("eth0")) {
  fmt::print("RX: {:.2f} Mbps, TX: {:.2f} Mbps\n",
             rates->rxMbps(), rates->txMbps());
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
 * @brief Retrieve information for a specific network interface.
 * @param ifname Interface name (e.g., "eth0").
 * @return Populated InterfaceInfo, or zeroed struct if not found.
 * @note RT-safe: Single interface query with bounded file reads.
 */
[[nodiscard]] InterfaceInfo getInterfaceInfo(const char* ifname) noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, IF_NAME_SIZE> ifname{};
std::array<char, IF_STRING_SIZE> driver{};

// Instead of std::vector (allocates)
InterfaceInfo interfaces[MAX_INTERFACES]{};
NicIrqInfo nics[MAX_INTERFACES]{};
```

This allows single-interface queries to be RT-safe.

### Snapshot + Delta Separation

For counter-based metrics, we separate:

1. **Snapshot** - Raw counters with timestamp (RT-safe, cheap)
2. **Delta computation** - Rates per second (RT-safe, pure function)
3. **toString()** - Human-readable output (NOT RT-safe, allocates)

This lets RT code collect snapshots in the hot path and defer formatting to cold paths.

### Graceful Degradation

All functions are `noexcept`. Missing files or failed reads result in zeroed/default fields, not exceptions:

```cpp
auto info = getInterfaceInfo("nonexistent");
if (info.mtu == 0) {
  // Interface doesn't exist - handle gracefully
}

auto cfg = getSocketBufferConfig();
if (cfg.rmemMax < 0) {
  // Cannot read /proc/sys/net (container, permissions)
}
```

---

## Module Reference

---

### InterfaceInfo

**Header:** `InterfaceInfo.hpp`
**Purpose:** Query NIC link status, speed, driver, and hardware properties.

#### Key Types

```cpp
inline constexpr std::size_t MAX_INTERFACES = 32;
inline constexpr std::size_t IF_NAME_SIZE = 16;
inline constexpr std::size_t IF_STRING_SIZE = 32;
inline constexpr std::size_t MAC_STRING_SIZE = 18;

struct InterfaceInfo {
  std::array<char, IF_NAME_SIZE> ifname{};      ///< Interface name
  std::array<char, IF_STRING_SIZE> operState{}; ///< "up", "down", "unknown"
  std::array<char, IF_STRING_SIZE> duplex{};    ///< "full", "half", "unknown"
  std::array<char, IF_STRING_SIZE> driver{};    ///< Driver module name
  std::array<char, MAC_STRING_SIZE> macAddress{};///< MAC address

  int speedMbps{0};    ///< Link speed (0 if unknown)
  int mtu{0};          ///< MTU in bytes
  int rxQueues{0};     ///< Number of RX queues
  int txQueues{0};     ///< Number of TX queues
  int numaNode{-1};    ///< NUMA node (-1 if unknown)

  bool isUp() const noexcept;       ///< operState == "up"
  bool isPhysical() const noexcept; ///< Has /sys/class/net/<if>/device
  bool hasLink() const noexcept;    ///< isUp() && speedMbps > 0
  std::string toString() const;     ///< NOT RT-safe
};

struct InterfaceList {
  InterfaceInfo interfaces[MAX_INTERFACES]{};
  std::size_t count{0};

  const InterfaceInfo* find(const char* ifname) const noexcept;
  bool empty() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};
```

#### API

```cpp
/// Check if interface is virtual (RT-safe)
[[nodiscard]] bool isVirtualInterface(const char* ifname) noexcept;

/// Query single interface (RT-safe)
[[nodiscard]] InterfaceInfo getInterfaceInfo(const char* ifname) noexcept;

/// Get all interfaces (NOT RT-safe: directory enumeration)
[[nodiscard]] InterfaceList getAllInterfaces() noexcept;

/// Get physical interfaces only (NOT RT-safe)
[[nodiscard]] InterfaceList getPhysicalInterfaces() noexcept;

/// Format speed for display (NOT RT-safe)
[[nodiscard]] std::string formatSpeed(int speedMbps);
```

#### Usage

```cpp
using namespace seeker::network;

// Query specific interface (RT-safe)
auto eth0 = getInterfaceInfo("eth0");
if (eth0.hasLink()) {
  fmt::print("{}: {} @ {} Mbps\n",
             eth0.ifname.data(),
             eth0.operState.data(),
             eth0.speedMbps);
}

// List all physical NICs
auto physical = getPhysicalInterfaces();
for (std::size_t i = 0; i < physical.count; ++i) {
  const auto& iface = physical.interfaces[i];
  fmt::print("{}: {} queues, NUMA {}\n",
             iface.ifname.data(),
             iface.rxQueues,
             iface.numaNode);
}
```

#### Data Sources

- `/sys/class/net/<if>/operstate` - Link state
- `/sys/class/net/<if>/speed` - Link speed (Mbps)
- `/sys/class/net/<if>/duplex` - Duplex mode
- `/sys/class/net/<if>/mtu` - MTU size
- `/sys/class/net/<if>/address` - MAC address
- `/sys/class/net/<if>/device/driver` - Driver (symlink)
- `/sys/class/net/<if>/device/numa_node` - NUMA affinity
- `/sys/class/net/<if>/queues/` - RX/TX queue count

---

### InterfaceStats

**Header:** `InterfaceStats.hpp`
**Purpose:** Traffic counters with snapshot + delta pattern for rate computation.

#### Key Types

```cpp
struct InterfaceCounters {
  std::array<char, IF_NAME_SIZE> ifname{};

  std::uint64_t rxBytes{0};
  std::uint64_t txBytes{0};
  std::uint64_t rxPackets{0};
  std::uint64_t txPackets{0};
  std::uint64_t rxErrors{0};
  std::uint64_t txErrors{0};
  std::uint64_t rxDropped{0};
  std::uint64_t txDropped{0};
  std::uint64_t collisions{0};
  std::uint64_t rxMulticast{0};

  std::uint64_t totalErrors() const noexcept;
  std::uint64_t totalDrops() const noexcept;
  bool hasIssues() const noexcept;
};

struct InterfaceStatsSnapshot {
  InterfaceCounters interfaces[MAX_INTERFACES]{};
  std::size_t count{0};
  std::uint64_t timestampNs{0};  ///< Monotonic timestamp

  const InterfaceCounters* find(const char* ifname) const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};

struct InterfaceRates {
  std::array<char, IF_NAME_SIZE> ifname{};
  double durationSec{0.0};          ///< Sample duration

  double rxBytesPerSec{0.0};
  double txBytesPerSec{0.0};
  double rxPacketsPerSec{0.0};
  double txPacketsPerSec{0.0};
  double rxErrorsPerSec{0.0};
  double txErrorsPerSec{0.0};
  double rxDroppedPerSec{0.0};
  double txDroppedPerSec{0.0};
  double collisionsPerSec{0.0};     ///< Collision rate

  double rxMbps() const noexcept;   ///< RX megabits/sec
  double txMbps() const noexcept;   ///< TX megabits/sec
  double totalMbps() const noexcept;
  bool hasErrors() const noexcept;
  bool hasDrops() const noexcept;
  std::string toString() const;     ///< NOT RT-safe
};

struct InterfaceStatsDelta {
  InterfaceRates interfaces[MAX_INTERFACES]{};
  std::size_t count{0};
  double durationSec{0.0};

  const InterfaceRates* find(const char* ifname) const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};
```

#### API

```cpp
/// Get raw counters for single interface (RT-safe)
[[nodiscard]] InterfaceCounters getInterfaceCounters(const char* ifname) noexcept;

/// Snapshot all interfaces (NOT RT-safe: enumeration)
[[nodiscard]] InterfaceStatsSnapshot getInterfaceStatsSnapshot() noexcept;

/// Snapshot single interface (RT-safe)
[[nodiscard]] InterfaceStatsSnapshot getInterfaceStatsSnapshot(const char* ifname) noexcept;

/// Compute rates from two snapshots (RT-safe: pure computation)
[[nodiscard]] InterfaceStatsDelta computeStatsDelta(
    const InterfaceStatsSnapshot& before,
    const InterfaceStatsSnapshot& after) noexcept;

/// Format throughput for display (NOT RT-safe)
[[nodiscard]] std::string formatThroughput(double bytesPerSec);
```

#### Usage

```cpp
using namespace seeker::network;

// Monitor single interface (RT-safe pattern)
auto before = getInterfaceStatsSnapshot("eth0");
std::this_thread::sleep_for(std::chrono::seconds(1));
auto after = getInterfaceStatsSnapshot("eth0");
auto delta = computeStatsDelta(before, after);

if (const auto* rates = delta.find("eth0")) {
  if (rates->hasDrops()) {
    fmt::print("WARNING: Drops on eth0: {:.0f}/s\n",
               rates->rxDroppedPerSec + rates->txDroppedPerSec);
  }
}
```

#### Data Sources

- `/sys/class/net/<if>/statistics/rx_bytes`
- `/sys/class/net/<if>/statistics/tx_bytes`
- `/sys/class/net/<if>/statistics/rx_packets`
- `/sys/class/net/<if>/statistics/tx_packets`
- `/sys/class/net/<if>/statistics/rx_errors`
- `/sys/class/net/<if>/statistics/tx_errors`
- `/sys/class/net/<if>/statistics/rx_dropped`
- `/sys/class/net/<if>/statistics/tx_dropped`
- `/sys/class/net/<if>/statistics/collisions`
- `/sys/class/net/<if>/statistics/multicast`

---

### SocketBufferConfig

**Header:** `SocketBufferConfig.hpp`
**Purpose:** System-wide socket buffer limits, TCP tunables, and busy polling configuration.

#### Key Types

```cpp
inline constexpr std::size_t CC_STRING_SIZE = 32;

struct SocketBufferConfig {
  // Core buffer limits (bytes, -1 if unavailable)
  std::int64_t rmemDefault{-1};      ///< Default receive buffer
  std::int64_t rmemMax{-1};          ///< Maximum receive buffer
  std::int64_t wmemDefault{-1};      ///< Default send buffer
  std::int64_t wmemMax{-1};          ///< Maximum send buffer
  std::int64_t optmemMax{-1};        ///< Ancillary data limit
  std::int64_t netdevMaxBacklog{-1}; ///< Input queue length for incoming packets
  std::int64_t netdevBudget{-1};     ///< NAPI polling budget per softirq

  // TCP buffer auto-tuning (min/default/max)
  std::int64_t tcpRmemMin{-1};
  std::int64_t tcpRmemDefault{-1};
  std::int64_t tcpRmemMax{-1};
  std::int64_t tcpWmemMin{-1};
  std::int64_t tcpWmemDefault{-1};
  std::int64_t tcpWmemMax{-1};

  // TCP options
  std::array<char, CC_STRING_SIZE> tcpCongestionControl{}; ///< e.g., "cubic"
  int tcpTimestamps{-1};
  int tcpSack{-1};
  int tcpWindowScaling{-1};
  int tcpLowLatency{-1};
  int tcpNoMetricsSave{-1};

  // Busy polling (us, 0=disabled)
  int busyRead{-1};
  int busyPoll{-1};

  // UDP parameters
  std::int64_t udpRmemMin{-1};  ///< UDP receive buffer minimum
  std::int64_t udpWmemMin{-1};  ///< UDP send buffer minimum

  // Assessment methods
  bool isBusyPollingEnabled() const noexcept;
  bool isLowLatencyConfig() const noexcept;
  bool isHighThroughputConfig() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};
```

#### API

```cpp
/// Query socket buffer configuration (RT-safe)
[[nodiscard]] SocketBufferConfig getSocketBufferConfig() noexcept;

/// Format buffer size for display (NOT RT-safe)
[[nodiscard]] std::string formatBufferSize(std::int64_t bytes);
```

#### Usage

```cpp
using namespace seeker::network;

auto cfg = getSocketBufferConfig();

// Check busy polling
if (!cfg.isBusyPollingEnabled()) {
  fmt::print("Busy polling disabled - enable for lower latency:\n");
  fmt::print("  echo 50 > /proc/sys/net/core/busy_read\n");
  fmt::print("  echo 50 > /proc/sys/net/core/busy_poll\n");
}

// Check buffer sizes
if (cfg.rmemMax < 16 * 1024 * 1024) {
  fmt::print("Buffer limit low: {} (recommend 16+ MiB)\n",
             formatBufferSize(cfg.rmemMax));
}
```

#### Data Sources

- `/proc/sys/net/core/rmem_default`, `rmem_max`
- `/proc/sys/net/core/wmem_default`, `wmem_max`
- `/proc/sys/net/core/optmem_max`
- `/proc/sys/net/core/busy_read`, `busy_poll`
- `/proc/sys/net/core/netdev_max_backlog`, `netdev_budget`
- `/proc/sys/net/ipv4/tcp_rmem`, `tcp_wmem` (space-separated triples)
- `/proc/sys/net/ipv4/tcp_congestion_control`
- `/proc/sys/net/ipv4/tcp_timestamps`, `tcp_sack`, etc.
- `/proc/sys/net/ipv4/udp_rmem_min`, `udp_wmem_min`

---

### NetworkIsolation

**Header:** `NetworkIsolation.hpp`
**Purpose:** Analyze NIC IRQ CPU affinity for RT core conflict detection.

#### Key Types

```cpp
inline constexpr std::size_t MAX_NIC_IRQS = 64;
inline constexpr std::size_t MAX_CPUS = 256;
inline constexpr std::size_t IRQ_NAME_SIZE = 64;

struct NicIrqInfo {
  std::array<char, IF_NAME_SIZE> ifname{};

  int irqNumbers[MAX_NIC_IRQS]{};
  std::size_t irqCount{0};

  std::uint64_t affinity[MAX_NIC_IRQS]{};  ///< CPU bitmask per IRQ

  int numaNode{-1};

  bool hasIrqOnCpu(int cpu) const noexcept;
  bool hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept;
  std::uint64_t getCombinedAffinity() const noexcept;
  std::string getAffinityCpuList() const;  ///< NOT RT-safe
  std::string toString() const;            ///< NOT RT-safe
};

struct NetworkIsolation {
  NicIrqInfo nics[MAX_INTERFACES]{};
  std::size_t nicCount{0};

  const NicIrqInfo* find(const char* ifname) const noexcept;
  bool hasIrqOnCpu(int cpu) const noexcept;
  bool hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept;
  std::string getConflictingNics(std::uint64_t cpuMask) const;  ///< NOT RT-safe
  std::size_t getTotalIrqCount() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};

struct IrqConflictResult {
  bool hasConflict{false};
  std::size_t conflictCount{0};
  std::array<char, IF_NAME_SIZE * 4> conflictingNics{};
  int conflictingCpus[MAX_CPUS]{};
  std::size_t conflictingCpuCount{0};

  std::string toString() const;  ///< NOT RT-safe
};
```

#### API

```cpp
/// Gather NIC IRQ information (NOT RT-safe: parses /proc/interrupts)
[[nodiscard]] NetworkIsolation getNetworkIsolation() noexcept;

/// Check for IRQ conflicts with RT CPU mask (RT-safe: pure computation)
[[nodiscard]] IrqConflictResult checkIrqConflict(
    const NetworkIsolation& ni,
    std::uint64_t rtCpuMask) noexcept;

/// Parse CPU list string to bitmask (RT-safe)
[[nodiscard]] std::uint64_t parseCpuListToMask(const char* cpuList) noexcept;

/// Format CPU bitmask as list string (NOT RT-safe)
[[nodiscard]] std::string formatCpuMask(std::uint64_t mask);
```

#### Usage

```cpp
using namespace seeker::network;

auto netIso = getNetworkIsolation();

// Check for conflicts with RT cores 2-3
std::uint64_t rtMask = parseCpuListToMask("2-3");
auto conflict = checkIrqConflict(netIso, rtMask);

if (conflict.hasConflict) {
  fmt::print("WARNING: {} NIC IRQs on RT cores!\n", conflict.conflictCount);
  fmt::print("Conflicting: {}\n", conflict.conflictingNics.data());
  fmt::print("Move IRQs with: echo <mask> > /proc/irq/<n>/smp_affinity\n");
}
```

#### Data Sources

- `/proc/interrupts` - IRQ numbers and device names
- `/proc/irq/<n>/smp_affinity` - Per-IRQ CPU affinity mask
- `/sys/class/net/<if>/device/numa_node` - NIC NUMA affinity
- `/sys/class/net/<if>/device/msi_irqs/` - MSI IRQ enumeration

---

### EthtoolInfo

**Header:** `EthtoolInfo.hpp`
**Purpose:** Query NIC driver features, ring buffers, interrupt coalescing, and offload settings.

This module provides low-level NIC tuning information critical for RT network optimization:

- **Ring buffers** - Larger rings add latency headroom but increase worst-case latency
- **Interrupt coalescing** - Low values essential for latency; adaptive mode bad for RT
- **Offload features** - GRO/LRO batching can add latency jitter
- **Pause frames** - Can cause unexpected transmission stalls

#### Key Types

```cpp
/// Maximum feature name length
inline constexpr std::size_t FEATURE_NAME_SIZE = 48;

/// Maximum features per NIC
inline constexpr std::size_t MAX_FEATURES = 64;

/// Coalescing threshold for low-latency classification (microseconds)
inline constexpr std::uint32_t LOW_LATENCY_USECS_THRESHOLD = 10;

/// Coalescing threshold for low-latency classification (frames)
inline constexpr std::uint32_t LOW_LATENCY_FRAMES_THRESHOLD = 4;

/// Ring buffer threshold for RT warning
inline constexpr std::uint32_t RT_RING_SIZE_WARN_THRESHOLD = 4096;

struct RingBufferConfig {
  std::uint32_t rxPending{0};      ///< Current RX ring size
  std::uint32_t rxMax{0};          ///< Maximum RX ring size
  std::uint32_t txPending{0};      ///< Current TX ring size
  std::uint32_t txMax{0};          ///< Maximum TX ring size
  std::uint32_t rxMiniPending{0};  ///< Mini RX ring size (if supported)
  std::uint32_t rxMiniMax{0};      ///< Maximum mini RX ring size
  std::uint32_t rxJumboPending{0}; ///< Jumbo RX ring size (if supported)
  std::uint32_t rxJumboMax{0};     ///< Maximum jumbo RX ring size

  bool isValid() const noexcept;      ///< Query succeeded
  bool isRxAtMax() const noexcept;    ///< RX at maximum
  bool isTxAtMax() const noexcept;    ///< TX at maximum
  bool isRtFriendly() const noexcept; ///< Rings not excessively large
  std::string toString() const;       ///< NOT RT-safe
};

struct CoalesceConfig {
  std::uint32_t rxUsecs{0};          ///< RX interrupt delay (microseconds)
  std::uint32_t rxMaxFrames{0};      ///< RX frames before interrupt
  std::uint32_t txUsecs{0};          ///< TX interrupt delay
  std::uint32_t txMaxFrames{0};      ///< TX frames before interrupt

  std::uint32_t rxUsecsIrq{0};      ///< RX usecs while IRQ pending
  std::uint32_t rxMaxFramesIrq{0};  ///< RX frames while IRQ pending
  std::uint32_t txUsecsIrq{0};      ///< TX usecs while IRQ pending
  std::uint32_t txMaxFramesIrq{0};  ///< TX frames while IRQ pending

  std::uint32_t statsBlockUsecs{0}; ///< Stats block coalescing

  bool useAdaptiveRx{false};         ///< Adaptive RX coalescing (bad for RT)
  bool useAdaptiveTx{false};         ///< Adaptive TX coalescing (bad for RT)
  std::uint32_t pktRateLow{0};      ///< Low packet rate threshold
  std::uint32_t pktRateHigh{0};     ///< High packet rate threshold
  std::uint32_t rxUsecsLow{0};      ///< RX usecs at low rate
  std::uint32_t rxUsecsHigh{0};     ///< RX usecs at high rate
  std::uint32_t txUsecsLow{0};      ///< TX usecs at low rate
  std::uint32_t txUsecsHigh{0};     ///< TX usecs at high rate

  bool isValid() const noexcept;       ///< Query succeeded
  bool isLowLatency() const noexcept;  ///< Low delay values
  bool hasAdaptive() const noexcept;   ///< Any adaptive enabled
  bool isRtFriendly() const noexcept;  ///< Good for RT
  std::string toString() const;        ///< NOT RT-safe
};

struct PauseConfig {
  bool autoneg{false};  ///< Pause auto-negotiated
  bool rxPause{false};  ///< Honor incoming pause frames
  bool txPause{false};  ///< Send pause frames

  bool isEnabled() const noexcept; ///< Any pause enabled
  std::string toString() const;    ///< NOT RT-safe
};

struct NicFeature {
  std::array<char, FEATURE_NAME_SIZE> name{};
  bool available{false};
  bool enabled{false};
  bool requested{false};  ///< User requested state
  bool fixed{false};      ///< Cannot be changed
};

struct NicFeatures {
  NicFeature features[MAX_FEATURES]{};
  std::size_t count{0};

  const NicFeature* find(const char* name) const noexcept;
  bool isEnabled(const char* name) const noexcept;
  std::size_t countEnabled() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};

struct EthtoolInfo {
  std::array<char, IF_NAME_SIZE> ifname{};
  RingBufferConfig rings{};
  CoalesceConfig coalesce{};
  PauseConfig pause{};
  NicFeatures features{};
  bool supportsEthtool{false};

  // Feature helpers
  bool hasTso() const noexcept;           ///< TCP segmentation offload
  bool hasGro() const noexcept;           ///< Generic receive offload
  bool hasGso() const noexcept;           ///< Generic segmentation offload
  bool hasLro() const noexcept;           ///< Large receive offload (adds latency)
  bool hasRxChecksum() const noexcept;    ///< RX checksum offload
  bool hasTxChecksum() const noexcept;    ///< TX checksum offload
  bool hasScatterGather() const noexcept; ///< Scatter-gather support

  // RT assessment
  bool isRtFriendly() const noexcept;   ///< Overall RT-friendly config
  int rtScore() const noexcept;         ///< 0-100 RT readiness score

  std::string toString() const;         ///< NOT RT-safe
};

struct EthtoolInfoList {
  EthtoolInfo nics[MAX_INTERFACES]{};
  std::size_t count{0};

  const EthtoolInfo* find(const char* ifname) const noexcept;
  bool empty() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};
```

#### API

```cpp
/// Get ethtool info for specific interface (RT-safe: bounded ioctls)
[[nodiscard]] EthtoolInfo getEthtoolInfo(const char* ifname) noexcept;

/// Get ethtool info for all physical interfaces (NOT RT-safe: enumeration)
[[nodiscard]] EthtoolInfoList getAllEthtoolInfo() noexcept;

/// Get ring buffer config only (RT-safe)
[[nodiscard]] RingBufferConfig getRingBufferConfig(const char* ifname) noexcept;

/// Get coalescing config only (RT-safe)
[[nodiscard]] CoalesceConfig getCoalesceConfig(const char* ifname) noexcept;

/// Get pause frame config only (RT-safe)
[[nodiscard]] PauseConfig getPauseConfig(const char* ifname) noexcept;
```

#### Usage

```cpp
using namespace seeker::network;

// Query single NIC (RT-safe)
auto eth0 = getEthtoolInfo("eth0");

if (eth0.supportsEthtool) {
  fmt::print("Ring buffers: RX {}/{} TX {}/{}\n",
             eth0.rings.rxPending, eth0.rings.rxMax,
             eth0.rings.txPending, eth0.rings.txMax);

  fmt::print("Coalescing: RX {}us/{} frames\n",
             eth0.coalesce.rxUsecs, eth0.coalesce.rxMaxFrames);

  if (eth0.coalesce.hasAdaptive()) {
    fmt::print("WARNING: Adaptive coalescing enabled (bad for RT)\n");
  }

  if (eth0.hasLro()) {
    fmt::print("WARNING: LRO enabled (adds latency variance)\n");
  }

  fmt::print("RT Score: {}/100\n", eth0.rtScore());
}

// Query all NICs (NOT RT-safe)
auto all = getAllEthtoolInfo();
for (std::size_t i = 0; i < all.count; ++i) {
  const auto& nic = all.nics[i];
  fmt::print("{}: RT Score {}/100 ({})\n",
             nic.ifname.data(),
             nic.rtScore(),
             nic.isRtFriendly() ? "good" : "needs tuning");
}
```

#### RT Score Calculation

The `rtScore()` method returns 0-100 based on NIC tuning:

| Factor                      | Impact     |
| --------------------------- | ---------- |
| Adaptive coalescing enabled | -20 points |
| RX coalescing > 100us       | -15 points |
| RX coalescing > 50us        | -10 points |
| RX coalescing > 10us        | -5 points  |
| Ring buffer > 8192          | -15 points |
| Ring buffer > 4096          | -10 points |
| LRO enabled                 | -15 points |
| Pause frames enabled        | -10 points |

#### Data Sources

- `SIOCETHTOOL` ioctl with `ETHTOOL_GRINGPARAM` - Ring buffer sizes
- `SIOCETHTOOL` ioctl with `ETHTOOL_GCOALESCE` - Interrupt coalescing
- `SIOCETHTOOL` ioctl with `ETHTOOL_GPAUSEPARAM` - Pause frame config
- `SIOCETHTOOL` ioctl with `ETHTOOL_GFEATURES` - Offload features

---

### LoopbackBench

**Header:** `LoopbackBench.hpp`
**Purpose:** Measure network stack latency and throughput via localhost.

#### Key Types

```cpp
inline constexpr std::size_t MAX_LATENCY_SAMPLES = 8192;
inline constexpr std::size_t DEFAULT_THROUGHPUT_BUFFER_SIZE = 65536;
inline constexpr std::size_t DEFAULT_LATENCY_MESSAGE_SIZE = 64;

struct LatencyResult {
  double minUs{0.0};
  double maxUs{0.0};
  double meanUs{0.0};
  double medianUs{0.0};
  double p50Us{0.0};
  double p90Us{0.0};
  double p95Us{0.0};
  double p99Us{0.0};
  double p999Us{0.0};
  double stddevUs{0.0};
  std::size_t sampleCount{0};
  bool success{false};

  std::string toString() const;  ///< NOT RT-safe
};

struct ThroughputResult {
  double mibPerSec{0.0};
  double mbitsPerSec{0.0};
  std::uint64_t bytesTransferred{0};
  double durationSec{0.0};
  bool success{false};

  std::string toString() const;  ///< NOT RT-safe
};

struct LoopbackBenchResult {
  LatencyResult tcpLatency;
  LatencyResult udpLatency;
  ThroughputResult tcpThroughput;
  ThroughputResult udpThroughput;

  bool anySuccess() const noexcept;
  bool allSuccess() const noexcept;
  std::string toString() const;  ///< NOT RT-safe
};

struct LoopbackBenchConfig {
  std::chrono::milliseconds totalBudget{1000};
  std::size_t latencyMessageSize{DEFAULT_LATENCY_MESSAGE_SIZE};
  std::size_t throughputBufferSize{DEFAULT_THROUGHPUT_BUFFER_SIZE};
  std::size_t maxLatencySamples{MAX_LATENCY_SAMPLES};
  bool runTcpLatency{true};
  bool runUdpLatency{true};
  bool runTcpThroughput{true};
  bool runUdpThroughput{true};
};
```

#### API

```cpp
/// Run full benchmark suite (NOT RT-safe: spawns threads, socket I/O)
[[nodiscard]] LoopbackBenchResult runLoopbackBench(
    std::chrono::milliseconds budget) noexcept;

/// Run with custom configuration (NOT RT-safe)
[[nodiscard]] LoopbackBenchResult runLoopbackBench(
    const LoopbackBenchConfig& config) noexcept;

/// Individual measurements (NOT RT-safe)
[[nodiscard]] LatencyResult measureTcpLatency(
    std::chrono::milliseconds budget,
    std::size_t messageSize = DEFAULT_LATENCY_MESSAGE_SIZE,
    std::size_t maxSamples = MAX_LATENCY_SAMPLES) noexcept;

[[nodiscard]] LatencyResult measureUdpLatency(
    std::chrono::milliseconds budget,
    std::size_t messageSize = DEFAULT_LATENCY_MESSAGE_SIZE,
    std::size_t maxSamples = MAX_LATENCY_SAMPLES) noexcept;

[[nodiscard]] ThroughputResult measureTcpThroughput(
    std::chrono::milliseconds budget,
    std::size_t bufferSize = DEFAULT_THROUGHPUT_BUFFER_SIZE) noexcept;

[[nodiscard]] ThroughputResult measureUdpThroughput(
    std::chrono::milliseconds budget,
    std::size_t bufferSize = DEFAULT_THROUGHPUT_BUFFER_SIZE) noexcept;
```

#### Usage

```cpp
using namespace seeker::network;

// Quick benchmark (1 second total)
auto result = runLoopbackBench(std::chrono::seconds(1));

fmt::print("TCP Latency: p50={:.1f}us p99={:.1f}us\n",
           result.tcpLatency.p50Us,
           result.tcpLatency.p99Us);

fmt::print("TCP Throughput: {:.0f} Mbps\n",
           result.tcpThroughput.mbitsPerSec);

// Custom benchmark - latency only
LoopbackBenchConfig cfg{};
cfg.totalBudget = std::chrono::milliseconds(500);
cfg.runTcpThroughput = false;
cfg.runUdpThroughput = false;

auto latencyOnly = runLoopbackBench(cfg);
```

---

## Common Patterns

### RT-Safe Single Interface Monitoring

```cpp
using namespace seeker::network;

// This pattern is RT-safe - no allocation, bounded I/O
void rtMonitorCallback() {
  static InterfaceStatsSnapshot prev = getInterfaceStatsSnapshot("eth0");

  auto curr = getInterfaceStatsSnapshot("eth0");
  auto delta = computeStatsDelta(prev, curr);

  if (const auto* rates = delta.find("eth0")) {
    if (rates->hasDrops()) {
      // Handle drops in RT context
    }
  }

  prev = curr;
}
```

### Pre-Flight RT Network Check

```cpp
using namespace seeker::network;

bool validateNetworkForRT(std::uint64_t rtCpuMask) {
  bool ok = true;

  // Check NIC IRQ affinity
  auto netIso = getNetworkIsolation();
  auto conflict = checkIrqConflict(netIso, rtCpuMask);
  if (conflict.hasConflict) {
    fmt::print("FAIL: NIC IRQs on RT cores\n");
    ok = false;
  }

  // Check busy polling
  auto cfg = getSocketBufferConfig();
  if (!cfg.isBusyPollingEnabled()) {
    fmt::print("WARN: Busy polling disabled\n");
  }

  // Check for drops
  auto before = getInterfaceStatsSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto after = getInterfaceStatsSnapshot();
  auto delta = computeStatsDelta(before, after);

  for (std::size_t i = 0; i < delta.count; ++i) {
    if (delta.interfaces[i].hasDrops()) {
      fmt::print("WARN: Drops on {}\n", delta.interfaces[i].ifname.data());
    }
  }

  return ok;
}
```

---

## Real-Time Considerations

### RT-Safe Functions

These can be called from RT threads:

- `isVirtualInterface(ifname)` - Virtual interface check
- `getInterfaceInfo(ifname)` - Single interface query
- `getInterfaceCounters(ifname)` - Single interface counters
- `getInterfaceStatsSnapshot(ifname)` - Single interface snapshot
- `computeStatsDelta()` - Pure computation
- `getSocketBufferConfig()` - Bounded file reads
- `checkIrqConflict()` - Pure computation
- `parseCpuListToMask()` - Pure parsing
- `getEthtoolInfo(ifname)` - Single interface ethtool query
- `getRingBufferConfig(ifname)` - Single ioctl
- `getCoalesceConfig(ifname)` - Single ioctl
- `getPauseConfig(ifname)` - Single ioctl

### NOT RT-Safe Functions

Call these from setup/monitoring threads only:

- `getAllInterfaces()` - Directory enumeration
- `getPhysicalInterfaces()` - Directory enumeration
- `getInterfaceStatsSnapshot()` (no args) - Enumerates all interfaces
- `getNetworkIsolation()` - Parses /proc/interrupts
- `getAllEthtoolInfo()` - Directory enumeration + multiple ioctls
- `runLoopbackBench()` - Spawns threads, socket I/O
- All `toString()` methods - String allocation

### Recommended RT Network Configuration

For best RT performance, verify these with the diagnostics:

1. **IRQ Affinity:** No NIC IRQs on RT cores
2. **Busy Polling:** Enabled (`busy_read` and `busy_poll` > 0)
3. **Socket Buffers:** Sufficient size (16+ MiB for high throughput)
4. **Netdev Backlog:** High enough to prevent drops (10000+)
5. **NUMA Locality:** NIC on same NUMA node as RT cores
6. **Packet Drops:** Zero drops during normal operation
7. **Interrupt Coalescing:** Low values, adaptive disabled
8. **LRO:** Disabled (adds latency variance)
9. **Pause Frames:** Disabled (can cause stalls)

### NIC IRQ Steering

To move NIC IRQs off RT cores:

```bash
# Find NIC IRQs
grep eth0 /proc/interrupts

# Set affinity to non-RT cores (e.g., CPUs 0-1)
echo 3 > /proc/irq/<irq_num>/smp_affinity

# Or use irqbalance with banned CPUs
echo "IRQBALANCE_BANNED_CPUS=0x0c" >> /etc/default/irqbalance
```

---

## CLI Tools

The network domain includes 3 command-line tools: `net-info`, `net-rtcheck`, `net-stat`.

See: `tools/cpp/network/README.md` for detailed tool usage.

---

## Example: RT Network Validation

```cpp
#include "src/network/inc/InterfaceInfo.hpp"
#include "src/network/inc/InterfaceStats.hpp"
#include "src/network/inc/NetworkIsolation.hpp"
#include "src/network/inc/SocketBufferConfig.hpp"
#include "src/network/inc/EthtoolInfo.hpp"

#include <fmt/core.h>
#include <chrono>
#include <thread>

using namespace seeker::network;

int main() {
  fmt::print("=== RT Network Validation ===\n\n");

  // 1. Interface Status
  auto interfaces = getAllInterfaces();
  fmt::print("Interfaces: {} found\n", interfaces.count);
  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const auto& iface = interfaces.interfaces[i];
    fmt::print("  {}: {} {}\n",
               iface.ifname.data(),
               iface.operState.data(),
               iface.hasLink() ? formatSpeed(iface.speedMbps) : "no link");
  }

  // 2. Socket Buffer Configuration
  auto cfg = getSocketBufferConfig();
  fmt::print("\nSocket Buffers:\n");
  fmt::print("  rmem_max: {}\n", formatBufferSize(cfg.rmemMax));
  fmt::print("  wmem_max: {}\n", formatBufferSize(cfg.wmemMax));

  // 3. Busy Polling
  fmt::print("\nBusy Polling: ");
  if (cfg.isBusyPollingEnabled()) {
    fmt::print("ENABLED (read={}us poll={}us)\n",
               cfg.busyRead, cfg.busyPoll);
  } else {
    fmt::print("DISABLED\n");
  }

  // 4. IRQ Affinity Check (RT cores 2-3)
  auto netIso = getNetworkIsolation();
  std::uint64_t rtMask = parseCpuListToMask("2-3");
  auto conflict = checkIrqConflict(netIso, rtMask);

  fmt::print("\nIRQ Affinity (RT CPUs 2-3):\n");
  if (conflict.hasConflict) {
    fmt::print("  WARNING: {} IRQs on RT cores\n", conflict.conflictCount);
    fmt::print("  Conflicting: {}\n", conflict.conflictingNics.data());
  } else {
    fmt::print("  OK: No NIC IRQs on RT cores\n");
  }

  // 5. Ethtool Configuration Check
  fmt::print("\nNIC Tuning:\n");
  auto ethtoolList = getAllEthtoolInfo();
  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const auto& eth = ethtoolList.nics[i];
    fmt::print("  {}: RT Score {}/100", eth.ifname.data(), eth.rtScore());

    if (eth.coalesce.hasAdaptive()) {
      fmt::print(" [WARN: adaptive coalescing]");
    }
    if (eth.hasLro()) {
      fmt::print(" [WARN: LRO enabled]");
    }
    if (eth.pause.isEnabled()) {
      fmt::print(" [WARN: pause frames]");
    }

    fmt::print("\n");
  }

  // 6. Drop Check (100ms sample)
  fmt::print("\nChecking for drops...\n");
  auto before = getInterfaceStatsSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto after = getInterfaceStatsSnapshot();
  auto delta = computeStatsDelta(before, after);

  bool hasDrops = false;
  for (std::size_t i = 0; i < delta.count; ++i) {
    if (delta.interfaces[i].hasDrops()) {
      fmt::print("  WARNING: Drops on {}\n",
                 delta.interfaces[i].ifname.data());
      hasDrops = true;
    }
  }
  if (!hasDrops) {
    fmt::print("  OK: No drops detected\n");
  }

  return 0;
}
```

---

## See Also

- `seeker::cpu` - CPU telemetry (topology, IRQs, isolation)
- `seeker::memory` - Memory topology, NUMA, hugepages, ECC/EDAC
- `seeker::storage` - Block device I/O, scheduler settings
- `seeker::timing` - Clock sources, timer config, time sync
