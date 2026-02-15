/**
 * @file InterfaceStats.cpp
 * @brief Implementation of network interface statistics.
 */

#include "src/network/inc/InterfaceStats.hpp"
#include "src/helpers/inc/Strings.hpp"

#include "src/helpers/inc/Cpu.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace network {

namespace {

using seeker::helpers::cpu::getMonotonicNs;
using seeker::helpers::strings::copyToFixedArray;

/* ----------------------------- Constants ----------------------------- */

constexpr const char* NET_SYS_PATH = "/sys/class/net";
constexpr std::size_t PATH_BUFFER_SIZE = 256;
constexpr std::size_t READ_BUFFER_SIZE = 64;

/* ----------------------------- File Helpers ----------------------------- */

/**
 * Read uint64 from sysfs counter file.
 * Returns 0 on error.
 */
inline std::uint64_t readCounter(const char* path) noexcept {
  const int FD = ::open(path, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return 0;
  }

  char buf[READ_BUFFER_SIZE];
  const ssize_t N = ::read(FD, buf, sizeof(buf) - 1);
  ::close(FD);

  if (N <= 0) {
    return 0;
  }

  buf[N] = '\0';
  char* endPtr = nullptr;
  const unsigned long long VAL = std::strtoull(buf, &endPtr, 10);

  return (endPtr != buf) ? static_cast<std::uint64_t>(VAL) : 0;
}

/**
 * Compute rate, handling counter wrap.
 * Returns 0 if counter appears to have wrapped.
 */
inline double computeRate(std::uint64_t before, std::uint64_t after, double durationSec) noexcept {
  if (durationSec <= 0.0 || after < before) {
    return 0.0;
  }
  return static_cast<double>(after - before) / durationSec;
}

} // namespace

/* ----------------------------- InterfaceCounters Methods ----------------------------- */

std::uint64_t InterfaceCounters::totalErrors() const noexcept { return rxErrors + txErrors; }

std::uint64_t InterfaceCounters::totalDrops() const noexcept { return rxDropped + txDropped; }

bool InterfaceCounters::hasIssues() const noexcept {
  return totalErrors() > 0 || totalDrops() > 0 || collisions > 0;
}

/* ----------------------------- InterfaceStatsSnapshot Methods ----------------------------- */

const InterfaceCounters* InterfaceStatsSnapshot::find(const char* ifname) const noexcept {
  if (ifname == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(interfaces[i].ifname.data(), ifname) == 0) {
      return &interfaces[i];
    }
  }

  return nullptr;
}

std::string InterfaceStatsSnapshot::toString() const {
  std::string out;
  out += fmt::format("Timestamp: {} ns\n", timestampNs);
  out += fmt::format("Interfaces: {}\n", count);

  for (std::size_t i = 0; i < count; ++i) {
    const InterfaceCounters& C = interfaces[i];
    out += fmt::format("  {}: rx={} tx={} bytes, rx={} tx={} pkts", C.ifname.data(), C.rxBytes,
                       C.txBytes, C.rxPackets, C.txPackets);
    if (C.hasIssues()) {
      out += fmt::format(" [errors={} drops={} coll={}]", C.totalErrors(), C.totalDrops(),
                         C.collisions);
    }
    out += '\n';
  }

  return out;
}

/* ----------------------------- InterfaceRates Methods ----------------------------- */

double InterfaceRates::rxMbps() const noexcept { return (rxBytesPerSec * 8.0) / 1'000'000.0; }

double InterfaceRates::txMbps() const noexcept { return (txBytesPerSec * 8.0) / 1'000'000.0; }

double InterfaceRates::totalMbps() const noexcept { return rxMbps() + txMbps(); }

bool InterfaceRates::hasErrors() const noexcept {
  return rxErrorsPerSec > 0.0 || txErrorsPerSec > 0.0;
}

bool InterfaceRates::hasDrops() const noexcept {
  return rxDroppedPerSec > 0.0 || txDroppedPerSec > 0.0;
}

std::string InterfaceRates::toString() const {
  std::string out;
  out += fmt::format("{}: rx={:.2f} Mbps tx={:.2f} Mbps", ifname.data(), rxMbps(), txMbps());
  out += fmt::format(" ({:.0f}/{:.0f} pps)", rxPacketsPerSec, txPacketsPerSec);

  if (hasErrors() || hasDrops()) {
    out += fmt::format(" [err={:.0f}/s drop={:.0f}/s]", rxErrorsPerSec + txErrorsPerSec,
                       rxDroppedPerSec + txDroppedPerSec);
  }

  return out;
}

/* ----------------------------- InterfaceStatsDelta Methods ----------------------------- */

const InterfaceRates* InterfaceStatsDelta::find(const char* ifname) const noexcept {
  if (ifname == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(interfaces[i].ifname.data(), ifname) == 0) {
      return &interfaces[i];
    }
  }

  return nullptr;
}

