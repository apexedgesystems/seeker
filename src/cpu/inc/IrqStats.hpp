#ifndef SEEKER_CPU_IRQ_STATS_HPP
#define SEEKER_CPU_IRQ_STATS_HPP
/**
 * @file IrqStats.hpp
 * @brief Per-core interrupt statistics from /proc/interrupts.
 * @note Linux-only. Reads /proc/interrupts for IRQ distribution.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify isolated cores receive no IRQs
 *  - Detect IRQ storms on specific cores
 *  - Monitor interrupt affinity compliance
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace cpu {

/// Maximum supported CPUs for per-core IRQ tracking.
inline constexpr std::size_t IRQ_MAX_CPUS = 256;

/// Maximum number of IRQ lines to track.
inline constexpr std::size_t IRQ_MAX_LINES = 512;

/// Maximum IRQ name length.
inline constexpr std::size_t IRQ_NAME_SIZE = 32;

/// Maximum IRQ description length.
inline constexpr std::size_t IRQ_DESC_SIZE = 64;

/* ----------------------------- Single IRQ Line ----------------------------- */

/**
 * @brief Statistics for a single IRQ line across all CPUs.
 */
struct IrqLineStats {
  std::array<char, IRQ_NAME_SIZE> name{}; ///< IRQ number/name (e.g., "0", "NMI", "LOC")
  std::array<char, IRQ_DESC_SIZE> desc{}; ///< Description (e.g., "timer", "eth0")
  std::uint64_t perCore[IRQ_MAX_CPUS]{};  ///< Per-core interrupt counts
  std::uint64_t total{0};                 ///< Sum across all cores

  /// @brief Human-readable one-line summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString(std::size_t coreCount) const;
};

/* ----------------------------- Snapshot ----------------------------- */

/**
 * @brief Snapshot of all IRQ statistics.
 */
struct IrqSnapshot {
  IrqLineStats lines[IRQ_MAX_LINES]; ///< Per-IRQ statistics
  std::size_t lineCount{0};          ///< Valid entries in lines[]
  std::size_t coreCount{0};          ///< Number of CPUs in the snapshot
  std::uint64_t timestampNs{0};      ///< Monotonic timestamp (ns)

  /// @brief Get total interrupts across all IRQs for a specific core.
  [[nodiscard]] std::uint64_t totalForCore(std::size_t core) const noexcept;

  /// @brief Get total interrupts across all cores for all IRQs.
  [[nodiscard]] std::uint64_t totalAllCores() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Delta ----------------------------- */

/**
 * @brief Delta statistics between two snapshots.
 */
struct IrqDelta {
  std::array<char, IRQ_NAME_SIZE> names[IRQ_MAX_LINES]{};    ///< IRQ names (copied)
  std::uint64_t perCoreDelta[IRQ_MAX_LINES][IRQ_MAX_CPUS]{}; ///< Per-IRQ, per-core deltas
  std::uint64_t lineTotals[IRQ_MAX_LINES]{};                 ///< Per-IRQ total deltas
  std::size_t lineCount{0};                                  ///< Valid IRQ lines
  std::size_t coreCount{0};                                  ///< Number of CPUs
  std::uint64_t intervalNs{0};                               ///< Time between snapshots

  /// @brief Get delta interrupts for a specific core across all IRQs.
  [[nodiscard]] std::uint64_t totalForCore(std::size_t core) const noexcept;

  /// @brief Get interrupt rate (per second) for a specific core.
  [[nodiscard]] double rateForCore(std::size_t core) const noexcept;

  /// @brief Human-readable summary with rates.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Capture current IRQ statistics from /proc/interrupts.
 * @return Snapshot with per-core interrupt counts.
 * @note RT-safe: Single file read, fixed-size arrays, bounded parsing.
 */
[[nodiscard]] IrqSnapshot getIrqSnapshot() noexcept;

/**
 * @brief Compute delta between two IRQ snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with per-core interrupt deltas.
 * @note RT-safe: Pure computation, no I/O, no allocation.
 */
[[nodiscard]] IrqDelta computeIrqDelta(const IrqSnapshot& before,
                                       const IrqSnapshot& after) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_IRQ_STATS_HPP