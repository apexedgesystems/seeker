/**
 * @file WatchdogStatus.cpp
 * @brief Implementation of watchdog status collection from /sys/class/watchdog/.
 */

#include "src/system/inc/WatchdogStatus.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::readFileToBuffer;

/// Read unsigned integer from sysfs file.
std::uint32_t readUintFromFile(const char* path) noexcept {
  std::array<char, 32> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::strtoul(buf.data(), nullptr, 10));
}

/// Copy string safely with null termination.
template <std::size_t N> void safeCopy(std::array<char, N>& dest, const char* src) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  std::size_t i = 0;
  while (i < N - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/// Build sysfs path for watchdog attribute.
void buildWatchdogPath(char* buf, std::size_t bufSize, std::uint32_t index,
                       const char* attr) noexcept {
  std::snprintf(buf, bufSize, "/sys/class/watchdog/watchdog%u/%s", index, attr);
}

/// Check if a watchdog device exists at given index.
bool watchdogExists(std::uint32_t index) noexcept {
  std::array<char, 64> path{};
  std::snprintf(path.data(), path.size(), "/sys/class/watchdog/watchdog%u", index);

  const int FD = ::open(path.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (FD >= 0) {
    ::close(FD);
    return true;
  }
  return false;
}

/// Parse capability flags from raw bitmask.
WatchdogCapabilities parseCapabilities(std::uint32_t raw) noexcept {
  WatchdogCapabilities caps{};
  caps.raw = raw;

  // WDIOF_* flag definitions from linux/watchdog.h
  constexpr std::uint32_t WDIOF_SETTIMEOUT = 0x0080;
  constexpr std::uint32_t WDIOF_MAGICCLOSE = 0x0100;
  constexpr std::uint32_t WDIOF_PRETIMEOUT = 0x0200;
  constexpr std::uint32_t WDIOF_KEEPALIVEPING = 0x8000;
  constexpr std::uint32_t WDIOF_ALARMONLY = 0x0400;
  constexpr std::uint32_t WDIOF_POWEROVER = 0x0040;
  constexpr std::uint32_t WDIOF_FANFAULT = 0x0002;
  constexpr std::uint32_t WDIOF_EXTERN1 = 0x0004;
  constexpr std::uint32_t WDIOF_OVERHEAT = 0x0001;

  caps.settimeout = (raw & WDIOF_SETTIMEOUT) != 0;
  caps.magicclose = (raw & WDIOF_MAGICCLOSE) != 0;
  caps.pretimeout = (raw & WDIOF_PRETIMEOUT) != 0;
  caps.keepaliveping = (raw & WDIOF_KEEPALIVEPING) != 0;
  caps.alarmonly = (raw & WDIOF_ALARMONLY) != 0;
  caps.powerover = (raw & WDIOF_POWEROVER) != 0;
  caps.fanfault = (raw & WDIOF_FANFAULT) != 0;
  caps.externPowerFault = (raw & WDIOF_EXTERN1) != 0;
  caps.overheat = (raw & WDIOF_OVERHEAT) != 0;

  return caps;
}

/// Check if string contains substring (for softdog detection).
bool containsSubstring(const char* haystack, const char* needle) noexcept {
  return std::strstr(haystack, needle) != nullptr;
}

} // namespace

/* ----------------------------- WatchdogCapabilities Methods ----------------------------- */

bool WatchdogCapabilities::hasAny() const noexcept { return raw != 0; }

std::string WatchdogCapabilities::toString() const {
  if (raw == 0) {
    return "none";
  }

  std::string out;
  out.reserve(128);

  if (settimeout) {
    if (!out.empty())
      out += ", ";
    out += "settimeout";
  }
  if (magicclose) {
    if (!out.empty())
      out += ", ";
    out += "magicclose";
  }
  if (pretimeout) {
    if (!out.empty())
      out += ", ";
    out += "pretimeout";
  }
  if (keepaliveping) {
    if (!out.empty())
      out += ", ";
    out += "keepalive";
  }
  if (alarmonly) {
    if (!out.empty())
      out += ", ";
    out += "alarmonly";
  }
  if (powerover) {
    if (!out.empty())
      out += ", ";
    out += "powerover";
  }
  if (fanfault) {
    if (!out.empty())
      out += ", ";
    out += "fanfault";
  }
  if (overheat) {
    if (!out.empty())
      out += ", ";
    out += "overheat";
  }

  return out;
}

/* ----------------------------- WatchdogDevice Methods ----------------------------- */

bool WatchdogDevice::isPrimary() const noexcept { return index == 0; }

bool WatchdogDevice::canSetTimeout() const noexcept { return capabilities.settimeout; }

bool WatchdogDevice::hasPretimeout() const noexcept {
  return capabilities.pretimeout && pretimeout > 0;
}

bool WatchdogDevice::isRtSuitable() const noexcept {
  // RT-suitable if we can set timeout and minimum timeout is reasonable
  if (!valid) {
    return false;
  }

  // Must be able to configure timeout
  if (!capabilities.settimeout) {
    return false;
  }

  // Min timeout should allow reasonable heartbeat intervals (< 5s min)
  if (minTimeout > 5) {
    return false;
  }

  return true;
}

std::string WatchdogDevice::toString() const {
  if (!valid) {
    return fmt::format("watchdog{}: not available", index);
  }

  std::string out;
  out.reserve(256);

  out += fmt::format("watchdog{}: {}\n", index, identity.data());
  out += fmt::format("  Device:     {}\n", devicePath.data());
  out += fmt::format("  Timeout:    {}s (range: {}-{}s)\n", timeout, minTimeout, maxTimeout);
  out += fmt::format("  Active:     {}\n", active ? "yes" : "no");
  out += fmt::format("  Nowayout:   {}\n", nowayout ? "yes" : "no");

  if (capabilities.pretimeout) {
    out += fmt::format("  Pretimeout: {}s (governor: {})\n", pretimeout, pretimeoutGovernor.data());
  }

  out += fmt::format("  Capabilities: {}\n", capabilities.toString());

  return out;
}

/* ----------------------------- WatchdogStatus Methods ----------------------------- */

const WatchdogDevice* WatchdogStatus::find(std::uint32_t index) const noexcept {
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].index == index) {
      return &devices[i];
    }
  }
  return nullptr;
}

