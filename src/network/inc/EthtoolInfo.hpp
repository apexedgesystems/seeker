#ifndef SEEKER_NETWORK_ETHTOOL_INFO_HPP
#define SEEKER_NETWORK_ETHTOOL_INFO_HPP
/**
 * @file EthtoolInfo.hpp
 * @brief NIC driver features, ring buffers, interrupt coalescing, and offload settings.
 * @note Linux-only. Uses ethtool ioctl interface for NIC configuration queries.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides low-level NIC tuning information for RT network optimization:
 *  - Ring buffer sizes (affects latency vs throughput tradeoff)
 *  - Interrupt coalescing (critical for latency tuning)
 *  - Offload features (some add latency jitter)
 *  - Pause frame settings (can cause unexpected stalls)
 */

#include "src/network/inc/InterfaceInfo.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- Constants ----------------------------- */

// Note: MAX_INTERFACES and IF_NAME_SIZE are defined in InterfaceInfo.hpp

/// Maximum feature name length.
inline constexpr std::size_t FEATURE_NAME_SIZE = 48;

/// Maximum number of features to track per NIC.
inline constexpr std::size_t MAX_FEATURES = 64;

/// Coalescing threshold for low-latency classification (microseconds).
inline constexpr std::uint32_t LOW_LATENCY_USECS_THRESHOLD = 10;

/// Coalescing threshold for low-latency classification (frames).
inline constexpr std::uint32_t LOW_LATENCY_FRAMES_THRESHOLD = 4;

/// Ring buffer threshold for RT warning (entries).
inline constexpr std::uint32_t RT_RING_SIZE_WARN_THRESHOLD = 4096;

/* ----------------------------- RingBufferConfig ----------------------------- */

/**
 * @brief Ring buffer configuration for a NIC.
 *
 * Ring buffers hold packets between the NIC and kernel. Larger rings provide
 * more headroom for burst traffic but increase worst-case latency.
 */
struct RingBufferConfig {
  std::uint32_t rxPending{0};      ///< Current RX ring size (entries)
  std::uint32_t rxMax{0};          ///< Maximum RX ring size supported
  std::uint32_t txPending{0};      ///< Current TX ring size (entries)
  std::uint32_t txMax{0};          ///< Maximum TX ring size supported
  std::uint32_t rxMiniPending{0};  ///< Mini RX ring size (if supported)
  std::uint32_t rxMiniMax{0};      ///< Maximum mini RX ring size
  std::uint32_t rxJumboPending{0}; ///< Jumbo RX ring size (if supported)
  std::uint32_t rxJumboMax{0};     ///< Maximum jumbo RX ring size

  /// @brief Check if ring buffer query succeeded.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Check if RX ring is at maximum size.
  [[nodiscard]] bool isRxAtMax() const noexcept;

  /// @brief Check if TX ring is at maximum size.
  [[nodiscard]] bool isTxAtMax() const noexcept;

  /// @brief Check if ring sizes are RT-friendly (not excessively large).
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- CoalesceConfig ----------------------------- */

/**
 * @brief Interrupt coalescing settings for a NIC.
 *
 * Coalescing delays interrupts to batch multiple packets, reducing CPU overhead
 * but increasing latency. For RT systems, minimal coalescing is preferred.
 */
struct CoalesceConfig {
  std::uint32_t rxUsecs{0};     ///< RX interrupt delay (microseconds)
  std::uint32_t rxMaxFrames{0}; ///< RX frames before interrupt
  std::uint32_t txUsecs{0};     ///< TX interrupt delay (microseconds)
  std::uint32_t txMaxFrames{0}; ///< TX frames before interrupt

  std::uint32_t rxUsecsIrq{0}; ///< RX usecs while IRQ pending

  std::uint32_t rxMaxFramesIrq{0}; ///< RX frames while IRQ pending
  std::uint32_t txUsecsIrq{0};     ///< TX usecs while IRQ pending
  std::uint32_t txMaxFramesIrq{0}; ///< TX frames while IRQ pending

  std::uint32_t statsBlockUsecs{0}; ///< Stats block coalescing

  bool useAdaptiveRx{false};    ///< Adaptive RX coalescing enabled
  bool useAdaptiveTx{false};    ///< Adaptive TX coalescing enabled
  std::uint32_t pktRateLow{0};  ///< Low packet rate threshold
  std::uint32_t pktRateHigh{0}; ///< High packet rate threshold
  std::uint32_t rxUsecsLow{0};  ///< RX usecs at low rate
  std::uint32_t rxUsecsHigh{0}; ///< RX usecs at high rate
  std::uint32_t txUsecsLow{0};  ///< TX usecs at low rate
  std::uint32_t txUsecsHigh{0}; ///< TX usecs at high rate

  /// @brief Check if coalescing query succeeded.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Check if settings are low-latency optimized.
  [[nodiscard]] bool isLowLatency() const noexcept;

  /// @brief Check if adaptive coalescing is enabled (bad for RT).
  [[nodiscard]] bool hasAdaptive() const noexcept;

  /// @brief Check if settings are RT-friendly.
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- PauseConfig ----------------------------- */

/**
 * @brief Pause frame (flow control) settings.
 *
 * Pause frames can cause the NIC to stop transmitting, leading to
 * unpredictable latency spikes. Often disabled for RT applications.
 */
struct PauseConfig {
  bool autoneg{false}; ///< Pause auto-negotiated
  bool rxPause{false}; ///< RX pause enabled (honor incoming pause)
  bool txPause{false}; ///< TX pause enabled (send pause frames)

