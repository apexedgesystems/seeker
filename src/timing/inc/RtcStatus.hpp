#ifndef SEEKER_TIMING_RTC_STATUS_HPP
#define SEEKER_TIMING_RTC_STATUS_HPP
/**
 * @file RtcStatus.hpp
 * @brief Hardware Real-Time Clock (RTC) status (Linux).
 * @note Linux-only. Reads /sys/class/rtc/, /dev/rtc*, RTC ioctls.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides hardware RTC information for persistent timekeeping:
 *  - RTC device enumeration and capabilities
 *  - Wakealarm status and configuration
 *  - RTC vs system time drift detection
 *  - Battery-backed clock status
 *
 * Important for systems requiring time persistence across power cycles
 * and wake-from-suspend functionality.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t, std::int32_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/// Maximum RTC devices to enumerate.
inline constexpr std::size_t RTC_MAX_DEVICES = 4;

/// Maximum length for RTC device name.
inline constexpr std::size_t RTC_DEVICE_NAME_SIZE = 16;

/// Maximum length for RTC driver/chip name.
inline constexpr std::size_t RTC_DRIVER_NAME_SIZE = 64;

/* ----------------------------- RtcCapabilities ----------------------------- */

/**
 * @brief RTC hardware capabilities.
 *
 * Describes what features the RTC hardware supports.
 */
struct RtcCapabilities {
  bool hasAlarm{false};       ///< Supports alarm interrupts
  bool hasPeriodicIrq{false}; ///< Supports periodic interrupts
  bool hasUpdateIrq{false};   ///< Supports update-complete interrupts
  bool hasWakeAlarm{false};   ///< Supports wake-from-suspend via alarm
  bool hasBattery{false};     ///< Battery-backed (inferred)
  std::int32_t irqFreqMin{0}; ///< Minimum IRQ frequency (if periodic IRQ)
  std::int32_t irqFreqMax{0}; ///< Maximum IRQ frequency (if periodic IRQ)

  /// @brief Check if RTC can wake system from suspend.
  [[nodiscard]] bool canWakeFromSuspend() const noexcept;
};

/* ----------------------------- RtcTime ----------------------------- */

/**
 * @brief RTC time snapshot.
 *
 * Captures RTC time and comparison with system time.
 */
struct RtcTime {
  std::int32_t year{0};   ///< Year (e.g., 2024)
  std::int32_t month{0};  ///< Month (1-12)
  std::int32_t day{0};    ///< Day of month (1-31)
  std::int32_t hour{0};   ///< Hour (0-23)
  std::int32_t minute{0}; ///< Minute (0-59)
  std::int32_t second{0}; ///< Second (0-59)

  std::int64_t epochSeconds{0};   ///< Unix epoch seconds
  std::int64_t systemEpochSec{0}; ///< System time at query (for drift calc)
  std::int64_t driftSeconds{0};   ///< RTC - system time (positive = RTC ahead)
  bool querySucceeded{false};     ///< True if RTC time read succeeded

  /// @brief Check if RTC time appears valid.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Check if drift is within acceptable bounds (+/- 5 seconds).
  [[nodiscard]] bool isDriftAcceptable() const noexcept;

  /// @brief Get absolute drift in seconds.
  [[nodiscard]] std::int64_t absDrift() const noexcept;
};

/* ----------------------------- RtcAlarm ----------------------------- */

/**
 * @brief RTC alarm/wakealarm status.
 */
struct RtcAlarm {
  bool enabled{false};          ///< Alarm is set and enabled
  bool pending{false};          ///< Alarm has fired but not cleared
  std::int64_t alarmEpoch{0};   ///< Alarm time (Unix epoch), 0 if not set
  std::int64_t secondsUntil{0}; ///< Seconds until alarm fires (negative = past)
  bool querySucceeded{false};   ///< True if alarm status read succeeded

  /// @brief Check if alarm is set for the future.
  [[nodiscard]] bool isFutureAlarm() const noexcept;
};

/* ----------------------------- RtcDevice ----------------------------- */

