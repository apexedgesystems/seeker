#ifndef SEEKER_TIMING_CLOCK_SOURCE_HPP
#define SEEKER_TIMING_CLOCK_SOURCE_HPP
/**
 * @file ClockSource.hpp
 * @brief Kernel clocksource and timer resolution queries (Linux).
 * @note Linux-only. Reads /sys/devices/system/clocksource/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides clocksource and timer resolution information critical for RT systems:
 *  - Active clocksource (TSC, HPET, acpi_pm)
 *  - Available clocksource alternatives
 *  - Timer resolution for all major clock types
 *
 * TSC (Time Stamp Counter) is preferred for RT systems due to lowest overhead.
 * HPET and acpi_pm have higher latency but may be more stable on some hardware.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/* ----------------------------- Constants ----------------------------- */

/// Maximum length for clocksource name strings.
inline constexpr std::size_t CLOCKSOURCE_NAME_SIZE = 32;

/// Maximum number of available clocksources.
inline constexpr std::size_t MAX_CLOCKSOURCES = 8;

/* ----------------------------- ClockResolution ----------------------------- */

/**
 * @brief Timer resolution for a specific clock type.
 */
struct ClockResolution {
  std::int64_t resolutionNs{0}; ///< clock_getres() result in nanoseconds
  bool available{false};        ///< True if clock type is accessible

  /// @brief Check if resolution indicates high-res timers (<= 1 microsecond).
  [[nodiscard]] bool isHighRes() const noexcept;

  /// @brief Check if resolution indicates coarse timers (> 1 millisecond).
  [[nodiscard]] bool isCoarse() const noexcept;
};

/* ----------------------------- ClockSource ----------------------------- */

/**
 * @brief Clocksource and timer resolution snapshot.
 *
 * Captures the active kernel clocksource, available alternatives, and
 * resolution for all major clock types used in timing-sensitive applications.
 */
struct ClockSource {
  // Clocksource information
  std::array<char, CLOCKSOURCE_NAME_SIZE> current{}; ///< Active clocksource (e.g., "tsc")
  std::array<std::array<char, CLOCKSOURCE_NAME_SIZE>, MAX_CLOCKSOURCES> available{};
  std::size_t availableCount{0}; ///< Valid entries in available[]

  // Timer resolutions for key clock types
  ClockResolution monotonic{};       ///< CLOCK_MONOTONIC (recommended for intervals)
  ClockResolution monotonicRaw{};    ///< CLOCK_MONOTONIC_RAW (no NTP adjustments)
  ClockResolution monotonicCoarse{}; ///< CLOCK_MONOTONIC_COARSE (fast, lower precision)
  ClockResolution realtime{};        ///< CLOCK_REALTIME (wall clock, may jump)
  ClockResolution realtimeCoarse{};  ///< CLOCK_REALTIME_COARSE (fast wall clock)
  ClockResolution boottime{};        ///< CLOCK_BOOTTIME (includes suspend time)

  /// @brief Check if active clocksource is TSC (lowest overhead).
  [[nodiscard]] bool isTsc() const noexcept;

  /// @brief Check if active clocksource is HPET.
  [[nodiscard]] bool isHpet() const noexcept;

  /// @brief Check if active clocksource is acpi_pm.
  [[nodiscard]] bool isAcpiPm() const noexcept;

  /// @brief Check if high-resolution timers are active (MONOTONIC <= 1us).
  [[nodiscard]] bool hasHighResTimers() const noexcept;

  /// @brief Check if a specific clocksource is available.
  /// @param name Clocksource name to check (e.g., "tsc").
  [[nodiscard]] bool hasClockSource(const char* name) const noexcept;

  /// @brief Get RT suitability score (0-100).
  /// 100 = TSC with high-res timers, lower for HPET/acpi_pm or coarse timers.
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query clocksource and timer resolution.
 * @return Populated ClockSource with current settings and resolutions.
 * @note RT-safe: Bounded syscalls and file reads, fixed-size output.
 *
 * Sources:
 *  - /sys/devices/system/clocksource/clocksource0/current_clocksource
 *  - /sys/devices/system/clocksource/clocksource0/available_clocksource
 *  - clock_getres(2) for all clock types
 */
[[nodiscard]] ClockSource getClockSource() noexcept;

/**
 * @brief Get resolution for a specific clock type.
 * @param clockId POSIX clock ID (e.g., CLOCK_MONOTONIC).
 * @return Resolution in nanoseconds, 0 if clock unavailable.
 * @note RT-safe: Single syscall, no allocation.
 */
[[nodiscard]] std::int64_t getClockResolutionNs(int clockId) noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_CLOCK_SOURCE_HPP
