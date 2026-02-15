#ifndef SEEKER_GPU_TELEMETRY_HPP
#define SEEKER_GPU_TELEMETRY_HPP
/**
 * @file GpuTelemetry.hpp
 * @brief GPU telemetry: temperature, power, clocks, throttling.
 * @note Linux-only. Queries via NVML for NVIDIA; hwmon for others.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstdint> // std::uint32_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- ThrottleReasons ----------------------------- */

/**
 * @brief GPU clock throttle reason flags.
 */
struct ThrottleReasons {
  bool gpuIdle{false};           ///< GPU is idle
  bool applicationClocks{false}; ///< Application clocks setting
  bool swPowerCap{false};        ///< Software power cap
  bool hwSlowdown{false};        ///< Hardware slowdown (thermal/power)
  bool syncBoost{false};         ///< Sync boost limiter
  bool swThermal{false};         ///< Software thermal slowdown
  bool hwThermal{false};         ///< Hardware thermal slowdown
  bool hwPowerBrake{false};      ///< Hardware power brake
  bool displayClocks{false};     ///< Display clock setting

  /// @brief Check if any throttling is active.
  [[nodiscard]] bool isThrottling() const noexcept;

  /// @brief Check if thermal throttling is active.
  [[nodiscard]] bool isThermalThrottling() const noexcept;

  /// @brief Check if power throttling is active.
  [[nodiscard]] bool isPowerThrottling() const noexcept;

  /// @brief Human-readable list of active reasons.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpuTelemetry ----------------------------- */

/**
 * @brief GPU telemetry snapshot.
 */
struct GpuTelemetry {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)
  std::string name;    ///< Device name

  // Temperature (Celsius)
  int temperatureC{0};         ///< Current GPU temperature
  int temperatureSlowdownC{0}; ///< Slowdown threshold temperature
  int temperatureShutdownC{0}; ///< Shutdown threshold temperature
  int temperatureMemoryC{0};   ///< Memory temperature (if available)

  // Power (milliwatts)
  std::uint32_t powerMilliwatts{0};        ///< Current power draw
  std::uint32_t powerLimitMilliwatts{0};   ///< Enforced power limit
  std::uint32_t powerDefaultMilliwatts{0}; ///< Default power limit
  std::uint32_t powerMaxMilliwatts{0};     ///< Maximum power limit

  // Clocks (MHz)
  int smClockMHz{0};       ///< Current SM clock
  int smClockMaxMHz{0};    ///< Maximum SM clock
  int memClockMHz{0};      ///< Current memory clock
  int memClockMaxMHz{0};   ///< Maximum memory clock
  int graphicsClockMHz{0}; ///< Current graphics clock
  int videoClockMHz{0};    ///< Current video clock

  // Performance state
  int perfState{0}; ///< Performance state (P0-P15, 0=max)

  // Throttling
  ThrottleReasons throttleReasons; ///< Active throttle reasons

  // Utilization (percent, 0-100)
  int gpuUtilization{0};     ///< GPU compute utilization
  int memoryUtilization{0};  ///< Memory bandwidth utilization
  int encoderUtilization{0}; ///< Video encoder utilization
  int decoderUtilization{0}; ///< Video decoder utilization

  // Fan (percent, -1 if passive/unavailable)
  int fanSpeedPercent{-1}; ///< Fan speed percentage

  /// @brief Check if GPU is in performance state P0.
  [[nodiscard]] bool isMaxPerformance() const noexcept { return perfState == 0; }

  /// @brief Check if any throttling is active.
  [[nodiscard]] bool isThrottling() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query telemetry for a specific GPU.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated telemetry; defaults on failure.
 * @note RT-safe for single device query (no allocation beyond result).
 */
[[nodiscard]] GpuTelemetry getGpuTelemetry(int deviceIndex) noexcept;

/**
 * @brief Query telemetry for all GPUs.
 * @return Vector of telemetry for each GPU.
 * @note NOT RT-safe: Allocates vector.
 */
[[nodiscard]] std::vector<GpuTelemetry> getAllGpuTelemetry() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_TELEMETRY_HPP
