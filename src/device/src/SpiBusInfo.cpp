/**
 * @file SpiBusInfo.cpp
 * @brief Implementation of SPI bus enumeration and device information queries.
 */

#include "src/device/inc/SpiBusInfo.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
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

constexpr const char* SPI_DEV_PATH = "/dev";
constexpr const char* SPI_SYS_CLASS_PATH = "/sys/class/spidev";
constexpr const char* SPI_SYS_BUS_PATH = "/sys/bus/spi/devices";
constexpr std::size_t PATH_BUFFER_SIZE = 256;

/* ----------------------------- SPI Helpers ----------------------------- */

/**
 * Open SPI device for configuration query.
 */
inline int openSpiDevice(std::uint32_t bus, std::uint32_t cs) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/spidev%u.%u", SPI_DEV_PATH, bus, cs);
  return ::open(path, O_RDWR | O_CLOEXEC);
}

/**
 * Query SPI mode and flags.
 */
inline void querySpiMode(int fd, SpiConfig& cfg) noexcept {
  std::uint8_t mode = 0;
  if (::ioctl(fd, SPI_IOC_RD_MODE, &mode) == 0) {
    // Extract mode (lower 2 bits)
    cfg.mode = static_cast<SpiMode>(mode & 0x03);

    // Extract flags
    cfg.csHigh = ((mode & SPI_CS_HIGH) != 0);
    cfg.lsbFirst = ((mode & SPI_LSB_FIRST) != 0);
    cfg.threeWire = ((mode & SPI_3WIRE) != 0);
    cfg.loopback = ((mode & SPI_LOOP) != 0);
    cfg.noCs = ((mode & SPI_NO_CS) != 0);
    cfg.ready = ((mode & SPI_READY) != 0);
  }
}

/**
 * Query SPI bits per word.
 */
inline void querySpiBitsPerWord(int fd, SpiConfig& cfg) noexcept {
  std::uint8_t bits = 0;
  if (::ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) == 0) {
    cfg.bitsPerWord = (bits == 0) ? 8 : bits; // 0 means 8
  }
}

/**
 * Query SPI max speed.
 */
inline void querySpiMaxSpeed(int fd, SpiConfig& cfg) noexcept {
  std::uint32_t speed = 0;
  if (::ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) == 0) {
    cfg.maxSpeedHz = speed;
  }
}

/**
 * Query full SPI configuration.
 */
inline SpiConfig querySpiConfig(int fd) noexcept {
  SpiConfig cfg{};

  querySpiMode(fd, cfg);
  querySpiBitsPerWord(fd, cfg);
  querySpiMaxSpeed(fd, cfg);

  return cfg;
}

/**
 * Get driver name from sysfs.
 */
inline void queryDriverName(std::uint32_t bus, std::uint32_t cs, char* buf,
                            std::size_t bufSize) noexcept {
  char linkPath[PATH_BUFFER_SIZE];

  // Try sysfs class path first
  std::snprintf(linkPath, sizeof(linkPath), "%s/spidev%u.%u/device/driver", SPI_SYS_CLASS_PATH, bus,
                cs);

  char resolved[PATH_BUFFER_SIZE];
  const char* REAL = ::realpath(linkPath, resolved);

  if (REAL == nullptr) {
    // Fallback to sysfs bus path
    std::snprintf(linkPath, sizeof(linkPath), "%s/spi%u.%u/driver", SPI_SYS_BUS_PATH, bus, cs);
    REAL = ::realpath(linkPath, resolved);
  }

  if (REAL == nullptr) {
    buf[0] = '\0';
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
    buf[i] = NAME[i];
  }
  buf[i] = '\0';
}

/**
 * Get modalias from sysfs.
 */
inline void queryModalias(std::uint32_t bus, std::uint32_t cs, char* buf,
                          std::size_t bufSize) noexcept {
  char path[PATH_BUFFER_SIZE];

  // Try sysfs class path
  std::snprintf(path, sizeof(path), "%s/spidev%u.%u/device/modalias", SPI_SYS_CLASS_PATH, bus, cs);

  if (readFileToBuffer(path, buf, bufSize) > 0) {
    return;
  }

  // Fallback to sysfs bus path
  std::snprintf(path, sizeof(path), "%s/spi%u.%u/modalias", SPI_SYS_BUS_PATH, bus, cs);

  (void)readFileToBuffer(path, buf, bufSize);
}

} // anonymous namespace

/* ----------------------------- SpiMode ----------------------------- */

