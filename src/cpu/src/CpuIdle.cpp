/**
 * @file CpuIdle.cpp
 * @brief CPU idle state statistics from sysfs cpuidle.
 * @note Reads /sys/devices/system/cpu/cpuN/cpuidle/stateM/ for each state.
 */

#include "src/cpu/inc/CpuIdle.hpp"
#include "src/helpers/inc/Strings.hpp"

#include "src/helpers/inc/Cpu.hpp"

#include <algorithm>  // std::min
#include <array>      // std::array
#include <cstdio>     // fopen, fgets, fclose
#include <cstdlib>    // strtoul, strtoull
#include <cstring>    // strlen, memcpy
#include <filesystem> // directory iteration

#include <fmt/core.h>

namespace fs = std::filesystem;

namespace seeker {

namespace cpu {

namespace {

/* ----------------------------- Helpers ----------------------------- */

using seeker::helpers::cpu::getMonotonicNs;
using seeker::helpers::strings::copyToFixedArray;

/// Read single line from file, trim newline.
inline bool readFileLine(const fs::path& path, char* buf, std::size_t bufSize) noexcept {
  std::FILE* file = std::fopen(path.c_str(), "r");
  if (file == nullptr) {
    return false;
  }
  if (std::fgets(buf, static_cast<int>(bufSize), file) == nullptr) {
    std::fclose(file);
    return false;
  }
  std::fclose(file);

  // Trim trailing newline
  std::size_t len = std::strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
    buf[--len] = '\0';
  }
  return true;
}

/// Read unsigned integer from file.
inline std::uint64_t readFileUint(const fs::path& path) noexcept {
  std::array<char, 64> buf{};
  if (!readFileLine(path, buf.data(), buf.size())) {
    return 0;
  }
  return std::strtoull(buf.data(), nullptr, 10);
}

/// Parse CPU ID from directory name like "cpu0", "cpu12".
inline int parseCpuId(const std::string& name) noexcept {
  if (name.size() < 4 || name.compare(0, 3, "cpu") != 0) {
    return -1;
  }
  // Check remaining chars are digits
  for (std::size_t i = 3; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
  }
  return std::atoi(name.c_str() + 3);
}

/// Parse state index from directory name like "state0", "state3".
inline int parseStateIdx(const std::string& name) noexcept {
  if (name.size() < 6 || name.compare(0, 5, "state") != 0) {
    return -1;
  }
  for (std::size_t i = 5; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
  }
  return std::atoi(name.c_str() + 5);
}

/// Read C-state info from a stateN directory.
inline CStateInfo readStateInfo(const fs::path& stateDir) noexcept {
  CStateInfo info{};
  std::array<char, 128> buf{};

  // Name
  if (readFileLine(stateDir / "name", buf.data(), buf.size())) {
    copyToFixedArray(info.name, buf.data(), std::strlen(buf.data()));
  }

  // Description
  if (readFileLine(stateDir / "desc", buf.data(), buf.size())) {
    copyToFixedArray(info.desc, buf.data(), std::strlen(buf.data()));
  }

  // Latency (microseconds)
  info.latencyUs = static_cast<std::uint32_t>(readFileUint(stateDir / "latency"));

  // Target residency (microseconds)
  info.residencyUs = static_cast<std::uint32_t>(readFileUint(stateDir / "residency"));

  // Usage count
  info.usageCount = readFileUint(stateDir / "usage");

  // Time spent (microseconds)
  info.timeUs = readFileUint(stateDir / "time");

  // Disabled flag
  info.disabled = (readFileUint(stateDir / "disable") != 0);

  return info;
}

} // namespace

/* ----------------------------- CpuIdleStats ----------------------------- */

std::uint64_t CpuIdleStats::totalIdleTimeUs() const noexcept {
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < stateCount; ++i) {
    total += states[i].timeUs;
  }
  return total;
}

int CpuIdleStats::deepestEnabledState() const noexcept {
  int deepest = -1;
  for (std::size_t i = 0; i < stateCount; ++i) {
    if (!states[i].disabled) {
      deepest = static_cast<int>(i);
    }
  }
  return deepest;
}

std::string CpuIdleStats::toString() const {
  std::string out;
  out += fmt::format("CPU {}: {} states\n", cpuId, stateCount);
  for (std::size_t i = 0; i < stateCount; ++i) {
    const auto& S = states[i];
    out += fmt::format("  {}: {} lat={}us res={}us usage={} time={}us {}\n", S.name.data(),
                       S.desc.data(), S.latencyUs, S.residencyUs, S.usageCount, S.timeUs,
                       S.disabled ? "(disabled)" : "");
  }
  return out;
}

/* ----------------------------- CpuIdleSnapshot ----------------------------- */

std::string CpuIdleSnapshot::toString() const {
  std::string out;
  out += fmt::format("Timestamp: {} ns\n", timestampNs);
  out += fmt::format("CPUs with cpuidle: {}\n", cpuCount);
  for (std::size_t i = 0; i < cpuCount; ++i) {
    out += perCpu[i].toString();
  }
  return out;
}

