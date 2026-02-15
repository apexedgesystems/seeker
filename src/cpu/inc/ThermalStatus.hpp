#ifndef SEEKER_CPU_THERMAL_STATUS_HPP
#define SEEKER_CPU_THERMAL_STATUS_HPP
/**
 * @file ThermalStatus.hpp
 * @brief CPU thermal, power, and throttling status.
 * @note Linux-only. Reads /sys/class/thermal, /sys/class/hwmon, /sys/class/powercap.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum sensor/domain name length.
inline constexpr std::size_t THERMAL_NAME_SIZE = 32;

/* ----------------------------- TemperatureSensor ----------------------------- */

/**
 * @brief Temperature reading from a sensor.
 */
struct TemperatureSensor {
  std::array<char, THERMAL_NAME_SIZE> name{}; ///< e.g., "Package id 0", "Core 0"
  double tempCelsius{0.0};                    ///< Temperature in degrees Celsius

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- PowerLimit ----------------------------- */

/**
 * @brief Power limit (RAPL) information.
 */
struct PowerLimit {
  std::array<char, THERMAL_NAME_SIZE> domain{}; ///< e.g., "package-0", "core", "dram"
  double watts{0.0};                            ///< Current power cap in watts
  bool enforced{false};                         ///< True if limit is being enforced

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ThrottleHints ----------------------------- */

/**
 * @brief Throttling indicator flags.
 */
struct ThrottleHints {
  bool powerLimit{false}; ///< Power limit throttling active/recent
  bool thermal{false};    ///< Thermal throttling active/recent
  bool current{false};    ///< Electrical current limit throttle
};

/* ----------------------------- ThermalStatus ----------------------------- */

/**
 * @brief Aggregate thermal and power status.
 */
struct ThermalStatus {
  std::vector<TemperatureSensor> sensors{}; ///< All detected temperature sensors
  std::vector<PowerLimit> powerLimits{};    ///< RAPL power limits (Intel)
  ThrottleHints throttling{};               ///< Throttle indicator flags

  /// @brief Human-readable multi-line summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect thermal, power, and throttling status from sysfs.
 * @return Populated status; empty vectors on failure or missing data.
 * @note NOT RT-safe: Allocates vectors, performs file I/O.
 */
[[nodiscard]] ThermalStatus getThermalStatus() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_THERMAL_STATUS_HPP