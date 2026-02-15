#ifndef SEEKER_CPU_ISOLATION_HPP
#define SEEKER_CPU_ISOLATION_HPP
/**
 * @file CpuIsolation.hpp
 * @brief CPU isolation configuration for real-time systems.
 * @note Linux-only. Reads /sys/devices/system/cpu/ and /proc/cmdline.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Queries kernel boot parameters and runtime state for:
 *  - isolcpus: CPUs excluded from general scheduler
 *  - nohz_full: Tickless CPUs (no timer interrupts when single task running)
 *  - rcu_nocbs: CPUs with RCU callbacks offloaded to other CPUs
 *
 * These settings are critical for RT systems to minimize jitter on dedicated cores.
 */

#include "src/cpu/inc/Affinity.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace seeker {

namespace cpu {

/// Maximum kernel command line length to capture.
inline constexpr std::size_t CMDLINE_MAX_SIZE = 4096;

/* ----------------------------- Configuration Struct ----------------------------- */

/**
 * @brief CPU isolation configuration from kernel parameters.
 *
 * All CpuSet fields use the same MAX_CPUS limit as Affinity.hpp.
 * Empty sets indicate the feature is not configured.
 */
struct CpuIsolationConfig {
  CpuSet isolcpus{}; ///< CPUs isolated from scheduler (isolcpus= parameter)
  CpuSet nohzFull{}; ///< Tickless CPUs (nohz_full= parameter)
  CpuSet rcuNocbs{}; ///< RCU callback offload CPUs (rcu_nocbs= parameter)

  bool isolcpusManaged{false}; ///< True if isolcpus=managed_irq was specified
  bool nohzFullAll{false};     ///< True if nohz_full=all was specified

  /// @brief Check if a CPU has all three isolation features enabled.
  [[nodiscard]] bool isFullyIsolated(std::size_t cpuId) const noexcept;

  /// @brief Check if any isolation is configured.
  [[nodiscard]] bool hasAnyIsolation() const noexcept;

  /// @brief Get CPUs that have all three isolation features.
  [[nodiscard]] CpuSet getFullyIsolatedCpus() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Validation Result ----------------------------- */

/**
 * @brief Result of validating a CPU set against isolation config.
 */
struct IsolationValidation {
  CpuSet missingIsolcpus{}; ///< Requested CPUs not in isolcpus
  CpuSet missingNohzFull{}; ///< Requested CPUs not in nohz_full
  CpuSet missingRcuNocbs{}; ///< Requested CPUs not in rcu_nocbs

  /// @brief True if all requested CPUs have full isolation.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Human-readable validation report.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query current CPU isolation configuration.
 * @return Populated CpuIsolationConfig from sysfs and /proc/cmdline.
 * @note RT-safe: Bounded file reads, no allocation.
 *
 * Reads from:
 *  - /sys/devices/system/cpu/isolated
 *  - /sys/devices/system/cpu/nohz_full
 *  - /proc/cmdline (for rcu_nocbs and managed_irq flag)
 */
[[nodiscard]] CpuIsolationConfig getCpuIsolationConfig() noexcept;

/**
 * @brief Validate that a set of CPUs has proper RT isolation.
 * @param config Isolation configuration to check against.
 * @param rtCpus CPUs intended for RT workloads.
 * @return Validation result showing any missing isolation.
 * @note RT-safe: Pure computation, no I/O.
 */
[[nodiscard]] IsolationValidation validateIsolation(const CpuIsolationConfig& config,
                                                    const CpuSet& rtCpus) noexcept;

/**
 * @brief Parse a CPU list string (e.g., "0,2-4,6") into a CpuSet.
 * @param cpuList Kernel-style CPU list string.
 * @return Populated CpuSet, empty on parse failure.
 * @note RT-safe: No allocation, bounded parsing.
 *
 * Supported formats:
 *  - Single CPU: "3"
 *  - Range: "2-5"
 *  - List: "0,2,4"
 *  - Mixed: "0,2-4,6,8-10"
 */
[[nodiscard]] CpuSet parseCpuList(const char* cpuList) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_ISOLATION_HPP