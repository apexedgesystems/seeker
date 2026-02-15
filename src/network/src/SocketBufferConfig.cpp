/**
 * @file SocketBufferConfig.cpp
 * @brief Implementation of socket buffer configuration queries.
 */

#include "src/network/inc/SocketBufferConfig.hpp"
#include "src/helpers/inc/Strings.hpp"
#include "src/helpers/inc/Files.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace network {

using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileInt64;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr std::size_t READ_BUFFER_SIZE = 256;

/**
 * Parse space-separated triple "min default max" into three int64 values.
 * Returns true if all three parsed successfully.
 */
inline bool parseTriple(const char* buf, std::int64_t& valMin, std::int64_t& valDefault,
                        std::int64_t& valMax) noexcept {
  char* endPtr = nullptr;

  // Parse min
  const long long V1 = std::strtoll(buf, &endPtr, 10);
  if (endPtr == buf) {
    return false;
  }

  // Skip whitespace
  while (*endPtr == ' ' || *endPtr == '\t') {
    ++endPtr;
  }

  // Parse default
  const char* start2 = endPtr;
  const long long V2 = std::strtoll(start2, &endPtr, 10);
  if (endPtr == start2) {
    return false;
  }

  // Skip whitespace
  while (*endPtr == ' ' || *endPtr == '\t') {
    ++endPtr;
  }

  // Parse max
  const char* start3 = endPtr;
  const long long V3 = std::strtoll(start3, &endPtr, 10);
  if (endPtr == start3) {
    return false;
  }

  valMin = static_cast<std::int64_t>(V1);
  valDefault = static_cast<std::int64_t>(V2);
  valMax = static_cast<std::int64_t>(V3);

  return true;
}

} // namespace

/* ----------------------------- SocketBufferConfig Methods ----------------------------- */

bool SocketBufferConfig::isBusyPollingEnabled() const noexcept {
  return (busyRead > 0) || (busyPoll > 0);
}

bool SocketBufferConfig::isLowLatencyConfig() const noexcept {
  // Check for busy polling enabled
  if (!isBusyPollingEnabled()) {
    return false;
  }

  // Check for reasonable buffer sizes (at least 256 KB)
  constexpr std::int64_t MIN_LOW_LATENCY_BUFFER = 256 * 1024;
  if (rmemMax < MIN_LOW_LATENCY_BUFFER || wmemMax < MIN_LOW_LATENCY_BUFFER) {
    return false;
  }

  return true;
}

bool SocketBufferConfig::isHighThroughputConfig() const noexcept {
  // Check for large buffers (at least 16 MB)
  constexpr std::int64_t MIN_HIGH_THROUGHPUT_BUFFER = 16 * 1024 * 1024;

  return (rmemMax >= MIN_HIGH_THROUGHPUT_BUFFER) && (wmemMax >= MIN_HIGH_THROUGHPUT_BUFFER) &&
         (tcpRmemMax >= MIN_HIGH_THROUGHPUT_BUFFER) && (tcpWmemMax >= MIN_HIGH_THROUGHPUT_BUFFER);
}

