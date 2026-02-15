/**
 * @file MountInfo.cpp
 * @brief Implementation of mount table queries.
 */

#include "src/storage/inc/MountInfo.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace storage {

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* PROC_MOUNTS = "/proc/mounts";
constexpr std::size_t LINE_BUF_SIZE = 1024;

/* ----------------------------- String Helpers ----------------------------- */

/// Copy string into fixed-size array with null termination.
inline void copyString(char* dest, std::size_t destSize, const char* src) noexcept {
  if (destSize == 0) {
    return;
  }
  std::size_t i = 0;
  while (i < destSize - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/// Copy string up to specified length.
inline void copyStringN(char* dest, std::size_t destSize, const char* src,
                        std::size_t srcLen) noexcept {
  if (destSize == 0) {
    return;
  }
  const std::size_t COPY_LEN = (srcLen < destSize - 1) ? srcLen : destSize - 1;
  std::memcpy(dest, src, COPY_LEN);
  dest[COPY_LEN] = '\0';
}

/// Check if option string contains a specific option.
/// Handles comma-separated options and options with values (e.g., "data=ordered").
inline bool hasOption(const char* options, const char* opt) noexcept {
  if (options == nullptr || opt == nullptr) {
    return false;
  }

  const std::size_t OPT_LEN = std::strlen(opt);
  if (OPT_LEN == 0) {
    return false;
  }

  const char* pos = options;
  while ((pos = std::strstr(pos, opt)) != nullptr) {
    // Check that we're at the start of options or after a comma
    const bool AT_START = (pos == options);
    const bool AFTER_COMMA = (pos > options && *(pos - 1) == ',');

    if (AT_START || AFTER_COMMA) {
      // Check that option ends at comma, equals, or end of string
      const char NEXT = pos[OPT_LEN];
      if (NEXT == '\0' || NEXT == ',' || NEXT == '=') {
        return true;
      }
    }
    pos += OPT_LEN;
  }
  return false;
}

/// Extract value after '=' for an option (e.g., "data=ordered" -> "ordered").
/// Returns pointer within options string, or nullptr if not found.
inline const char* getOptionValue(const char* options, const char* opt,
                                  std::size_t* outLen) noexcept {
  if (options == nullptr || opt == nullptr || outLen == nullptr) {
    return nullptr;
  }

  const std::size_t OPT_LEN = std::strlen(opt);
  if (OPT_LEN == 0) {
    return nullptr;
  }

  const char* pos = options;
  while ((pos = std::strstr(pos, opt)) != nullptr) {
    // Verify at start or after comma
    const bool AT_START = (pos == options);
    const bool AFTER_COMMA = (pos > options && *(pos - 1) == ',');

    if (AT_START || AFTER_COMMA) {
      // Check for '=' after option name
      if (pos[OPT_LEN] == '=') {
        const char* VALUE_START = pos + OPT_LEN + 1;
        const char* VALUE_END = VALUE_START;
        while (*VALUE_END != '\0' && *VALUE_END != ',') {
          ++VALUE_END;
        }
        *outLen = static_cast<std::size_t>(VALUE_END - VALUE_START);
        return VALUE_START;
      }
    }
    pos += OPT_LEN;
  }
  return nullptr;
}

/// Extract base device name from full path (e.g., "/dev/nvme0n1p2" -> "nvme0n1").
/// Strips partition suffix for whole-device identification.
inline void extractDevName(char* dest, std::size_t destSize, const char* devicePath) noexcept {
  dest[0] = '\0';
  if (devicePath == nullptr || destSize == 0) {
    return;
  }

  // Find the last '/' to get basename
  const char* basename = std::strrchr(devicePath, '/');
  if (basename != nullptr) {
    ++basename; // Skip '/'
  } else {
    basename = devicePath;
  }

  // Copy basename
  copyString(dest, destSize, basename);

  // For NVMe, strip partition (nvme0n1p2 -> nvme0n1)
  if (std::strncmp(dest, "nvme", 4) == 0) {
    char* pChar = std::strrchr(dest, 'p');
    if (pChar != nullptr) {
      // Check if there's a digit after 'p' and 'p' comes after 'n'
      const char* nChar = std::strchr(dest + 4, 'n');
      if (nChar != nullptr && pChar > nChar && pChar[1] >= '0' && pChar[1] <= '9') {
        *pChar = '\0';
      }
    }
    return;
  }

  // For traditional devices (sda1 -> sda), strip trailing digits
  std::size_t len = std::strlen(dest);
  while (len > 0 && dest[len - 1] >= '0' && dest[len - 1] <= '9') {
    --len;
  }
  if (len > 0) {
    dest[len] = '\0';
  }
}

} // namespace

/* ----------------------------- MountEntry Methods ----------------------------- */

bool MountEntry::isReadOnly() const noexcept { return hasOption(options.data(), "ro"); }

bool MountEntry::hasNoAtime() const noexcept { return hasOption(options.data(), "noatime"); }

bool MountEntry::hasNoDirAtime() const noexcept { return hasOption(options.data(), "nodiratime"); }

bool MountEntry::hasRelAtime() const noexcept { return hasOption(options.data(), "relatime"); }

bool MountEntry::hasNoBarrier() const noexcept {
  // Check various ways barriers can be disabled
  if (hasOption(options.data(), "nobarrier")) {
    return true;
  }
  if (hasOption(options.data(), "barrier=0")) {
    return true;
  }
  return false;
}

bool MountEntry::isSync() const noexcept { return hasOption(options.data(), "sync"); }

bool MountEntry::isBlockDevice() const noexcept {
  // Real block devices start with /dev/
  return std::strncmp(device.data(), "/dev/", 5) == 0;
}

bool MountEntry::isNetworkFs() const noexcept {
  const char* FS = fsType.data();
  return std::strcmp(FS, "nfs") == 0 || std::strcmp(FS, "nfs4") == 0 ||
         std::strcmp(FS, "cifs") == 0 || std::strcmp(FS, "smb") == 0 ||
         std::strcmp(FS, "smbfs") == 0 || std::strcmp(FS, "fuse.sshfs") == 0 ||
         std::strcmp(FS, "fuse.glusterfs") == 0 || std::strcmp(FS, "fuse.cephfs") == 0;
}

bool MountEntry::isTmpFs() const noexcept {
  const char* FS = fsType.data();
  return std::strcmp(FS, "tmpfs") == 0 || std::strcmp(FS, "ramfs") == 0 ||
         std::strcmp(FS, "devtmpfs") == 0;
}

const char* MountEntry::ext4DataMode() const noexcept {
  if (std::strcmp(fsType.data(), "ext4") != 0) {
    return "";
  }

  // Thread-local buffer for returning the value
  static thread_local char dataMode[32] = "";

  std::size_t len = 0;
  const char* VAL = getOptionValue(options.data(), "data", &len);
  if (VAL != nullptr && len > 0 && len < sizeof(dataMode)) {
    copyStringN(dataMode, sizeof(dataMode), VAL, len);
    return dataMode;
  }

  // Default for ext4 when not specified is "ordered"
  return "ordered";
}

std::string MountEntry::toString() const {
  return fmt::format("{} on {} ({}): opts={}", device.data(), mountPoint.data(), fsType.data(),
                     options.data());
}

/* ----------------------------- MountTable Methods ----------------------------- */

const MountEntry* MountTable::findByMountPoint(const char* path) const noexcept {
  if (path == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(mounts[i].mountPoint.data(), path) == 0) {
      return &mounts[i];
    }
  }
  return nullptr;
}

