/**
 * @file SerialPortInfo.cpp
 * @brief Implementation of serial port enumeration and configuration queries.
 */

#include "src/device/inc/SerialPortInfo.hpp"
#include "src/helpers/inc/Strings.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace device {

using seeker::helpers::files::isCharDevice;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* TTY_SYS_PATH = "/sys/class/tty";
constexpr const char* DEV_PATH = "/dev";
constexpr std::size_t PATH_BUFFER_SIZE = 512;
constexpr std::size_t READ_BUFFER_SIZE = 256;

/// Known serial port prefixes for hardware UARTs.
constexpr const char* UART_PREFIXES[] = {
    "ttyS",     // Standard 8250/16550 UARTs
    "ttyAMA",   // ARM AMBA PL011 UARTs (Raspberry Pi, etc.)
    "ttySAC",   // Samsung S3C/S5P UARTs
    "ttyO",     // OMAP UARTs
    "ttyMSM",   // Qualcomm MSM UARTs
    "ttyHS",    // Tegra high-speed UARTs
    "ttyTHS",   // Tegra high-speed UARTs (alternative)
    "ttymxc",   // i.MX UARTs
    "ttyLP",    // i.MX LPUART
    "ttyPS",    // Xilinx Zynq UARTs
    "ttyUL",    // Xilinx UARTLite
    "ttyAL",    // Altera/Intel FPGA UARTs
    "ttyNS",    // Nuvoton UARTs
    "ttyRPMSG", // RPMsg virtual UARTs
};

/// Known USB-serial prefixes.
constexpr const char* USB_PREFIXES[] = {
    "ttyUSB", // USB-serial (FTDI, PL2303, CH340, etc.)
    "ttyACM", // USB CDC ACM (Arduino, modems, etc.)
};

/// Virtual/pseudo terminal prefixes to exclude.
constexpr const char* VIRTUAL_PREFIXES[] = {
    "tty", // Virtual console (just "tty" followed by digit)
    "pty", // Pseudo terminals
    "pts", // Pseudo terminal slaves
};

/**
 * Read hex value from sysfs file.
 */
inline std::uint32_t readHexFromFile(const char* path) noexcept {
  char buf[READ_BUFFER_SIZE];
  if (readFileToBuffer(path, buf, sizeof(buf)) == 0) {
    return 0;
  }

  char* endPtr = nullptr;
  const unsigned long VAL = std::strtoul(buf, &endPtr, 16);
  if (endPtr == buf) {
    return 0;
  }

  return static_cast<std::uint32_t>(VAL);
}

/* ----------------------------- Baud Rate Helpers ----------------------------- */

/**
 * Convert termios speed_t to numeric baud rate.
 */
inline std::uint32_t speedToBaud(speed_t speed) noexcept {
  switch (speed) {
  case B0:
    return 0;
  case B50:
    return 50;
  case B75:
    return 75;
  case B110:
    return 110;
  case B134:
    return 134;
  case B150:
    return 150;
  case B200:
    return 200;
  case B300:
    return 300;
  case B600:
    return 600;
  case B1200:
    return 1200;
  case B1800:
    return 1800;
  case B2400:
    return 2400;
  case B4800:
    return 4800;
  case B9600:
    return 9600;
  case B19200:
    return 19200;
  case B38400:
    return 38400;
  case B57600:
    return 57600;
  case B115200:
    return 115200;
  case B230400:
    return 230400;
  case B460800:
    return 460800;
  case B500000:
    return 500000;
  case B576000:
    return 576000;
  case B921600:
    return 921600;
  case B1000000:
    return 1000000;
  case B1152000:
    return 1152000;
  case B1500000:
    return 1500000;
  case B2000000:
    return 2000000;
  case B2500000:
    return 2500000;
  case B3000000:
    return 3000000;
  case B3500000:
    return 3500000;
  case B4000000:
    return 4000000;
  default:
    return 0;
  }
}

/* ----------------------------- Type Classification ----------------------------- */

/**
 * Classify serial port type from name.
 */
