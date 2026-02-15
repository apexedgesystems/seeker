#ifndef SEEKER_DEVICE_SPI_BUS_INFO_HPP
#define SEEKER_DEVICE_SPI_BUS_INFO_HPP
/**
 * @file SpiBusInfo.hpp
 * @brief SPI bus enumeration and device information.
 * @note Linux-only. Uses /sys/class/spidev/ and /sys/bus/spi/ interfaces.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides SPI bus information for embedded/flight software:
 *  - Bus and chip-select enumeration
 *  - Device mode and speed configuration
 *  - Bits per word settings
 *  - RT safety considerations for SPI access
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

/// Maximum SPI device name length (e.g., "spidev0.0").
inline constexpr std::size_t SPI_NAME_SIZE = 32;

/// Maximum SPI device path length.
inline constexpr std::size_t SPI_PATH_SIZE = 128;

/// Maximum driver/modalias string length.
inline constexpr std::size_t SPI_DRIVER_SIZE = 64;

/// Maximum number of SPI devices to enumerate.
inline constexpr std::size_t MAX_SPI_DEVICES = 32;

/// Maximum SPI speed for validation (100 MHz, typical max).
inline constexpr std::uint32_t MAX_SPI_SPEED_HZ = 100000000;

/// Default SPI bits per word.
inline constexpr std::uint8_t DEFAULT_SPI_BITS_PER_WORD = 8;

/* ----------------------------- SpiMode ----------------------------- */

/**
 * @brief SPI mode configuration (CPOL/CPHA).
 *
 * Mode | CPOL | CPHA | Clock Idle | Data Capture
 * -----|------|------|------------|-------------
 *  0   |  0   |  0   | Low        | Rising edge
 *  1   |  0   |  1   | Low        | Falling edge
 *  2   |  1   |  0   | High       | Falling edge
 *  3   |  1   |  1   | High       | Rising edge
 */
enum class SpiMode : std::uint8_t {
  MODE_0 = 0, ///< CPOL=0, CPHA=0
  MODE_1 = 1, ///< CPOL=0, CPHA=1
  MODE_2 = 2, ///< CPOL=1, CPHA=0
  MODE_3 = 3, ///< CPOL=1, CPHA=1
};

/// @brief Convert SpiMode to string.
/// @param mode SPI mode.
/// @return String representation (e.g., "mode0").
[[nodiscard]] const char* toString(SpiMode mode) noexcept;

/* ----------------------------- SpiConfig ----------------------------- */

/**
 * @brief SPI device configuration parameters.
 *
 * Reflects the settings from SPI_IOC_RD_* ioctls.
 */
struct SpiConfig {
  SpiMode mode{SpiMode::MODE_0}; ///< Clock polarity and phase
  std::uint8_t bitsPerWord{8};   ///< Bits per word (usually 8)
  std::uint32_t maxSpeedHz{0};   ///< Maximum clock speed in Hz

  bool lsbFirst{false};  ///< LSB first (vs MSB first)
  bool csHigh{false};    ///< Chip select active high
  bool threeWire{false}; ///< Three-wire mode (bidirectional)
  bool loopback{false};  ///< Loopback mode (for testing)
  bool noCs{false};      ///< No chip select
  bool ready{false};     ///< Slave ready signal

  /// @brief Check if configuration was successfully read.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Get CPOL (clock polarity) from mode.
  [[nodiscard]] bool cpol() const noexcept;

  /// @brief Get CPHA (clock phase) from mode.
  [[nodiscard]] bool cpha() const noexcept;

  /// @brief Get speed in MHz for display.
  [[nodiscard]] double speedMHz() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SpiDeviceInfo ----------------------------- */

/**
 * @brief Complete information for an SPI device.
 *
 * Represents a single chip-select on an SPI bus (e.g., spidev0.0).
 */
struct SpiDeviceInfo {
  std::array<char, SPI_NAME_SIZE> name{};       ///< Device name (e.g., "spidev0.0")
  std::array<char, SPI_PATH_SIZE> devicePath{}; ///< Device path (e.g., "/dev/spidev0.0")
  std::array<char, SPI_PATH_SIZE> sysfsPath{};  ///< Sysfs path
  std::array<char, SPI_DRIVER_SIZE> driver{};   ///< Driver name
  std::array<char, SPI_DRIVER_SIZE> modalias{}; ///< Device modalias

