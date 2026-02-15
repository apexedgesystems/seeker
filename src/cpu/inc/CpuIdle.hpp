#ifndef SEEKER_CPU_IDLE_HPP
#define SEEKER_CPU_IDLE_HPP
/**
 * @file CpuIdle.hpp
 * @brief CPU idle state (C-state) statistics from sysfs.
 * @note Linux-only. Reads /sys/devices/system/cpu/cpuN/cpuidle/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect deep C-states that add wake latency
 *  - Verify C-state disable settings
 *  - Monitor idle residency distribution
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace cpu {

/// Maximum C-states per CPU.
inline constexpr std::size_t IDLE_MAX_STATES = 16;

/// Maximum CPUs for idle tracking.
inline constexpr std::size_t IDLE_MAX_CPUS = 256;

/// Maximum state name length.
inline constexpr std::size_t IDLE_NAME_SIZE = 32;

/// Maximum state description length.
inline constexpr std::size_t IDLE_DESC_SIZE = 64;

/* ----------------------------- Single C-state ----------------------------- */

/**
 * @brief Statistics for a single C-state on a single CPU.
 */
struct CStateInfo {
  std::array<char, IDLE_NAME_SIZE> name{}; ///< State name (e.g., "POLL", "C1", "C6")
  std::array<char, IDLE_DESC_SIZE> desc{}; ///< Description (e.g., "MWAIT 0x00")
  std::uint32_t latencyUs{0};              ///< Exit latency in microseconds
  std::uint32_t residencyUs{0};            ///< Target residency in microseconds
  std::uint64_t usageCount{0};             ///< Number of times entered
  std::uint64_t timeUs{0};                 ///< Total time spent in this state (us)
  bool disabled{false};                    ///< True if state is disabled
};

/* ----------------------------- Per-CPU Idle Stats ----------------------------- */

/**
 * @brief Idle statistics for a single CPU.
 */
struct CpuIdleStats {
  int cpuId{-1};                        ///< CPU index
  CStateInfo states[IDLE_MAX_STATES]{}; ///< C-state info
  std::size_t stateCount{0};            ///< Valid entries in states[]

  /// @brief Get total idle time across all states (microseconds).
  [[nodiscard]] std::uint64_t totalIdleTimeUs() const noexcept;

  /// @brief Get deepest C-state that is not disabled.
  /// @return State index, or -1 if all disabled.
  [[nodiscard]] int deepestEnabledState() const noexcept;

  /// @brief Human-readable summary for this CPU.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- System-wide Snapshot ----------------------------- */

/**
 * @brief Snapshot of idle statistics for all CPUs.
 */
struct CpuIdleSnapshot {
  CpuIdleStats perCpu[IDLE_MAX_CPUS]{}; ///< Per-CPU idle stats
  std::size_t cpuCount{0};              ///< Valid entries in perCpu[]
  std::uint64_t timestampNs{0};         ///< Monotonic timestamp (ns)

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Delta ----------------------------- */

/**
 * @brief Delta statistics between two snapshots.
 */
struct CpuIdleDelta {
  /// Per-CPU, per-state usage delta (times entered).
  std::uint64_t usageDelta[IDLE_MAX_CPUS][IDLE_MAX_STATES]{};
  /// Per-CPU, per-state time delta (microseconds).
  std::uint64_t timeDeltaUs[IDLE_MAX_CPUS][IDLE_MAX_STATES]{};
  /// Number of C-states per CPU.
  std::size_t stateCount[IDLE_MAX_CPUS]{};
  std::size_t cpuCount{0};
  std::uint64_t intervalNs{0};

  /// @brief Get C-state residency percentage for a CPU.
  /// @param cpuId CPU index.
  /// @param stateIdx C-state index.
  /// @return Percentage of interval spent in this state.
  [[nodiscard]] double residencyPercent(std::size_t cpuId, std::size_t stateIdx) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Capture current C-state statistics from sysfs.
 * @return Snapshot with per-CPU, per-state idle info.
 * @note NOT RT-safe: Scans sysfs directories, file I/O per CPU per state.
 */
[[nodiscard]] CpuIdleSnapshot getCpuIdleSnapshot() noexcept;

/**
 * @brief Compute delta between two idle snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with usage and time changes.
 * @note RT-safe: Pure computation, no I/O.
 */
[[nodiscard]] CpuIdleDelta computeCpuIdleDelta(const CpuIdleSnapshot& before,
                                               const CpuIdleSnapshot& after) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_IDLE_HPP