inline SerialPortType classifyPortType(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return SerialPortType::UNKNOWN;
  }

  // USB-serial devices
  for (const char* prefix : USB_PREFIXES) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
      if (std::strncmp(name, "ttyACM", 6) == 0) {
        return SerialPortType::USB_ACM;
      }
      return SerialPortType::USB_SERIAL;
    }
  }

  // Built-in UARTs
  for (const char* prefix : UART_PREFIXES) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
      return SerialPortType::BUILTIN_UART;
    }
  }

  // Check for virtual/pseudo terminals
  for (const char* prefix : VIRTUAL_PREFIXES) {
    const std::size_t LEN = std::strlen(prefix);
    if (std::strncmp(name, prefix, LEN) == 0) {
      // "tty" followed by digit is virtual console
      if (std::strcmp(prefix, "tty") == 0 && name[3] >= '0' && name[3] <= '9') {
        return SerialPortType::VIRTUAL;
      }
      if (std::strcmp(prefix, "pty") == 0 || std::strcmp(prefix, "pts") == 0) {
        return SerialPortType::VIRTUAL;
      }
    }
  }

  return SerialPortType::UNKNOWN;
}

/* ----------------------------- Query Helpers ----------------------------- */

/**
 * Open serial port for configuration query (non-blocking, no modem control).
 */
inline int openSerialForQuery(const char* path) noexcept {
  return ::open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
}

/**
 * Query termios configuration.
 */
inline SerialConfig queryTermios(int fd) noexcept {
  SerialConfig cfg{};

  struct termios tio{};
  if (::tcgetattr(fd, &tio) != 0) {
    return cfg;
  }

  // Data bits
  switch (tio.c_cflag & CSIZE) {
  case CS5:
    cfg.dataBits = 5;
    break;
  case CS6:
    cfg.dataBits = 6;
    break;
  case CS7:
    cfg.dataBits = 7;
    break;
  case CS8:
    cfg.dataBits = 8;
    break;
  default:
    cfg.dataBits = 8;
    break;
  }

  // Parity
  if ((tio.c_cflag & PARENB) == 0) {
    cfg.parity = 'N';
  } else if ((tio.c_cflag & PARODD) != 0) {
    cfg.parity = 'O';
  } else {
    cfg.parity = 'E';
  }

  // Stop bits
  cfg.stopBits = ((tio.c_cflag & CSTOPB) != 0) ? 2 : 1;

  // Flow control
  cfg.hwFlowControl = ((tio.c_cflag & CRTSCTS) != 0);
  cfg.swFlowControl = ((tio.c_iflag & (IXON | IXOFF)) != 0);

  // Other flags
  cfg.localMode = ((tio.c_cflag & CLOCAL) != 0);
  cfg.rawMode = ((tio.c_lflag & ICANON) == 0);

  // Baud rate
  cfg.baudRate.input = speedToBaud(cfgetispeed(&tio));
  cfg.baudRate.output = speedToBaud(cfgetospeed(&tio));

  return cfg;
}

/**
 * Query RS485 configuration.
 */
inline Rs485Config queryRs485(int fd) noexcept {
  Rs485Config cfg{};

  struct serial_rs485 rs485{};
  if (::ioctl(fd, TIOCGRS485, &rs485) != 0) {
    return cfg;
  }

  cfg.enabled = ((rs485.flags & SER_RS485_ENABLED) != 0);
  cfg.rtsOnSend = ((rs485.flags & SER_RS485_RTS_ON_SEND) != 0);
  cfg.rtsAfterSend = ((rs485.flags & SER_RS485_RTS_AFTER_SEND) != 0);
  cfg.rxDuringTx = ((rs485.flags & SER_RS485_RX_DURING_TX) != 0);

#ifdef SER_RS485_TERMINATE_BUS
  cfg.terminationEnabled = ((rs485.flags & SER_RS485_TERMINATE_BUS) != 0);
#endif

  cfg.delayRtsBeforeSend = static_cast<std::int32_t>(rs485.delay_rts_before_send);
  cfg.delayRtsAfterSend = static_cast<std::int32_t>(rs485.delay_rts_after_send);

  return cfg;
}

/**
 * Query USB device information from sysfs.
 */
