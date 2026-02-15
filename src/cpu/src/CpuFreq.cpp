/**
 * @file CpuFreq.cpp
 * @brief CPU frequency and governor collection from sysfs.
 * @note Scans /sys/devices/system/cpu/cpuN/cpufreq/ for per-core data.
 */

#include "src/cpu/inc/CpuFreq.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <cctype>     // std::isdigit
#include <cstdlib>    // std::atoi
#include <filesystem> // std::filesystem
#include <fstream>    // std::ifstream
#include <limits>     // std::numeric_limits

#include <fmt/core.h>

namespace fs = std::filesystem;

namespace seeker {

namespace cpu {

namespace {
using seeker::helpers::strings::copyToFixedArray;

/// Read first line of a text file; empty on failure.
inline std::string readLine(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::string line;
  std::getline(file, line);
  return line;
}

/// Read kHz value from file; 0 on failure.
inline std::int64_t readKHz(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return 0;
  }
  long long value = 0;
  file >> value;
  if (!file || value < 0 || value > std::numeric_limits<std::int64_t>::max()) {
    return 0;
  }
  return static_cast<std::int64_t>(value);
}

/// Check if path exists (suppresses exceptions).
inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

} // namespace

/* ----------------------------- CoreFrequency ----------------------------- */

std::string CoreFrequency::toString() const {
  return fmt::format("cpu{}: {:>12}  min/max/cur: {:>7}/{:>7}/{:>7} kHz  turbo:{}", cpuId,
                     governor[0] != '\0' ? governor.data() : "-", minKHz, maxKHz, curKHz,
                     turboAvailable ? "y" : "n");
}

/* ----------------------------- CpuFrequencySummary ----------------------------- */

std::string CpuFrequencySummary::toString() const {
  if (cores.empty()) {
    return "No cpufreq data available";
  }

  std::string out;
  out.reserve(cores.size() * 80);

  for (const CoreFrequency& core : cores) {
    out += core.toString();
    out += '\n';
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

CpuFrequencySummary getCpuFrequencySummary() noexcept {
  CpuFrequencySummary summary{};

  const fs::path CPU_SYS{"/sys/devices/system/cpu"};
  if (!pathExists(CPU_SYS)) {
    return summary;
  }

  std::error_code ec;
  for (const auto& ENTRY : fs::directory_iterator(CPU_SYS, ec)) {
    if (!ENTRY.is_directory()) {
      continue;
    }

    const std::string NAME = ENTRY.path().filename().string();

    // Match "cpu<N>" directories, skip "cpufreq", "cpuidle"
    if (NAME.rfind("cpu", 0) != 0 || NAME == "cpufreq" || NAME == "cpuidle") {
      continue;
    }

    // Parse CPU id from directory name
    int cpuId = -1;
    {
      std::size_t i = 3;
      while (i < NAME.size() && std::isdigit(static_cast<unsigned char>(NAME[i])) != 0) {
        ++i;
      }
      if (i > 3) {
        cpuId = std::atoi(NAME.c_str() + 3);
      }
    }

    if (cpuId < 0) {
      continue;
    }

    const fs::path FREQ_DIR = ENTRY.path() / "cpufreq";
    if (!pathExists(FREQ_DIR)) {
      continue;
    }

    CoreFrequency core{};
    core.cpuId = cpuId;

    // Governor
    const std::string GOV = readLine(FREQ_DIR / "scaling_governor");
    copyToFixedArray(core.governor, GOV);

    // Frequencies: prefer scaling_* over cpuinfo_*
    if (pathExists(FREQ_DIR / "scaling_min_freq")) {
      core.minKHz = readKHz(FREQ_DIR / "scaling_min_freq");
    } else if (pathExists(FREQ_DIR / "cpuinfo_min_freq")) {
      core.minKHz = readKHz(FREQ_DIR / "cpuinfo_min_freq");
    }

    if (pathExists(FREQ_DIR / "scaling_max_freq")) {
      core.maxKHz = readKHz(FREQ_DIR / "scaling_max_freq");
    } else if (pathExists(FREQ_DIR / "cpuinfo_max_freq")) {
      core.maxKHz = readKHz(FREQ_DIR / "cpuinfo_max_freq");
    }

    if (pathExists(FREQ_DIR / "scaling_cur_freq")) {
      core.curKHz = readKHz(FREQ_DIR / "scaling_cur_freq");
    }

    // Turbo detection: Intel pstate toggle or cur > max heuristic
    core.turboAvailable = false;
    const fs::path INTEL_TURBO{"/sys/devices/system/cpu/intel_pstate/no_turbo"};
    if (pathExists(INTEL_TURBO)) {
      std::ifstream file(INTEL_TURBO);
      int value = 0;
      file >> value;
      // no_turbo=0 means turbo is available
      core.turboAvailable = (file && value == 0);
    } else if (core.maxKHz > 0 && core.curKHz > core.maxKHz) {
      // Current exceeds "max" suggests turbo active
      core.turboAvailable = true;
    }

    summary.cores.push_back(core);
  }

  return summary;
}

} // namespace cpu

} // namespace seeker