/**
 * @file InterfaceInfo.cpp
 * @brief Implementation of network interface information queries.
 */

#include "src/network/inc/InterfaceInfo.hpp"
#include "src/helpers/inc/Strings.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace network {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Path Constants ----------------------------- */

constexpr const char* NET_SYS_PATH = "/sys/class/net";
constexpr std::size_t PATH_BUFFER_SIZE = 256;
constexpr std::size_t READ_BUFFER_SIZE = 128;

/* ----------------------------- File Helpers ----------------------------- */

/**
 * Read symlink target basename.
 * Returns true if successful.
 */
inline bool readSymlinkBasename(const char* path, char* buf, std::size_t bufSize) noexcept {
  char linkBuf[PATH_BUFFER_SIZE];
  const ssize_t LEN = ::readlink(path, linkBuf, sizeof(linkBuf) - 1);
  if (LEN <= 0) {
    buf[0] = '\0';
    return false;
  }
  linkBuf[LEN] = '\0';

  // Find last '/' to get basename
  const char* basename = std::strrchr(linkBuf, '/');
  if (basename != nullptr) {
    ++basename; // Skip the '/'
  } else {
    basename = linkBuf;
  }

  // Copy to output buffer
  std::size_t i = 0;
  for (; i < bufSize - 1 && basename[i] != '\0'; ++i) {
    buf[i] = basename[i];
  }
  buf[i] = '\0';

  return true;
}

/**
 * Count directories matching prefix in a directory.
 * Used for counting rx-* and tx-* queue directories.
 */
inline int countDirsWithPrefix(const char* dirPath, const char* prefix) noexcept {
  DIR* dir = ::opendir(dirPath);
  if (dir == nullptr) {
    return 0;
  }

  const std::size_t PREFIX_LEN = std::strlen(prefix);
  int count = 0;

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr) {
    if (std::strncmp(entry->d_name, prefix, PREFIX_LEN) == 0) {
      ++count;
    }
  }

  ::closedir(dir);
  return count;
}

} // namespace

/* ----------------------------- isVirtualInterface ----------------------------- */

bool isVirtualInterface(const char* ifname) noexcept {
  if (ifname == nullptr) {
    return true;
  }

  // Loopback is always virtual
  if (std::strcmp(ifname, "lo") == 0) {
    return true;
  }

  // Known virtual interface prefixes
  static constexpr const char* VIRTUAL_PREFIXES[] = {
      "veth",   // Virtual ethernet (containers)
      "docker", // Docker bridge
      "br-",    // Bridge
      "virbr",  // Libvirt bridge
      "vnet",   // Virtual network
      "tap",    // TAP device
      "tun",    // TUN device
      "dummy",  // Dummy device
      "bond",   // Bonding (could be argued either way)
  };

  for (const char* prefix : VIRTUAL_PREFIXES) {
    if (std::strncmp(ifname, prefix, std::strlen(prefix)) == 0) {
      return true;
    }
  }

  char pathBuf[PATH_BUFFER_SIZE];

  // Standard check: device symlink exists (typical for PCIe/USB NICs)
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/device", NET_SYS_PATH, ifname);
  if (pathExists(pathBuf)) {
    return false; // Has device symlink -> physical
  }

  // Embedded/ARM platform fallback: check for physical indicators
  // Real NICs report positive speed when link is up
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/speed", NET_SYS_PATH, ifname);
  const int SPEED = readFileInt(pathBuf, 0);
  if (SPEED > 0) {
    return false; // Has real link speed -> physical
  }

  // Real NICs have duplex setting (full/half)
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/duplex", NET_SYS_PATH, ifname);
  char duplexBuf[READ_BUFFER_SIZE];
  if (readFileToBuffer(pathBuf, duplexBuf, sizeof(duplexBuf)) > 0) {
    if (std::strcmp(duplexBuf, "full") == 0 || std::strcmp(duplexBuf, "half") == 0) {
      return false; // Has real duplex setting -> physical
    }
  }

  // No physical indicators found -> virtual
  return true;
}

/* ----------------------------- InterfaceInfo Methods ----------------------------- */

bool InterfaceInfo::isUp() const noexcept { return std::strcmp(operState.data(), "up") == 0; }

bool InterfaceInfo::isPhysical() const noexcept { return !isVirtualInterface(ifname.data()); }

bool InterfaceInfo::hasLink() const noexcept { return isUp() && speedMbps > 0; }