inline UsbSerialInfo queryUsbInfo(const char* sysfsPath) noexcept {
  UsbSerialInfo info{};

  if (sysfsPath == nullptr || sysfsPath[0] == '\0') {
    return info;
  }

  char pathBuf[PATH_BUFFER_SIZE];
  char readBuf[READ_BUFFER_SIZE];

  // Walk up the sysfs path to find the USB device
  // The structure is: /sys/class/tty/ttyUSBx/device -> ../../usb.../...
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../idVendor", sysfsPath);
  info.vendorId = static_cast<std::uint16_t>(readHexFromFile(pathBuf));

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../idProduct", sysfsPath);
  info.productId = static_cast<std::uint16_t>(readHexFromFile(pathBuf));

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../manufacturer", sysfsPath);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.manufacturer, readBuf);
  }

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../product", sysfsPath);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.product, readBuf);
  }

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../serial", sysfsPath);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.serial, readBuf);
  }

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../busnum", sysfsPath);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    info.busNum = static_cast<std::uint8_t>(std::atoi(readBuf));
  }

  std::snprintf(pathBuf, sizeof(pathBuf), "%s/device/../devnum", sysfsPath);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    info.devNum = static_cast<std::uint8_t>(std::atoi(readBuf));
  }

  return info;
}

/**
 * Query driver name from sysfs.
 */
inline void queryDriverName(const char* sysfsPath, char* driverBuf, std::size_t bufSize) noexcept {
  if (sysfsPath == nullptr || driverBuf == nullptr || bufSize == 0) {
    if (driverBuf != nullptr && bufSize > 0) {
      driverBuf[0] = '\0';
    }
    return;
  }

  char linkPath[PATH_BUFFER_SIZE];
  std::snprintf(linkPath, sizeof(linkPath), "%s/device/driver", sysfsPath);

  char resolved[PATH_BUFFER_SIZE];
  const char* REAL = ::realpath(linkPath, resolved);
  if (REAL == nullptr) {
    driverBuf[0] = '\0';
    return;
  }

  // Extract driver name from path (last component)
  const char* NAME = std::strrchr(resolved, '/');
  if (NAME != nullptr) {
    ++NAME;
  } else {
    NAME = resolved;
  }

  std::size_t i = 0;
  for (; i < bufSize - 1 && NAME[i] != '\0'; ++i) {
    driverBuf[i] = NAME[i];
  }
  driverBuf[i] = '\0';
}

/**
 * Check if this tty device looks like a real serial port.
 * Filters out virtual consoles, pseudo terminals, etc.
 */
inline bool looksLikeSerialPort(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  // Check for known serial prefixes
  for (const char* prefix : UART_PREFIXES) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
      return true;
    }
  }

  for (const char* prefix : USB_PREFIXES) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
      return true;
    }
  }

  return false;
}

} // anonymous namespace

/* ----------------------------- SerialPortType ----------------------------- */

const char* toString(SerialPortType type) noexcept {
  switch (type) {
  case SerialPortType::UNKNOWN:
    return "unknown";
  case SerialPortType::BUILTIN_UART:
    return "builtin-uart";
  case SerialPortType::USB_SERIAL:
    return "usb-serial";
  case SerialPortType::USB_ACM:
    return "usb-acm";
  case SerialPortType::PLATFORM:
    return "platform";
  case SerialPortType::VIRTUAL:
    return "virtual";
  default:
    return "unknown";
  }
}

/* ----------------------------- SerialBaudRate Methods ----------------------------- */

bool SerialBaudRate::isSet() const noexcept { return input > 0 || output > 0; }

bool SerialBaudRate::isSymmetric() const noexcept { return input == output; }

/* ----------------------------- SerialConfig Methods ----------------------------- */

std::array<char, 8> SerialConfig::notation() const noexcept {
  std::array<char, 8> buf{};
  std::snprintf(buf.data(), buf.size(), "%u%c%u", dataBits, parity, stopBits);
  return buf;
}

bool SerialConfig::isValid() const noexcept {
  return dataBits >= 5 && dataBits <= 8 && (parity == 'N' || parity == 'E' || parity == 'O') &&
         (stopBits == 1 || stopBits == 2);
}

