#ifndef SEEKER_SYSTEM_WATCHDOG_STATUS_HPP
#define SEEKER_SYSTEM_WATCHDOG_STATUS_HPP
/**
 * @file WatchdogStatus.hpp
 * @brief Hardware and software watchdog status (Linux).
 * @note Linux-only. Reads /sys/class/watchdog/ and watchdog device attributes.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify watchdog availability before enabling heartbeat
 *  - Check timeout configuration for deadline requirements
 *  - Detect pretimeout support for graceful degradation
 *  - Monitor watchdog state without opening device (which arms it)
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Maximum watchdog devices to enumerate.
inline constexpr std::size_t MAX_WATCHDOG_DEVICES = 8;

/// Buffer size for watchdog identity string.
inline constexpr std::size_t WATCHDOG_IDENTITY_SIZE = 64;

/// Buffer size for watchdog device path.
inline constexpr std::size_t WATCHDOG_PATH_SIZE = 32;

/// Buffer size for governor/action strings.
inline constexpr std::size_t WATCHDOG_GOVERNOR_SIZE = 32;

/* ----------------------------- WatchdogCapabilities ----------------------------- */

/**
 * @brief Watchdog capability flags.
 *
 * Mirrors WDIOF_* flags from linux/watchdog.h.
 * These indicate what features the watchdog hardware supports.
 */
struct WatchdogCapabilities {
  bool settimeout{false};       ///< WDIOF_SETTIMEOUT: Can set timeout
  bool magicclose{false};       ///< WDIOF_MAGICCLOSE: Supports magic close
  bool pretimeout{false};       ///< WDIOF_PRETIMEOUT: Has pretimeout support
  bool keepaliveping{false};    ///< WDIOF_KEEPALIVEPING: Keep alive ping
  bool alarmonly{false};        ///< WDIOF_ALARMONLY: Alarm only, no reboot
  bool powerover{false};        ///< WDIOF_POWEROVER: Power over event
  bool fanfault{false};         ///< WDIOF_FANFAULT: Fan fault detection
  bool externPowerFault{false}; ///< WDIOF_EXTERN1: External power fault 1
  bool overheat{false};         ///< WDIOF_OVERHEAT: Overheat detection

  /// @brief Raw capability bitmask (WDIOF_* combined).
  std::uint32_t raw{0};

  /// @brief Check if any capability is available.
  [[nodiscard]] bool hasAny() const noexcept;

  /// @brief Human-readable capability list.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- WatchdogDevice ----------------------------- */

/**
 * @brief Information about a single watchdog device.
 *
 * Collected from /sys/class/watchdog/watchdogN/ without opening the device.
 * Opening the watchdog device typically arms it, so we read sysfs instead.
 */
struct WatchdogDevice {
  /// Device index (0 = /dev/watchdog0).
  std::uint32_t index{0};

  /// Device path (e.g., "/dev/watchdog0").
  std::array<char, WATCHDOG_PATH_SIZE> devicePath{};

  /// Hardware identity string.
  std::array<char, WATCHDOG_IDENTITY_SIZE> identity{};

  /// Current timeout in seconds (0 if unknown).
  std::uint32_t timeout{0};

  /// Minimum timeout supported in seconds.
  std::uint32_t minTimeout{0};

  /// Maximum timeout supported in seconds.
  std::uint32_t maxTimeout{0};

  /// Pretimeout in seconds (0 = disabled or unsupported).
  std::uint32_t pretimeout{0};

  /// Time left before watchdog fires (only valid if device is active).
  std::uint32_t timeleft{0};

  /// Boot status flags (WDIOF_BOOTSTATUS_*).
  std::uint32_t bootstatus{0};

  /// Device capabilities.
  WatchdogCapabilities capabilities{};

  /// Pretimeout governor (if pretimeout supported).
  std::array<char, WATCHDOG_GOVERNOR_SIZE> pretimeoutGovernor{};

  /// True if device state was successfully read.
  bool valid{false};

  /// True if watchdog is currently running (armed).
  bool active{false};

  /// True if nowayout is enabled (cannot stop watchdog once started).
  bool nowayout{false};

  /* ----------------------------- Query Helpers ----------------------------- */

  /// @brief Check if this is the primary watchdog (/dev/watchdog).
  [[nodiscard]] bool isPrimary() const noexcept;

  /// @brief Check if timeout can be configured.
  [[nodiscard]] bool canSetTimeout() const noexcept;

  /// @brief Check if pretimeout is available and configured.
  [[nodiscard]] bool hasPretimeout() const noexcept;

  /// @brief Check if watchdog is suitable for RT heartbeat.
  /// @return True if settable timeout and reasonable min timeout.
  [[nodiscard]] bool isRtSuitable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- WatchdogStatus ----------------------------- */

/**
 * @brief System watchdog status snapshot.
 *
 * Enumerates all watchdog devices and their configurations.
 */
struct WatchdogStatus {
  /// Watchdog devices found.
  WatchdogDevice devices[MAX_WATCHDOG_DEVICES]{};

  /// Number of devices found.
  std::size_t deviceCount{0};

  /// True if software watchdog (softdog) is loaded.
  bool softdogLoaded{false};

  /// True if any hardware watchdog is present.
  bool hasHardwareWatchdog{false};

  /* ----------------------------- Query Helpers ----------------------------- */

  /// @brief Find device by index.
  /// @param index Device index (e.g., 0 for watchdog0).
  /// @return Pointer to device info, or nullptr if not found.
  [[nodiscard]] const WatchdogDevice* find(std::uint32_t index) const noexcept;

  /// @brief Find primary watchdog (index 0).
  /// @return Pointer to primary device, or nullptr if not present.
  [[nodiscard]] const WatchdogDevice* primary() const noexcept;

  /// @brief Check if any watchdog is available.
  [[nodiscard]] bool hasWatchdog() const noexcept;

  /// @brief Check if any watchdog is currently active.
  [[nodiscard]] bool anyActive() const noexcept;

  /// @brief Find first RT-suitable watchdog.
  /// @return Pointer to suitable device, or nullptr if none.
  [[nodiscard]] const WatchdogDevice* findRtSuitable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query all watchdog devices in the system.
 * @return Populated WatchdogStatus structure.
 * @note RT-safe: Bounded sysfs reads, fixed-size output, no allocation.
 *
 * Sources:
 *  - /sys/class/watchdog/watchdogN/ for device enumeration
 *  - /sys/class/watchdog/watchdogN/identity, timeout, etc. for attributes
 *  - /proc/modules for softdog detection
 *
 * Does NOT open /dev/watchdog* (which would arm the watchdog).
 */
[[nodiscard]] WatchdogStatus getWatchdogStatus() noexcept;

/**
 * @brief Query a specific watchdog device by index.
 * @param index Device index (e.g., 0 for /dev/watchdog0).
 * @return Populated WatchdogDevice, or invalid device if not found.
 * @note RT-safe: Bounded sysfs reads.
 */
[[nodiscard]] WatchdogDevice getWatchdogDevice(std::uint32_t index) noexcept;

/**
 * @brief Check if softdog kernel module is loaded.
 * @return True if softdog is present.
 * @note RT-safe: Single file read.
 */
[[nodiscard]] bool isSoftdogLoaded() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_WATCHDOG_STATUS_HPP