/* ----------------------------- CpuIdleDelta ----------------------------- */

double CpuIdleDelta::residencyPercent(std::size_t cpuId, std::size_t stateIdx) const noexcept {
  if (cpuId >= cpuCount || stateIdx >= stateCount[cpuId] || intervalNs == 0) {
    return 0.0;
  }
  // Convert interval from ns to us
  const double INTERVAL_US = static_cast<double>(intervalNs) / 1000.0;
  const double TIME_US = static_cast<double>(timeDeltaUs[cpuId][stateIdx]);
  return (TIME_US / INTERVAL_US) * 100.0;
}

std::string CpuIdleDelta::toString() const {
  std::string out;
  out += fmt::format("Interval: {:.2f} ms\n", static_cast<double>(intervalNs) / 1'000'000.0);

  for (std::size_t cpu = 0; cpu < cpuCount; ++cpu) {
    out += fmt::format("CPU {}:", cpu);
    for (std::size_t s = 0; s < stateCount[cpu]; ++s) {
      const double PCT = residencyPercent(cpu, s);
      if (PCT > 0.1) {
        out += fmt::format(" S{}={:.1f}%", s, PCT);
      }
    }
    out += "\n";
  }
  return out;
}

/* ----------------------------- API ----------------------------- */

CpuIdleSnapshot getCpuIdleSnapshot() noexcept {
  CpuIdleSnapshot snap{};
  snap.timestampNs = getMonotonicNs();

  const fs::path CPU_BASE{"/sys/devices/system/cpu"};
  std::error_code ec;

  if (!fs::exists(CPU_BASE, ec)) {
    return snap;
  }

  // Iterate cpu directories
  for (const auto& cpuEntry : fs::directory_iterator(CPU_BASE, ec)) {
    if (!cpuEntry.is_directory(ec)) {
      continue;
    }

    const std::string CPU_NAME = cpuEntry.path().filename().string();
    const int CPU_ID = parseCpuId(CPU_NAME);
    if (CPU_ID < 0 || static_cast<std::size_t>(CPU_ID) >= IDLE_MAX_CPUS) {
      continue;
    }

    const fs::path IDLE_DIR = cpuEntry.path() / "cpuidle";
    if (!fs::exists(IDLE_DIR, ec)) {
      continue;
    }

    CpuIdleStats& cpuStats = snap.perCpu[snap.cpuCount];
    cpuStats.cpuId = CPU_ID;

    // Iterate state directories
    for (const auto& stateEntry : fs::directory_iterator(IDLE_DIR, ec)) {
      if (!stateEntry.is_directory(ec)) {
        continue;
      }

      const std::string STATE_NAME = stateEntry.path().filename().string();
      const int STATE_IDX = parseStateIdx(STATE_NAME);
      if (STATE_IDX < 0 || static_cast<std::size_t>(STATE_IDX) >= IDLE_MAX_STATES) {
        continue;
      }

      const CStateInfo INFO = readStateInfo(stateEntry.path());
      cpuStats.states[STATE_IDX] = INFO;

      if (static_cast<std::size_t>(STATE_IDX) >= cpuStats.stateCount) {
        cpuStats.stateCount = static_cast<std::size_t>(STATE_IDX) + 1;
      }
    }

    if (cpuStats.stateCount > 0) {
      ++snap.cpuCount;
    }
  }

  return snap;
}

CpuIdleDelta computeCpuIdleDelta(const CpuIdleSnapshot& before,
                                 const CpuIdleSnapshot& after) noexcept {
  CpuIdleDelta delta{};

  // Compute interval
  if (after.timestampNs > before.timestampNs) {
    delta.intervalNs = after.timestampNs - before.timestampNs;
  }

  delta.cpuCount = std::min(before.cpuCount, after.cpuCount);

  // Match CPUs by index in the arrays
  for (std::size_t i = 0; i < delta.cpuCount; ++i) {
    const auto& BEFORE_CPU = before.perCpu[i];
    const auto& AFTER_CPU = after.perCpu[i];

    // Skip if CPU IDs don't match (shouldn't happen normally)
    if (BEFORE_CPU.cpuId != AFTER_CPU.cpuId) {
      continue;
    }

    const std::size_t N_STATES = std::min(BEFORE_CPU.stateCount, AFTER_CPU.stateCount);
    delta.stateCount[i] = N_STATES;

    for (std::size_t s = 0; s < N_STATES; ++s) {
      const auto& B = BEFORE_CPU.states[s];
      const auto& A = AFTER_CPU.states[s];

      // Usage delta
      delta.usageDelta[i][s] = (A.usageCount >= B.usageCount) ? (A.usageCount - B.usageCount) : 0;

      // Time delta
      delta.timeDeltaUs[i][s] = (A.timeUs >= B.timeUs) ? (A.timeUs - B.timeUs) : 0;
    }
  }

  return delta;
}

} // namespace cpu

} // namespace seeker