std::string SerialConfig::toString() const {
  std::string out;
  out += fmt::format("{}, ", notation().data());

  if (baudRate.isSet()) {
    if (baudRate.isSymmetric()) {
      out += fmt::format("{} baud", baudRate.output);
    } else {
      out += fmt::format("{}i/{}o baud", baudRate.input, baudRate.output);
    }
  } else {
    out += "baud unknown";
  }

  if (hwFlowControl) {
    out += ", HW flow";
  }
  if (swFlowControl) {
    out += ", SW flow";
  }
  if (rawMode) {
    out += ", raw";
  }

  return out;
}

/* ----------------------------- Rs485Config Methods ----------------------------- */

bool Rs485Config::isConfigured() const noexcept { return enabled; }

std::string Rs485Config::toString() const {
  if (!enabled) {
    return "RS485: disabled";
  }

  std::string out = "RS485: enabled";

  if (rtsOnSend) {
    out += ", RTS on send";
  }
  if (rtsAfterSend) {
    out += ", RTS after send";
  }
  if (rxDuringTx) {
    out += ", RX during TX";
  }
  if (terminationEnabled) {
    out += ", terminated";
  }
  if (delayRtsBeforeSend > 0) {
    out += fmt::format(", {}us pre-delay", delayRtsBeforeSend);
  }
  if (delayRtsAfterSend > 0) {
    out += fmt::format(", {}us post-delay", delayRtsAfterSend);
  }

  return out;
}

/* ----------------------------- UsbSerialInfo Methods ----------------------------- */

bool UsbSerialInfo::isAvailable() const noexcept { return vendorId != 0 || productId != 0; }

std::string UsbSerialInfo::toString() const {
  if (!isAvailable()) {
    return "USB: not available";
  }

  std::string out = fmt::format("USB: {:04x}:{:04x}", vendorId, productId);

  if (manufacturer[0] != '\0') {
    out += fmt::format(" {}", manufacturer.data());
  }
  if (product[0] != '\0') {
    out += fmt::format(" {}", product.data());
  }
  if (serial[0] != '\0') {
    out += fmt::format(" [{}]", serial.data());
  }
  if (busNum > 0 || devNum > 0) {
    out += fmt::format(" (bus {} dev {})", busNum, devNum);
  }

  return out;
}

/* ----------------------------- SerialPortInfo Methods ----------------------------- */

bool SerialPortInfo::isUsb() const noexcept {
  return type == SerialPortType::USB_SERIAL || type == SerialPortType::USB_ACM;
}

bool SerialPortInfo::isAccessible() const noexcept { return exists && (readable || writable); }

bool SerialPortInfo::supportsRs485() const noexcept {
  return rs485.enabled || (isOpen && type == SerialPortType::BUILTIN_UART);
}

std::string SerialPortInfo::toString() const {
  std::string out = fmt::format("{}: {}", name.data(), seeker::device::toString(type));

  if (!exists) {
    out += " (not found)";
    return out;
  }

  if (!isAccessible()) {
    out += " (no access)";
  } else {
    std::string access;
    if (readable)
      access += "r";
    if (writable)
      access += "w";
    out += fmt::format(" ({})", access);
  }

  if (driver[0] != '\0') {
    out += fmt::format("\n  Driver: {}", driver.data());
  }

  if (isOpen && config.isValid()) {
    out += fmt::format("\n  Config: {}", config.toString());
  }

  if (rs485.enabled) {
    out += fmt::format("\n  {}", rs485.toString());
  }

  if (isUsb() && usbInfo.isAvailable()) {
    out += fmt::format("\n  {}", usbInfo.toString());
  }

  return out;
}

/* ----------------------------- SerialPortList Methods ----------------------------- */

const SerialPortInfo* SerialPortList::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(ports[i].name.data(), name) == 0) {
      return &ports[i];
    }
  }

  return nullptr;
}

const SerialPortInfo* SerialPortList::findByPath(const char* path) const noexcept {
  if (path == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(ports[i].devicePath.data(), path) == 0) {
      return &ports[i];
    }
  }

  return nullptr;
}

bool SerialPortList::empty() const noexcept { return count == 0; }

std::size_t SerialPortList::countByType(SerialPortType type) const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (ports[i].type == type) {
      ++n;
    }
  }
  return n;
}

std::size_t SerialPortList::countAccessible() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (ports[i].isAccessible()) {
      ++n;
    }
  }
  return n;
}