std::string SocketBufferConfig::toString() const {
  std::string out;

  out += "Socket Buffer Configuration:\n";
  out += fmt::format("  Core buffers:\n");
  out += fmt::format("    rmem: default={} max={}\n", formatBufferSize(rmemDefault),
                     formatBufferSize(rmemMax));
  out += fmt::format("    wmem: default={} max={}\n", formatBufferSize(wmemDefault),
                     formatBufferSize(wmemMax));
  out += fmt::format("    optmem_max: {}\n", formatBufferSize(optmemMax));

  if (netdevMaxBacklog >= 0) {
    out += fmt::format("    netdev_max_backlog: {}\n", netdevMaxBacklog);
  }
  if (netdevBudget >= 0) {
    out += fmt::format("    netdev_budget: {}\n", netdevBudget);
  }

  out += fmt::format("  TCP buffers:\n");
  out += fmt::format("    tcp_rmem: min={} default={} max={}\n", formatBufferSize(tcpRmemMin),
                     formatBufferSize(tcpRmemDefault), formatBufferSize(tcpRmemMax));
  out += fmt::format("    tcp_wmem: min={} default={} max={}\n", formatBufferSize(tcpWmemMin),
                     formatBufferSize(tcpWmemDefault), formatBufferSize(tcpWmemMax));

  if (tcpCongestionControl[0] != '\0') {
    out += fmt::format("  TCP congestion: {}\n", tcpCongestionControl.data());
  }

  out += fmt::format("  TCP options: timestamps={} sack={} window_scaling={}\n", tcpTimestamps,
                     tcpSack, tcpWindowScaling);

  out += fmt::format("  Busy polling: read={}us poll={}us ({})\n", busyRead, busyPoll,
                     isBusyPollingEnabled() ? "enabled" : "disabled");

  if (udpRmemMin >= 0 || udpWmemMin >= 0) {
    out += fmt::format("  UDP: rmem_min={} wmem_min={}\n", formatBufferSize(udpRmemMin),
                       formatBufferSize(udpWmemMin));
  }

  // Summary assessment
  out += "  Assessment: ";
  if (isLowLatencyConfig()) {
    out += "low-latency ready";
  } else if (isHighThroughputConfig()) {
    out += "high-throughput ready";
  } else {
    out += "default configuration";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

SocketBufferConfig getSocketBufferConfig() noexcept {
  SocketBufferConfig cfg{};
  char buf[READ_BUFFER_SIZE];

  // Core socket buffers
  cfg.rmemDefault = readFileInt64("/proc/sys/net/core/rmem_default");
  cfg.rmemMax = readFileInt64("/proc/sys/net/core/rmem_max");
  cfg.wmemDefault = readFileInt64("/proc/sys/net/core/wmem_default");
  cfg.wmemMax = readFileInt64("/proc/sys/net/core/wmem_max");
  cfg.optmemMax = readFileInt64("/proc/sys/net/core/optmem_max");
  cfg.netdevMaxBacklog = readFileInt64("/proc/sys/net/core/netdev_max_backlog");
  cfg.netdevBudget = readFileInt64("/proc/sys/net/core/netdev_budget");

  // Busy polling
  cfg.busyRead = readFileInt("/proc/sys/net/core/busy_read");
  cfg.busyPoll = readFileInt("/proc/sys/net/core/busy_poll");

  // TCP receive buffer (min default max)
  if (readFileToBuffer("/proc/sys/net/ipv4/tcp_rmem", buf, sizeof(buf)) > 0) {
    parseTriple(buf, cfg.tcpRmemMin, cfg.tcpRmemDefault, cfg.tcpRmemMax);
  }

  // TCP send buffer (min default max)
  if (readFileToBuffer("/proc/sys/net/ipv4/tcp_wmem", buf, sizeof(buf)) > 0) {
    parseTriple(buf, cfg.tcpWmemMin, cfg.tcpWmemDefault, cfg.tcpWmemMax);
  }

  // TCP congestion control
  if (readFileToBuffer("/proc/sys/net/ipv4/tcp_congestion_control", buf, sizeof(buf)) > 0) {
    copyToFixedArray(cfg.tcpCongestionControl, buf);
  }

  // TCP options
  cfg.tcpTimestamps = readFileInt("/proc/sys/net/ipv4/tcp_timestamps");
  cfg.tcpSack = readFileInt("/proc/sys/net/ipv4/tcp_sack");
  cfg.tcpWindowScaling = readFileInt("/proc/sys/net/ipv4/tcp_window_scaling");
  cfg.tcpLowLatency = readFileInt("/proc/sys/net/ipv4/tcp_low_latency");
  cfg.tcpNoMetricsSave = readFileInt("/proc/sys/net/ipv4/tcp_no_metrics_save");

  // UDP buffers
  cfg.udpRmemMin = readFileInt64("/proc/sys/net/ipv4/udp_rmem_min");
  cfg.udpWmemMin = readFileInt64("/proc/sys/net/ipv4/udp_wmem_min");

  return cfg;
}

std::string formatBufferSize(std::int64_t bytes) {
  if (bytes < 0) {
    return "unknown";
  }
  if (bytes == 0) {
    return "0";
  }

  constexpr std::int64_t KIB = 1024LL;
  constexpr std::int64_t MIB = KIB * 1024LL;
  constexpr std::int64_t GIB = MIB * 1024LL;

  // Prefer exact representation
  if (bytes >= GIB && bytes % GIB == 0) {
    return fmt::format("{} GiB", bytes / GIB);
  }
  if (bytes >= MIB && bytes % MIB == 0) {
    return fmt::format("{} MiB", bytes / MIB);
  }
  if (bytes >= KIB && bytes % KIB == 0) {
    return fmt::format("{} KiB", bytes / KIB);
  }

  // Fall back to decimal
  if (bytes >= MIB) {
    return fmt::format("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MIB));
  }
  if (bytes >= KIB) {
    return fmt::format("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KIB));
  }

  return fmt::format("{} B", bytes);
}

} // namespace network

} // namespace seeker