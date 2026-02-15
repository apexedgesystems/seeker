/**
 * @file IrqStats.cpp
 * @brief Per-core interrupt statistics from /proc/interrupts.
 * @note Parses header for CPU count, then each IRQ line for counts.
 */

#include "src/cpu/inc/IrqStats.hpp"

#include "src/helpers/inc/Cpu.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <algorithm> // std::min
#include <array>     // std::array
#include <cstdio>    // fopen, fgets, fclose
#include <cstdlib>   // strtoull
#include <cstring>   // strncmp, strlen, strcpy

#include <fmt/core.h>

namespace seeker {

namespace cpu {

namespace {

/* ----------------------------- Helpers ----------------------------- */

using seeker::helpers::cpu::getMonotonicNs;
using seeker::helpers::strings::copyToFixedArray;
using seeker::helpers::strings::skipWhitespace;

/// Count CPUs from header line like "           CPU0       CPU1       CPU2"
inline std::size_t parseCpuCount(const char* line) noexcept {
  std::size_t count = 0;
  const char* p = line;
  while (*p != '\0' && *p != '\n') {
    p = skipWhitespace(p);
    if (std::strncmp(p, "CPU", 3) == 0) {
      ++count;
      p += 3;
      // Skip the CPU number
      while (*p >= '0' && *p <= '9') {
        ++p;
      }
    } else {
      break;
    }
  }
  return count;
}

/// Parse an IRQ line. Returns true if valid.
/// Format: "  0:      1234      5678   IO-APIC  2-edge      timer"
///   or:   "NMI:      1234      5678   Non-maskable interrupts"
inline bool parseIrqLine(const char* line, IrqLineStats& out, std::size_t cpuCount) noexcept {
  const char* p = skipWhitespace(line);

  // Find IRQ name (everything before ':')
  const char* colonPos = std::strchr(p, ':');
  if (colonPos == nullptr) {
    return false;
  }

  const std::size_t NAME_LEN = static_cast<std::size_t>(colonPos - p);
  if (NAME_LEN == 0 || NAME_LEN >= IRQ_NAME_SIZE) {
    return false;
  }

  copyToFixedArray(out.name, p, NAME_LEN);
  p = colonPos + 1;

  // Parse per-CPU counts
  out.total = 0;
  for (std::size_t cpu = 0; cpu < cpuCount && cpu < IRQ_MAX_CPUS; ++cpu) {
    p = skipWhitespace(p);
    char* end = nullptr;
    const std::uint64_t VAL = std::strtoull(p, &end, 10);
    if (end == p) {
      // No number found - might be end of counts, rest is description
      break;
    }
    out.perCore[cpu] = VAL;
    out.total += VAL;
    p = end;
  }

  // Rest of line is description (skip leading spaces)
  p = skipWhitespace(p);

  // Trim trailing newline/spaces
  std::size_t descLen = std::strlen(p);
  while (descLen > 0 && (p[descLen - 1] == '\n' || p[descLen - 1] == ' ')) {
    --descLen;
  }

  if (descLen > 0) {
    copyToFixedArray(out.desc, p, descLen);
  }

  return true;
}

} // namespace

/* ----------------------------- IrqLineStats ----------------------------- */

std::string IrqLineStats::toString(std::size_t coreCount) const {
  std::string out = fmt::format("{:>8}: ", name.data());
  for (std::size_t i = 0; i < coreCount && i < IRQ_MAX_CPUS; ++i) {
    out += fmt::format("{:>10} ", perCore[i]);
  }
  out += fmt::format(" {} ", desc.data());
  return out;
}

/* ----------------------------- IrqSnapshot ----------------------------- */

std::uint64_t IrqSnapshot::totalForCore(std::size_t core) const noexcept {
  if (core >= coreCount || core >= IRQ_MAX_CPUS) {
    return 0;
  }
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < lineCount; ++i) {
    sum += lines[i].perCore[core];
  }
  return sum;
}

std::uint64_t IrqSnapshot::totalAllCores() const noexcept {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < lineCount; ++i) {
    sum += lines[i].total;
  }
  return sum;
}

std::string IrqSnapshot::toString() const {
  std::string out;
  out += fmt::format("Timestamp: {} ns\n", timestampNs);
  out += fmt::format("CPUs: {}  IRQ lines: {}\n", coreCount, lineCount);
  out += fmt::format("Total interrupts: {}\n", totalAllCores());

  // Show per-core totals
  out += "Per-core totals:";
  for (std::size_t i = 0; i < coreCount && i < IRQ_MAX_CPUS; ++i) {
    out += fmt::format(" cpu{}={}", i, totalForCore(i));
  }
  out += "\n";
  return out;
}

