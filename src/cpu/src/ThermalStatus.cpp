/**
 * @file ThermalStatus.cpp
 * @brief CPU thermal, power, and throttling collection from sysfs.
 * @note Reads hwmon, thermal zones, and RAPL (Intel) when present.
 */

#include "src/cpu/inc/ThermalStatus.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <algorithm>  // std::min
#include <cstring>    // std::memcpy
#include <filesystem> // std::filesystem
#include <fstream>    // std::ifstream

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

/// Read millidegrees Celsius; convert to degrees.
inline double readMilliCelsius(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return 0.0;
  }
  long value = 0;
  file >> value;
  return file ? static_cast<double>(value) / 1000.0 : 0.0;
}

/// Read microwatts; convert to watts.
inline double readMicrowattsAsWatts(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return 0.0;
  }
  long long uw = 0;
  file >> uw;
  return file ? static_cast<double>(uw) / 1'000'000.0 : 0.0;
}

/// Check if path exists (suppresses exceptions).
inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

} // namespace

/* ----------------------------- TemperatureSensor ----------------------------- */

std::string TemperatureSensor::toString() const {
  return fmt::format("{}: {:.1f} C", name.data(), tempCelsius);
}

/* ----------------------------- PowerLimit ----------------------------- */

std::string PowerLimit::toString() const {
  return fmt::format("{}: {:.1f} W (enforced: {})", domain.data(), watts, enforced ? "yes" : "no");
}

/* ----------------------------- ThermalStatus ----------------------------- */

std::string ThermalStatus::toString() const {
  std::string out;

  out += "Temperatures:\n";
  if (sensors.empty()) {
    out += "  (none detected)\n";
  } else {
    for (const TemperatureSensor& SENSOR : sensors) {
      out += fmt::format("  {:<24} {:5.1f} C\n", SENSOR.name.data(), SENSOR.tempCelsius);
    }
  }

  out += "Power limits:\n";
  if (powerLimits.empty()) {
    out += "  (none detected)\n";
  } else {
    for (const PowerLimit& LIMIT : powerLimits) {
      out += fmt::format("  {:<24} {:5.1f} W  enforced: {}\n", LIMIT.domain.data(), LIMIT.watts,
                         LIMIT.enforced ? "yes" : "no");
    }
  }

  out += fmt::format("Throttle hints: power={} thermal={} current={}",
                     throttling.powerLimit ? "yes" : "no", throttling.thermal ? "yes" : "no",
                     throttling.current ? "yes" : "no");

  return out;
}

/* ----------------------------- API ----------------------------- */

ThermalStatus getThermalStatus() noexcept {
  ThermalStatus status{};
  std::error_code ec;

  // --- Thermal zones (generic interface) ---
  const fs::path THERMAL{"/sys/class/thermal"};
  if (pathExists(THERMAL)) {
    for (const auto& ENTRY : fs::directory_iterator(THERMAL, ec)) {
      if (!ENTRY.is_directory()) {
        continue;
      }

      const std::string BASE = ENTRY.path().filename().string();
      if (BASE.rfind("thermal_zone", 0) != 0) {
        continue;
      }

      const std::string TYPE = readLine(ENTRY.path() / "type");
      const double TEMP = readMilliCelsius(ENTRY.path() / "temp");

      if (!TYPE.empty()) {
        TemperatureSensor sensor{};
        copyToFixedArray(sensor.name, TYPE);
        sensor.tempCelsius = TEMP;
        status.sensors.push_back(sensor);

        // Heuristic: high package temp suggests thermal throttling
        if (TYPE.find("x86_pkg_temp") != std::string::npos && TEMP >= 90.0) {
          status.throttling.thermal = true;
        }
      }
    }
  }

  // --- hwmon sensors (often more detailed) ---
  const fs::path HWMON{"/sys/class/hwmon"};
  if (pathExists(HWMON)) {
    for (const auto& ENTRY : fs::directory_iterator(HWMON, ec)) {
      if (!ENTRY.is_directory()) {
        continue;
      }

      const fs::path DEV = ENTRY.path();
      const std::string CHIP_NAME = readLine(DEV / "name");

      // Iterate temp1..temp32
      for (int idx = 1; idx <= 32; ++idx) {
        const fs::path LABEL = DEV / fmt::format("temp{}_label", idx);
        const fs::path INPUT = DEV / fmt::format("temp{}_input", idx);

        if (!pathExists(INPUT)) {
          continue;
        }

        std::string label = pathExists(LABEL) ? readLine(LABEL) : CHIP_NAME;
        if (label.empty()) {
          label = CHIP_NAME;
        }

        const double TEMP = readMilliCelsius(INPUT);
        if (TEMP > 0.0) {
          TemperatureSensor sensor{};
          copyToFixedArray(sensor.name, label);
          sensor.tempCelsius = TEMP;
          status.sensors.push_back(sensor);
        }
      }
    }
  }

  // --- RAPL power limits (Intel) ---
  const fs::path POWERCAP{"/sys/class/powercap"};
  if (pathExists(POWERCAP)) {
    for (const auto& ENTRY : fs::directory_iterator(POWERCAP, ec)) {
      if (!ENTRY.is_directory()) {
        continue;
      }

      const std::string BASE = ENTRY.path().filename().string();
      if (BASE.rfind("intel-rapl", 0) != 0) {
        continue;
      }

      const std::string DOMAIN = readLine(ENTRY.path() / "name");
      bool anyConstraint = false;

      // Check constraint_0 through constraint_3
      for (int c = 0; c < 4; ++c) {
        const fs::path POWER_PATH = ENTRY.path() / fmt::format("constraint_{}_power_limit_uw", c);

        if (pathExists(POWER_PATH)) {
          PowerLimit limit{};
          copyToFixedArray(limit.domain, DOMAIN.empty() ? BASE : DOMAIN);
          limit.watts = readMicrowattsAsWatts(POWER_PATH);
          limit.enforced = (limit.watts > 0.0);
          status.powerLimits.push_back(limit);
          anyConstraint = true;
        }
      }

      if (anyConstraint) {
        // Power caps present suggest potential power limiting
        status.throttling.powerLimit = true;
      }
    }
  }

  // --- Throttle counters (Intel thermal_throttle) ---
  const fs::path CPU_SYS{"/sys/devices/system/cpu"};
  if (pathExists(CPU_SYS)) {
    for (const auto& ENTRY : fs::directory_iterator(CPU_SYS, ec)) {
      if (!ENTRY.is_directory()) {
        continue;
      }

      const std::string NAME = ENTRY.path().filename().string();
      if (NAME.rfind("cpu", 0) != 0 || NAME == "cpufreq" || NAME == "cpuidle") {
        continue;
      }

      const fs::path THROTTLE_DIR = ENTRY.path() / "thermal_throttle";
      const fs::path PKG_COUNT = THROTTLE_DIR / "package_throttle_count";
      const fs::path CORE_COUNT = THROTTLE_DIR / "core_throttle_count";

      if (pathExists(PKG_COUNT)) {
        std::ifstream file(PKG_COUNT);
        long long count = 0;
        file >> count;
        if (file && count > 0) {
          status.throttling.thermal = true;
        }
      }

      if (pathExists(CORE_COUNT)) {
        std::ifstream file(CORE_COUNT);
        long long count = 0;
        file >> count;
        if (file && count > 0) {
          status.throttling.thermal = true;
        }
      }
    }
  }

  return status;
}

} // namespace cpu

} // namespace seeker