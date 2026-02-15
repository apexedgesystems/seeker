#ifndef SEEKER_MEMORY_PAGE_SIZES_HPP
#define SEEKER_MEMORY_PAGE_SIZES_HPP
/**
 * @file PageSizes.hpp
 * @brief Base page size and available hugepage sizes (Linux).
 * @note Linux-only. Reads /sys/kernel/mm/hugepages/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of distinct hugepage sizes supported.
inline constexpr std::size_t MAX_HUGEPAGE_SIZES = 8;

/* ----------------------------- PageSizes ----------------------------- */

/**
 * @brief Page size information snapshot.
 *
 * Contains the system's base page size and all available hugepage sizes.
 * Common hugepage sizes on x86_64:
 *  - 2 MiB (2097152 bytes) - most common
 *  - 1 GiB (1073741824 bytes) - requires CPU support and boot-time allocation
 */
struct PageSizes {
  std::uint64_t basePageBytes{0};                ///< Base page size from getpagesize()
  std::uint64_t hugeSizes[MAX_HUGEPAGE_SIZES]{}; ///< Available hugepage sizes (bytes)
  std::size_t hugeSizeCount{0};                  ///< Valid entries in hugeSizes[]

  /// @brief Check if a specific hugepage size is available.
  /// @param sizeBytes Size in bytes to check (e.g., 2097152 for 2MiB).
  /// @return True if the size is available.
  [[nodiscard]] bool hasHugePageSize(std::uint64_t sizeBytes) const noexcept;

  /// @brief Check if any hugepages are available.
  [[nodiscard]] bool hasHugePages() const noexcept;

  /// @brief Get the largest available hugepage size.
  /// @return Largest size in bytes, or 0 if no hugepages available.
  [[nodiscard]] std::uint64_t largestHugePageSize() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect page sizes from system.
 * @return Populated PageSizes with base page and available hugepage sizes.
 * @note RT-safe: Bounded syscall and directory scan, fixed-size output.
 *
 * Sources:
 *  - getpagesize(2) for base page size
 *  - /sys/kernel/mm/hugepages/hugepages-*kB directories for hugepage sizes
 */
[[nodiscard]] PageSizes getPageSizes() noexcept;

/**
 * @brief Format bytes as human-readable size string.
 * @param bytes Size in bytes.
 * @return Formatted string (e.g., "4 KiB", "2 MiB", "1 GiB").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_PAGE_SIZES_HPP