const char* toString(SpiMode mode) noexcept {
  switch (mode) {
  case SpiMode::MODE_0:
    return "mode0";
  case SpiMode::MODE_1:
    return "mode1";
  case SpiMode::MODE_2:
    return "mode2";
  case SpiMode::MODE_3:
    return "mode3";
  default:
    return "unknown";
  }
}

/* ----------------------------- SpiConfig Methods ----------------------------- */

bool SpiConfig::isValid() const noexcept {
  // Config is valid if bits per word is reasonable
  return bitsPerWord >= 1 && bitsPerWord <= 32;
}

bool SpiConfig::cpol() const noexcept { return (static_cast<std::uint8_t>(mode) & 0x02) != 0; }

bool SpiConfig::cpha() const noexcept { return (static_cast<std::uint8_t>(mode) & 0x01) != 0; }

double SpiConfig::speedMHz() const noexcept { return static_cast<double>(maxSpeedHz) / 1000000.0; }

std::string SpiConfig::toString() const {
  std::string out = fmt::format("{}, {}-bit", seeker::device::toString(mode), bitsPerWord);

  if (maxSpeedHz > 0) {
    if (maxSpeedHz >= 1000000) {
      out += fmt::format(", {:.1f} MHz", speedMHz());
    } else if (maxSpeedHz >= 1000) {
      out += fmt::format(", {} kHz", maxSpeedHz / 1000);
    } else {
      out += fmt::format(", {} Hz", maxSpeedHz);
    }
  }

  // Flags
  if (lsbFirst)
    out += ", LSB-first";
  if (csHigh)
    out += ", CS-high";
  if (threeWire)
    out += ", 3-wire";
  if (loopback)
    out += ", loopback";
  if (noCs)
    out += ", no-CS";

  return out;
}

/* ----------------------------- SpiDeviceInfo Methods ----------------------------- */

bool SpiDeviceInfo::isUsable() const noexcept { return exists && accessible && config.isValid(); }

std::string SpiDeviceInfo::toString() const {
  std::string out = fmt::format("{}: ", name.data());

  if (!exists) {
    out += "not found";
    return out;
  }

  if (!accessible) {
    out += "no access";
    return out;
  }

  out += fmt::format("bus {} cs {}", busNumber, chipSelect);

  if (driver[0] != '\0') {
    out += fmt::format("\n  Driver: {}", driver.data());
  }

  if (modalias[0] != '\0') {
    out += fmt::format("\n  Modalias: {}", modalias.data());
  }

  if (config.isValid()) {
    out += fmt::format("\n  Config: {}", config.toString());
  }

  return out;
}

/* ----------------------------- SpiDeviceList Methods ----------------------------- */

const SpiDeviceInfo* SpiDeviceList::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(devices[i].name.data(), name) == 0) {
      return &devices[i];
    }
  }

  return nullptr;
}

const SpiDeviceInfo* SpiDeviceList::findByBusCs(std::uint32_t busNumber,
                                                std::uint32_t chipSelect) const noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].busNumber == busNumber && devices[i].chipSelect == chipSelect) {
      return &devices[i];
    }
  }

  return nullptr;
}

bool SpiDeviceList::empty() const noexcept { return count == 0; }

std::size_t SpiDeviceList::countAccessible() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].accessible) {
      ++n;
    }
  }
  return n;
}

std::size_t SpiDeviceList::countUniqueBuses() const noexcept {
  // Simple O(n^2) unique count - fine for small lists
  std::size_t uniqueBuses = 0;

  for (std::size_t i = 0; i < count; ++i) {
    bool isNew = true;
    for (std::size_t j = 0; j < i; ++j) {
      if (devices[j].busNumber == devices[i].busNumber) {
        isNew = false;
        break;
      }
    }
    if (isNew) {
      ++uniqueBuses;
    }
  }

  return uniqueBuses;
}

