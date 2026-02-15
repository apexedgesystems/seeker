#ifndef SEEKER_MEMORY_STATS_HPP
#define SEEKER_MEMORY_STATS_HPP
/**
 * @file MemoryStats.hpp
 * @brief System memory usage and VM policy settings (Linux).
 * @note Linux-only. Reads /proc/meminfo, /proc/sys/vm/, /sys/kernel/mm/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Maximum length for THP setting strings.
inline constexpr std::size_t THP_STRING_SIZE = 64;

/* ----------------------------- MemoryStats ----------------------------- */

/**
 * @brief System memory usage and VM policy snapshot.
 *
 * Captures current RAM/swap levels and kernel VM settings relevant to
 * real-time and performance-critical systems.
 */
struct MemoryStats {
  // RAM usage (from /proc/meminfo)
  std::uint64_t totalBytes{0};     ///< MemTotal
  std::uint64_t freeBytes{0};      ///< MemFree
  std::uint64_t availableBytes{0}; ///< MemAvailable (estimate of allocatable memory)
  std::uint64_t buffersBytes{0};   ///< Buffers
  std::uint64_t cachedBytes{0};    ///< Cached + SReclaimable

  // Swap usage (from /proc/meminfo)
  std::uint64_t swapTotalBytes{0}; ///< SwapTotal
  std::uint64_t swapFreeBytes{0};  ///< SwapFree

  // VM policies
  int swappiness{-1};       ///< /proc/sys/vm/swappiness (0-100), -1 if unavailable
  int zoneReclaimMode{-1};  ///< /proc/sys/vm/zone_reclaim_mode (0-7), -1 if unavailable
  int overcommitMemory{-1}; ///< /proc/sys/vm/overcommit_memory (0-2), -1 if unavailable

  // Transparent Huge Pages settings
  std::array<char, THP_STRING_SIZE> thpEnabled{}; ///< e.g., "[always] madvise never"
  std::array<char, THP_STRING_SIZE> thpDefrag{};  ///< e.g., "always defer [madvise] never"

  /// @brief Calculate used memory (total - free - buffers - cached).
  [[nodiscard]] std::uint64_t usedBytes() const noexcept;

  /// @brief Calculate swap used (total - free).
  [[nodiscard]] std::uint64_t swapUsedBytes() const noexcept;

  /// @brief Get memory utilization percentage (0-100).
  [[nodiscard]] double utilizationPercent() const noexcept;

  /// @brief Get swap utilization percentage (0-100).
  [[nodiscard]] double swapUtilizationPercent() const noexcept;

  /// @brief Check if THP is enabled (not "never").
  [[nodiscard]] bool isTHPEnabled() const noexcept;

  /// @brief Check if swappiness is low (RT-friendly, <= 10).
  [[nodiscard]] bool isSwappinessLow() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect memory statistics and VM policies.
 * @return Populated MemoryStats snapshot.
 * @note RT-safe: Bounded file reads, fixed-size output.
 *
 * Sources:
 *  - /proc/meminfo - RAM and swap usage
 *  - /proc/sys/vm/swappiness - Swap tendency (0-100)
 *  - /proc/sys/vm/zone_reclaim_mode - NUMA zone reclaim policy
 *  - /proc/sys/vm/overcommit_memory - Memory overcommit policy
 *  - /sys/kernel/mm/transparent_hugepage/enabled - THP mode
 *  - /sys/kernel/mm/transparent_hugepage/defrag - THP defrag policy
 */
[[nodiscard]] MemoryStats getMemoryStats() noexcept;

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_STATS_HPP