/**
 * @brief Complete RTC device information.
 */
struct RtcDevice {
  std::array<char, RTC_DEVICE_NAME_SIZE> device{};  ///< Device name (e.g., "rtc0")
  std::array<char, RTC_DRIVER_NAME_SIZE> name{};    ///< Driver/chip name
  std::array<char, RTC_DRIVER_NAME_SIZE> hctosys{}; ///< "1" if system clock set from this RTC
  std::int32_t index{-1};                           ///< RTC index (0, 1, ...)

  RtcCapabilities caps{};
  RtcTime time{};
  RtcAlarm alarm{};

  bool isSystemRtc{false}; ///< True if this is the system RTC (rtc0 or hctosys)

  /// @brief Check if this entry is valid.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Get health assessment string.
  [[nodiscard]] const char* healthString() const noexcept;
};

/* ----------------------------- RtcStatus ----------------------------- */

/**
 * @brief Complete RTC subsystem status snapshot.
 */
struct RtcStatus {
  std::array<RtcDevice, RTC_MAX_DEVICES> devices{};
  std::size_t deviceCount{0};

  /// Status flags
  bool rtcSupported{false};   ///< RTC subsystem available (/sys/class/rtc exists)
  bool hasHardwareRtc{false}; ///< At least one hardware RTC present
  bool hasWakeCapable{false}; ///< At least one RTC can wake from suspend

  /// System RTC info
  std::int32_t systemRtcIndex{-1}; ///< Index of system RTC, -1 if not determined

  /// @brief Find device by name (e.g., "rtc0").
  /// @return Pointer to device, or nullptr if not found.
  [[nodiscard]] const RtcDevice* findByName(const char* name) const noexcept;

  /// @brief Find device by index.
  /// @return Pointer to device, or nullptr if not found.
  [[nodiscard]] const RtcDevice* findByIndex(std::int32_t index) const noexcept;

  /// @brief Get system RTC (rtc0 or hctosys).
  /// @return Pointer to system RTC, or nullptr if not found.
  [[nodiscard]] const RtcDevice* getSystemRtc() const noexcept;

  /// @brief Get maximum drift across all RTCs (absolute seconds).
  [[nodiscard]] std::int64_t maxDriftSeconds() const noexcept;

  /// @brief Check if all RTCs have acceptable drift.
  [[nodiscard]] bool allDriftAcceptable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief JSON representation.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toJson() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Enumerate RTC devices and query status.
 * @return Populated RtcStatus snapshot.
 * @note NOT RT-safe: Directory iteration, sysfs/ioctl reads.
 *
 * Sources:
 *  - /sys/class/rtc/rtc* - RTC device enumeration
 *  - /sys/class/rtc/rtcN/name - Driver/chip name
 *  - /sys/class/rtc/rtcN/hctosys - System clock source flag
 *  - /sys/class/rtc/rtcN/wakealarm - Wakealarm epoch
 *  - /sys/class/rtc/rtcN/time - Current RTC time
 *  - /sys/class/rtc/rtcN/date - Current RTC date
 */
[[nodiscard]] RtcStatus getRtcStatus() noexcept;

/**
 * @brief Read current time from a specific RTC.
 * @param device Device name (e.g., "rtc0") or path (e.g., "/dev/rtc0").
 * @return RtcTime snapshot; querySucceeded=false if read fails.
 * @note NOT RT-safe: Sysfs read.
 */
[[nodiscard]] RtcTime getRtcTime(const char* device) noexcept;

/**
 * @brief Read alarm status from a specific RTC.
 * @param device Device name (e.g., "rtc0").
 * @return RtcAlarm status; querySucceeded=false if read fails.
 * @note NOT RT-safe: Sysfs read.
 */
[[nodiscard]] RtcAlarm getRtcAlarm(const char* device) noexcept;

/**
 * @brief Check if RTC subsystem is available.
 * @return True if /sys/class/rtc/ exists.
 * @note RT-safe: Single stat() call.
 */
[[nodiscard]] bool isRtcSupported() noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_RTC_STATUS_HPP