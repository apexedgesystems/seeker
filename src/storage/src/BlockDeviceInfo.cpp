/**
 * @file BlockDeviceInfo.cpp
 * @brief Implementation of block device property queries.
 */

#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace storage {

using seeker::helpers::files::readFileInt64;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;
using seeker::helpers::strings::copyToBuffer;

namespace {

/* ----------------------------- Path Constants ----------------------------- */

constexpr const char* SYS_BLOCK = "/sys/block";
constexpr std::size_t PATH_BUF_SIZE = 512;
constexpr std::size_t READ_BUF_SIZE = 128;

/// Read sysfs block device attribute into buffer.
inline std::size_t readBlockDevAttr(const char* name, const char* attr, char* buf,
                                    std::size_t bufSize) noexcept {
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/%s", SYS_BLOCK, name, attr);
  return readFileToBuffer(path, buf, bufSize);
}

/// Read sysfs block device attribute as uint64.
inline std::uint64_t readBlockDevUint64(const char* name, const char* attr,
                                        std::uint64_t defaultVal = 0) noexcept {
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/%s", SYS_BLOCK, name, attr);
  return readFileUint64(path, defaultVal);
}

/// Read sysfs block device attribute as int64.
inline std::int64_t readBlockDevInt64(const char* name, const char* attr,
                                      std::int64_t defaultVal = 0) noexcept {
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/%s", SYS_BLOCK, name, attr);
  return readFileInt64(path, defaultVal);
}

/// Check if device should be filtered out (loop, ram, dm-*).
inline bool shouldFilterDevice(const char* name) noexcept {
  // Filter loop devices
  if (std::strncmp(name, "loop", 4) == 0) {
    return true;
  }
  // Filter RAM disks
  if (std::strncmp(name, "ram", 3) == 0) {
    return true;
  }
  // Filter device mapper
  if (std::strncmp(name, "dm-", 3) == 0) {
    return true;
  }
  // Filter zram (compressed RAM)
  if (std::strncmp(name, "zram", 4) == 0) {
    return true;
  }
  // Filter floppy (yes, still exists in sysfs)
  if (std::strncmp(name, "fd", 2) == 0 && name[2] >= '0' && name[2] <= '9') {
    return true;
  }
  return false;
}

/// Check if path exists.
inline bool pathExists(const char* path) noexcept { return ::access(path, F_OK) == 0; }

} // namespace

/* ----------------------------- BlockDevice Methods ----------------------------- */

bool BlockDevice::isNvme() const noexcept { return std::strncmp(name.data(), "nvme", 4) == 0; }

bool BlockDevice::isSsd() const noexcept { return !rotational && !removable; }

bool BlockDevice::isHdd() const noexcept { return rotational && !removable; }

bool BlockDevice::isAdvancedFormat() const noexcept { return physicalBlockSize >= 4096; }

const char* BlockDevice::deviceType() const noexcept {
  if (isNvme()) {
    return "NVMe";
  }
  if (isHdd()) {
    return "HDD";
  }
  if (isSsd()) {
    return "SSD";
  }
  if (removable) {
    return "Removable";
  }
  return "Unknown";
}

std::string BlockDevice::toString() const {
  return fmt::format("{}: {} {} [{}] size={} lbs={} pbs={} trim={}", name.data(), vendor.data(),
                     model.data(), deviceType(), formatCapacity(sizeBytes), logicalBlockSize,
                     physicalBlockSize, hasTrim ? "yes" : "no");
}

/* ----------------------------- BlockDeviceList Methods ----------------------------- */

const BlockDevice* BlockDeviceList::find(const char* name) const noexcept {
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

std::size_t BlockDeviceList::countNvme() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].isNvme()) {
      ++n;
    }
  }
  return n;
}

std::size_t BlockDeviceList::countSsd() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].isSsd() && !devices[i].isNvme()) {
      ++n;
    }
  }
  return n;
}

std::size_t BlockDeviceList::countHdd() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (devices[i].isHdd()) {
      ++n;
    }
  }
  return n;
}

std::string BlockDeviceList::toString() const {
  std::string out;
  out += fmt::format("Block devices: {} total ({} NVMe, {} SSD, {} HDD)\n", count, countNvme(),
                     countSsd(), countHdd());

  for (std::size_t i = 0; i < count; ++i) {
    out += "  ";
    out += devices[i].toString();
    out += "\n";
  }
  return out;
}

/* ----------------------------- API ----------------------------- */

