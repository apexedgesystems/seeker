/**
 * @file CpuUtilization.cpp
 * @brief Per-core CPU utilization collection from /proc/stat.
 * @note Parses cpu and cpuN lines for time breakdown.
 */

#include "src/cpu/inc/CpuUtilization.hpp"

#include "src/helpers/inc/Cpu.hpp"

#include <algorithm> // std::min
#include <array>     // std::array
#include <cstdio>    // fopen, fgets, fclose
#include <cstdlib>   // strtoull
#include <cstring>   // strncmp

#include <fmt/core.h>

namespace seeker {

namespace cpu {

namespace {

/* ----------------------------- File Helpers ----------------------------- */

using seeker::helpers::cpu::getMonotonicNs;

/// Parse a cpu line from /proc/stat into counters.
/// Format: "cpu[N] user nice system idle iowait irq softirq steal guest guest_nice"
inline bool parseCpuLine(const char* line, CpuTimeCounters& out, int& cpuId) noexcept {
  // Check prefix
  if (std::strncmp(line, "cpu", 3) != 0) {
    return false;
  }

  const char* ptr = line + 3;

  // Determine if aggregate (cpu ) or per-core (cpuN )
  if (*ptr == ' ') {
    cpuId = -1; // Aggregate line
    ++ptr;
  } else if (*ptr >= '0' && *ptr <= '9') {
    // Parse CPU id
    char* end = nullptr;
    cpuId = static_cast<int>(std::strtol(ptr, &end, 10));
    if (end == ptr || cpuId < 0) {
      return false;
    }
    ptr = end;
    // Skip space after CPU id
    while (*ptr == ' ') {
      ++ptr;
    }
  } else {
    return false;
  }

  // Parse 10 values
  std::array<std::uint64_t, 10> vals{};
  for (std::size_t i = 0; i < vals.size(); ++i) {
    char* end = nullptr;
    vals[i] = std::strtoull(ptr, &end, 10);
    if (end == ptr) {
      // Fewer than 10 fields is OK (older kernels)
      break;
    }
    ptr = end;
    while (*ptr == ' ') {
      ++ptr;
    }
  }

  out.user = vals[0];
  out.nice = vals[1];
  out.system = vals[2];
  out.idle = vals[3];
  out.iowait = vals[4];
  out.irq = vals[5];
  out.softirq = vals[6];
  out.steal = vals[7];
  out.guest = vals[8];
  out.guestNice = vals[9];

  return true;
}

/// Compute percentage from delta counters.
inline CpuUtilizationPercent computePercent(const CpuTimeCounters& before,
                                            const CpuTimeCounters& after) noexcept {
  CpuUtilizationPercent pct{};

  const std::uint64_t TOTAL_BEFORE = before.total();
  const std::uint64_t TOTAL_AFTER = after.total();

  if (TOTAL_AFTER <= TOTAL_BEFORE) {
    // No time elapsed or counter wrapped
    return pct;
  }

  const double TOTAL_DELTA = static_cast<double>(TOTAL_AFTER - TOTAL_BEFORE);

  auto delta = [&](std::uint64_t b, std::uint64_t a) -> double {
    return (a >= b) ? (static_cast<double>(a - b) * 100.0 / TOTAL_DELTA) : 0.0;
  };

  pct.user = delta(before.user, after.user);
  pct.nice = delta(before.nice, after.nice);
  pct.system = delta(before.system, after.system);
  pct.idle = delta(before.idle, after.idle);
  pct.iowait = delta(before.iowait, after.iowait);
  pct.irq = delta(before.irq, after.irq);
  pct.softirq = delta(before.softirq, after.softirq);
  pct.steal = delta(before.steal, after.steal);
  pct.guest = delta(before.guest, after.guest);
  pct.guestNice = delta(before.guestNice, after.guestNice);

  return pct;
}

} // namespace

/* ----------------------------- CpuTimeCounters ----------------------------- */

std::uint64_t CpuTimeCounters::total() const noexcept {
  return user + nice + system + idle + iowait + irq + softirq + steal + guest + guestNice;
}

std::uint64_t CpuTimeCounters::active() const noexcept {
  const std::uint64_t TOTAL = total();
  const std::uint64_t INACTIVE = idle + iowait;
  return (TOTAL >= INACTIVE) ? (TOTAL - INACTIVE) : 0;
}

/* ----------------------------- CpuUtilizationPercent ----------------------------- */

double CpuUtilizationPercent::active() const noexcept {
  return user + nice + system + irq + softirq + steal + guest + guestNice;
}

/* ----------------------------- API ----------------------------- */

CpuUtilizationSnapshot getCpuUtilizationSnapshot() noexcept {
  CpuUtilizationSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  // Use C-style I/O for RT-safety (no std::ifstream allocation)
  std::FILE* file = std::fopen("/proc/stat", "r");
  if (file == nullptr) {
    return snap;
  }

  std::array<char, 512> lineBuf{};
  std::size_t maxCpuId = 0;

  while (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), file) != nullptr) {
    CpuTimeCounters counters{};
    int cpuId = -1;

    if (!parseCpuLine(lineBuf.data(), counters, cpuId)) {
      // Not a cpu line (could be ctxt, btime, etc.)
      continue;
    }

    if (cpuId < 0) {
      // Aggregate line
      snap.aggregate = counters;
    } else if (static_cast<std::size_t>(cpuId) < MAX_CPUS) {
      snap.perCore[cpuId] = counters;
      if (static_cast<std::size_t>(cpuId) >= maxCpuId) {
        maxCpuId = static_cast<std::size_t>(cpuId) + 1;
      }
    }
  }

