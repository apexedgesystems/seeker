/**
 * @file SoftirqStats.cpp
 * @brief Per-core software interrupt statistics from /proc/softirqs.
 * @note Format: header row with CPU columns, then type rows with counts.
 */

#include "src/cpu/inc/SoftirqStats.hpp"

#include "src/helpers/inc/Cpu.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <algorithm> // std::min
#include <array>     // std::array
#include <cstdio>    // fopen, fgets, fclose
#include <cstdlib>   // strtoull
#include <cstring>   // strncmp, strlen, strcmp

#include <fmt/core.h>

namespace seeker {

namespace cpu {

namespace {

/* ----------------------------- Helpers ----------------------------- */

using seeker::helpers::cpu::getMonotonicNs;
using seeker::helpers::strings::copyToFixedArray;
using seeker::helpers::strings::skipWhitespace;

/// Count CPUs from header line.
inline std::size_t parseCpuCount(const char* line) noexcept {
  std::size_t count = 0;
  const char* p = line;
  while (*p != '\0' && *p != '\n') {
    p = skipWhitespace(p);
    if (std::strncmp(p, "CPU", 3) == 0) {
      ++count;
      p += 3;
      while (*p >= '0' && *p <= '9') {
        ++p;
      }
    } else {
      break;
    }
  }
  return count;
}

/// Parse softirq type name to enum.
inline SoftirqType parseType(const char* name) noexcept {
  if (std::strcmp(name, "HI") == 0)
    return SoftirqType::HI;
  if (std::strcmp(name, "TIMER") == 0)
    return SoftirqType::TIMER;
  if (std::strcmp(name, "NET_TX") == 0)
    return SoftirqType::NET_TX;
  if (std::strcmp(name, "NET_RX") == 0)
    return SoftirqType::NET_RX;
  if (std::strcmp(name, "BLOCK") == 0)
    return SoftirqType::BLOCK;
  if (std::strcmp(name, "IRQ_POLL") == 0)
    return SoftirqType::IRQ_POLL;
  if (std::strcmp(name, "TASKLET") == 0)
    return SoftirqType::TASKLET;
  if (std::strcmp(name, "SCHED") == 0)
    return SoftirqType::SCHED;
  if (std::strcmp(name, "HRTIMER") == 0)
    return SoftirqType::HRTIMER;
  if (std::strcmp(name, "RCU") == 0)
    return SoftirqType::RCU;
  return SoftirqType::UNKNOWN;
}

/// Parse a softirq line.
/// Format: "    NET_RX:    12345    23456    ..."
inline bool parseSoftirqLine(const char* line, SoftirqTypeStats& out,
                             std::size_t cpuCount) noexcept {
  const char* p = skipWhitespace(line);

  // Find type name (ends with ':')
  const char* colonPos = std::strchr(p, ':');
  if (colonPos == nullptr) {
    return false;
  }

  const std::size_t NAME_LEN = static_cast<std::size_t>(colonPos - p);
  if (NAME_LEN == 0 || NAME_LEN >= SOFTIRQ_NAME_SIZE) {
    return false;
  }

  copyToFixedArray(out.name, p, NAME_LEN);
  out.type = parseType(out.name.data());

  p = colonPos + 1;

  // Parse per-CPU counts
  out.total = 0;
  for (std::size_t cpu = 0; cpu < cpuCount && cpu < SOFTIRQ_MAX_CPUS; ++cpu) {
    p = skipWhitespace(p);
    char* end = nullptr;
    const std::uint64_t VAL = std::strtoull(p, &end, 10);
    if (end == p) {
      break;
    }
    out.perCore[cpu] = VAL;
    out.total += VAL;
    p = end;
  }

  return true;
}

} // namespace

/* ----------------------------- Type Name Helper ----------------------------- */

const char* softirqTypeName(SoftirqType type) noexcept {
  switch (type) {
  case SoftirqType::HI:
    return "HI";
  case SoftirqType::TIMER:
    return "TIMER";
  case SoftirqType::NET_TX:
    return "NET_TX";
  case SoftirqType::NET_RX:
    return "NET_RX";
  case SoftirqType::BLOCK:
    return "BLOCK";
  case SoftirqType::IRQ_POLL:
    return "IRQ_POLL";
  case SoftirqType::TASKLET:
    return "TASKLET";
  case SoftirqType::SCHED:
    return "SCHED";
  case SoftirqType::HRTIMER:
    return "HRTIMER";
  case SoftirqType::RCU:
    return "RCU";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- SoftirqSnapshot ----------------------------- */

std::uint64_t SoftirqSnapshot::totalForCpu(std::size_t cpu) const noexcept {
  if (cpu >= cpuCount || cpu >= SOFTIRQ_MAX_CPUS) {
    return 0;
  }
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < typeCount; ++i) {
    sum += types[i].perCore[cpu];
  }
  return sum;
}

const SoftirqTypeStats* SoftirqSnapshot::getType(SoftirqType type) const noexcept {
  for (std::size_t i = 0; i < typeCount; ++i) {
    if (types[i].type == type) {
      return &types[i];
    }
  }
  return nullptr;
}

std::string SoftirqSnapshot::toString() const {
  std::string out;
  out += fmt::format("Timestamp: {} ns\n", timestampNs);
  out += fmt::format("CPUs: {}  Types: {}\n", cpuCount, typeCount);

  for (std::size_t i = 0; i < typeCount; ++i) {
    out += fmt::format("  {:>10}: {} total\n", types[i].name.data(), types[i].total);
  }

  out += "Per-CPU totals:";
  for (std::size_t cpu = 0; cpu < cpuCount && cpu < 16; ++cpu) {
    out += fmt::format(" cpu{}={}", cpu, totalForCpu(cpu));
  }
  if (cpuCount > 16) {
    out += " ...";
  }
  out += "\n";

  return out;
}

/* ----------------------------- SoftirqDelta ----------------------------- */

std::uint64_t SoftirqDelta::totalForCpu(std::size_t cpu) const noexcept {
  if (cpu >= cpuCount || cpu >= SOFTIRQ_MAX_CPUS) {
    return 0;
  }
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < typeCount; ++i) {
    sum += perCoreDelta[i][cpu];
  }
  return sum;
}

double SoftirqDelta::rateForCpu(std::size_t cpu) const noexcept {
  if (intervalNs == 0) {
    return 0.0;
  }
  const std::uint64_t COUNT = totalForCpu(cpu);
  const double SECONDS = static_cast<double>(intervalNs) / 1'000'000'000.0;
  return static_cast<double>(COUNT) / SECONDS;
}

double SoftirqDelta::rateForType(SoftirqType type) const noexcept {
  if (intervalNs == 0) {
    return 0.0;
  }

  // Find the type
  for (std::size_t i = 0; i < typeCount; ++i) {
    if (typeEnums[i] == type) {
      const double SECONDS = static_cast<double>(intervalNs) / 1'000'000'000.0;
      return static_cast<double>(typeTotals[i]) / SECONDS;
    }
  }
  return 0.0;
}

std::string SoftirqDelta::toString() const {
  std::string out;
  out += fmt::format("Interval: {:.2f} ms\n", static_cast<double>(intervalNs) / 1'000'000.0);

  // Show per-type rates
  out += "Softirq rates (per second):\n";
  for (std::size_t i = 0; i < typeCount; ++i) {
    if (typeTotals[i] > 0) {
      const double SECONDS = static_cast<double>(intervalNs) / 1'000'000'000.0;
      const double RATE = static_cast<double>(typeTotals[i]) / SECONDS;
      out += fmt::format("  {:>10}: {:.0f}/s\n", names[i].data(), RATE);
    }
  }

  // Show per-CPU rates (abbreviated)
  out += "Per-CPU rates:";
  for (std::size_t cpu = 0; cpu < cpuCount && cpu < 16; ++cpu) {
    out += fmt::format(" cpu{}={:.0f}", cpu, rateForCpu(cpu));
  }
  if (cpuCount > 16) {
    out += " ...";
  }
  out += "\n";

  return out;
}

/* ----------------------------- API ----------------------------- */

SoftirqSnapshot getSoftirqSnapshot() noexcept {
  SoftirqSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  std::FILE* file = std::fopen("/proc/softirqs", "r");
  if (file == nullptr) {
    return snap;
  }

  std::array<char, 1024> lineBuf{};

  // First line is header with CPU columns
  if (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), file) != nullptr) {
    snap.cpuCount = parseCpuCount(lineBuf.data());
  }