std::string InterfaceInfo::toString() const {
  std::string out;
  out += fmt::format("{}: state={} speed={}", ifname.data(),
                     operState[0] != '\0' ? operState.data() : "unknown",
                     speedMbps > 0 ? formatSpeed(speedMbps) : "unknown");

  if (duplex[0] != '\0') {
    out += fmt::format(" duplex={}", duplex.data());
  }

  out += fmt::format(" mtu={}", mtu);

  if (driver[0] != '\0') {
    out += fmt::format(" driver={}", driver.data());
  }

  if (rxQueues > 0 || txQueues > 0) {
    out += fmt::format(" queues=rx:{}/tx:{}", rxQueues, txQueues);
  }

  if (numaNode >= 0) {
    out += fmt::format(" numa={}", numaNode);
  }

  if (macAddress[0] != '\0') {
    out += fmt::format(" mac={}", macAddress.data());
  }

  return out;
}

/* ----------------------------- InterfaceList Methods ----------------------------- */

const InterfaceInfo* InterfaceList::find(const char* ifname) const noexcept {
  if (ifname == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(interfaces[i].ifname.data(), ifname) == 0) {
      return &interfaces[i];
    }
  }

  return nullptr;
}

bool InterfaceList::empty() const noexcept { return count == 0; }

std::string InterfaceList::toString() const {
  if (count == 0) {
    return "No interfaces found";
  }

  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) {
      out += '\n';
    }
    out += interfaces[i].toString();
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

InterfaceInfo getInterfaceInfo(const char* ifname) noexcept {
  InterfaceInfo info{};

  if (ifname == nullptr || ifname[0] == '\0') {
    return info;
  }

  // Copy interface name
  copyToFixedArray(info.ifname, ifname);

  char pathBuf[PATH_BUFFER_SIZE];
  char readBuf[READ_BUFFER_SIZE];

  // Check if interface exists
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s", NET_SYS_PATH, ifname);
  if (!pathExists(pathBuf)) {
    return info;
  }

  // Operational state
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/operstate", NET_SYS_PATH, ifname);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.operState, readBuf);
  }

  // Link speed (may return -1 or error if link down)
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/speed", NET_SYS_PATH, ifname);
  const int SPEED = readFileInt(pathBuf, 0);
  info.speedMbps = (SPEED > 0) ? SPEED : 0;

  // Duplex mode
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/duplex", NET_SYS_PATH, ifname);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.duplex, readBuf);
  }

  // MTU
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/mtu", NET_SYS_PATH, ifname);
  info.mtu = readFileInt(pathBuf, 0);

  // MAC address
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/address", NET_SYS_PATH, ifname);
  if (readFileToBuffer(pathBuf, readBuf, sizeof(readBuf)) > 0) {
    copyToFixedArray(info.macAddress, readBuf);
  }

  // Driver name (from device/driver/module symlink)
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/device/driver/module", NET_SYS_PATH, ifname);
  if (readSymlinkBasename(pathBuf, readBuf, sizeof(readBuf))) {
    copyToFixedArray(info.driver, readBuf);
  } else {
    // Fallback: try device/driver symlink
    std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/device/driver", NET_SYS_PATH, ifname);
    if (readSymlinkBasename(pathBuf, readBuf, sizeof(readBuf))) {
      copyToFixedArray(info.driver, readBuf);
    }
  }

  // NUMA node
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/device/numa_node", NET_SYS_PATH, ifname);
  const int NUMA = readFileInt(pathBuf, -1);
  info.numaNode = (NUMA >= 0) ? NUMA : -1;

  // Queue counts
  std::snprintf(pathBuf, sizeof(pathBuf), "%s/%s/queues", NET_SYS_PATH, ifname);
  if (pathExists(pathBuf)) {
    info.rxQueues = countDirsWithPrefix(pathBuf, "rx-");
    info.txQueues = countDirsWithPrefix(pathBuf, "tx-");
  }

  return info;
}

InterfaceList getAllInterfaces() noexcept {
  InterfaceList list{};

  DIR* dir = ::opendir(NET_SYS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_INTERFACES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    list.interfaces[list.count] = getInterfaceInfo(entry->d_name);

    // Only count if we got valid info (interface name was set)
    if (list.interfaces[list.count].ifname[0] != '\0') {
      ++list.count;
    }
  }

  ::closedir(dir);
  return list;
}

InterfaceList getPhysicalInterfaces() noexcept {
  InterfaceList list{};

  DIR* dir = ::opendir(NET_SYS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_INTERFACES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Skip virtual interfaces
    if (isVirtualInterface(entry->d_name)) {
      continue;
    }

    list.interfaces[list.count] = getInterfaceInfo(entry->d_name);

    // Only count if we got valid info
    if (list.interfaces[list.count].ifname[0] != '\0') {
      ++list.count;
    }
  }

  ::closedir(dir);
  return list;
}

std::string formatSpeed(int speedMbps) {
  if (speedMbps <= 0) {
    return "unknown";
  }

  if (speedMbps >= 1000 && speedMbps % 1000 == 0) {
    return fmt::format("{} Gbps", speedMbps / 1000);
  }

  return fmt::format("{} Mbps", speedMbps);
}

} // namespace network

} // namespace seeker