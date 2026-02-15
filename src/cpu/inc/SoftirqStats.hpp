#ifndef SEEKER_CPU_SOFTIRQ_STATS_HPP
#define SEEKER_CPU_SOFTIRQ_STATS_HPP
/**
 * @file SoftirqStats.hpp
 * @brief Per-core software interrupt statistics from /proc/softirqs.
 * @note Linux-only. Reads /proc/softirqs for softirq distribution.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect network softirq storms (NET_RX, NET_TX)
 *  - Monitor timer overhead (TIMER, HRTIMER)
 *  - Identify scheduling overhead (SCHED, RCU)
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace cpu {

/// Maximum CPUs for softirq tracking.
inline constexpr std::size_t SOFTIRQ_MAX_CPUS = 256;

/// Maximum softirq types.
inline constexpr std::size_t SOFTIRQ_MAX_TYPES = 16;

/// Maximum softirq type name length.
inline constexpr std::size_t SOFTIRQ_NAME_SIZE = 16;

/* ----------------------------- Softirq Types ----------------------------- */

/**
 * @brief Well-known softirq types (matches kernel order).
 */
enum class SoftirqType : std::uint8_t {
  HI = 0,   ///< High-priority tasklets
  TIMER,    ///< Timer interrupts
  NET_TX,   ///< Network transmit
  NET_RX,   ///< Network receive
  BLOCK,    ///< Block device
  IRQ_POLL, ///< IRQ polling
  TASKLET,  ///< Regular tasklets
  SCHED,    ///< Scheduler
  HRTIMER,  ///< High-resolution timers
  RCU,      ///< Read-copy-update
  UNKNOWN   ///< Unknown type
};

/// Convert softirq type to string.
[[nodiscard]] const char* softirqTypeName(SoftirqType type) noexcept;

/* ----------------------------- Single Softirq Type ----------------------------- */

/**
 * @brief Statistics for a single softirq type across all CPUs.
 */
struct SoftirqTypeStats {
  std::array<char, SOFTIRQ_NAME_SIZE> name{}; ///< Type name (e.g., "NET_RX")
  SoftirqType type{SoftirqType::UNKNOWN};     ///< Parsed type enum
  std::uint64_t perCore[SOFTIRQ_MAX_CPUS]{};  ///< Per-core counts
  std::uint64_t total{0};                     ///< Sum across all cores
};

/* ----------------------------- Snapshot ----------------------------- */

/**
 * @brief Snapshot of all softirq statistics.
 */
struct SoftirqSnapshot {
  SoftirqTypeStats types[SOFTIRQ_MAX_TYPES]{}; ///< Per-type statistics
  std::size_t typeCount{0};                    ///< Valid entries in types[]
  std::size_t cpuCount{0};                     ///< Number of CPUs
  std::uint64_t timestampNs{0};                ///< Monotonic timestamp (ns)

  /// @brief Get total softirqs for a specific CPU.
  [[nodiscard]] std::uint64_t totalForCpu(std::size_t cpu) const noexcept;

  /// @brief Get counts for a specific softirq type.
  /// @return Pointer to stats, or nullptr if not found.
  [[nodiscard]] const SoftirqTypeStats* getType(SoftirqType type) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Delta ----------------------------- */

/**
 * @brief Delta statistics between two snapshots.
 */
struct SoftirqDelta {
  std::array<char, SOFTIRQ_NAME_SIZE> names[SOFTIRQ_MAX_TYPES]{};
  SoftirqType typeEnums[SOFTIRQ_MAX_TYPES]{};
  std::uint64_t perCoreDelta[SOFTIRQ_MAX_TYPES][SOFTIRQ_MAX_CPUS]{};
  std::uint64_t typeTotals[SOFTIRQ_MAX_TYPES]{};
  std::size_t typeCount{0};
  std::size_t cpuCount{0};
  std::uint64_t intervalNs{0};

  /// @brief Get delta count for a CPU across all softirq types.
  [[nodiscard]] std::uint64_t totalForCpu(std::size_t cpu) const noexcept;

  /// @brief Get softirq rate (per second) for a CPU.
  [[nodiscard]] double rateForCpu(std::size_t cpu) const noexcept;

  /// @brief Get rate for a specific softirq type across all CPUs.
  [[nodiscard]] double rateForType(SoftirqType type) const noexcept;

  /// @brief Human-readable summary with rates.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Capture current softirq statistics from /proc/softirqs.
 * @return Snapshot with per-core, per-type counts.
 * @note RT-safe: Single file read, fixed-size arrays, bounded parsing.
 */
[[nodiscard]] SoftirqSnapshot getSoftirqSnapshot() noexcept;

/**
 * @brief Compute delta between two softirq snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with per-type, per-core deltas.
 * @note RT-safe: Pure computation, no I/O.
 */
[[nodiscard]] SoftirqDelta computeSoftirqDelta(const SoftirqSnapshot& before,
                                               const SoftirqSnapshot& after) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_SOFTIRQ_STATS_HPP