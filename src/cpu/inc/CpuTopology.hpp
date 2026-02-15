#ifndef SEEKER_CPU_TOPOLOGY_HPP
#define SEEKER_CPU_TOPOLOGY_HPP
/**
 * @file CpuTopology.hpp
 * @brief CPU topology snapshot: sockets, cores, threads, NUMA, caches.
 * @note Linux-only. Reads /sys/devices/system/cpu/ and /sys/devices/system/node/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum cache type/policy string length.
inline constexpr std::size_t CACHE_STRING_SIZE = 16;

/* ----------------------------- CacheInfo ----------------------------- */

/**
 * @brief Cache level descriptor (L1/L2/L3).
 */
struct CacheInfo {
  int level{0};                                 ///< 1=L1, 2=L2, 3=L3, etc.
  std::array<char, CACHE_STRING_SIZE> type{};   ///< "Data", "Instruction", "Unified"
  std::uint64_t sizeBytes{0};                   ///< Cache size in bytes
  std::uint64_t lineBytes{0};                   ///< Cache line size in bytes
  int associativity{0};                         ///< Ways of associativity; 0 if unknown
  std::array<char, CACHE_STRING_SIZE> policy{}; ///< Write policy if known

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ThreadInfo ----------------------------- */

/**
 * @brief Per-logical-CPU thread descriptor.
 */
struct ThreadInfo {
  int cpuId{-1};     ///< Linux logical CPU id (0-based)
  int coreId{-1};    ///< Physical core id within package
  int packageId{-1}; ///< Socket/package id
  int numaNode{-1};  ///< NUMA node id (-1 if unknown)
};

/* ----------------------------- CoreInfo ----------------------------- */

/**
 * @brief Physical core descriptor with sibling threads and caches.
 */
struct CoreInfo {
  int coreId{-1};                  ///< Physical core id
  int packageId{-1};               ///< Socket/package id
  int numaNode{-1};                ///< NUMA node id (-1 if unknown)
  std::vector<int> threadCpuIds{}; ///< Sibling logical CPU ids (HT/SMT)
  std::vector<CacheInfo> caches{}; ///< Per-core caches (L1/L2)

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CpuTopology ----------------------------- */

/**
 * @brief High-level CPU topology snapshot.
 */
struct CpuTopology {
  int packages{0};                       ///< Socket/package count
  int physicalCores{0};                  ///< Total physical core count
  int logicalCpus{0};                    ///< Total logical CPU (thread) count
  int numaNodes{0};                      ///< NUMA node count (0 if unknown)
  std::vector<CoreInfo> cores{};         ///< Per-physical-core details
  std::vector<CacheInfo> sharedCaches{}; ///< Package-level shared caches (L3+)

  /// @brief Compute SMT (threads per core) ratio.
  [[nodiscard]] int threadsPerCore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect CPU topology from sysfs.
 * @return Populated topology; empty/zero on failure.
 * @note NOT RT-safe: Allocates vectors, performs file I/O.
 *       Missing files are tolerated; fields default to zero/empty.
 */
[[nodiscard]] CpuTopology getCpuTopology() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_TOPOLOGY_HPP