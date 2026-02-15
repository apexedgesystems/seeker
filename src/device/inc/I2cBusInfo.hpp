#ifndef SEEKER_DEVICE_I2C_BUS_INFO_HPP
#define SEEKER_DEVICE_I2C_BUS_INFO_HPP
/**
 * @file I2cBusInfo.hpp
 * @brief I2C bus enumeration and device discovery.
 * @note Linux-only. Uses /sys/class/i2c-adapter/ and i2c-dev interface.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides I2C bus information for embedded/flight software:
 *  - Bus enumeration with adapter identification
 *  - Device scanning (address detection)
 *  - Functionality flags (SMBus, 10-bit addressing, etc.)
 *  - RT safety considerations for bus access
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

/// Maximum I2C bus name length.
inline constexpr std::size_t I2C_NAME_SIZE = 64;

/// Maximum I2C adapter path length.
inline constexpr std::size_t I2C_PATH_SIZE = 128;

/// Maximum number of I2C buses to enumerate.
inline constexpr std::size_t MAX_I2C_BUSES = 16;

/// I2C address range (7-bit: 0x03-0x77, excluding reserved).
inline constexpr std::uint8_t I2C_ADDR_MIN = 0x03;
inline constexpr std::uint8_t I2C_ADDR_MAX = 0x77;

/// Maximum devices per bus (theoretical: 112 for 7-bit addressing).
inline constexpr std::size_t MAX_I2C_DEVICES = 128;

/* ----------------------------- I2cFunctionality ----------------------------- */

/**
 * @brief I2C bus functionality flags.
 *
 * Reflects the capabilities reported by I2C_FUNCS ioctl.
 */
struct I2cFunctionality {
  bool i2c{false};              ///< Plain I2C transactions
  bool tenBitAddr{false};       ///< 10-bit addressing
  bool smbusQuick{false};       ///< SMBus quick command
  bool smbusByte{false};        ///< SMBus read/write byte
  bool smbusWord{false};        ///< SMBus read/write word
  bool smbusBlock{false};       ///< SMBus block read/write
  bool smbusPec{false};         ///< SMBus packet error checking
  bool smbusI2cBlock{false};    ///< SMBus I2C block read/write
  bool protocolMangling{false}; ///< Protocol mangling (nostart, etc.)

  /// @brief Check if basic I2C is supported.
  [[nodiscard]] bool hasBasicI2c() const noexcept;

  /// @brief Check if SMBus is supported.
  [[nodiscard]] bool hasSmbus() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- I2cDevice ----------------------------- */

/**
 * @brief Information about a discovered I2C device.
 */
struct I2cDevice {
  std::uint8_t address{0}; ///< 7-bit I2C address
  bool responsive{false};  ///< Device responded to probe

  /// @brief Check if this is a valid device entry.
  [[nodiscard]] bool isValid() const noexcept;
};

/* ----------------------------- I2cDeviceList ----------------------------- */

/**
 * @brief List of discovered I2C devices on a bus.
 */
struct I2cDeviceList {
  I2cDevice devices[MAX_I2C_DEVICES]{};
  std::size_t count{0};

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Check if a specific address is present.
  /// @param addr 7-bit I2C address to check.
  /// @return true if device at address responded.
  [[nodiscard]] bool hasAddress(std::uint8_t addr) const noexcept;

  /// @brief Get list of all detected addresses.
  /// @return Comma-separated hex addresses string.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string addressList() const;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- I2cBusInfo ----------------------------- */

/**
 * @brief Complete information for an I2C bus.
 *
 * Aggregates adapter identification, functionality flags,
 * and optionally discovered devices.
 */
struct I2cBusInfo {
  std::array<char, I2C_NAME_SIZE> name{};        ///< Adapter name (e.g., "i2c-1")
  std::array<char, I2C_PATH_SIZE> devicePath{};  ///< Device path (e.g., "/dev/i2c-1")
  std::array<char, I2C_PATH_SIZE> sysfsPath{};   ///< Sysfs path
  std::array<char, I2C_NAME_SIZE> adapterName{}; ///< Human-readable adapter name

  std::uint32_t busNumber{0}; ///< Bus number (from i2c-N)

  I2cFunctionality functionality{}; ///< Bus capabilities
  I2cDeviceList scannedDevices{};   ///< Discovered devices (if scanned)

