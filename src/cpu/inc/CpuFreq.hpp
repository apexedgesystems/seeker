#ifndef SEEKER_CPU_FREQ_HPP
#define SEEKER_CPU_FREQ_HPP
/**
 * @file CpuFreq.hpp
 * @brief CPU frequency and governor snapshot via sysfs.
 * @note Linux-only. Reads /sys/devices/system/cpu/cpuN/cpufreq/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum governor string length (covers all known governors + null).
inline constexpr std::size_t GOVERNOR_STRING_SIZE = 24;

/* ----------------------------- CoreFrequency ----------------------------- */

/**
 * @brief Per-core frequency data.
 */
struct CoreFrequency {
  int cpuId{-1};                                     ///< Logical CPU id (0-based)
  std::array<char, GOVERNOR_STRING_SIZE> governor{}; ///< e.g., "performance", "powersave"
  std::int64_t minKHz{0};                            ///< Minimum configured frequency (kHz)
  std::int64_t maxKHz{0};                            ///< Maximum configured frequency (kHz)
  std::int64_t curKHz{0};                            ///< Current/last sampled frequency (kHz)
  bool turboAvailable{false};                        ///< Turbo/boost exposed by sysfs

  /// @brief Human-readable single-line summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CpuFrequencySummary ----------------------------- */

/**
 * @brief Aggregate frequency summary for all cores.
 */
struct CpuFrequencySummary {
  std::vector<CoreFrequency> cores{}; ///< One entry per logical CPU

  /// @brief Human-readable multi-line summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect per-core cpufreq data from sysfs.
 * @return Summary with one CoreFrequency per detected CPU.
 * @note NOT RT-safe: Allocates vector, performs file I/O.
 *       Missing files are tolerated; fields default to zero/empty.
 */
[[nodiscard]] CpuFrequencySummary getCpuFrequencySummary() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_FREQ_HPP