std::string SerialPortList::toString() const {
  if (count == 0) {
    return "No serial ports found";
  }

  std::string out =
      fmt::format("Serial ports: {} found ({} accessible)\n", count, countAccessible());

  for (std::size_t i = 0; i < count; ++i) {
    out += "\n" + ports[i].toString() + "\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

SerialPortInfo getSerialPortInfo(const char* name) noexcept {
  SerialPortInfo info{};

  if (name == nullptr || name[0] == '\0') {
    return info;
  }

  // Handle both "/dev/ttyUSB0" and "ttyUSB0" forms
  const char* devName = name;
  if (std::strncmp(name, "/dev/", 5) == 0) {
    devName = name + 5;
  }

  copyToFixedArray(info.name, devName);

  // Build device path
  std::snprintf(info.devicePath.data(), info.devicePath.size(), "%s/%s", DEV_PATH, devName);

  // Build sysfs path
  std::snprintf(info.sysfsPath.data(), info.sysfsPath.size(), "%s/%s", TTY_SYS_PATH, devName);

  // Classify type
  info.type = classifyPortType(devName);

  // Check existence
  struct stat st;
  if (::stat(info.devicePath.data(), &st) != 0) {
    info.exists = false;
    return info;
  }

  info.exists = true;

  // Check permissions
  info.readable = (::access(info.devicePath.data(), R_OK) == 0);
  info.writable = (::access(info.devicePath.data(), W_OK) == 0);

  // Get driver name
  queryDriverName(info.sysfsPath.data(), info.driver.data(), info.driver.size());

  // Try to open for configuration query
  const int FD = openSerialForQuery(info.devicePath.data());
  if (FD >= 0) {
    info.isOpen = true;
    info.config = queryTermios(FD);
    info.rs485 = queryRs485(FD);
    ::close(FD);
  }

  // Get USB info for USB-serial devices
  if (info.isUsb()) {
    info.usbInfo = queryUsbInfo(info.sysfsPath.data());
  }

  return info;
}

SerialConfig getSerialConfig(const char* name) noexcept {
  SerialConfig cfg{};

  if (name == nullptr || name[0] == '\0') {
    return cfg;
  }

  // Build path
  char path[PATH_BUFFER_SIZE];
  if (std::strncmp(name, "/dev/", 5) == 0) {
    std::snprintf(path, sizeof(path), "%s", name);
  } else {
    std::snprintf(path, sizeof(path), "%s/%s", DEV_PATH, name);
  }

  const int FD = openSerialForQuery(path);
  if (FD < 0) {
    return cfg;
  }

  cfg = queryTermios(FD);
  ::close(FD);

  return cfg;
}

Rs485Config getRs485Config(const char* name) noexcept {
  Rs485Config cfg{};

  if (name == nullptr || name[0] == '\0') {
    return cfg;
  }

  // Build path
  char path[PATH_BUFFER_SIZE];
  if (std::strncmp(name, "/dev/", 5) == 0) {
    std::snprintf(path, sizeof(path), "%s", name);
  } else {
    std::snprintf(path, sizeof(path), "%s/%s", DEV_PATH, name);
  }

  const int FD = openSerialForQuery(path);
  if (FD < 0) {
    return cfg;
  }

  cfg = queryRs485(FD);
  ::close(FD);

  return cfg;
}

SerialPortList getAllSerialPorts() noexcept {
  SerialPortList list{};

  DIR* dir = ::opendir(TTY_SYS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_SERIAL_PORTS) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Only include entries that look like serial ports
    if (!looksLikeSerialPort(entry->d_name)) {
      continue;
    }

    // Check if device file exists
    char devPath[PATH_BUFFER_SIZE];
    std::snprintf(devPath, sizeof(devPath), "%s/%s", DEV_PATH, entry->d_name);
    if (!isCharDevice(devPath)) {
      continue;
    }

    list.ports[list.count] = getSerialPortInfo(entry->d_name);
    ++list.count;
  }

  ::closedir(dir);
  return list;
}

bool isSerialPortName(const char* name) noexcept { return looksLikeSerialPort(name); }

} // namespace device

} // namespace seeker