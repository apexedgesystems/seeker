/**
 * @file I2cBusInfo.cpp
 * @brief Implementation of I2C bus enumeration and device scanning.
 */

#include "src/device/inc/I2cBusInfo.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* I2C_DEV_PATH = "/dev";
constexpr const char* I2C_SYS_PATH = "/sys/class/i2c-adapter";
constexpr std::size_t PATH_BUFFER_SIZE = 256;

/// Reserved I2C addresses to skip during scanning.
/// 0x00-0x02: Reserved for general call, CBUS, etc.
/// 0x78-0x7F: Reserved for 10-bit addressing and future use.
constexpr std::uint8_t RESERVED_ADDR_START_LOW = 0x00;
constexpr std::uint8_t RESERVED_ADDR_END_LOW = 0x02;
constexpr std::uint8_t RESERVED_ADDR_START_HIGH = 0x78;
constexpr std::uint8_t RESERVED_ADDR_END_HIGH = 0x7F;

/* ----------------------------- I2C Helpers ----------------------------- */

/**
 * Open I2C device for operations.
 */
inline int openI2cDevice(std::uint32_t busNumber) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/i2c-%u", I2C_DEV_PATH, busNumber);
  return ::open(path, O_RDWR | O_CLOEXEC);
}

/**
 * Query I2C functionality flags.
 */
inline I2cFunctionality queryFunctionality(int fd) noexcept {
  I2cFunctionality func{};

  unsigned long funcs = 0;
  if (::ioctl(fd, I2C_FUNCS, &funcs) < 0) {
    return func;
  }

  func.i2c = ((funcs & I2C_FUNC_I2C) != 0);
  func.tenBitAddr = ((funcs & I2C_FUNC_10BIT_ADDR) != 0);
  func.smbusQuick = ((funcs & I2C_FUNC_SMBUS_QUICK) != 0);
  func.smbusByte = ((funcs & (I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE)) != 0);
  func.smbusWord =
      ((funcs & (I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_WRITE_WORD_DATA)) != 0);
  func.smbusBlock =
      ((funcs & (I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_BLOCK_DATA)) != 0);
  func.smbusPec = ((funcs & I2C_FUNC_SMBUS_PEC) != 0);
  func.smbusI2cBlock = ((funcs & I2C_FUNC_SMBUS_READ_I2C_BLOCK) != 0);
  func.protocolMangling = ((funcs & I2C_FUNC_PROTOCOL_MANGLING) != 0);

  return func;
}

/**
 * Check if address is reserved and should be skipped.
 */
inline bool isReservedAddress(std::uint8_t addr) noexcept {
  return (addr >= RESERVED_ADDR_START_LOW && addr <= RESERVED_ADDR_END_LOW) ||
         (addr >= RESERVED_ADDR_START_HIGH && addr <= RESERVED_ADDR_END_HIGH);
}

/**
 * Probe a single I2C address using SMBus quick command.
 */
inline bool probeAddressQuick(int fd, std::uint8_t addr) noexcept {
  // Set slave address
  if (::ioctl(fd, I2C_SLAVE, addr) < 0) {
    // Try force if EBUSY (device already has a driver)
    if (errno == EBUSY) {
      if (::ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return false;
      }
    } else {
      return false;
    }
  }

  // SMBus quick write
  struct i2c_smbus_ioctl_data args{};
  args.read_write = I2C_SMBUS_WRITE;
  args.command = 0;
  args.size = I2C_SMBUS_QUICK;
  args.data = nullptr;

  return ::ioctl(fd, I2C_SMBUS, &args) >= 0;
}

/**
 * Probe a single I2C address using read byte.
 */
inline bool probeAddressRead(int fd, std::uint8_t addr) noexcept {
  // Set slave address
  if (::ioctl(fd, I2C_SLAVE, addr) < 0) {
    if (errno == EBUSY) {
      if (::ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
        return false;
      }
    } else {
      return false;
    }
  }

  // Try to read a byte
  union i2c_smbus_data data{};
  struct i2c_smbus_ioctl_data args{};
  args.read_write = I2C_SMBUS_READ;
  args.command = 0;
  args.size = I2C_SMBUS_BYTE;
  args.data = &data;

  return ::ioctl(fd, I2C_SMBUS, &args) >= 0;
}

/**
 * Get adapter name from sysfs.
 */