const MountEntry* MountTable::findForPath(const char* path) const noexcept {
  if (path == nullptr) {
    return nullptr;
  }

  const MountEntry* best = nullptr;
  std::size_t bestLen = 0;
  const std::size_t PATH_LEN = std::strlen(path);

  for (std::size_t i = 0; i < count; ++i) {
    const char* MP = mounts[i].mountPoint.data();
    const std::size_t MP_LEN = std::strlen(MP);

    // Check if mount point is a prefix of path
    if (PATH_LEN >= MP_LEN && std::strncmp(path, MP, MP_LEN) == 0) {
      // Ensure it's a proper prefix (ends at / or end of mount point)
      if (MP_LEN == 1 ||         // root
          PATH_LEN == MP_LEN ||  // exact match
          path[MP_LEN] == '/') { // path continues with /
        if (MP_LEN > bestLen) {
          best = &mounts[i];
          bestLen = MP_LEN;
        }
      }
    }
  }
  return best;
}

const MountEntry* MountTable::findByDevice(const char* devName) const noexcept {
  if (devName == nullptr) {
    return nullptr;
  }

  // Normalize: if given /dev/xxx, just use xxx for comparison
  const char* searchName = devName;
  if (std::strncmp(devName, "/dev/", 5) == 0) {
    searchName = devName + 5;
  }

  for (std::size_t i = 0; i < count; ++i) {
    // Compare against full device path
    if (std::strcmp(mounts[i].device.data(), devName) == 0) {
      return &mounts[i];
    }
    // Compare against just the device name part
    const char* entryName = mounts[i].device.data();
    if (std::strncmp(entryName, "/dev/", 5) == 0) {
      if (std::strcmp(entryName + 5, searchName) == 0) {
        return &mounts[i];
      }
    }
  }
  return nullptr;
}