  std::uint32_t busNumber{0};  ///< SPI bus number
  std::uint32_t chipSelect{0}; ///< Chip select number

  SpiConfig config{}; ///< Device configuration

  bool exists{false};     ///< Device file exists
  bool accessible{false}; ///< Device is accessible (permissions)

  /// @brief Check if device is usable.
  [[nodiscard]] bool isUsable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SpiDeviceList ----------------------------- */

/**
 * @brief Collection of SPI device information.
 */
struct SpiDeviceList {
  SpiDeviceInfo devices[MAX_SPI_DEVICES]{};
  std::size_t count{0};

  /// @brief Find device by name (e.g., "spidev0.0").
  /// @param name Device name to search for.
  /// @return Pointer to device info, or nullptr if not found.
  [[nodiscard]] const SpiDeviceInfo* find(const char* name) const noexcept;

  /// @brief Find device by bus and chip-select.
  /// @param busNumber SPI bus number.
  /// @param chipSelect Chip select number.
  /// @return Pointer to device info, or nullptr if not found.
  [[nodiscard]] const SpiDeviceInfo* findByBusCs(std::uint32_t busNumber,
                                                 std::uint32_t chipSelect) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Count accessible devices.
  [[nodiscard]] std::size_t countAccessible() const noexcept;

  /// @brief Count unique buses represented.
  [[nodiscard]] std::size_t countUniqueBuses() const noexcept;

  /// @brief Human-readable summary of all devices.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get information for a specific SPI device.
 * @param busNumber SPI bus number.
 * @param chipSelect Chip select number.
 * @return Populated SpiDeviceInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 *
 * Queries:
 *  - Device existence and permissions
 *  - Mode, bits per word, speed via ioctls
 *  - Driver info from sysfs
 */
[[nodiscard]] SpiDeviceInfo getSpiDeviceInfo(std::uint32_t busNumber,
                                             std::uint32_t chipSelect) noexcept;

/**
 * @brief Get SPI device information by name.
 * @param name Device name (e.g., "spidev0.0") or path (e.g., "/dev/spidev0.0").
 * @return Populated SpiDeviceInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 */
[[nodiscard]] SpiDeviceInfo getSpiDeviceInfoByName(const char* name) noexcept;

/**
 * @brief Get SPI device configuration only.
 * @param busNumber SPI bus number.
 * @param chipSelect Chip select number.
 * @return SpiConfig, or default if not readable.
 * @note RT-safe: Bounded ioctl calls.
 */
[[nodiscard]] SpiConfig getSpiConfig(std::uint32_t busNumber, std::uint32_t chipSelect) noexcept;

/**
 * @brief Enumerate all SPI devices on the system.
 * @return List of SPI device information.
 * @note NOT RT-safe: Directory enumeration over /sys/class/spidev/.
 *
 * Discovers all spidevX.Y devices (user-mode SPI driver).
 */
[[nodiscard]] SpiDeviceList getAllSpiDevices() noexcept;

/**
 * @brief Parse bus and chip-select from device name.
 * @param name Device name (e.g., "spidev0.0", "/dev/spidev0.0", "0.0").
 * @param outBus Output bus number.
 * @param outCs Output chip select number.
 * @return true if parsed successfully.
 * @note RT-safe: String parsing only.
 */
[[nodiscard]] bool parseSpiDeviceName(const char* name, std::uint32_t& outBus,
                                      std::uint32_t& outCs) noexcept;

/**
 * @brief Check if an SPI device exists.
 * @param busNumber SPI bus number.
 * @param chipSelect Chip select number.
 * @return true if device exists.
 * @note RT-safe: Single stat call.
 */
[[nodiscard]] bool spiDeviceExists(std::uint32_t busNumber, std::uint32_t chipSelect) noexcept;

} // namespace device

} // namespace seeker

#endif // SEEKER_DEVICE_SPI_BUS_INFO_HPP
