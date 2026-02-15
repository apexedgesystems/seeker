#ifndef SEEKER_MEMORY_HUGEPAGE_STATUS_HPP
#define SEEKER_MEMORY_HUGEPAGE_STATUS_HPP
/**
 * @file HugepageStatus.hpp
 * @brief Hugepage allocation status: per-size and per-NUMA (Linux).
 * @note Linux-only. Reads /sys/kernel/mm/hugepages/ and /sys/devices/system/node/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Maximum hugepage sizes tracked.
inline constexpr std::size_t HP_MAX_SIZES = 8;

/// Maximum NUMA nodes for per-node hugepage tracking.
inline constexpr std::size_t HP_MAX_NUMA_NODES = 64;

/* ----------------------------- HugepageSizeStatus ----------------------------- */

/**
 * @brief Allocation status for a single hugepage size.
 */
struct HugepageSizeStatus {
  std::uint64_t pageSize{0}; ///< Page size in bytes (e.g., 2MiB, 1GiB)
  std::uint64_t total{0};    ///< nr_hugepages: Total configured pages
  std::uint64_t free{0};     ///< free_hugepages: Currently free pages
  std::uint64_t reserved{0}; ///< resv_hugepages: Reserved but not allocated
  std::uint64_t surplus{0};  ///< surplus_hugepages: Allocated beyond nr_hugepages

  /// @brief Calculate pages currently in use (total + surplus - free).
  [[nodiscard]] std::uint64_t used() const noexcept;

  /// @brief Calculate total bytes reserved by this pool.
  [[nodiscard]] std::uint64_t totalBytes() const noexcept;

  /// @brief Calculate free bytes in this pool.
  [[nodiscard]] std::uint64_t freeBytes() const noexcept;

  /// @brief Calculate used bytes in this pool.
  [[nodiscard]] std::uint64_t usedBytes() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- HugepageNodeStatus ----------------------------- */

/**
 * @brief Per-NUMA node hugepage allocation for a specific size.
 */
struct HugepageNodeStatus {
  int nodeId{-1};           ///< NUMA node ID
  std::uint64_t total{0};   ///< nr_hugepages on this node
  std::uint64_t free{0};    ///< free_hugepages on this node
  std::uint64_t surplus{0}; ///< surplus_hugepages on this node
};

/* ----------------------------- HugepageStatus ----------------------------- */

/**
 * @brief Complete hugepage status snapshot.
 */
struct HugepageStatus {
  /// Per-size global allocation status
  HugepageSizeStatus sizes[HP_MAX_SIZES]{};
  std::size_t sizeCount{0};

  /// Per-NUMA node allocation (indexed as [sizeIdx][nodeIdx])
  HugepageNodeStatus perNode[HP_MAX_SIZES][HP_MAX_NUMA_NODES]{};
  std::size_t nodeCount{0}; ///< Number of NUMA nodes with hugepage info

  /// @brief Check if any hugepages are configured.
  [[nodiscard]] bool hasHugepages() const noexcept;

  /// @brief Get total hugepage memory across all sizes.
  [[nodiscard]] std::uint64_t totalBytes() const noexcept;

  /// @brief Get free hugepage memory across all sizes.
  [[nodiscard]] std::uint64_t freeBytes() const noexcept;

  /// @brief Get used hugepage memory across all sizes.
  [[nodiscard]] std::uint64_t usedBytes() const noexcept;

  /// @brief Find status for a specific page size.
  /// @return Pointer to status, or nullptr if not found.
  [[nodiscard]] const HugepageSizeStatus* findSize(std::uint64_t pageSize) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect hugepage allocation status from sysfs.
 * @return Populated HugepageStatus; sizeCount=0 if no hugepages configured.
 * @note NOT RT-safe: Scans sysfs directories, performs file I/O.
 *
 * Sources:
 *  - /sys/kernel/mm/hugepages/hugepages-NkB/ (global per-size stats)
 *  - /sys/devices/system/node/nodeN/hugepages/hugepages-NkB/ (per-NUMA stats)
 */
[[nodiscard]] HugepageStatus getHugepageStatus() noexcept;

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_HUGEPAGE_STATUS_HPP