std::string InterfaceStatsDelta::toString() const {
  std::string out;
  out += fmt::format("Interval: {:.3f} sec\n", durationSec);

  for (std::size_t i = 0; i < count; ++i) {
    out += "  ";
    out += interfaces[i].toString();
    out += '\n';
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

InterfaceCounters getInterfaceCounters(const char* ifname) noexcept {
  InterfaceCounters counters{};

  if (ifname == nullptr || ifname[0] == '\0') {
    return counters;
  }

  copyToFixedArray(counters.ifname, ifname);

  char pathBuf[PATH_BUFFER_SIZE];
  const char* BASE = NET_SYS_PATH;

  // Read all counter files
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/rx_bytes", BASE, ifname);
  counters.rxBytes = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/tx_bytes", BASE, ifname);
  counters.txBytes = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/rx_packets", BASE, ifname);
  counters.rxPackets = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/tx_packets", BASE, ifname);
  counters.txPackets = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/rx_errors", BASE, ifname);
  counters.rxErrors = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/tx_errors", BASE, ifname);
  counters.txErrors = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/rx_dropped", BASE, ifname);
  counters.rxDropped = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/tx_dropped", BASE, ifname);
  counters.txDropped = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/collisions", BASE, ifname);
  counters.collisions = readCounter(pathBuf);

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics/multicast", BASE, ifname);
  counters.rxMulticast = readCounter(pathBuf);

  return counters;
}

InterfaceStatsSnapshot getInterfaceStatsSnapshot() noexcept {
  InterfaceStatsSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  DIR* dir = ::opendir(NET_SYS_PATH);
  if (dir == nullptr) {
    return snap;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && snap.count < MAX_INTERFACES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    snap.interfaces[snap.count] = getInterfaceCounters(entry->d_name);

    // Only count if we got valid data
    if (snap.interfaces[snap.count].ifname[0] != '\0') {
      ++snap.count;
    }
  }

  ::closedir(dir);
  return snap;
}

InterfaceStatsSnapshot getInterfaceStatsSnapshot(const char* ifname) noexcept {
  InterfaceStatsSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  if (ifname != nullptr && ifname[0] != '\0') {
    snap.interfaces[0] = getInterfaceCounters(ifname);
    // Only count if we got valid data (check if any counters are non-zero
    // or if the interface stats directory existed - indicated by having packets)
    // For a truly non-existent interface, all counters will be 0
    // We verify by checking if the stats path exists
    char pathBuf[PATH_BUFFER_SIZE];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/statistics", NET_SYS_PATH, ifname);
    struct stat st;
    if (::stat(pathBuf, &st) == 0) {
      snap.count = 1;
    }
  }

  return snap;
}

InterfaceStatsDelta computeStatsDelta(const InterfaceStatsSnapshot& before,
                                      const InterfaceStatsSnapshot& after) noexcept {
  InterfaceStatsDelta delta{};

  // Compute duration
  if (after.timestampNs <= before.timestampNs) {
    return delta;
  }

  delta.durationSec = static_cast<double>(after.timestampNs - before.timestampNs) / 1'000'000'000.0;

  // Match interfaces between snapshots
  for (std::size_t i = 0; i < after.count && delta.count < MAX_INTERFACES; ++i) {
    const InterfaceCounters& AFTER_C = after.interfaces[i];
    const InterfaceCounters* beforeC = before.find(AFTER_C.ifname.data());

    if (beforeC == nullptr) {
      continue; // Interface not in before snapshot
    }

    InterfaceRates& rates = delta.interfaces[delta.count];
    copyToFixedArray(rates.ifname, AFTER_C.ifname.data());
    rates.durationSec = delta.durationSec;

    // Compute all rates
    rates.rxBytesPerSec = computeRate(beforeC->rxBytes, AFTER_C.rxBytes, delta.durationSec);
    rates.txBytesPerSec = computeRate(beforeC->txBytes, AFTER_C.txBytes, delta.durationSec);
    rates.rxPacketsPerSec = computeRate(beforeC->rxPackets, AFTER_C.rxPackets, delta.durationSec);
    rates.txPacketsPerSec = computeRate(beforeC->txPackets, AFTER_C.txPackets, delta.durationSec);
    rates.rxErrorsPerSec = computeRate(beforeC->rxErrors, AFTER_C.rxErrors, delta.durationSec);
    rates.txErrorsPerSec = computeRate(beforeC->txErrors, AFTER_C.txErrors, delta.durationSec);
    rates.rxDroppedPerSec = computeRate(beforeC->rxDropped, AFTER_C.rxDropped, delta.durationSec);
    rates.txDroppedPerSec = computeRate(beforeC->txDropped, AFTER_C.txDropped, delta.durationSec);
    rates.collisionsPerSec =
        computeRate(beforeC->collisions, AFTER_C.collisions, delta.durationSec);

    ++delta.count;
  }

  return delta;
}

std::string formatThroughput(double bytesPerSec) {
  if (bytesPerSec <= 0.0) {
    return "0 bps";
  }

  const double BITS_PER_SEC = bytesPerSec * 8.0;

  constexpr double KBPS = 1'000.0;
  constexpr double MBPS = 1'000'000.0;
  constexpr double GBPS = 1'000'000'000.0;

  if (BITS_PER_SEC >= GBPS) {
    return fmt::format("{:.2f} Gbps", BITS_PER_SEC / GBPS);
  }
  if (BITS_PER_SEC >= MBPS) {
    return fmt::format("{:.2f} Mbps", BITS_PER_SEC / MBPS);
  }
  if (BITS_PER_SEC >= KBPS) {
    return fmt::format("{:.2f} Kbps", BITS_PER_SEC / KBPS);
  }

  return fmt::format("{:.0f} bps", BITS_PER_SEC);
}

} // namespace network

} // namespace seeker