/* ----------------------------- IrqDelta ----------------------------- */

std::uint64_t IrqDelta::totalForCore(std::size_t core) const noexcept {
  if (core >= coreCount || core >= IRQ_MAX_CPUS) {
    return 0;
  }
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < lineCount; ++i) {
    sum += perCoreDelta[i][core];
  }
  return sum;
}

double IrqDelta::rateForCore(std::size_t core) const noexcept {
  if (intervalNs == 0) {
    return 0.0;
  }
  const std::uint64_t COUNT = totalForCore(core);
  const double SECONDS = static_cast<double>(intervalNs) / 1'000'000'000.0;
  return static_cast<double>(COUNT) / SECONDS;
}

std::string IrqDelta::toString() const {
  std::string out;
  out += fmt::format("Interval: {:.2f} ms\n", static_cast<double>(intervalNs) / 1'000'000.0);

  // Show per-core rates
  out += "IRQ rates (per second):";
  for (std::size_t i = 0; i < coreCount && i < IRQ_MAX_CPUS; ++i) {
    out += fmt::format(" cpu{}={:.0f}", i, rateForCore(i));
  }
  out += "\n";

  // Show top IRQ lines by total delta
  out += "Top IRQs:\n";
  std::size_t shown = 0;
  for (std::size_t i = 0; i < lineCount && shown < 10; ++i) {
    if (lineTotals[i] > 0) {
      out += fmt::format("  {}: {} total\n", names[i].data(), lineTotals[i]);
      ++shown;
    }
  }
  return out;
}

/* ----------------------------- API ----------------------------- */

IrqSnapshot getIrqSnapshot() noexcept {
  IrqSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  std::FILE* file = std::fopen("/proc/interrupts", "r");
  if (file == nullptr) {
    return snap;
  }

  std::array<char, 1024> lineBuf{};

  // First line is header with CPU columns
  if (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), file) != nullptr) {
    snap.coreCount = parseCpuCount(lineBuf.data());
  }

  // Parse IRQ lines
  while (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), file) != nullptr) {
    if (snap.lineCount >= IRQ_MAX_LINES) {
      break;
    }

    IrqLineStats stats{};
    if (parseIrqLine(lineBuf.data(), stats, snap.coreCount)) {
      snap.lines[snap.lineCount] = stats;
      ++snap.lineCount;
    }
  }

  std::fclose(file);
  return snap;
}

IrqDelta computeIrqDelta(const IrqSnapshot& before, const IrqSnapshot& after) noexcept {
  IrqDelta delta{};

  // Compute interval
  if (after.timestampNs > before.timestampNs) {
    delta.intervalNs = after.timestampNs - before.timestampNs;
  }

  delta.coreCount = std::min(before.coreCount, after.coreCount);

  // Match IRQ lines by name and compute deltas
  for (std::size_t a = 0; a < after.lineCount && delta.lineCount < IRQ_MAX_LINES; ++a) {
    const auto& AFTER_LINE = after.lines[a];

    // Find matching line in before
    const IrqLineStats* beforeLine = nullptr;
    for (std::size_t b = 0; b < before.lineCount; ++b) {
      if (std::strcmp(before.lines[b].name.data(), AFTER_LINE.name.data()) == 0) {
        beforeLine = &before.lines[b];
        break;
      }
    }

    // Copy name
    delta.names[delta.lineCount] = AFTER_LINE.name;

    // Compute per-core deltas
    std::uint64_t lineTotal = 0;
    for (std::size_t cpu = 0; cpu < delta.coreCount && cpu < IRQ_MAX_CPUS; ++cpu) {
      const std::uint64_t BEFORE_VAL = beforeLine ? beforeLine->perCore[cpu] : 0;
      const std::uint64_t AFTER_VAL = AFTER_LINE.perCore[cpu];
      const std::uint64_t D = (AFTER_VAL >= BEFORE_VAL) ? (AFTER_VAL - BEFORE_VAL) : 0;
      delta.perCoreDelta[delta.lineCount][cpu] = D;
      lineTotal += D;
    }
    delta.lineTotals[delta.lineCount] = lineTotal;
    ++delta.lineCount;
  }

  return delta;
}

} // namespace cpu

} // namespace seeker