std::size_t MountTable::countBlockDevices() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (mounts[i].isBlockDevice()) {
      ++n;
    }
  }
  return n;
}

std::string MountTable::toString() const {
  std::string out;
  out += fmt::format("Mount table: {} entries ({} block devices)\n", count, countBlockDevices());

  for (std::size_t i = 0; i < count; ++i) {
    if (mounts[i].isBlockDevice()) {
      out += "  ";
      out += mounts[i].toString();
      out += "\n";
    }
  }
  return out;
}

/* ----------------------------- API ----------------------------- */

MountTable getMountTable() noexcept {
  MountTable table{};

  FILE* fp = std::fopen(PROC_MOUNTS, "r");
  if (fp == nullptr) {
    return table;
  }

  char line[LINE_BUF_SIZE];

  while (std::fgets(line, sizeof(line), fp) != nullptr && table.count < MAX_MOUNTS) {
    // Parse: device mountpoint fstype options dump pass
    char device[PATH_SIZE];
    char mountPoint[PATH_SIZE];
    char fsType[FSTYPE_SIZE];
    char options[MOUNT_OPTIONS_SIZE];
    int dump = 0;
    int pass = 0;

    // sscanf with field widths to prevent overflow
    const int FIELDS = std::sscanf(line, "%255s %255s %31s %511s %d %d", device, mountPoint, fsType,
                                   options, &dump, &pass);
    if (FIELDS < 4) {
      continue;
    }

    MountEntry& entry = table.mounts[table.count];
    copyString(entry.device.data(), PATH_SIZE, device);
    copyString(entry.mountPoint.data(), PATH_SIZE, mountPoint);
    copyString(entry.fsType.data(), FSTYPE_SIZE, fsType);
    copyString(entry.options.data(), MOUNT_OPTIONS_SIZE, options);
    extractDevName(entry.devName.data(), MOUNT_DEVICE_NAME_SIZE, device);

    ++table.count;
  }

  std::fclose(fp);
  return table;
}

MountEntry getMountForPath(const char* path) noexcept {
  MountEntry result{};
  if (path == nullptr || path[0] == '\0') {
    return result;
  }

  FILE* fp = std::fopen(PROC_MOUNTS, "r");
  if (fp == nullptr) {
    return result;
  }

  char line[LINE_BUF_SIZE];
  std::size_t bestLen = 0;
  const std::size_t PATH_LEN = std::strlen(path);

  while (std::fgets(line, sizeof(line), fp) != nullptr) {
    char device[PATH_SIZE];
    char mountPoint[PATH_SIZE];
    char fsType[FSTYPE_SIZE];
    char options[MOUNT_OPTIONS_SIZE];

    const int FIELDS =
        std::sscanf(line, "%255s %255s %31s %511s", device, mountPoint, fsType, options);
    if (FIELDS < 4) {
      continue;
    }

    const std::size_t MP_LEN = std::strlen(mountPoint);

    // Check prefix match
    if (PATH_LEN >= MP_LEN && std::strncmp(path, mountPoint, MP_LEN) == 0) {
      if (MP_LEN == 1 || PATH_LEN == MP_LEN || path[MP_LEN] == '/') {
        if (MP_LEN > bestLen) {
          copyString(result.device.data(), PATH_SIZE, device);
          copyString(result.mountPoint.data(), PATH_SIZE, mountPoint);
          copyString(result.fsType.data(), FSTYPE_SIZE, fsType);
          copyString(result.options.data(), MOUNT_OPTIONS_SIZE, options);
          extractDevName(result.devName.data(), MOUNT_DEVICE_NAME_SIZE, device);
          bestLen = MP_LEN;
        }
      }
    }
  }

  std::fclose(fp);
  return result;
}

} // namespace storage

} // namespace seeker