inline void queryAdapterName(std::uint32_t busNumber, char* buf, std::size_t bufSize) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/i2c-%u/name", I2C_SYS_PATH, busNumber);

  if (readFileToBuffer(path, buf, bufSize) == 0) {
    buf[0] = '\0';
  }
}

} // anonymous namespace

/* ----------------------------- I2cFunctionality Methods ----------------------------- */

bool I2cFunctionality::hasBasicI2c() const noexcept { return i2c; }

bool I2cFunctionality::hasSmbus() const noexcept {
  return smbusQuick || smbusByte || smbusWord || smbusBlock;
}

std::string I2cFunctionality::toString() const {
  std::string out = "I2C functionality:";

  if (i2c)
    out += " I2C";
  if (tenBitAddr)
    out += " 10-bit";
  if (smbusQuick)
    out += " SMBus-quick";
  if (smbusByte)
    out += " SMBus-byte";
  if (smbusWord)
    out += " SMBus-word";
  if (smbusBlock)
    out += " SMBus-block";
  if (smbusPec)
    out += " PEC";
  if (smbusI2cBlock)
    out += " I2C-block";
  if (protocolMangling)
    out += " mangling";

  if (!hasBasicI2c() && !hasSmbus()) {
    out += " none";
  }

  return out;
}

/* ----------------------------- I2cDevice Methods ----------------------------- */

bool I2cDevice::isValid() const noexcept {
  return address >= I2C_ADDR_MIN && address <= I2C_ADDR_MAX && responsive;
}

/* ----------------------------- I2cDeviceList Methods ----------------------------- */

bool I2cDeviceList::empty() const noexcept { return count == 0; }

bool I2cDeviceList::hasAddress(std::uint8_t addr) const noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].address == addr && devices[i].responsive) {
      return true;
    }
  }
  return false;
}

std::string I2cDeviceList::addressList() const {
  if (count == 0) {
    return "none";
  }

  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].responsive) {
      if (!out.empty()) {
        out += ", ";
      }
      out += fmt::format("0x{:02x}", devices[i].address);
    }
  }

  return out.empty() ? "none" : out;
}

std::string I2cDeviceList::toString() const {
  if (count == 0) {
    return "No devices found";
  }

  return fmt::format("{} device(s): {}", count, addressList());
}

/* ----------------------------- I2cBusInfo Methods ----------------------------- */

bool I2cBusInfo::isUsable() const noexcept {
  return exists && accessible && (functionality.hasBasicI2c() || functionality.hasSmbus());
}

bool I2cBusInfo::supports10BitAddr() const noexcept { return functionality.tenBitAddr; }

bool I2cBusInfo::supportsSmbus() const noexcept { return functionality.hasSmbus(); }

std::string I2cBusInfo::toString() const {
  std::string out = fmt::format("{}: ", name.data());

  if (!exists) {
    out += "not found";
    return out;
  }

  if (!accessible) {
    out += "no access";
    return out;
  }

  if (adapterName[0] != '\0') {
    out += fmt::format("{}", adapterName.data());
  } else {
    out += "unknown adapter";
  }

  out += fmt::format("\n  {}", functionality.toString());

  if (scanned) {
    out += fmt::format("\n  Devices: {}", scannedDevices.toString());
  }

  return out;
}

/* ----------------------------- I2cBusList Methods ----------------------------- */

const I2cBusInfo* I2cBusList::findByNumber(std::uint32_t busNumber) const noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (buses[i].busNumber == busNumber) {
      return &buses[i];
    }
  }
  return nullptr;
}

const I2cBusInfo* I2cBusList::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(buses[i].name.data(), name) == 0) {
      return &buses[i];
    }
  }

  return nullptr;
}

bool I2cBusList::empty() const noexcept { return count == 0; }

std::size_t I2cBusList::countAccessible() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (buses[i].accessible) {
      ++n;
    }
  }
  return n;
}