  snap.coreCount = maxCpuId;
  std::fclose(file);
  return snap;
}

CpuUtilizationDelta computeUtilizationDelta(const CpuUtilizationSnapshot& before,
                                            const CpuUtilizationSnapshot& after) noexcept {
  CpuUtilizationDelta delta{};

  // Compute interval
  if (after.timestampNs > before.timestampNs) {
    delta.intervalNs = after.timestampNs - before.timestampNs;
  }

  // Aggregate
  delta.aggregate = computePercent(before.aggregate, after.aggregate);

  // Per-core
  delta.coreCount = std::min(before.coreCount, after.coreCount);
  for (std::size_t i = 0; i < delta.coreCount; ++i) {
    delta.perCore[i] = computePercent(before.perCore[i], after.perCore[i]);
  }

  return delta;
}

/* ----------------------------- toString ----------------------------- */

std::string CpuUtilizationSnapshot::toString() const {
  std::string out;
  out += fmt::format("Timestamp: {} ns\n", timestampNs);
  out += fmt::format("Aggregate: user={} nice={} sys={} idle={} iowait={} irq={} softirq={}\n",
                     aggregate.user, aggregate.nice, aggregate.system, aggregate.idle,
                     aggregate.iowait, aggregate.irq, aggregate.softirq);
  out += fmt::format("Cores: {}\n", coreCount);
  return out;
}

std::string CpuUtilizationDelta::toString() const {
  std::string out;
  out += fmt::format("Interval: {:.2f} ms\n", static_cast<double>(intervalNs) / 1'000'000.0);
  out += fmt::format("Aggregate: {:.1f}% active (user={:.1f}% sys={:.1f}% idle={:.1f}%)\n",
                     aggregate.active(), aggregate.user, aggregate.system, aggregate.idle);

  for (std::size_t i = 0; i < coreCount; ++i) {
    const auto& C = perCore[i];
    out += fmt::format("  cpu{}: {:.1f}% active (user={:.1f}% sys={:.1f}% idle={:.1f}%)\n", i,
                       C.active(), C.user, C.system, C.idle);
  }
  return out;
}

} // namespace cpu

} // namespace seeker