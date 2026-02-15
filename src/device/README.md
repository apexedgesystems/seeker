# Device Diagnostics Module

**Namespace:** `seeker::device`
**Platform:** Linux-only
**C++ Standard:** C++23

Comprehensive device bus diagnostics for embedded systems, flight software, and real-time applications. This module provides 5 focused components for enumerating and inspecting serial ports, I2C buses, SPI devices, CAN interfaces, and GPIO chips.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [SerialPortInfo](#serialportinfo) - Serial/UART port enumeration
   - [I2cBusInfo](#i2cbusinfo) - I2C bus enumeration and scanning
   - [SpiBusInfo](#spibusinfo) - SPI device enumeration
   - [CanBusInfo](#canbusinfo) - SocketCAN interface status
   - [GpioInfo](#gpioinfo) - GPIO chip and line enumeration
5. [Common Patterns](#common-patterns)
6. [Real-Time Considerations](#real-time-considerations)
7. [CLI Tools](#cli-tools)
8. [Example: Device Bus Health Check](#example-device-bus-health-check)

---

## Overview

The device diagnostics module answers these questions for embedded and RT systems:

| Question                                            | Module           |
| --------------------------------------------------- | ---------------- |
| What serial ports are available?                    | `SerialPortInfo` |
| Is this a USB-serial adapter or built-in UART?      | `SerialPortInfo` |
| What are the termios settings (baud, parity, etc.)? | `SerialPortInfo` |
| Does the port support RS485?                        | `SerialPortInfo` |
| What I2C buses are available?                       | `I2cBusInfo`     |
| What devices are on an I2C bus?                     | `I2cBusInfo`     |
| Does the I2C adapter support SMBus?                 | `I2cBusInfo`     |
| What SPI devices are available?                     | `SpiBusInfo`     |
| What are the SPI mode and speed settings?           | `SpiBusInfo`     |
| What CAN interfaces are available?                  | `CanBusInfo`     |
| What is the CAN bitrate and bus state?              | `CanBusInfo`     |
| Are there CAN error counters indicating problems?   | `CanBusInfo`     |
| What GPIO chips are available?                      | `GpioInfo`       |
| Which GPIO lines are in use and by whom?            | `GpioInfo`       |
| What is the direction and configuration of a GPIO?  | `GpioInfo`       |

---

## Quick Reference

### Headers and Include Paths

```cpp
#include "src/device/inc/SerialPortInfo.hpp"
#include "src/device/inc/I2cBusInfo.hpp"
#include "src/device/inc/SpiBusInfo.hpp"
#include "src/device/inc/CanBusInfo.hpp"
#include "src/device/inc/GpioInfo.hpp"
```

### One-Shot Queries

```cpp
using namespace seeker::device;

// Serial port inspection
auto ports = getAllSerialPorts();             // Enumerate all serial ports
auto port = getSerialPortInfo("ttyUSB0");     // Query specific port
auto config = getSerialConfig("ttyUSB0");     // Get termios settings
auto rs485 = getRs485Config("ttyUSB0");       // Get RS485 configuration

// I2C bus inspection
auto buses = getAllI2cBuses();                // Enumerate all I2C buses
auto bus = getI2cBusInfo(1);                  // Query bus 1
auto devices = scanI2cBus(1);                 // Scan for devices on bus 1
auto func = getI2cFunctionality(1);           // Check adapter capabilities

// SPI device inspection
auto spiDevs = getAllSpiDevices();            // Enumerate all SPI devices
auto spi = getSpiDeviceInfo(0, 0);            // Query bus 0, CS 0
auto spiCfg = getSpiConfig(0, 0);             // Get mode and speed

// CAN interface inspection
auto canIfaces = getAllCanInterfaces();       // Enumerate CAN interfaces
auto can = getCanInterfaceInfo("can0");       // Query specific interface
auto timing = getCanBitTiming("can0");        // Get bitrate and timing
auto errors = getCanErrorCounters("can0");    // Get TEC/REC counters
auto state = getCanBusState("can0");          // Get bus state (ISO 11898)

// GPIO chip inspection
auto chips = getAllGpioChips();               // Enumerate GPIO chips
auto chip = getGpioChipInfo(0);               // Query gpiochip0
auto lines = getGpioLines(0);                 // Get all lines on chip 0
auto line = getGpioLineInfo(0, 17);           // Query chip 0, line 17
```

---

## Design Principles

### RT-Safety Annotations

Every public function documents its RT-safety:

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for RT threads        |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

Example from header:

```cpp
/**
 * @brief Get information for a specific serial port.
 * @param name Device name (e.g., "ttyUSB0") or full path (e.g., "/dev/ttyUSB0").
 * @return Populated SerialPortInfo, or default-initialized if not found.
 * @note RT-safe: Bounded operations, no heap allocation.
 */
[[nodiscard]] SerialPortInfo getSerialPortInfo(const char* name) noexcept;
```

### Fixed-Size Data Structures

All structs use fixed-size arrays to avoid heap allocation:

```cpp
// Instead of std::string (allocates)
std::array<char, SERIAL_NAME_SIZE> name{};

// Instead of std::vector (allocates)
SerialPortInfo ports[MAX_SERIAL_PORTS]{};
```

This allows most query functions to be RT-safe.

### Graceful Degradation

All functions return valid default-initialized structs when devices are missing or inaccessible:

```cpp
auto port = getSerialPortInfo("nonexistent");
if (!port.exists) {
  // Port not found - struct is valid but empty
}

auto bus = getI2cBusInfo(99);
if (!bus.exists || !bus.accessible) {
  // Bus missing or permission denied
}
```

---

## Module Reference

### SerialPortInfo

**Header:** `SerialPortInfo.hpp`
**Purpose:** Enumerate and inspect serial ports (UART, USB-serial, RS485).

#### Key Types

```cpp
enum class SerialPortType : std::uint8_t {
  UNKNOWN = 0,  // Unknown or unclassified
  BUILTIN_UART, // Built-in UART (ttyS*, ttyAMA*, etc.)
  USB_SERIAL,   // USB-to-serial adapter (ttyUSB*)
  USB_ACM,      // USB CDC ACM device (ttyACM*)
  PLATFORM,     // Platform device UART (embedded SoC)
  VIRTUAL,      // Virtual/pseudo terminal
};

struct SerialBaudRate {
  std::uint32_t input{0};   // Input baud rate (bps)
  std::uint32_t output{0};  // Output baud rate (bps)

  bool isSet() const noexcept;
  bool isSymmetric() const noexcept;
};

struct SerialConfig {
  std::uint8_t dataBits{8};       // Data bits (5, 6, 7, or 8)
  char parity{'N'};               // 'N'=none, 'E'=even, 'O'=odd
  std::uint8_t stopBits{1};       // Stop bits (1 or 2)
  bool hwFlowControl{false};      // RTS/CTS hardware flow control
  bool swFlowControl{false};      // XON/XOFF software flow control
  bool localMode{false};          // CLOCAL: ignore modem control lines
  bool rawMode{false};            // Raw input mode (no line processing)
  SerialBaudRate baudRate{};      // Current baud rate

  std::array<char, 8> notation() const noexcept;  // e.g., "8N1"
  bool isValid() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct Rs485Config {
  bool enabled{false};                  // RS485 mode enabled
  bool rtsOnSend{false};                // RTS active during transmission
  bool rtsAfterSend{false};             // RTS active after transmission
  bool rxDuringTx{false};               // Receive own transmission
  bool terminationEnabled{false};       // Bus termination enabled (if supported)
  std::int32_t delayRtsBeforeSend{0};   // Delay before send (microseconds)
  std::int32_t delayRtsAfterSend{0};    // Delay after send (microseconds)

  bool isConfigured() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct UsbSerialInfo {
  std::uint16_t vendorId{0};      // USB VID
  std::uint16_t productId{0};     // USB PID
  std::array<char, 128> manufacturer{};
  std::array<char, 128> product{};
  std::array<char, 128> serial{};
  std::uint8_t busNum{0};
  std::uint8_t devNum{0};

  bool isAvailable() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct SerialPortInfo {
  std::array<char, 32> name{};         // Device name (e.g., "ttyUSB0")
  std::array<char, 128> devicePath{};  // Full path (e.g., "/dev/ttyUSB0")
  std::array<char, 128> sysfsPath{};   // Sysfs path for this device
  std::array<char, 64> driver{};       // Driver name
  SerialPortType type{SerialPortType::UNKNOWN};
  SerialConfig config{};               // Current configuration (if readable)
  Rs485Config rs485{};
  UsbSerialInfo usbInfo{};
  bool exists{false};
  bool readable{false};
  bool writable{false};
  bool isOpen{false};                  // Successfully opened for config query

  bool isUsb() const noexcept;
  bool isAccessible() const noexcept;
  bool supportsRs485() const noexcept;
  std::string toString() const;        // NOT RT-safe
};

struct SerialPortList {
  SerialPortInfo ports[MAX_SERIAL_PORTS]{};
  std::size_t count{0};

  const SerialPortInfo* find(const char* name) const noexcept;
  const SerialPortInfo* findByPath(const char* path) const noexcept;
  bool empty() const noexcept;
  std::size_t countByType(SerialPortType type) const noexcept;
  std::size_t countAccessible() const noexcept;
  std::string toString() const;   // NOT RT-safe
};
```

#### API

```cpp
// Query specific port (RT-safe)
[[nodiscard]] SerialPortInfo getSerialPortInfo(const char* name) noexcept;

// Get termios configuration (RT-safe)
[[nodiscard]] SerialConfig getSerialConfig(const char* name) noexcept;

// Get RS485 configuration (RT-safe)
[[nodiscard]] Rs485Config getRs485Config(const char* name) noexcept;

// Enumerate all ports (NOT RT-safe: directory scan)
[[nodiscard]] SerialPortList getAllSerialPorts() noexcept;

// Check if name is a serial port (RT-safe)
[[nodiscard]] bool isSerialPortName(const char* name) noexcept;
```

#### Usage

```cpp
using namespace seeker::device;

// Check USB-serial adapter details
auto port = getSerialPortInfo("ttyUSB0");
if (port.isUsb() && port.usbInfo.isAvailable()) {
  fmt::print("USB device: {:04x}:{:04x} {}\n",
             port.usbInfo.vendorId, port.usbInfo.productId,
             port.usbInfo.product.data());
}

// Get current line configuration
auto config = getSerialConfig("ttyUSB0");
if (config.isValid()) {
  fmt::print("Config: {} @ {} baud\n",
             config.notation().data(), config.baudRate.output);
}
```

---

### I2cBusInfo

**Header:** `I2cBusInfo.hpp`
**Purpose:** Enumerate I2C buses and scan for devices.

#### Key Types

```cpp
struct I2cFunctionality {
  bool i2c{false};              // Plain I2C transactions
  bool tenBitAddr{false};       // 10-bit addressing
  bool smbusQuick{false};       // SMBus quick command
  bool smbusByte{false};        // SMBus read/write byte
  bool smbusWord{false};        // SMBus read/write word
  bool smbusBlock{false};       // SMBus block read/write
  bool smbusPec{false};         // SMBus packet error checking
  bool smbusI2cBlock{false};    // SMBus I2C block read/write
  bool protocolMangling{false}; // Protocol mangling (nostart, etc.)

  bool hasBasicI2c() const noexcept;
  bool hasSmbus() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct I2cDevice {
  std::uint8_t address{0};      // 7-bit I2C address
  bool responsive{false};       // Device responded to probe

  bool isValid() const noexcept;
};

struct I2cDeviceList {
  I2cDevice devices[MAX_I2C_DEVICES]{};
  std::size_t count{0};

  bool empty() const noexcept;
  bool hasAddress(std::uint8_t addr) const noexcept;
  std::string addressList() const;  // "0x20, 0x48, 0x68" (NOT RT-safe)
  std::string toString() const;     // NOT RT-safe
};

struct I2cBusInfo {
  std::array<char, 64> name{};          // "i2c-1"
  std::array<char, 128> devicePath{};   // "/dev/i2c-1"
  std::array<char, 128> sysfsPath{};    // Sysfs path
  std::array<char, 64> adapterName{};   // "Synopsys DesignWare I2C"
  std::uint32_t busNumber{0};
  I2cFunctionality functionality{};
  I2cDeviceList scannedDevices{};       // Discovered devices (if scanned)
  bool exists{false};
  bool accessible{false};
  bool scanned{false};                  // Device scan was performed

  bool isUsable() const noexcept;
  bool supports10BitAddr() const noexcept;
  bool supportsSmbus() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct I2cBusList {
  I2cBusInfo buses[MAX_I2C_BUSES]{};
  std::size_t count{0};

  const I2cBusInfo* findByNumber(std::uint32_t busNumber) const noexcept;
  const I2cBusInfo* find(const char* name) const noexcept;
  bool empty() const noexcept;
  std::size_t countAccessible() const noexcept;
  std::string toString() const;   // NOT RT-safe
};
```

#### API

```cpp
// Query specific bus by number (RT-safe)
[[nodiscard]] I2cBusInfo getI2cBusInfo(std::uint32_t busNumber) noexcept;

// Query specific bus by name (RT-safe)
[[nodiscard]] I2cBusInfo getI2cBusInfoByName(const char* name) noexcept;

// Get adapter functionality flags (RT-safe)
[[nodiscard]] I2cFunctionality getI2cFunctionality(std::uint32_t busNumber) noexcept;

// Scan for devices on bus (NOT RT-safe: probes all addresses)
[[nodiscard]] I2cDeviceList scanI2cBus(std::uint32_t busNumber) noexcept;

// Enumerate all buses (NOT RT-safe: directory scan)
[[nodiscard]] I2cBusList getAllI2cBuses() noexcept;

// Probe single address (Semi-RT-safe: single blocking I2C transaction)
[[nodiscard]] bool probeI2cAddress(std::uint32_t busNumber, std::uint8_t address) noexcept;

// Parse bus number from name (RT-safe)
[[nodiscard]] bool parseI2cBusNumber(const char* name, std::uint32_t& outBusNumber) noexcept;
```

#### Usage

```cpp
using namespace seeker::device;

// Check I2C bus capabilities
auto bus = getI2cBusInfo(1);
if (bus.isUsable()) {
  fmt::print("Bus {}: {} ({})\n",
             bus.busNumber, bus.adapterName.data(),
             bus.functionality.hasSmbus() ? "SMBus" : "I2C only");
}

// Scan for devices
auto devices = scanI2cBus(1);
if (!devices.empty()) {
  fmt::print("Found {} devices: {}\n",
             devices.count, devices.addressList());
}
```

---

### SpiBusInfo

**Header:** `SpiBusInfo.hpp`
**Purpose:** Enumerate SPI devices and query configuration.

#### Key Types

```cpp
enum class SpiMode : std::uint8_t {
  MODE_0 = 0,  // CPOL=0, CPHA=0
  MODE_1 = 1,  // CPOL=0, CPHA=1
  MODE_2 = 2,  // CPOL=1, CPHA=0
  MODE_3 = 3,  // CPOL=1, CPHA=1
};

struct SpiConfig {
  SpiMode mode{SpiMode::MODE_0};
  std::uint8_t bitsPerWord{8};
  std::uint32_t maxSpeedHz{0};
  bool lsbFirst{false};          // LSB first (vs MSB first)
  bool csHigh{false};            // Chip select active high
  bool threeWire{false};         // Three-wire mode (bidirectional)
  bool loopback{false};          // Loopback mode (for testing)
  bool noCs{false};              // No chip select
  bool ready{false};             // Slave ready signal

  bool isValid() const noexcept;
  bool cpol() const noexcept;    // Clock polarity
  bool cpha() const noexcept;    // Clock phase
  double speedMHz() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct SpiDeviceInfo {
  std::array<char, 32> name{};        // "spidev0.0"
  std::array<char, 128> devicePath{}; // "/dev/spidev0.0"
  std::array<char, 128> sysfsPath{};  // Sysfs path
  std::array<char, 64> driver{};      // Driver name
  std::array<char, 64> modalias{};    // Device modalias
  std::uint32_t busNumber{0};
  std::uint32_t chipSelect{0};
  SpiConfig config{};
  bool exists{false};
  bool accessible{false};

  bool isUsable() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct SpiDeviceList {
  SpiDeviceInfo devices[MAX_SPI_DEVICES]{};
  std::size_t count{0};

  const SpiDeviceInfo* find(const char* name) const noexcept;
  const SpiDeviceInfo* findByBusCs(std::uint32_t busNumber,
                                    std::uint32_t chipSelect) const noexcept;
  bool empty() const noexcept;
  std::size_t countAccessible() const noexcept;
  std::size_t countUniqueBuses() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
// Query specific device (RT-safe)
[[nodiscard]] SpiDeviceInfo getSpiDeviceInfo(std::uint32_t busNumber,
                                             std::uint32_t chipSelect) noexcept;

// Query by device name (RT-safe)
[[nodiscard]] SpiDeviceInfo getSpiDeviceInfoByName(const char* name) noexcept;

// Get SPI configuration (RT-safe)
[[nodiscard]] SpiConfig getSpiConfig(std::uint32_t busNumber,
                                     std::uint32_t chipSelect) noexcept;

// Enumerate all devices (NOT RT-safe: directory scan)
[[nodiscard]] SpiDeviceList getAllSpiDevices() noexcept;

// Parse device name (RT-safe)
[[nodiscard]] bool parseSpiDeviceName(const char* name, std::uint32_t& outBus,
                                      std::uint32_t& outCs) noexcept;

// Check device existence (RT-safe)
[[nodiscard]] bool spiDeviceExists(std::uint32_t busNumber,
                                   std::uint32_t chipSelect) noexcept;
```

#### Usage

```cpp
using namespace seeker::device;

// Check SPI device configuration
auto spi = getSpiDeviceInfo(0, 0);
if (spi.isUsable()) {
  fmt::print("SPI {}.{}: {} @ {:.1f} MHz\n",
             spi.busNumber, spi.chipSelect,
             toString(spi.config.mode), spi.config.speedMHz());
}
```

---

### CanBusInfo

**Header:** `CanBusInfo.hpp`
**Purpose:** Enumerate SocketCAN interfaces and query status.

#### Key Types

```cpp
enum class CanInterfaceType : std::uint8_t {
  UNKNOWN = 0,  // Unknown interface type
  PHYSICAL,     // Physical CAN controller (can0, can1)
  VIRTUAL,      // Virtual CAN for testing (vcan0)
  SLCAN,        // Serial-line CAN (slcan0)
  SOCKETCAND,   // Network-based CAN (socketcand)
  PEAK,         // PEAK-System PCAN devices
  KVASER,       // Kvaser devices
  VECTOR,       // Vector Informatik devices
};

enum class CanBusState : std::uint8_t {
  UNKNOWN = 0,    // State unknown or unavailable
  ERROR_ACTIVE,   // Normal operation (TEC/REC < 128)
  ERROR_WARNING,  // Error warning threshold reached (TEC/REC >= 96)
  ERROR_PASSIVE,  // Error passive state (TEC/REC >= 128)
  BUS_OFF,        // Bus-off state (TEC >= 256)
  STOPPED,        // Interface administratively stopped
};

struct CanCtrlMode {
  bool loopback{false};       // Local loopback mode
  bool listenOnly{false};     // Listen-only (no ACK/TX)
  bool tripleSampling{false}; // Triple sampling
  bool oneShot{false};        // One-shot mode (no retransmit)
  bool berr{false};           // Bus error reporting
  bool fd{false};             // CAN FD mode enabled
  bool presumeAck{false};     // Presume ACK on TX
  bool fdNonIso{false};       // Non-ISO CAN FD mode
  bool ccLen8Dlc{false};      // Classic CAN DLC = 8 encoding

  bool hasSpecialModes() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct CanBitTiming {
  std::uint32_t bitrate{0};      // Bits/second
  std::uint32_t samplePoint{0};  // Tenths of percent (875 = 87.5%)
  std::uint32_t tq{0};           // Time quantum (ns)
  std::uint32_t propSeg{0};
  std::uint32_t phaseSeg1{0};
  std::uint32_t phaseSeg2{0};
  std::uint32_t sjw{0};
  std::uint32_t brp{0};

  bool isConfigured() const noexcept;
  double samplePointPercent() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct CanErrorCounters {
  std::uint16_t txErrors{0};        // TEC
  std::uint16_t rxErrors{0};        // REC
  std::uint32_t busErrors{0};       // Bus error count (from berr)
  std::uint32_t errorWarning{0};    // Error warning transitions
  std::uint32_t errorPassive{0};    // Error passive transitions
  std::uint32_t busOff{0};          // Bus-off events
  std::uint32_t arbitrationLost{0}; // Arbitration lost events
  std::uint32_t restarts{0};        // Controller restart count

  bool hasErrors() const noexcept;
  std::uint32_t totalErrors() const noexcept;
  std::string toString() const;   // NOT RT-safe
};

struct CanInterfaceStats {
  std::uint64_t txFrames{0};
  std::uint64_t rxFrames{0};
  std::uint64_t txBytes{0};
  std::uint64_t rxBytes{0};
  std::uint64_t txDropped{0};
  std::uint64_t rxDropped{0};
  std::uint64_t txErrors{0};
  std::uint64_t rxErrors{0};

  std::string toString() const;   // NOT RT-safe
};

struct CanInterfaceInfo {
  std::array<char, 32> name{};       // "can0"
  std::array<char, 128> sysfsPath{};
  std::array<char, 64> driver{};
  CanInterfaceType type{CanInterfaceType::UNKNOWN};
  CanBusState state{CanBusState::UNKNOWN};
  CanBitTiming bitTiming{};           // Arbitration phase timing
  CanBitTiming dataBitTiming{};       // Data phase timing (CAN FD only)
  CanCtrlMode ctrlMode{};
  CanErrorCounters errors{};
  CanInterfaceStats stats{};
  std::uint32_t clockFreq{0};        // Controller clock frequency (Hz)
  std::uint32_t txqLen{0};           // Transmit queue length
  std::int32_t ifindex{-1};          // Interface index
  bool exists{false};
  bool isUp{false};
  bool isRunning{false};

  bool isUsable() const noexcept;
  bool isFd() const noexcept;
  bool hasErrors() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct CanInterfaceList {
  CanInterfaceInfo interfaces[MAX_CAN_INTERFACES]{};
  std::size_t count{0};

  const CanInterfaceInfo* find(const char* name) const noexcept;
  bool empty() const noexcept;
  std::size_t countUp() const noexcept;
  std::size_t countPhysical() const noexcept;
  std::size_t countWithErrors() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
// Query specific interface (Mostly RT-safe)
[[nodiscard]] CanInterfaceInfo getCanInterfaceInfo(const char* name) noexcept;

// Get bit timing (RT-safe)
[[nodiscard]] CanBitTiming getCanBitTiming(const char* name) noexcept;

// Get error counters (RT-safe)
[[nodiscard]] CanErrorCounters getCanErrorCounters(const char* name) noexcept;

// Get bus state (RT-safe)
[[nodiscard]] CanBusState getCanBusState(const char* name) noexcept;

// Enumerate all interfaces (NOT RT-safe: directory scan)
[[nodiscard]] CanInterfaceList getAllCanInterfaces() noexcept;

// Check if interface is CAN type (RT-safe)
[[nodiscard]] bool isCanInterface(const char* name) noexcept;

// Check interface existence (RT-safe)
[[nodiscard]] bool canInterfaceExists(const char* name) noexcept;
```

#### Usage

```cpp
using namespace seeker::device;

// Check CAN interface status
auto can = getCanInterfaceInfo("can0");
if (can.isUsable()) {
  fmt::print("CAN {}: {} @ {} kbps, state: {}\n",
             can.name.data(), toString(can.type),
             can.bitTiming.bitrate / 1000, toString(can.state));

  if (can.hasErrors()) {
    fmt::print("  Errors: TEC={}, REC={}\n",
               can.errors.txErrors, can.errors.rxErrors);
  }
}
```

---

### GpioInfo

**Header:** `GpioInfo.hpp`
**Purpose:** Enumerate GPIO chips and query line configuration.

#### Key Types

```cpp
enum class GpioDirection : std::uint8_t {
  UNKNOWN = 0,  // Direction unknown or unavailable
  INPUT,        // Line configured as input
  OUTPUT,       // Line configured as output
};

enum class GpioDrive : std::uint8_t {
  UNKNOWN = 0,  // Drive mode unknown
  PUSH_PULL,    // Push-pull (default)
  OPEN_DRAIN,   // Open drain (requires external pull-up)
  OPEN_SOURCE,  // Open source (requires external pull-down)
};

enum class GpioBias : std::uint8_t {
  UNKNOWN = 0,  // Bias unknown
  DISABLED,     // No internal bias
  PULL_UP,      // Internal pull-up enabled
  PULL_DOWN,    // Internal pull-down enabled
};

enum class GpioEdge : std::uint8_t {
  NONE = 0,  // No edge detection
  RISING,    // Rising edge only
  FALLING,   // Falling edge only
  BOTH,      // Both edges
};

struct GpioLineFlags {
  bool used{false};           // Line is in use by a consumer
  bool activeLow{false};      // Active-low polarity
  GpioDirection direction{GpioDirection::UNKNOWN};
  GpioDrive drive{GpioDrive::UNKNOWN};
  GpioBias bias{GpioBias::UNKNOWN};
  GpioEdge edge{GpioEdge::NONE};

  bool hasSpecialConfig() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpioLineInfo {
  std::uint32_t offset{0};                      // Line offset within chip (0-based)
  std::array<char, 64> name{};                  // Line name (may be empty)
  std::array<char, 64> consumer{};              // Consumer holding line
  GpioLineFlags flags{};

  bool hasName() const noexcept;
  bool isUsed() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpioChipInfo {
  std::array<char, 64> name{};        // "gpiochip0"
  std::array<char, 64> label{};       // Controller label (e.g., "pinctrl-bcm2835")
  std::array<char, 128> path{};       // "/dev/gpiochip0"
  std::uint32_t numLines{0};          // Number of GPIO lines on this chip
  std::uint32_t linesUsed{0};         // Count of lines currently in use
  std::int32_t chipNumber{-1};        // Chip number (parsed from name)
  bool exists{false};
  bool accessible{false};

  bool isUsable() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpioChipList {
  GpioChipInfo chips[MAX_GPIO_CHIPS]{};
  std::size_t count{0};

  const GpioChipInfo* find(const char* name) const noexcept;
  const GpioChipInfo* findByNumber(std::int32_t chipNum) const noexcept;
  bool empty() const noexcept;
  std::uint32_t totalLines() const noexcept;
  std::uint32_t totalUsed() const noexcept;
  std::string toString() const;  // NOT RT-safe
};

struct GpioLineList {
  GpioLineInfo lines[MAX_GPIO_LINES_DETAILED]{};
  std::size_t count{0};
  std::int32_t chipNumber{-1};   // Source chip number

  const GpioLineInfo* findByOffset(std::uint32_t offset) const noexcept;
  const GpioLineInfo* findByName(const char* name) const noexcept;
  bool empty() const noexcept;
  std::size_t countUsed() const noexcept;
  std::size_t countInputs() const noexcept;
  std::size_t countOutputs() const noexcept;
  std::string toString() const;  // NOT RT-safe
};
```

#### API

```cpp
// Query specific chip by number (RT-safe)
[[nodiscard]] GpioChipInfo getGpioChipInfo(std::int32_t chipNum) noexcept;

// Query chip by name (RT-safe)
[[nodiscard]] GpioChipInfo getGpioChipInfoByName(const char* name) noexcept;

// Query specific line (RT-safe)
[[nodiscard]] GpioLineInfo getGpioLineInfo(std::int32_t chipNum,
                                           std::uint32_t lineOffset) noexcept;

// Get all lines on a chip (NOT RT-safe: multiple ioctls)
[[nodiscard]] GpioLineList getGpioLines(std::int32_t chipNum) noexcept;

// Enumerate all chips (NOT RT-safe: directory scan)
[[nodiscard]] GpioChipList getAllGpioChips() noexcept;

// Check chip existence (RT-safe)
[[nodiscard]] bool gpioChipExists(std::int32_t chipNum) noexcept;

// Parse chip number from name (RT-safe)
[[nodiscard]] bool parseGpioChipNumber(const char* name,
                                       std::int32_t& outChipNum) noexcept;

// Find GPIO by global number (NOT RT-safe: may enumerate chips)
[[nodiscard]] bool findGpioLine(std::int32_t gpioNum,
                                std::int32_t& outChipNum,
                                std::uint32_t& outOffset) noexcept;
```

#### Usage

```cpp
using namespace seeker::device;

// Check GPIO chip
auto chip = getGpioChipInfo(0);
if (chip.isUsable()) {
  fmt::print("GPIO chip {}: {} ({} lines)\n",
             chip.chipNumber, chip.label.data(), chip.numLines);
}

// Query specific line
auto line = getGpioLineInfo(0, 17);
if (line.isUsed()) {
  fmt::print("Line {}: {} (used by {})\n",
             line.offset, toString(line.flags.direction),
             line.consumer.data());
}
```

---

## Common Patterns

### Device Enumeration

```cpp
using namespace seeker::device;

// Enumerate all device buses
auto ports = getAllSerialPorts();
auto i2c = getAllI2cBuses();
auto spi = getAllSpiDevices();
auto can = getAllCanInterfaces();
auto gpio = getAllGpioChips();

fmt::print("Serial ports: {}\n", ports.count);
fmt::print("I2C buses:    {}\n", i2c.count);
fmt::print("SPI devices:  {}\n", spi.count);
fmt::print("CAN ifaces:   {}\n", can.count);
fmt::print("GPIO chips:   {}\n", gpio.count);
```

### Permission Checking

```cpp
using namespace seeker::device;

// Check device accessibility
auto port = getSerialPortInfo("ttyUSB0");
if (!port.exists) {
  fmt::print("Serial port not found\n");
} else if (!port.isAccessible()) {
  fmt::print("No permission to access {}\n", port.devicePath.data());
}

auto bus = getI2cBusInfo(1);
if (bus.exists && !bus.accessible) {
  fmt::print("Add user to i2c group: sudo usermod -a -G i2c $USER\n");
}
```

### Type Detection

```cpp
using namespace seeker::device;

// Detect serial port type
auto port = getSerialPortInfo("ttyUSB0");
switch (port.type) {
  case SerialPortType::USB_SERIAL:
    fmt::print("USB-serial: VID={:04x} PID={:04x}\n",
               port.usbInfo.vendorId, port.usbInfo.productId);
    break;
  case SerialPortType::BUILTIN_UART:
    fmt::print("Built-in UART\n");
    break;
  default:
    break;
}

// Detect CAN interface type
auto can = getCanInterfaceInfo("can0");
if (can.type == CanInterfaceType::VIRTUAL) {
  fmt::print("Virtual CAN (for testing)\n");
} else if (can.type == CanInterfaceType::PHYSICAL) {
  fmt::print("Physical CAN controller\n");
}
```

---

## Real-Time Considerations

### RT-Safe Functions

Most single-device query functions are RT-safe:

```cpp
// These are RT-safe (bounded sysfs reads, fixed-size output)
auto port = getSerialPortInfo("ttyUSB0");
auto bus = getI2cBusInfo(1);
auto spi = getSpiDeviceInfo(0, 0);
auto can = getCanInterfaceInfo("can0");
auto chip = getGpioChipInfo(0);
auto line = getGpioLineInfo(0, 17);
```

### NOT RT-Safe Functions

Enumeration and scanning functions are NOT RT-safe:

```cpp
// These are NOT RT-safe (directory scans, unbounded I/O)
auto ports = getAllSerialPorts();      // Scans /sys/class/tty
auto buses = getAllI2cBuses();         // Scans /sys/class/i2c-adapter
auto devs = scanI2cBus(1);             // Probes 128 addresses
auto spis = getAllSpiDevices();        // Scans /sys/class/spidev
auto cans = getAllCanInterfaces();     // Scans /sys/class/net
auto chips = getAllGpioChips();        // Scans /dev/gpiochip*
auto lines = getGpioLines(0);          // Multiple ioctls
```

### Recommended Pattern

```cpp
// Query at startup (non-RT context)
auto devices = getAllSerialPorts();
auto targetPort = devices.find("ttyUSB0");

// Store name/path for RT use
char portName[32];
std::strncpy(portName, targetPort->name.data(), sizeof(portName));

// In RT thread: quick status check only
auto status = getSerialPortInfo(portName);
if (!status.exists || !status.isAccessible()) {
  // Handle device disconnect
}
```

---

## CLI Tools

The device domain includes 5 command-line tools: `device-info`, `device-serial`, `device-i2c`, `device-can`, `device-rtcheck`.

See: `tools/cpp/device/README.md` for detailed tool usage.

---

## Example: Device Bus Health Check

```cpp
#include "src/device/inc/SerialPortInfo.hpp"
#include "src/device/inc/I2cBusInfo.hpp"
#include "src/device/inc/CanBusInfo.hpp"

#include <fmt/core.h>

using namespace seeker::device;

int main() {
  int issues = 0;

  // Check required serial port
  auto port = getSerialPortInfo("ttyUSB0");
  if (!port.exists) {
    fmt::print("[FAIL] ttyUSB0: not found\n");
    ++issues;
  } else if (!port.isAccessible()) {
    fmt::print("[FAIL] ttyUSB0: no access\n");
    ++issues;
  } else {
    fmt::print("[ OK ] ttyUSB0: {} {:04x}:{:04x}\n",
               port.usbInfo.product.data(),
               port.usbInfo.vendorId, port.usbInfo.productId);
  }

  // Check required I2C bus
  auto bus = getI2cBusInfo(1);
  if (!bus.isUsable()) {
    fmt::print("[FAIL] i2c-1: not accessible\n");
    ++issues;
  } else {
    auto devices = scanI2cBus(1);
    fmt::print("[ OK ] i2c-1: {} devices found\n", devices.count);
  }

  // Check CAN interface
  auto can = getCanInterfaceInfo("can0");
  if (!can.exists) {
    fmt::print("[WARN] can0: not found\n");
  } else if (!can.isUsable()) {
    fmt::print("[FAIL] can0: not usable (state: {})\n",
               toString(can.state));
    ++issues;
  } else if (can.hasErrors()) {
    fmt::print("[WARN] can0: errors detected (TEC={}, REC={})\n",
               can.errors.txErrors, can.errors.rxErrors);
  } else {
    fmt::print("[ OK ] can0: {} @ {} kbps\n",
               toString(can.type), can.bitTiming.bitrate / 1000);
  }

  return issues > 0 ? 1 : 0;
}
```

---

## See Also

- `seeker::cpu` - CPU topology, frequency, isolation
- `seeker::memory` - Memory topology, hugepages, NUMA
- `seeker::network` - Network interfaces, ethtool, statistics
- `seeker::timing` - Clock sources, PTP, latency benchmarks
- `seeker::system` - Kernel info, capabilities, containers