std::string SpiDeviceList::toString() const {
  if (count == 0) {
    return "No SPI devices found";
  }

  std::string out = fmt::format("SPI devices: {} found ({} accessible, {} buses)\n", count,
                                countAccessible(), countUniqueBuses());

  for (std::size_t i = 0; i < count; ++i) {
    out += "\n" + devices[i].toString() + "\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

SpiDeviceInfo getSpiDeviceInfo(std::uint32_t busNumber, std::uint32_t chipSelect) noexcept {
  SpiDeviceInfo info{};
  info.busNumber = busNumber;
  info.chipSelect = chipSelect;

  // Build paths
  std::snprintf(info.name.data(), info.name.size(), "spidev%u.%u", busNumber, chipSelect);
  std::snprintf(info.devicePath.data(), info.devicePath.size(), "%s/spidev%u.%u", SPI_DEV_PATH,
                busNumber, chipSelect);
  std::snprintf(info.sysfsPath.data(), info.sysfsPath.size(), "%s/spidev%u.%u", SPI_SYS_CLASS_PATH,
                busNumber, chipSelect);

  // Check existence
  info.exists = isCharDevice(info.devicePath.data());
  if (!info.exists) {
    return info;
  }

  // Check accessibility
  info.accessible = (::access(info.devicePath.data(), R_OK | W_OK) == 0);

  // Get driver and modalias
  queryDriverName(busNumber, chipSelect, info.driver.data(), info.driver.size());
  queryModalias(busNumber, chipSelect, info.modalias.data(), info.modalias.size());

  // Get configuration
  if (info.accessible) {
    const int FD = openSpiDevice(busNumber, chipSelect);
    if (FD >= 0) {
      info.config = querySpiConfig(FD);
      ::close(FD);
    }
  }

  return info;
}

SpiDeviceInfo getSpiDeviceInfoByName(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return SpiDeviceInfo{};
  }

  std::uint32_t bus = 0;
  std::uint32_t cs = 0;
  if (!parseSpiDeviceName(name, bus, cs)) {
    return SpiDeviceInfo{};
  }

  return getSpiDeviceInfo(bus, cs);
}

SpiConfig getSpiConfig(std::uint32_t busNumber, std::uint32_t chipSelect) noexcept {
  const int FD = openSpiDevice(busNumber, chipSelect);
  if (FD < 0) {
    return SpiConfig{};
  }

  SpiConfig cfg = querySpiConfig(FD);
  ::close(FD);

  return cfg;
}

SpiDeviceList getAllSpiDevices() noexcept {
  SpiDeviceList list{};

  DIR* dir = ::opendir(SPI_SYS_CLASS_PATH);
  if (dir == nullptr) {
    // Try scanning /dev for spidev* devices
    dir = ::opendir(SPI_DEV_PATH);
    if (dir == nullptr) {
      return list;
    }

    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_SPI_DEVICES) {
      // Look for spidevX.Y pattern
      if (std::strncmp(entry->d_name, "spidev", 6) != 0) {
        continue;
      }

      std::uint32_t bus = 0;
      std::uint32_t cs = 0;
      if (!parseSpiDeviceName(entry->d_name, bus, cs)) {
        continue;
      }

      list.devices[list.count] = getSpiDeviceInfo(bus, cs);
      ++list.count;
    }

    ::closedir(dir);
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_SPI_DEVICES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Look for spidevX.Y pattern
    if (std::strncmp(entry->d_name, "spidev", 6) != 0) {
      continue;
    }

    std::uint32_t bus = 0;
    std::uint32_t cs = 0;
    if (!parseSpiDeviceName(entry->d_name, bus, cs)) {
      continue;
    }

    list.devices[list.count] = getSpiDeviceInfo(bus, cs);
    ++list.count;
  }

  ::closedir(dir);
  return list;
}

bool parseSpiDeviceName(const char* name, std::uint32_t& outBus, std::uint32_t& outCs) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  // Skip /dev/ prefix if present
  const char* ptr = name;
  if (std::strncmp(ptr, "/dev/", 5) == 0) {
    ptr += 5;
  }

  // Skip spidev prefix if present
  if (std::strncmp(ptr, "spidev", 6) == 0) {
    ptr += 6;
  }

  // Parse bus.cs format
  char* endPtr = nullptr;
  const unsigned long BUS = std::strtoul(ptr, &endPtr, 10);

  if (endPtr == ptr || *endPtr != '.') {
    return false;
  }

  ptr = endPtr + 1; // Skip the '.'

  const unsigned long CS = std::strtoul(ptr, &endPtr, 10);

  if (endPtr == ptr) {
    return false;
  }

  // Check for trailing garbage
  if (*endPtr != '\0' && *endPtr != ' ' && *endPtr != '\n') {
    return false;
  }

  if (BUS > UINT32_MAX || CS > UINT32_MAX) {
    return false;
  }

  outBus = static_cast<std::uint32_t>(BUS);
  outCs = static_cast<std::uint32_t>(CS);
  return true;
}

bool spiDeviceExists(std::uint32_t busNumber, std::uint32_t chipSelect) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/spidev%u.%u", SPI_DEV_PATH, busNumber, chipSelect);
  return isCharDevice(path);
}

} // namespace device

} // namespace seeker