const WatchdogDevice* WatchdogStatus::primary() const noexcept { return find(0); }

bool WatchdogStatus::hasWatchdog() const noexcept { return deviceCount > 0; }

bool WatchdogStatus::anyActive() const noexcept {
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].active) {
      return true;
    }
  }
  return false;
}

const WatchdogDevice* WatchdogStatus::findRtSuitable() const noexcept {
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].isRtSuitable()) {
      return &devices[i];
    }
  }
  return nullptr;
}

std::string WatchdogStatus::toString() const {
  std::string out;
  out.reserve(512);

  out += "Watchdog Status:\n";
  out += fmt::format("  Devices found: {}\n", deviceCount);
  out += fmt::format("  Software watchdog: {}\n", softdogLoaded ? "loaded" : "not loaded");
  out += fmt::format("  Hardware watchdog: {}\n\n", hasHardwareWatchdog ? "yes" : "no");

  for (std::size_t i = 0; i < deviceCount; ++i) {
    out += devices[i].toString();
    out += "\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

WatchdogDevice getWatchdogDevice(std::uint32_t index) noexcept {
  WatchdogDevice dev{};
  dev.index = index;

  if (!watchdogExists(index)) {
    return dev;
  }

  dev.valid = true;

  // Build device path
  std::snprintf(dev.devicePath.data(), dev.devicePath.size(), "/dev/watchdog%u", index);

  // Read identity
  std::array<char, 128> path{};
  buildWatchdogPath(path.data(), path.size(), index, "identity");
  std::array<char, WATCHDOG_IDENTITY_SIZE> identBuf{};
  if (readFileToBuffer(path.data(), identBuf.data(), identBuf.size()) > 0) {
    safeCopy(dev.identity, identBuf.data());
  } else {
    safeCopy(dev.identity, "Unknown");
  }

  // Read timeout
  buildWatchdogPath(path.data(), path.size(), index, "timeout");
  dev.timeout = readUintFromFile(path.data());

  // Read min_timeout
  buildWatchdogPath(path.data(), path.size(), index, "min_timeout");
  dev.minTimeout = readUintFromFile(path.data());

  // Read max_timeout
  buildWatchdogPath(path.data(), path.size(), index, "max_timeout");
  dev.maxTimeout = readUintFromFile(path.data());

  // Read pretimeout
  buildWatchdogPath(path.data(), path.size(), index, "pretimeout");
  dev.pretimeout = readUintFromFile(path.data());

  // Read pretimeout_governor
  buildWatchdogPath(path.data(), path.size(), index, "pretimeout_governor");
  std::array<char, WATCHDOG_GOVERNOR_SIZE> govBuf{};
  if (readFileToBuffer(path.data(), govBuf.data(), govBuf.size()) > 0) {
    safeCopy(dev.pretimeoutGovernor, govBuf.data());
  }

  // Read timeleft
  buildWatchdogPath(path.data(), path.size(), index, "timeleft");
  dev.timeleft = readUintFromFile(path.data());

  // Read bootstatus
  buildWatchdogPath(path.data(), path.size(), index, "bootstatus");
  dev.bootstatus = readUintFromFile(path.data());

  // Read options (capabilities)
  buildWatchdogPath(path.data(), path.size(), index, "options");
  const std::uint32_t OPTIONS = readUintFromFile(path.data());
  dev.capabilities = parseCapabilities(OPTIONS);

  // Read state (active/inactive)
  buildWatchdogPath(path.data(), path.size(), index, "state");
  std::array<char, 32> stateBuf{};
  if (readFileToBuffer(path.data(), stateBuf.data(), stateBuf.size()) > 0) {
    dev.active = (std::strcmp(stateBuf.data(), "active") == 0);
  }

  // Read nowayout
  buildWatchdogPath(path.data(), path.size(), index, "nowayout");
  std::array<char, 8> nowayoutBuf{};
  if (readFileToBuffer(path.data(), nowayoutBuf.data(), nowayoutBuf.size()) > 0) {
    dev.nowayout = (nowayoutBuf[0] == '1');
  }

  return dev;
}

bool isSoftdogLoaded() noexcept {
  std::array<char, 8192> buf{};
  if (readFileToBuffer("/proc/modules", buf.data(), buf.size()) == 0) {
    return false;
  }
  return containsSubstring(buf.data(), "softdog");
}

WatchdogStatus getWatchdogStatus() noexcept {
  WatchdogStatus status{};

  // Enumerate watchdog devices
  for (std::uint32_t i = 0; i < MAX_WATCHDOG_DEVICES; ++i) {
    if (!watchdogExists(i)) {
      continue;
    }

    if (status.deviceCount >= MAX_WATCHDOG_DEVICES) {
      break;
    }

    status.devices[status.deviceCount] = getWatchdogDevice(i);
    ++status.deviceCount;
  }

  // Check for software watchdog
  status.softdogLoaded = isSoftdogLoaded();

  // Determine if hardware watchdog exists
  // Hardware watchdogs typically have identity strings not containing "Software" or "softdog"
  for (std::size_t i = 0; i < status.deviceCount; ++i) {
    const char* IDENT = status.devices[i].identity.data();
    if (!containsSubstring(IDENT, "Software") && !containsSubstring(IDENT, "softdog") &&
        !containsSubstring(IDENT, "soft_")) {
      status.hasHardwareWatchdog = true;
      break;
    }
  }

  return status;
}

} // namespace system

} // namespace seeker