#ifndef SEEKER_TIMING_PTP_STATUS_HPP
#define SEEKER_TIMING_PTP_STATUS_HPP
/**
 * @file PtpStatus.hpp
 * @brief Detailed PTP (Precision Time Protocol) hardware clock status (Linux).
 * @note Linux-only. Reads /sys/class/ptp/, /dev/ptp*, PTP_CLOCK_GETCAPS ioctl.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides detailed PTP hardware clock information for precision timing:
 *  - Clock capabilities (alarms, external timestamps, periodic outputs, PPS)
 *  - Maximum frequency adjustment range
 *  - Cross-timestamping support for PHC-to-system synchronization
 *  - Associated network interface binding
 *
 * Essential for distributed RT systems requiring sub-microsecond synchronization.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t, std::int32_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/// Maximum PTP clocks to enumerate.
inline constexpr std::size_t PTP_MAX_CLOCKS = 8;

/// Maximum length for PTP device name.
inline constexpr std::size_t PTP_DEVICE_NAME_SIZE = 16;

/// Maximum length for PTP clock name/identity.
inline constexpr std::size_t PTP_CLOCK_DRIVER_NAME_SIZE = 64;

/// Maximum length for associated interface name.
inline constexpr std::size_t PTP_IFACE_NAME_SIZE = 16;

/* ----------------------------- PtpClockCaps ----------------------------- */

/**
 * @brief PTP hardware clock capabilities from PTP_CLOCK_GETCAPS ioctl.
 *
 * Describes what features the PTP hardware supports.
 */
struct PtpClockCaps {
  std::int32_t maxAdjPpb{0};     ///< Maximum frequency adjustment (parts per billion)
  std::int32_t nAlarm{0};        ///< Number of programmable alarms
  std::int32_t nExtTs{0};        ///< Number of external timestamp channels
  std::int32_t nPerOut{0};       ///< Number of periodic output channels
  std::int32_t nPins{0};         ///< Number of programmable pins
  bool pps{false};               ///< PPS (pulse-per-second) output support
  bool crossTimestamp{false};    ///< Cross-timestamp support (PHC <-> system)
  bool adjustPhase{false};       ///< Phase adjustment support
  std::int32_t maxAdjPhaseNs{0}; ///< Maximum phase adjustment in nanoseconds

  /// @brief Check if clock has external timestamp capability.
  [[nodiscard]] bool hasExtTimestamp() const noexcept;

  /// @brief Check if clock has periodic output capability.
  [[nodiscard]] bool hasPeriodicOutput() const noexcept;

  /// @brief Check if clock supports high-precision sync (cross-timestamp + PPS).
  [[nodiscard]] bool hasHighPrecisionSync() const noexcept;
};

/* ----------------------------- PtpClock ----------------------------- */

/**
 * @brief Complete PTP hardware clock information.
 *
 * Aggregates device info, capabilities, and associated network interface.
 */
struct PtpClock {
  std::array<char, PTP_DEVICE_NAME_SIZE> device{};          ///< Device name (e.g., "ptp0")
  std::array<char, PTP_CLOCK_DRIVER_NAME_SIZE> clockName{}; ///< Clock identity/name
  std::int32_t index{-1};                                   ///< PTP index (0, 1, ...)
  std::int32_t phcIndex{-1};                                ///< PHC index for binding

  // Capabilities (from PTP_CLOCK_GETCAPS)
  PtpClockCaps caps{};
  bool capsQuerySucceeded{false}; ///< True if ioctl succeeded

  // Associated network interface (if any)
  std::array<char, PTP_IFACE_NAME_SIZE> boundInterface{}; ///< e.g., "eth0"
  bool hasBoundInterface{false};

  /// @brief Check if this entry is valid.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Get RT suitability score for this clock (0-100).
  [[nodiscard]] int rtScore() const noexcept;
};

/* ----------------------------- PtpStatus ----------------------------- */

/**
 * @brief Complete PTP subsystem status snapshot.
 *
 * Enumerates all PTP hardware clocks with capabilities and bindings.
 */
struct PtpStatus {
  std::array<PtpClock, PTP_MAX_CLOCKS> clocks{};
  std::size_t clockCount{0};

  /// Status flags
  bool ptpSupported{false};     ///< PTP subsystem available (/sys/class/ptp exists)
  bool hasHardwareClock{false}; ///< At least one hardware PTP clock present

  /// @brief Find clock by device name (e.g., "ptp0").
  /// @return Pointer to clock, or nullptr if not found.
  [[nodiscard]] const PtpClock* findByDevice(const char* device) const noexcept;

  /// @brief Find clock by index.
  /// @return Pointer to clock, or nullptr if not found.
  [[nodiscard]] const PtpClock* findByIndex(std::int32_t index) const noexcept;

  /// @brief Find clock bound to specific network interface.
  /// @return Pointer to clock, or nullptr if not found.
  [[nodiscard]] const PtpClock* findByInterface(const char* iface) const noexcept;

  /// @brief Get best clock for RT applications.
  /// @return Pointer to highest-scoring clock, or nullptr if none.
  [[nodiscard]] const PtpClock* getBestClock() const noexcept;

  /// @brief Get overall RT suitability score (0-100).
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief JSON representation.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toJson() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Enumerate PTP hardware clocks and query capabilities.
 * @return Populated PtpStatus snapshot.
 * @note NOT RT-safe: Directory iteration, ioctl calls.
 *
 * Sources:
 *  - /sys/class/ptp/ptp* - PTP device enumeration
 *  - /dev/ptp* - PTP_CLOCK_GETCAPS ioctl for capabilities
 *  - /sys/class/ptp/ptpN/clock_name - Clock identity
 *  - /sys/class/net/\<iface\>/device/ptp - Interface-to-PTP binding
 */
[[nodiscard]] PtpStatus getPtpStatus() noexcept;

/**
 * @brief Query capabilities for a specific PTP device.
 * @param device Device path (e.g., "/dev/ptp0") or name (e.g., "ptp0").
 * @return PtpClockCaps; all zeros if query fails.
 * @note NOT RT-safe: ioctl call.
 */
[[nodiscard]] PtpClockCaps getPtpClockCaps(const char* device) noexcept;

/**
 * @brief Check if PTP subsystem is available.
 * @return True if /sys/class/ptp/ exists.
 * @note RT-safe: Single stat() call.
 */
[[nodiscard]] bool isPtpSupported() noexcept;

/**
 * @brief Get PHC index for a network interface.
 * @param iface Interface name (e.g., "eth0").
 * @return PHC index >= 0, or -1 if not available.
 * @note RT-safe: Single file read.
 */
[[nodiscard]] std::int32_t getPhcIndexForInterface(const char* iface) noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_PTP_STATUS_HPP