  // Parse softirq type lines
  while (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), file) != nullptr) {
    if (snap.typeCount >= SOFTIRQ_MAX_TYPES) {
      break;
    }

    SoftirqTypeStats stats{};
    if (parseSoftirqLine(lineBuf.data(), stats, snap.cpuCount)) {
      snap.types[snap.typeCount] = stats;
      ++snap.typeCount;
    }
  }

  std::fclose(file);
  return snap;
}

SoftirqDelta computeSoftirqDelta(const SoftirqSnapshot& before,
                                 const SoftirqSnapshot& after) noexcept {
  SoftirqDelta delta{};

  // Compute interval
  if (after.timestampNs > before.timestampNs) {
    delta.intervalNs = after.timestampNs - before.timestampNs;
  }

  delta.cpuCount = std::min(before.cpuCount, after.cpuCount);

  // Match types by name and compute deltas
  for (std::size_t a = 0; a < after.typeCount && delta.typeCount < SOFTIRQ_MAX_TYPES; ++a) {
    const auto& AFTER_TYPE = after.types[a];

    // Find matching type in before
    const SoftirqTypeStats* beforeType = nullptr;
    for (std::size_t b = 0; b < before.typeCount; ++b) {
      if (std::strcmp(before.types[b].name.data(), AFTER_TYPE.name.data()) == 0) {
        beforeType = &before.types[b];
        break;
      }
    }

    // Copy name and type
    delta.names[delta.typeCount] = AFTER_TYPE.name;
    delta.typeEnums[delta.typeCount] = AFTER_TYPE.type;

    // Compute per-CPU deltas
    std::uint64_t typeTotal = 0;
    for (std::size_t cpu = 0; cpu < delta.cpuCount && cpu < SOFTIRQ_MAX_CPUS; ++cpu) {
      const std::uint64_t BEFORE_VAL = beforeType ? beforeType->perCore[cpu] : 0;
      const std::uint64_t AFTER_VAL = AFTER_TYPE.perCore[cpu];
      const std::uint64_t D = (AFTER_VAL >= BEFORE_VAL) ? (AFTER_VAL - BEFORE_VAL) : 0;
      delta.perCoreDelta[delta.typeCount][cpu] = D;
      typeTotal += D;
    }
    delta.typeTotals[delta.typeCount] = typeTotal;
    ++delta.typeCount;
  }

  return delta;
}

} // namespace cpu

} // namespace seeker