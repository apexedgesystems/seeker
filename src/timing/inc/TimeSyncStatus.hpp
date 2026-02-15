#ifndef SEEKER_TIMING_TIME_SYNC_STATUS_HPP
#define SEEKER_TIMING_TIME_SYNC_STATUS_HPP
/**
 * @file TimeSyncStatus.hpp
 * @brief Time synchronization status (NTP, PTP, chrony) (Linux).
 * @note Linux-only. Reads /sys/class/ptp/, /run/, adjtimex(2).
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides time synchronization information for precision timing applications:
 *  - Detection of active sync daemons (chrony, ntpd, systemd-timesyncd)
 *  - PTP hardware clock enumeration
 *  - Kernel time synchronization status via adjtimex(2)
 *
 * Time synchronization is essential for distributed RT systems where
 * coordinated timing across machines is required.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of PTP devices to enumerate.
inline constexpr std::size_t MAX_PTP_DEVICES = 8;

/// Maximum length for PTP device name.
inline constexpr std::size_t PTP_NAME_SIZE = 16;

/// Maximum length for PTP clock name.
inline constexpr std::size_t PTP_CLOCK_NAME_SIZE = 64;

/* ----------------------------- PtpDevice ----------------------------- */

/**
 * @brief PTP hardware clock device information.
 */
struct PtpDevice {
  std::array<char, PTP_NAME_SIZE> name{};        ///< Device name (e.g., "ptp0")
  std::array<char, PTP_CLOCK_NAME_SIZE> clock{}; ///< Clock identity/name
  std::int64_t maxAdjPpb{0};                     ///< Maximum adjustment (parts per billion)
  int ppsAvailable{-1};                          ///< PPS output available (1=yes, 0=no, -1=unknown)

  /// @brief Check if device entry is valid.
  [[nodiscard]] bool isValid() const noexcept;
};

/* ----------------------------- KernelTimeStatus ----------------------------- */

/**
 * @brief Kernel time synchronization status from adjtimex(2).
 */
struct KernelTimeStatus {
  bool synced{false};   ///< True if kernel clock is synchronized (STA_UNSYNC not set)
  bool pll{false};      ///< Phase-locked loop mode active
  bool ppsFreq{false};  ///< PPS frequency discipline active
  bool ppsTime{false};  ///< PPS time discipline active
  bool freqHold{false}; ///< Frequency hold mode

  std::int64_t offsetUs{0};   ///< Current time offset in microseconds
  std::int64_t freqPpb{0};    ///< Frequency adjustment (parts per billion)
  std::int64_t maxErrorUs{0}; ///< Maximum error estimate in microseconds
  std::int64_t estErrorUs{0}; ///< Estimated error in microseconds

  int clockState{0};          ///< Clock state from adjtimex return value
  bool querySucceeded{false}; ///< True if adjtimex query succeeded

  /// @brief Check if clock is well-synchronized (synced and low offset).
  [[nodiscard]] bool isWellSynced() const noexcept;

  /// @brief Get synchronization quality string ("excellent", "good", "fair", "poor").
  [[nodiscard]] const char* qualityString() const noexcept;
};

/* ----------------------------- TimeSyncStatus ----------------------------- */

/**
 * @brief Time synchronization status snapshot.
 *
 * Captures information about time sync daemons, PTP devices,
 * and kernel clock state.
 */
struct TimeSyncStatus {
  // Detected time sync daemons
  bool chronyDetected{false};          ///< chrony daemon detected
  bool ntpdDetected{false};            ///< ntpd daemon detected
  bool systemdTimesyncDetected{false}; ///< systemd-timesyncd detected
  bool ptpLinuxDetected{false};        ///< ptp4linux/linuxptp detected

  // PTP devices
  PtpDevice ptpDevices[MAX_PTP_DEVICES]{};
  std::size_t ptpDeviceCount{0}; ///< Valid entries in ptpDevices[]

  // Kernel time status
  KernelTimeStatus kernel{};

  /// @brief Check if any time sync daemon is detected.
  [[nodiscard]] bool hasAnySyncDaemon() const noexcept;

  /// @brief Check if PTP hardware is available.
  [[nodiscard]] bool hasPtpHardware() const noexcept;

  /// @brief Get primary synchronization method string.
  /// @return "chrony", "ntpd", "ptp", "systemd-timesyncd", or "none"
  [[nodiscard]] const char* primarySyncMethod() const noexcept;

  /// @brief Get RT suitability score for time sync (0-100).
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query time synchronization status.
 * @return Populated TimeSyncStatus snapshot.
 * @note NOT RT-safe: Directory iteration for PTP devices.
 *
 * Sources:
 *  - /run/chrony/, /var/run/chrony/ - chrony presence
 *  - /var/lib/ntp/, /run/ntpd.pid - ntpd presence
 *  - /run/systemd/timesync/ - systemd-timesyncd presence
 *  - /run/ptp4l* - linuxptp presence
 *  - /sys/class/ptp/ptp* - PTP device enumeration
 *  - adjtimex(2) - Kernel time status
 */
[[nodiscard]] TimeSyncStatus getTimeSyncStatus() noexcept;

/**
 * @brief Query kernel time status only.
 * @return KernelTimeStatus from adjtimex(2).
 * @note RT-safe: Single syscall.
 */
[[nodiscard]] KernelTimeStatus getKernelTimeStatus() noexcept;

/**
 * @brief Check if a specific sync daemon is running.
 * @param daemon Daemon name ("chrony", "ntpd", "systemd-timesyncd", "ptp4l").
 * @return True if daemon appears to be running.
 * @note NOT RT-safe: File existence checks.
 */
[[nodiscard]] bool isSyncDaemonRunning(const char* daemon) noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_TIME_SYNC_STATUS_HPP
