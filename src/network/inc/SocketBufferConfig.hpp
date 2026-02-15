#ifndef SEEKER_NETWORK_SOCKET_BUFFER_CONFIG_HPP
#define SEEKER_NETWORK_SOCKET_BUFFER_CONFIG_HPP
/**
 * @file SocketBufferConfig.hpp
 * @brief System-wide socket buffer limits and TCP configuration.
 * @note Linux-only. Reads /proc/sys/net/ for network tunables.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides socket buffer limits, TCP settings, and busy-polling configuration
 * relevant for low-latency and high-throughput networking.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- Constants ----------------------------- */

/// Congestion control algorithm name size.
inline constexpr std::size_t CC_STRING_SIZE = 32;

/* ----------------------------- SocketBufferConfig ----------------------------- */

/**
 * @brief System socket buffer and TCP configuration.
 *
 * Captures kernel tunables from /proc/sys/net/ that affect network performance.
 * A value of -1 indicates the parameter could not be read.
 */
struct SocketBufferConfig {
  /* Core socket buffers (/proc/sys/net/core/) */
  std::int64_t rmemDefault{-1};      ///< Default receive buffer size (bytes)
  std::int64_t rmemMax{-1};          ///< Maximum receive buffer size (bytes)
  std::int64_t wmemDefault{-1};      ///< Default send buffer size (bytes)
  std::int64_t wmemMax{-1};          ///< Maximum send buffer size (bytes)
  std::int64_t optmemMax{-1};        ///< Maximum ancillary buffer size (bytes)
  std::int64_t netdevMaxBacklog{-1}; ///< Input queue length for incoming packets
  std::int64_t netdevBudget{-1};     ///< NAPI polling budget per softirq

  /* TCP buffers (/proc/sys/net/ipv4/tcp_rmem, tcp_wmem) */
  std::int64_t tcpRmemMin{-1};     ///< TCP receive buffer minimum
  std::int64_t tcpRmemDefault{-1}; ///< TCP receive buffer default
  std::int64_t tcpRmemMax{-1};     ///< TCP receive buffer maximum
  std::int64_t tcpWmemMin{-1};     ///< TCP send buffer minimum
  std::int64_t tcpWmemDefault{-1}; ///< TCP send buffer default
  std::int64_t tcpWmemMax{-1};     ///< TCP send buffer maximum

  /* TCP tuning parameters */
  std::array<char, CC_STRING_SIZE> tcpCongestionControl{}; ///< CC algorithm (cubic, bbr, etc.)
  int tcpTimestamps{-1};                                   ///< TCP timestamps enabled (0/1)
  int tcpSack{-1};                                         ///< Selective ACK enabled (0/1)
  int tcpWindowScaling{-1};                                ///< Window scaling enabled (0/1)
  int tcpLowLatency{-1};    ///< Low latency mode (deprecated but still present)
  int tcpNoMetricsSave{-1}; ///< Don't cache TCP metrics (useful for benchmarks)

  /* Busy polling (/proc/sys/net/core/busy_*) */
  int busyRead{-1}; ///< Busy polling read timeout (microseconds, 0=disabled)
  int busyPoll{-1}; ///< Busy polling poll timeout (microseconds, 0=disabled)

  /* UDP parameters */
  std::int64_t udpRmemMin{-1}; ///< UDP receive buffer minimum
  std::int64_t udpWmemMin{-1}; ///< UDP send buffer minimum

  /// @brief Check if busy polling is enabled.
  [[nodiscard]] bool isBusyPollingEnabled() const noexcept;

  /// @brief Check if configuration is suitable for low latency.
  /// @return true if busy polling enabled and buffers are reasonably sized.
  [[nodiscard]] bool isLowLatencyConfig() const noexcept;

  /// @brief Check if configuration is suitable for high throughput.
  /// @return true if large buffers are available.
  [[nodiscard]] bool isHighThroughputConfig() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query system socket buffer configuration.
 * @return Populated SocketBufferConfig from /proc/sys/net/.
 * @note RT-safe: Bounded file reads, no allocation.
 *
 * Sources:
 *  - /proc/sys/net/core/rmem_default, rmem_max, wmem_default, wmem_max
 *  - /proc/sys/net/core/optmem_max, netdev_max_backlog, netdev_budget
 *  - /proc/sys/net/core/busy_read, busy_poll
 *  - /proc/sys/net/ipv4/tcp_rmem, tcp_wmem (space-separated: min default max)
 *  - /proc/sys/net/ipv4/tcp_congestion_control
 *  - /proc/sys/net/ipv4/tcp_timestamps, tcp_sack, tcp_window_scaling
 *  - /proc/sys/net/ipv4/udp_rmem_min, udp_wmem_min
 */
[[nodiscard]] SocketBufferConfig getSocketBufferConfig() noexcept;

/**
 * @brief Format buffer size as human-readable string.
 * @param bytes Size in bytes (-1 if unknown).
 * @return Formatted string (e.g., "16 MiB", "256 KiB", "unknown").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatBufferSize(std::int64_t bytes);

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_SOCKET_BUFFER_CONFIG_HPP