  bool exists{false};     ///< Device file exists
  bool accessible{false}; ///< Device is accessible (permissions)
  bool scanned{false};    ///< Device scan was performed

  /// @brief Check if bus is usable.
  [[nodiscard]] bool isUsable() const noexcept;

  /// @brief Check if bus supports 10-bit addressing.
  [[nodiscard]] bool supports10BitAddr() const noexcept;

  /// @brief Check if bus supports SMBus.
  [[nodiscard]] bool supportsSmbus() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- I2cBusList ----------------------------- */

/**
 * @brief Collection of I2C bus information.
 */
struct I2cBusList {
  I2cBusInfo buses[MAX_I2C_BUSES]{};
  std::size_t count{0};

  /// @brief Find bus by number (e.g., 1 for i2c-1).
  /// @param busNumber Bus number to search for.
  /// @return Pointer to bus info, or nullptr if not found.
  [[nodiscard]] const I2cBusInfo* findByNumber(std::uint32_t busNumber) const noexcept;

  /// @brief Find bus by name (e.g., "i2c-1").
  /// @param name Bus name to search for.
  /// @return Pointer to bus info, or nullptr if not found.
  [[nodiscard]] const I2cBusInfo* find(const char* name) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Count accessible buses.
  [[nodiscard]] std::size_t countAccessible() const noexcept;

  /// @brief Human-readable summary of all buses.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get information for a specific I2C bus.
 * @param busNumber Bus number (e.g., 1 for /dev/i2c-1).
 * @return Populated I2cBusInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 *
 * Queries:
 *  - Device existence and permissions
 *  - Adapter name from sysfs
 *  - Functionality flags via I2C_FUNCS ioctl
 */
[[nodiscard]] I2cBusInfo getI2cBusInfo(std::uint32_t busNumber) noexcept;

/**
 * @brief Get information for an I2C bus by name.
 * @param name Bus name (e.g., "i2c-1") or device path (e.g., "/dev/i2c-1").
 * @return Populated I2cBusInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 */
[[nodiscard]] I2cBusInfo getI2cBusInfoByName(const char* name) noexcept;

/**
 * @brief Get I2C bus functionality only.
 * @param busNumber Bus number.
 * @return I2cFunctionality flags.
 * @note RT-safe: Single ioctl call.
 */
[[nodiscard]] I2cFunctionality getI2cFunctionality(std::uint32_t busNumber) noexcept;

/**
 * @brief Scan I2C bus for devices.
 * @param busNumber Bus number to scan.
 * @return List of discovered devices.
 * @warning NOT RT-safe: Multiple blocking I2C transactions.
 * @warning May disrupt sensitive devices. Use with caution.
 *
 * Uses SMBus quick command if available, falls back to read byte.
 * Skips reserved addresses (0x00-0x02, 0x78-0x7F).
 */
[[nodiscard]] I2cDeviceList scanI2cBus(std::uint32_t busNumber) noexcept;

/**
 * @brief Enumerate all I2C buses on the system.
 * @return List of I2C bus information.
 * @note NOT RT-safe: Directory enumeration over /sys/class/i2c-adapter/.
 *
 * Does NOT scan for devices (call scanI2cBus separately if needed).
 */
[[nodiscard]] I2cBusList getAllI2cBuses() noexcept;

/**
 * @brief Check if a device responds at the given address.
 * @param busNumber Bus number.
 * @param address 7-bit I2C address.
 * @return true if device responds.
 * @warning May disrupt the device. Use with caution.
 * @note Semi-RT-safe: Single blocking I2C transaction.
 */
[[nodiscard]] bool probeI2cAddress(std::uint32_t busNumber, std::uint8_t address) noexcept;

/**
 * @brief Parse bus number from name string.
 * @param name Bus name (e.g., "i2c-1", "/dev/i2c-1", "1").
 * @param outBusNumber Output bus number.
 * @return true if parsed successfully.
 * @note RT-safe: String parsing only.
 */
[[nodiscard]] bool parseI2cBusNumber(const char* name, std::uint32_t& outBusNumber) noexcept;

} // namespace device

} // namespace seeker

#endif // SEEKER_DEVICE_I2C_BUS_INFO_HPP