BlockDevice getBlockDevice(const char* name) noexcept {
  BlockDevice dev{};
  if (name == nullptr || name[0] == '\0') {
    return dev;
  }

  // Copy name
  copyToBuffer(dev.name.data(), DEVICE_NAME_SIZE, name);

  // Check device exists
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s", SYS_BLOCK, name);

  if (!pathExists(path)) {
    return dev;
  }

  char buf[READ_BUF_SIZE];

  // Model (may be in /device/model or directly in block for some devices)
  (void)readBlockDevAttr(name, "device/model", buf, sizeof(buf));
  copyToBuffer(dev.model.data(), MODEL_STRING_SIZE, buf);

  // Vendor
  (void)readBlockDevAttr(name, "device/vendor", buf, sizeof(buf));
  copyToBuffer(dev.vendor.data(), MODEL_STRING_SIZE, buf);

  // Size in 512-byte sectors -> convert to bytes
  dev.sizeBytes = readBlockDevUint64(name, "size") * 512ULL;

  // Queue parameters
  dev.logicalBlockSize =
      static_cast<std::uint32_t>(readBlockDevUint64(name, "queue/logical_block_size", 512));
  dev.physicalBlockSize =
      static_cast<std::uint32_t>(readBlockDevUint64(name, "queue/physical_block_size", 512));
  dev.minIoSize = static_cast<std::uint32_t>(readBlockDevUint64(name, "queue/minimum_io_size"));
  dev.optimalIoSize = static_cast<std::uint32_t>(readBlockDevUint64(name, "queue/optimal_io_size"));

  // Rotational (1 = HDD, 0 = SSD/NVMe)
  dev.rotational = (readBlockDevInt64(name, "queue/rotational", 1) != 0);

  // Removable
  dev.removable = (readBlockDevInt64(name, "removable") != 0);

  // TRIM/discard support (discard_granularity > 0 indicates support)
  dev.hasTrim = (readBlockDevUint64(name, "queue/discard_granularity") > 0);

  return dev;
}

BlockDeviceList getBlockDevices() noexcept {
  BlockDeviceList list{};

  DIR* dir = ::opendir(SYS_BLOCK);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_BLOCK_DEVICES) {
    const char* NAME = entry->d_name;

    // Skip . and ..
    if (NAME[0] == '.') {
      continue;
    }

    // Skip filtered devices
    if (shouldFilterDevice(NAME)) {
      continue;
    }

    // Skip partition entries (contain digits after base name for most devices)
    // But be careful: nvme0n1 is a device, nvme0n1p1 is a partition
    // sda is a device, sda1 is a partition
    bool isPartition = false;
    const std::size_t LEN = std::strlen(NAME);

    if (std::strncmp(NAME, "nvme", 4) == 0) {
      // NVMe: partition has 'p' followed by digit after 'n' number
      // nvme0n1 = device, nvme0n1p1 = partition
      const char* pChar = std::strrchr(NAME, 'p');
      if (pChar != nullptr && pChar > NAME + 4) {
        // Check if there's a digit after 'p' and 'p' comes after 'n'
        const char* nChar = std::strchr(NAME + 4, 'n');
        if (nChar != nullptr && pChar > nChar && pChar[1] >= '0' && pChar[1] <= '9') {
          isPartition = true;
        }
      }
    } else {
      // Traditional: partition is base name + digit at end
      // sda = device, sda1 = partition
      if (LEN > 0 && NAME[LEN - 1] >= '0' && NAME[LEN - 1] <= '9') {
        // But sd/hd/vd base names don't end in digit
        if (std::strncmp(NAME, "sd", 2) == 0 || std::strncmp(NAME, "hd", 2) == 0 ||
            std::strncmp(NAME, "vd", 2) == 0 || std::strncmp(NAME, "xvd", 3) == 0) {
          isPartition = true;
        }
      }
    }

    if (isPartition) {
      continue;
    }

    list.devices[list.count] = getBlockDevice(NAME);
    ++list.count;
  }

  ::closedir(dir);
  return list;
}

std::string formatCapacity(std::uint64_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }

  // Use decimal units (SI) for storage capacity (industry standard)
  static constexpr std::uint64_t KB = 1000ULL;
  static constexpr std::uint64_t MB = KB * 1000ULL;
  static constexpr std::uint64_t GB = MB * 1000ULL;
  static constexpr std::uint64_t TB = GB * 1000ULL;
  static constexpr std::uint64_t PB = TB * 1000ULL;

  if (bytes >= PB) {
    return fmt::format("{:.1f} PB", static_cast<double>(bytes) / static_cast<double>(PB));
  }
  if (bytes >= TB) {
    return fmt::format("{:.1f} TB", static_cast<double>(bytes) / static_cast<double>(TB));
  }
  if (bytes >= GB) {
    return fmt::format("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(GB));
  }
  if (bytes >= MB) {
    return fmt::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(MB));
  }
  if (bytes >= KB) {
    return fmt::format("{:.1f} KB", static_cast<double>(bytes) / static_cast<double>(KB));
  }

  return fmt::format("{} B", bytes);
}

} // namespace storage

} // namespace seeker