#ifndef SEEKER_MEMORY_NUMA_TOPOLOGY_HPP
#define SEEKER_MEMORY_NUMA_TOPOLOGY_HPP
/**
 * @file NumaTopology.hpp
 * @brief NUMA topology: nodes, memory, CPU affinity, and distance matrix (Linux).
 * @note Linux-only. Reads /sys/devices/system/node/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t, std::uint8_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Maximum NUMA nodes supported.
inline constexpr std::size_t MAX_NUMA_NODES = 64;

/// Maximum CPUs per NUMA node.
inline constexpr std::size_t MAX_CPUS_PER_NODE = 256;

/// Distance value indicating no path or invalid.
inline constexpr std::uint8_t NUMA_DISTANCE_INVALID = 255;

/// Local node distance (same node).
inline constexpr std::uint8_t NUMA_DISTANCE_LOCAL = 10;

/* ----------------------------- NumaNodeInfo ----------------------------- */

/**
 * @brief Memory and CPU information for a single NUMA node.
 */
struct NumaNodeInfo {
  int nodeId{-1};                  ///< NUMA node ID (0-based)
  std::uint64_t totalBytes{0};     ///< Total memory on this node
  std::uint64_t freeBytes{0};      ///< Free memory on this node
  int cpuIds[MAX_CPUS_PER_NODE]{}; ///< CPU IDs belonging to this node
  std::size_t cpuCount{0};         ///< Number of valid entries in cpuIds[]

  /// @brief Calculate used memory (total - free).
  [[nodiscard]] std::uint64_t usedBytes() const noexcept;

  /// @brief Check if a CPU belongs to this node.
  [[nodiscard]] bool hasCpu(int cpuId) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- NumaTopology ----------------------------- */

/**
 * @brief Complete NUMA topology snapshot.
 */
struct NumaTopology {
  NumaNodeInfo nodes[MAX_NUMA_NODES]{}; ///< Per-node information
  std::size_t nodeCount{0};             ///< Valid entries in nodes[]

  /// Distance matrix: distance[from][to] = relative latency (10 = local).
  /// Values: 10 = local, 20-40 typical remote, 255 = invalid/no path.
  std::uint8_t distance[MAX_NUMA_NODES][MAX_NUMA_NODES]{};

  /// @brief Check if system has multiple NUMA nodes.
  [[nodiscard]] bool isNuma() const noexcept;

  /// @brief Get total memory across all nodes.
  [[nodiscard]] std::uint64_t totalMemoryBytes() const noexcept;

  /// @brief Get free memory across all nodes.
  [[nodiscard]] std::uint64_t freeMemoryBytes() const noexcept;

  /// @brief Find which node a CPU belongs to.
  /// @return Node index (0-based), or -1 if not found.
  [[nodiscard]] int findNodeForCpu(int cpuId) const noexcept;

  /// @brief Get distance between two nodes.
  /// @return Distance value, or NUMA_DISTANCE_INVALID if invalid indices.
  [[nodiscard]] std::uint8_t getDistance(std::size_t from, std::size_t to) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect NUMA topology from sysfs.
 * @return Populated NumaTopology; nodeCount=0 if NUMA not available.
 * @note NOT RT-safe: Scans sysfs directories, performs file I/O per node.
 *
 * Sources:
 *  - /sys/devices/system/node/nodeN/meminfo - Per-node memory
 *  - /sys/devices/system/node/nodeN/cpulist - CPUs on this node
 *  - /sys/devices/system/node/nodeN/distance - Distance to other nodes
 */
[[nodiscard]] NumaTopology getNumaTopology() noexcept;

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_NUMA_TOPOLOGY_HPP