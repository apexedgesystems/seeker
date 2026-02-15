#ifndef SEEKER_NETWORK_ISOLATION_HPP
#define SEEKER_NETWORK_ISOLATION_HPP
/**
 * @file NetworkIsolation.hpp
 * @brief NIC IRQ affinity analysis for real-time systems.
 * @note Linux-only. Reads /proc/interrupts, /sys/class/net/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Identifies network interface IRQs and their CPU affinity to detect
 * conflicts with isolated real-time cores.
 */

#include "src/network/inc/InterfaceInfo.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- Constants ----------------------------- */

/// Maximum IRQs per NIC (typical for multi-queue NICs).
inline constexpr std::size_t MAX_NIC_IRQS = 64;

/// Maximum CPUs for affinity bitmask.
inline constexpr std::size_t MAX_CPUS = 256;

/// IRQ name/description size.
inline constexpr std::size_t IRQ_NAME_SIZE = 64;

/* ----------------------------- NicIrqInfo ----------------------------- */

/**
 * @brief IRQ information for a single network interface.
 */
struct NicIrqInfo {
  std::array<char, IF_NAME_SIZE> ifname{}; ///< Interface name

  int irqNumbers[MAX_NIC_IRQS]{}; ///< IRQ numbers assigned to this NIC
  std::size_t irqCount{0};        ///< Valid entries in irqNumbers[]

  /// CPU affinity mask for each IRQ (bit N = CPU N)
  std::uint64_t affinity[MAX_NIC_IRQS]{};

  int numaNode{-1}; ///< NUMA node affinity (-1 if unknown)

  /// @brief Check if any IRQ is affine to given CPU.
  [[nodiscard]] bool hasIrqOnCpu(int cpu) const noexcept;

  /// @brief Check if any IRQ is affine to CPUs in mask.
  [[nodiscard]] bool hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept;

  /// @brief Get combined CPU mask for all IRQs.
  [[nodiscard]] std::uint64_t getCombinedAffinity() const noexcept;

  /// @brief Get list of CPUs that receive IRQs.
  [[nodiscard]] std::string getAffinityCpuList() const;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- NetworkIsolation ----------------------------- */

/**
 * @brief Network IRQ isolation status for all interfaces.
 */
struct NetworkIsolation {
  NicIrqInfo nics[MAX_INTERFACES]{}; ///< Per-NIC IRQ info
  std::size_t nicCount{0};           ///< Valid entries in nics[]

  /// @brief Find NIC by interface name.
  [[nodiscard]] const NicIrqInfo* find(const char* ifname) const noexcept;

  /// @brief Check if any NIC has IRQs on given CPU.
  [[nodiscard]] bool hasIrqOnCpu(int cpu) const noexcept;

  /// @brief Check if any NIC has IRQs on CPUs in mask.
  [[nodiscard]] bool hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept;

  /// @brief Get NICs with IRQs on given CPUs.
  /// @param cpuMask Bitmask of CPUs to check.
  /// @return Comma-separated list of conflicting interface names.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string getConflictingNics(std::uint64_t cpuMask) const;

  /// @brief Get total IRQ count across all NICs.
  [[nodiscard]] std::size_t getTotalIrqCount() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- IrqConflictResult ----------------------------- */

/**
 * @brief Result of checking for RT/network IRQ conflicts.
 */
struct IrqConflictResult {
  bool hasConflict{false};                              ///< True if any conflicts found
  std::size_t conflictCount{0};                         ///< Number of conflicting IRQs
  std::array<char, IF_NAME_SIZE * 4> conflictingNics{}; ///< Comma-separated NIC names
  int conflictingCpus[MAX_CPUS]{};                      ///< CPUs with conflicts
  std::size_t conflictingCpuCount{0};                   ///< Valid entries

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query NIC IRQ configuration for all interfaces.
 * @return Populated NetworkIsolation with IRQ numbers and affinities.
 * @note NOT RT-safe: Parses /proc/interrupts, directory enumeration.
 *
 * Sources:
 *  - /proc/interrupts (for IRQ numbers and device names)
 *  - /proc/irq/\<n\>/smp_affinity (for CPU affinity masks)
 *  - /sys/class/net/\<if\>/device/msi_irqs/ (for MSI IRQ enumeration)
 */
[[nodiscard]] NetworkIsolation getNetworkIsolation() noexcept;

/**
 * @brief Check for IRQ conflicts with RT CPUs.
 * @param ni Network isolation info.
 * @param rtCpuMask Bitmask of RT-isolated CPUs.
 * @return Conflict result with details of any conflicts found.
 * @note RT-safe: Pure computation, no I/O.
 */
[[nodiscard]] IrqConflictResult checkIrqConflict(const NetworkIsolation& ni,
                                                 std::uint64_t rtCpuMask) noexcept;

/**
 * @brief Parse CPU list string into bitmask.
 * @param cpuList Kernel-style CPU list (e.g., "0,2-4,6").
 * @return CPU bitmask (bit N = CPU N).
 * @note RT-safe: No allocation, bounded parsing.
 *
 * Supports formats: "3", "2-5", "0,2,4", "0,2-4,6,8-10"
 */
[[nodiscard]] std::uint64_t parseCpuListToMask(const char* cpuList) noexcept;

/**
 * @brief Format CPU mask as list string.
 * @param mask CPU bitmask.
 * @return Formatted string (e.g., "0,2-4,6").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatCpuMask(std::uint64_t mask);

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_ISOLATION_HPP