  /// @brief Check if any pause is enabled.
  [[nodiscard]] bool isEnabled() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- NicFeature ----------------------------- */

/**
 * @brief Single NIC feature (offload) state.
 */
struct NicFeature {
  std::array<char, FEATURE_NAME_SIZE> name{}; ///< Feature name
  bool available{false};                      ///< Driver supports this feature
  bool enabled{false};                        ///< Feature currently enabled
  bool requested{false};                      ///< User requested state
  bool fixed{false};                          ///< Cannot be changed (always on or off)
};

/* ----------------------------- NicFeatures ----------------------------- */

/**
 * @brief Collection of NIC features (offloads).
 */
struct NicFeatures {
  NicFeature features[MAX_FEATURES]{};
  std::size_t count{0};

  /// @brief Find feature by name.
  /// @param name Feature name to search for.
  /// @return Pointer to feature, or nullptr if not found.
  [[nodiscard]] const NicFeature* find(const char* name) const noexcept;

  /// @brief Check if a feature is enabled.
  /// @param name Feature name.
  /// @return true if feature exists and is enabled.
  [[nodiscard]] bool isEnabled(const char* name) const noexcept;

  /// @brief Count enabled features.
  [[nodiscard]] std::size_t countEnabled() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- EthtoolInfo ----------------------------- */

/**
 * @brief Complete ethtool information for a NIC.
 *
 * Aggregates ring buffer, coalescing, pause, and feature information
 * for comprehensive NIC tuning assessment.
 */
struct EthtoolInfo {
  std::array<char, IF_NAME_SIZE> ifname{}; ///< Interface name

  RingBufferConfig rings{};  ///< Ring buffer configuration
  CoalesceConfig coalesce{}; ///< Interrupt coalescing settings
  PauseConfig pause{};       ///< Pause frame settings
  NicFeatures features{};    ///< Offload features

  bool supportsEthtool{false}; ///< At least one ethtool query succeeded

  /* ----------------------------- Feature Helpers ----------------------------- */

  /// @brief Check for TCP segmentation offload.
  [[nodiscard]] bool hasTso() const noexcept;

  /// @brief Check for generic receive offload.
  [[nodiscard]] bool hasGro() const noexcept;

  /// @brief Check for generic segmentation offload.
  [[nodiscard]] bool hasGso() const noexcept;

  /// @brief Check for large receive offload (can add latency).
  [[nodiscard]] bool hasLro() const noexcept;

  /// @brief Check for RX checksum offload.
  [[nodiscard]] bool hasRxChecksum() const noexcept;

  /// @brief Check for TX checksum offload.
  [[nodiscard]] bool hasTxChecksum() const noexcept;

  /// @brief Check for scatter-gather support.
  [[nodiscard]] bool hasScatterGather() const noexcept;

  /* ----------------------------- RT Assessment ----------------------------- */

  /// @brief Check if overall config is RT-friendly.
  /// @return true if coalescing is low, no adaptive, rings reasonable.
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief RT score 0-100 based on tuning parameters.
  /// @return Score where 100 = optimal for RT, 0 = poor for RT.
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- EthtoolInfoList ----------------------------- */

/**
 * @brief Collection of ethtool info for multiple NICs.
 */
struct EthtoolInfoList {
  EthtoolInfo nics[MAX_INTERFACES]{};
  std::size_t count{0};

  /// @brief Find NIC by interface name.
  /// @param ifname Interface name to search for.
  /// @return Pointer to ethtool info, or nullptr if not found.
  [[nodiscard]] const EthtoolInfo* find(const char* ifname) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Human-readable summary of all NICs.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get ethtool information for a specific interface.
 * @param ifname Interface name (e.g., "eth0", "enp0s3").
 * @return Populated EthtoolInfo, or default-initialized if not found/unsupported.
 * @note RT-safe: Bounded ioctl calls, no allocation.
 *
 * Queries:
 *  - ETHTOOL_GRINGPARAM for ring buffer sizes
 *  - ETHTOOL_GCOALESCE for interrupt coalescing
 *  - ETHTOOL_GPAUSEPARAM for pause frame settings
 *  - ETHTOOL_GFEATURES for offload features
 */
[[nodiscard]] EthtoolInfo getEthtoolInfo(const char* ifname) noexcept;

/**
 * @brief Get ethtool information for all physical interfaces.
 * @return List of ethtool info for physical NICs.
 * @note NOT RT-safe: Directory enumeration over /sys/class/net/.
 */
[[nodiscard]] EthtoolInfoList getAllEthtoolInfo() noexcept;

/**
 * @brief Get ring buffer configuration for an interface.
 * @param ifname Interface name.
 * @return Ring buffer config (zeroed if unsupported).
 * @note RT-safe: Single ioctl call.
 */
[[nodiscard]] RingBufferConfig getRingBufferConfig(const char* ifname) noexcept;

/**
 * @brief Get coalescing configuration for an interface.
 * @param ifname Interface name.
 * @return Coalescing config (zeroed if unsupported).
 * @note RT-safe: Single ioctl call.
 */
[[nodiscard]] CoalesceConfig getCoalesceConfig(const char* ifname) noexcept;

/**
 * @brief Get pause frame configuration for an interface.
 * @param ifname Interface name.
 * @return Pause config (zeroed if unsupported).
 * @note RT-safe: Single ioctl call.
 */
[[nodiscard]] PauseConfig getPauseConfig(const char* ifname) noexcept;

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_ETHTOOL_INFO_HPP