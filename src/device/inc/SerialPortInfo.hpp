#ifndef SEEKER_DEVICE_SERIAL_PORT_INFO_HPP
#define SEEKER_DEVICE_SERIAL_PORT_INFO_HPP
/**
 * @file SerialPortInfo.hpp
 * @brief Serial port enumeration and configuration query.
 * @note Linux-only. Uses sysfs and termios for serial port information.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides serial port information for embedded/flight software:
 *  - Built-in UARTs (ttyS*, ttyAMA*, ttySAC*, etc.)
 *  - USB-serial adapters (ttyUSB*, ttyACM*)
 *  - RS485 configuration status
 *  - Hardware flow control capabilities
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

/// Maximum serial port name length (e.g., "/dev/ttyUSB0").
inline constexpr std::size_t SERIAL_NAME_SIZE = 32;

/// Maximum path length for device paths.
inline constexpr std::size_t SERIAL_PATH_SIZE = 128;

/// Maximum driver name length.
inline constexpr std::size_t DRIVER_NAME_SIZE = 64;

/// Maximum number of serial ports to enumerate.
inline constexpr std::size_t MAX_SERIAL_PORTS = 32;

/// Maximum USB product/manufacturer string length.
inline constexpr std::size_t USB_STRING_SIZE = 128;

/* ----------------------------- SerialPortType ----------------------------- */

/**
 * @brief Serial port type classification.
 */
enum class SerialPortType : std::uint8_t {
  UNKNOWN = 0,  ///< Unknown or unclassified
  BUILTIN_UART, ///< Built-in UART (ttyS*, ttyAMA*, etc.)
  USB_SERIAL,   ///< USB-to-serial adapter (ttyUSB*)
  USB_ACM,      ///< USB CDC ACM device (ttyACM*)
  PLATFORM,     ///< Platform device UART (embedded SoC)
  VIRTUAL,      ///< Virtual/pseudo terminal
};

/// @brief Convert SerialPortType to string.
/// @param type Serial port type.
/// @return String representation.
[[nodiscard]] const char* toString(SerialPortType type) noexcept;

/* ----------------------------- SerialBaudRate ----------------------------- */

/**
 * @brief Standard baud rate information.
 */
struct SerialBaudRate {
  std::uint32_t input{0};  ///< Input baud rate (bps)
  std::uint32_t output{0}; ///< Output baud rate (bps)

  /// @brief Check if baud rate is set.
  [[nodiscard]] bool isSet() const noexcept;

  /// @brief Check if input and output rates match.
  [[nodiscard]] bool isSymmetric() const noexcept;
};

/* ----------------------------- SerialConfig ----------------------------- */

/**
 * @brief Serial port configuration parameters.
 *
 * Reflects the termios settings for data bits, parity, stop bits, and flow control.
 */
struct SerialConfig {
  std::uint8_t dataBits{8}; ///< Data bits (5, 6, 7, or 8)
  char parity{'N'};         ///< Parity: 'N'=none, 'E'=even, 'O'=odd
  std::uint8_t stopBits{1}; ///< Stop bits (1 or 2)

  bool hwFlowControl{false}; ///< RTS/CTS hardware flow control
  bool swFlowControl{false}; ///< XON/XOFF software flow control

  bool localMode{false}; ///< CLOCAL: ignore modem control lines
  bool rawMode{false};   ///< Raw input mode (no line processing)

  SerialBaudRate baudRate{}; ///< Current baud rate

  /// @brief Get common notation string (e.g., "8N1").
  /// @return Configuration string like "8N1", "7E2", etc.
  [[nodiscard]] std::array<char, 8> notation() const noexcept;

  /// @brief Check if configuration is valid.
  [[nodiscard]] bool isValid() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Rs485Config ----------------------------- */

/**
 * @brief RS485 mode configuration.
 *
 * RS485 is a half-duplex differential signaling standard commonly used
 * in industrial and embedded systems for multi-drop communication.
 */
struct Rs485Config {
  bool enabled{false};            ///< RS485 mode enabled
  bool rtsOnSend{false};          ///< RTS active during transmission
  bool rtsAfterSend{false};       ///< RTS active after transmission
  bool rxDuringTx{false};         ///< Receive own transmission
  bool terminationEnabled{false}; ///< Bus termination enabled (if supported)

  std::int32_t delayRtsBeforeSend{0}; ///< Delay before send (microseconds)
  std::int32_t delayRtsAfterSend{0};  ///< Delay after send (microseconds)

  /// @brief Check if RS485 is configured.
  [[nodiscard]] bool isConfigured() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- UsbSerialInfo ----------------------------- */

/**
 * @brief USB-specific information for USB-serial adapters.
 */
struct UsbSerialInfo {
  std::uint16_t vendorId{0};                        ///< USB vendor ID
  std::uint16_t productId{0};                       ///< USB product ID
  std::array<char, USB_STRING_SIZE> manufacturer{}; ///< Manufacturer string
  std::array<char, USB_STRING_SIZE> product{};      ///< Product string
  std::array<char, USB_STRING_SIZE> serial{};       ///< Serial number
  std::uint8_t busNum{0};                           ///< USB bus number
  std::uint8_t devNum{0};                           ///< USB device number

