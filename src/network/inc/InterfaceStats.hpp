#ifndef SEEKER_NETWORK_INTERFACE_STATS_HPP
#define SEEKER_NETWORK_INTERFACE_STATS_HPP
/**
 * @file InterfaceStats.hpp
 * @brief Per-interface packet/byte counters with snapshot and delta computation.
 * @note Linux-only. Reads /sys/class/net/\<if\>/statistics/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Design: Snapshot + delta approach for RT-safe monitoring.
 *  - getInterfaceStatsSnapshot() captures raw counters (RT-safe)
 *  - computeStatsDelta() computes rates (pure function, RT-safe)
 *  - Caller controls sampling interval
 */

#include "src/network/inc/InterfaceInfo.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- InterfaceCounters ----------------------------- */

/**
 * @brief Raw network interface counters from /sys/class/net/\<if\>/statistics/.
 *
 * All values are cumulative since boot (or interface creation).
 * Counter wrapping is possible on 32-bit systems for high-speed interfaces.
 */
struct InterfaceCounters {
  std::array<char, IF_NAME_SIZE> ifname{}; ///< Interface name

  std::uint64_t rxBytes{0};     ///< Total bytes received
  std::uint64_t txBytes{0};     ///< Total bytes transmitted
  std::uint64_t rxPackets{0};   ///< Total packets received
  std::uint64_t txPackets{0};   ///< Total packets transmitted
  std::uint64_t rxErrors{0};    ///< Receive errors
  std::uint64_t txErrors{0};    ///< Transmit errors
  std::uint64_t rxDropped{0};   ///< Receive drops (no buffer space)
  std::uint64_t txDropped{0};   ///< Transmit drops
  std::uint64_t collisions{0};  ///< Collision count (half-duplex)
  std::uint64_t rxMulticast{0}; ///< Multicast packets received

  /// @brief Total errors (rx + tx).
  [[nodiscard]] std::uint64_t totalErrors() const noexcept;

  /// @brief Total drops (rx + tx).
  [[nodiscard]] std::uint64_t totalDrops() const noexcept;

  /// @brief Check if any errors or drops have occurred.
  [[nodiscard]] bool hasIssues() const noexcept;
};

/* ----------------------------- InterfaceStatsSnapshot ----------------------------- */

/**
 * @brief Snapshot of counters for all interfaces.
 */
struct InterfaceStatsSnapshot {
  InterfaceCounters interfaces[MAX_INTERFACES]{}; ///< Per-interface counters
  std::size_t count{0};                           ///< Valid entries
  std::uint64_t timestampNs{0};                   ///< Monotonic timestamp (ns)

  /// @brief Find counters by interface name.
  [[nodiscard]] const InterfaceCounters* find(const char* ifname) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- InterfaceRates ----------------------------- */

/**
 * @brief Per-interface rate metrics computed from delta.
 */
struct InterfaceRates {
  std::array<char, IF_NAME_SIZE> ifname{}; ///< Interface name
  double durationSec{0.0};                 ///< Sample duration

  double rxBytesPerSec{0.0};    ///< Receive rate (bytes/sec)
  double txBytesPerSec{0.0};    ///< Transmit rate (bytes/sec)
  double rxPacketsPerSec{0.0};  ///< Receive packet rate
  double txPacketsPerSec{0.0};  ///< Transmit packet rate
  double rxErrorsPerSec{0.0};   ///< Error rate
  double txErrorsPerSec{0.0};   ///< Error rate
  double rxDroppedPerSec{0.0};  ///< Drop rate
  double txDroppedPerSec{0.0};  ///< Drop rate
  double collisionsPerSec{0.0}; ///< Collision rate

  /// @brief Receive rate in megabits per second.
  [[nodiscard]] double rxMbps() const noexcept;

  /// @brief Transmit rate in megabits per second.
  [[nodiscard]] double txMbps() const noexcept;

  /// @brief Combined throughput in megabits per second.
  [[nodiscard]] double totalMbps() const noexcept;

  /// @brief Check if errors are occurring.
  [[nodiscard]] bool hasErrors() const noexcept;

  /// @brief Check if drops are occurring.
  [[nodiscard]] bool hasDrops() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- InterfaceStatsDelta ----------------------------- */

/**
 * @brief Delta result with rates for all interfaces.
 */
struct InterfaceStatsDelta {
  InterfaceRates interfaces[MAX_INTERFACES]{}; ///< Per-interface rates
  std::size_t count{0};                        ///< Valid entries
  double durationSec{0.0};                     ///< Total sample duration

  /// @brief Find rates by interface name.
  [[nodiscard]] const InterfaceRates* find(const char* ifname) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Capture counters for a single interface.
 * @param ifname Interface name.
 * @return Populated counters, or zeroed if interface not found.
 * @note RT-safe: Bounded file reads, no allocation.
 */
[[nodiscard]] InterfaceCounters getInterfaceCounters(const char* ifname) noexcept;

/**
 * @brief Capture counters for all interfaces.
 * @return Snapshot with counters for all discovered interfaces.
 * @note NOT RT-safe: Directory enumeration.
 */
[[nodiscard]] InterfaceStatsSnapshot getInterfaceStatsSnapshot() noexcept;

/**
 * @brief Capture counters for specific interface.
 * @param ifname Interface name to capture.
 * @return Snapshot with single interface counters.
 * @note RT-safe: Bounded file reads, no allocation.
 */
[[nodiscard]] InterfaceStatsSnapshot getInterfaceStatsSnapshot(const char* ifname) noexcept;

/**
 * @brief Compute rates from two snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with rates for matching interfaces.
 * @note RT-safe: Pure computation, no I/O, no allocation.
 *
 * Only interfaces present in both snapshots are included.
 * Counter wrapping is detected and results in zero rates.
 */
[[nodiscard]] InterfaceStatsDelta computeStatsDelta(const InterfaceStatsSnapshot& before,
                                                    const InterfaceStatsSnapshot& after) noexcept;

/**
 * @brief Format rate as human-readable throughput.
 * @param bytesPerSec Rate in bytes per second.
 * @return Formatted string (e.g., "1.5 Gbps", "100 Mbps").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatThroughput(double bytesPerSec);

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_INTERFACE_STATS_HPP