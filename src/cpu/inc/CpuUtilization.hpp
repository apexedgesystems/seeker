#ifndef SEEKER_CPU_UTILIZATION_HPP
#define SEEKER_CPU_UTILIZATION_HPP
/**
 * @file CpuUtilization.hpp
 * @brief Per-core CPU utilization snapshots and delta computation.
 * @note Linux-only. Reads /proc/stat for CPU time breakdown.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Design: Snapshot + delta approach for RT-safe monitoring.
 *  - getCpuUtilizationSnapshot() captures raw jiffies (RT-safe)
 *  - computeUtilizationDelta() computes percentages (pure function)
 *  - Caller controls sampling interval
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/cpu/inc/Affinity.hpp"

namespace seeker {

namespace cpu {

/* ----------------------------- Raw Counters ----------------------------- */

/**
 * @brief Raw CPU time counters from /proc/stat (in jiffies).
 *
 * All CpuSet fields use the same MAX_CPUS limit as Affinity.hpp.
 * Fields match /proc/stat columns:
 *   user nice system idle iowait irq softirq steal guest guest_nice
 *
 * All values are cumulative since boot.
 */
struct CpuTimeCounters {
  std::uint64_t user{0};      ///< Time in user mode
  std::uint64_t nice{0};      ///< Time in user mode with low priority
  std::uint64_t system{0};    ///< Time in kernel mode
  std::uint64_t idle{0};      ///< Time in idle task
  std::uint64_t iowait{0};    ///< Time waiting for I/O
  std::uint64_t irq{0};       ///< Time servicing hardware interrupts
  std::uint64_t softirq{0};   ///< Time servicing software interrupts
  std::uint64_t steal{0};     ///< Time stolen by hypervisor
  std::uint64_t guest{0};     ///< Time running guest OS
  std::uint64_t guestNice{0}; ///< Time running niced guest OS

  /// Total time across all fields.
  [[nodiscard]] std::uint64_t total() const noexcept;

  /// Active time (total minus idle and iowait).
  [[nodiscard]] std::uint64_t active() const noexcept;
};

/* ----------------------------- Snapshot ----------------------------- */

/**
 * @brief Snapshot of CPU time counters for all CPUs.
 */
struct CpuUtilizationSnapshot {
  CpuTimeCounters aggregate;         ///< Combined counters for all CPUs
  CpuTimeCounters perCore[MAX_CPUS]; ///< Per-core counters (indexed by CPU id)
  std::size_t coreCount{0};          ///< Valid entries in perCore[]
  std::uint64_t timestampNs{0};      ///< Monotonic timestamp (ns)

  /// @brief Human-readable summary of raw counters.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Utilization Percentages ----------------------------- */

/**
 * @brief CPU utilization percentages (0-100 scale).
 */
struct CpuUtilizationPercent {
  double user{0.0};      ///< User mode percentage
  double nice{0.0};      ///< Nice user mode percentage
  double system{0.0};    ///< Kernel mode percentage
  double idle{0.0};      ///< Idle percentage
  double iowait{0.0};    ///< I/O wait percentage
  double irq{0.0};       ///< Hardware IRQ percentage
  double softirq{0.0};   ///< Software IRQ percentage
  double steal{0.0};     ///< Hypervisor steal percentage
  double guest{0.0};     ///< Guest OS percentage
  double guestNice{0.0}; ///< Niced guest percentage

  /// Combined active usage (excludes idle and iowait).
  [[nodiscard]] double active() const noexcept;
};

/**
 * @brief Delta result with utilization percentages for all CPUs.
 */
struct CpuUtilizationDelta {
  CpuUtilizationPercent aggregate;         ///< Combined utilization
  CpuUtilizationPercent perCore[MAX_CPUS]; ///< Per-core utilization
  std::size_t coreCount{0};                ///< Valid entries in perCore[]
  std::uint64_t intervalNs{0};             ///< Time between snapshots (ns)

  /// @brief Human-readable summary with percentages.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Capture current CPU time counters from /proc/stat.
 * @return Snapshot with raw jiffies for all CPUs.
 * @note RT-safe: Single file read, no allocation, bounded parsing.
 */
[[nodiscard]] CpuUtilizationSnapshot getCpuUtilizationSnapshot() noexcept;

/**
 * @brief Compute utilization percentages from two snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with percentages (0-100 scale).
 * @note RT-safe: Pure computation, no I/O, no allocation.
 *
 * Returns zero percentages if interval is zero or counters wrapped.
 */
[[nodiscard]] CpuUtilizationDelta
computeUtilizationDelta(const CpuUtilizationSnapshot& before,
                        const CpuUtilizationSnapshot& after) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_UTILIZATION_HPP