std::string I2cBusList::toString() const {
  if (count == 0) {
    return "No I2C buses found";
  }

  std::string out = fmt::format("I2C buses: {} found ({} accessible)\n", count, countAccessible());

  for (std::size_t i = 0; i < count; ++i) {
    out += "\n" + buses[i].toString() + "\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

I2cBusInfo getI2cBusInfo(std::uint32_t busNumber) noexcept {
  I2cBusInfo info{};
  info.busNumber = busNumber;

  // Build paths
  std::snprintf(info.name.data(), info.name.size(), "i2c-%u", busNumber);
  std::snprintf(info.devicePath.data(), info.devicePath.size(), "%s/i2c-%u", I2C_DEV_PATH,
                busNumber);
  std::snprintf(info.sysfsPath.data(), info.sysfsPath.size(), "%s/i2c-%u", I2C_SYS_PATH, busNumber);

  // Check existence
  info.exists = isCharDevice(info.devicePath.data());
  if (!info.exists) {
    return info;
  }

  // Check accessibility
  info.accessible = (::access(info.devicePath.data(), R_OK | W_OK) == 0);

  // Get adapter name
  queryAdapterName(busNumber, info.adapterName.data(), info.adapterName.size());

  // Get functionality
  if (info.accessible) {
    const int FD = openI2cDevice(busNumber);
    if (FD >= 0) {
      info.functionality = queryFunctionality(FD);
      ::close(FD);
    }
  }

  return info;
}

I2cBusInfo getI2cBusInfoByName(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return I2cBusInfo{};
  }

  std::uint32_t busNumber = 0;
  if (!parseI2cBusNumber(name, busNumber)) {
    return I2cBusInfo{};
  }

  return getI2cBusInfo(busNumber);
}

I2cFunctionality getI2cFunctionality(std::uint32_t busNumber) noexcept {
  const int FD = openI2cDevice(busNumber);
  if (FD < 0) {
    return I2cFunctionality{};
  }

  I2cFunctionality func = queryFunctionality(FD);
  ::close(FD);

  return func;
}

I2cDeviceList scanI2cBus(std::uint32_t busNumber) noexcept {
  I2cDeviceList list{};

  const int FD = openI2cDevice(busNumber);
  if (FD < 0) {
    return list;
  }

  // Get functionality to choose probe method
  const I2cFunctionality FUNC = queryFunctionality(FD);
  const bool USE_QUICK = FUNC.smbusQuick;

  // Scan address range
  for (std::uint8_t addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX && list.count < MAX_I2C_DEVICES;
       ++addr) {
    if (isReservedAddress(addr)) {
      continue;
    }

    bool found = false;
    if (USE_QUICK) {
      found = probeAddressQuick(FD, addr);
    } else {
      found = probeAddressRead(FD, addr);
    }

    if (found) {
      list.devices[list.count].address = addr;
      list.devices[list.count].responsive = true;
      ++list.count;
    }
  }

  ::close(FD);
  return list;
}

I2cBusList getAllI2cBuses() noexcept {
  I2cBusList list{};

  DIR* dir = ::opendir(I2C_SYS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_I2C_BUSES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Look for i2c-N pattern
    if (std::strncmp(entry->d_name, "i2c-", 4) != 0) {
      continue;
    }

    // Parse bus number
    char* endPtr = nullptr;
    const unsigned long BUS_NUM = std::strtoul(entry->d_name + 4, &endPtr, 10);
    if (endPtr == entry->d_name + 4 || *endPtr != '\0') {
      continue;
    }

    list.buses[list.count] = getI2cBusInfo(static_cast<std::uint32_t>(BUS_NUM));
    ++list.count;
  }

  ::closedir(dir);
  return list;
}

bool probeI2cAddress(std::uint32_t busNumber, std::uint8_t address) noexcept {
  if (isReservedAddress(address)) {
    return false;
  }

  const int FD = openI2cDevice(busNumber);
  if (FD < 0) {
    return false;
  }

  // Get functionality to choose probe method
  const I2cFunctionality FUNC = queryFunctionality(FD);

  bool found = false;
  if (FUNC.smbusQuick) {
    found = probeAddressQuick(FD, address);
  } else {
    found = probeAddressRead(FD, address);
  }

  ::close(FD);
  return found;
}

bool parseI2cBusNumber(const char* name, std::uint32_t& outBusNumber) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  // Skip /dev/ prefix if present
  const char* ptr = name;
  if (std::strncmp(ptr, "/dev/", 5) == 0) {
    ptr += 5;
  }

  // Skip i2c- prefix if present
  if (std::strncmp(ptr, "i2c-", 4) == 0) {
    ptr += 4;
  }

  // Parse number
  char* endPtr = nullptr;
  const unsigned long VAL = std::strtoul(ptr, &endPtr, 10);

  if (endPtr == ptr || VAL > UINT32_MAX) {
    return false;
  }

  // Check for trailing garbage (allow but don't require end of string)
  if (*endPtr != '\0' && *endPtr != ' ' && *endPtr != '\n') {
    return false;
  }

  outBusNumber = static_cast<std::uint32_t>(VAL);
  return true;
}

} // namespace device

} // namespace seeker