  /// @brief Check if USB info is available.
  [[nodiscard]] bool isAvailable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SerialPortInfo ----------------------------- */

/**
 * @brief Complete information for a serial port.
 *
 * Aggregates device identification, configuration, RS485 status,
 * and USB information for comprehensive serial port assessment.
 */
struct SerialPortInfo {
  std::array<char, SERIAL_NAME_SIZE> name{};       ///< Device name (e.g., "ttyUSB0")
  std::array<char, SERIAL_PATH_SIZE> devicePath{}; ///< Full path (e.g., "/dev/ttyUSB0")
  std::array<char, SERIAL_PATH_SIZE> sysfsPath{};  ///< Sysfs path for this device
  std::array<char, DRIVER_NAME_SIZE> driver{};     ///< Driver name

  SerialPortType type{SerialPortType::UNKNOWN}; ///< Port type classification
  SerialConfig config{};                        ///< Current configuration (if readable)
  Rs485Config rs485{};                          ///< RS485 configuration
  UsbSerialInfo usbInfo{};                      ///< USB info (for USB-serial devices)

  bool exists{false};   ///< Device file exists
  bool readable{false}; ///< Device is readable
  bool writable{false}; ///< Device is writable
  bool isOpen{false};   ///< Successfully opened for config query

  /// @brief Check if this is a USB-based serial port.
  [[nodiscard]] bool isUsb() const noexcept;

  /// @brief Check if port appears accessible for use.
  [[nodiscard]] bool isAccessible() const noexcept;

  /// @brief Check if port supports RS485 mode.
  [[nodiscard]] bool supportsRs485() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SerialPortList ----------------------------- */

/**
 * @brief Collection of serial port information.
 */
struct SerialPortList {
  SerialPortInfo ports[MAX_SERIAL_PORTS]{};
  std::size_t count{0};

  /// @brief Find port by name (e.g., "ttyUSB0").
  /// @param name Port name to search for.
  /// @return Pointer to port info, or nullptr if not found.
  [[nodiscard]] const SerialPortInfo* find(const char* name) const noexcept;

  /// @brief Find port by device path (e.g., "/dev/ttyUSB0").
  /// @param path Device path to search for.
  /// @return Pointer to port info, or nullptr if not found.
  [[nodiscard]] const SerialPortInfo* findByPath(const char* path) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Count ports by type.
  /// @param type Port type to count.
  /// @return Number of ports of the specified type.
  [[nodiscard]] std::size_t countByType(SerialPortType type) const noexcept;

  /// @brief Count accessible ports.
  [[nodiscard]] std::size_t countAccessible() const noexcept;

  /// @brief Human-readable summary of all ports.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get information for a specific serial port.
 * @param name Device name (e.g., "ttyUSB0") or full path (e.g., "/dev/ttyUSB0").
 * @return Populated SerialPortInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 *
 * Queries:
 *  - Device existence and permissions
 *  - termios configuration (if openable)
 *  - RS485 status via TIOCGRS485 ioctl
 *  - USB information from sysfs (for USB-serial devices)
 */
[[nodiscard]] SerialPortInfo getSerialPortInfo(const char* name) noexcept;

/**
 * @brief Get serial port configuration only (no device enumeration).
 * @param name Device name or path.
 * @return Current SerialConfig, or default if not readable.
 * @note RT-safe: Single open/read/close sequence.
 */
[[nodiscard]] SerialConfig getSerialConfig(const char* name) noexcept;

/**
 * @brief Get RS485 configuration for a serial port.
 * @param name Device name or path.
 * @return RS485 config, or default if not supported.
 * @note RT-safe: Single ioctl call.
 */
[[nodiscard]] Rs485Config getRs485Config(const char* name) noexcept;

/**
 * @brief Enumerate all serial ports on the system.
 * @return List of serial port information.
 * @note NOT RT-safe: Directory enumeration over /sys/class/tty/.
 *
 * Discovers:
 *  - Built-in UARTs (ttyS*, ttyAMA*, ttySAC*, ttyO*, etc.)
 *  - USB-serial devices (ttyUSB*, ttyACM*)
 *  - Platform UARTs from device tree
 */
[[nodiscard]] SerialPortList getAllSerialPorts() noexcept;

/**
 * @brief Check if a serial port name is a known serial device pattern.
 * @param name Device name to check.
 * @return true if name matches a serial port pattern.
 * @note RT-safe: String comparison only.
 */
[[nodiscard]] bool isSerialPortName(const char* name) noexcept;

} // namespace device

} // namespace seeker

#endif // SEEKER_DEVICE_SERIAL_PORT_INFO_HPP
