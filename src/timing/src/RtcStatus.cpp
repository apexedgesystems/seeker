/**
 * @file RtcStatus.cpp
 * @brief RTC hardware status collection from sysfs.
 * @note Reads /sys/class/rtc/ hierarchy.
 */

#include "src/timing/inc/RtcStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>   // opendir, readdir, closedir
#include <fcntl.h>    // open, O_RDONLY
#include <sys/stat.h> // stat
#include <time.h>     // time, gmtime_r, timegm
#include <unistd.h>   // read, close

#include <array>   // std::array
#include <cstdlib> // atoi, strtoll
#include <cstring> // strncmp, strcmp, memcpy

#include <fmt/core.h>

namespace seeker {

namespace timing {

namespace {

using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileInt64;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;
using seeker::helpers::strings::sortFixedStrings;

/* ----------------------------- Constants ----------------------------- */

constexpr const char* RTC_CLASS_PATH = "/sys/class/rtc";
constexpr std::size_t PATH_BUF_SIZE = 512;
constexpr std::size_t READ_BUF_SIZE = 128;

// Acceptable drift threshold: 5 seconds
constexpr std::int64_t DRIFT_THRESHOLD_SEC = 5;

/* ----------------------------- File Helpers ----------------------------- */

/// Read string into fixed-size array.
template <std::size_t N> void readFileString(const char* path, std::array<char, N>& out) noexcept {
  out[0] = '\0';
  std::array<char, READ_BUF_SIZE> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN > 0) {
    const std::size_t COPY_LEN = (LEN < N - 1) ? LEN : (N - 1);
    std::memcpy(out.data(), buf.data(), COPY_LEN);
    out[COPY_LEN] = '\0';
  }
}

/// Read int32 from file (wrapper with different default).
std::int32_t readFileInt32(const char* path) noexcept { return readFileInt(path, 0); }

/// Check if path is a directory.
bool isDirectory(const char* path) noexcept {
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

/// Check if path exists.
bool pathExists(const char* path) noexcept { return ::access(path, F_OK) == 0; }

/* ----------------------------- Parsing Helpers ----------------------------- */

/// Extract index from directory name like "rtc0".
std::int32_t parseRtcIndex(const char* name) noexcept {
  if (std::strncmp(name, "rtc", 3) != 0) {
    return -1;
  }
  return std::atoi(name + 3);
}

/// Parse time string "HH:MM:SS" into components.
bool parseTimeString(const char* str, std::int32_t& hour, std::int32_t& minute,
                     std::int32_t& second) noexcept {
  if (str == nullptr || str[0] == '\0') {
    return false;
  }

  // Format: HH:MM:SS
  int h = 0;
  int m = 0;
  int s = 0;
  if (std::sscanf(str, "%d:%d:%d", &h, &m, &s) != 3) {
    return false;
  }

  hour = static_cast<std::int32_t>(h);
  minute = static_cast<std::int32_t>(m);
  second = static_cast<std::int32_t>(s);
  return true;
}

/// Parse date string "YYYY-MM-DD" into components.
bool parseDateString(const char* str, std::int32_t& year, std::int32_t& month,
                     std::int32_t& day) noexcept {
  if (str == nullptr || str[0] == '\0') {
    return false;
  }

  // Format: YYYY-MM-DD
  int y = 0;
  int m = 0;
  int d = 0;
  if (std::sscanf(str, "%d-%d-%d", &y, &m, &d) != 3) {
    return false;
  }

  year = static_cast<std::int32_t>(y);
  month = static_cast<std::int32_t>(m);
  day = static_cast<std::int32_t>(d);
  return true;
}

/// Convert date/time components to epoch seconds (UTC).
std::int64_t toEpochSeconds(std::int32_t year, std::int32_t month, std::int32_t day,
                            std::int32_t hour, std::int32_t minute, std::int32_t second) noexcept {
  struct tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = 0;

  // timegm interprets tm as UTC
  return static_cast<std::int64_t>(::timegm(&tm));
}

/* ----------------------------- Data Collection ----------------------------- */

/// Read RTC time from sysfs.
RtcTime readRtcTimeSysfs(const char* rtcName) noexcept {
  RtcTime rtcTime{};
  std::array<char, PATH_BUF_SIZE> pathBuf{};
  std::array<char, READ_BUF_SIZE> buf{};

  // Read time (HH:MM:SS)
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/time", RTC_CLASS_PATH, rtcName);
  if (readFileToBuffer(pathBuf.data(), buf.data(), buf.size()) == 0) {
    return rtcTime;
  }

  if (!parseTimeString(buf.data(), rtcTime.hour, rtcTime.minute, rtcTime.second)) {
    return rtcTime;
  }

  // Read date (YYYY-MM-DD)
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/date", RTC_CLASS_PATH, rtcName);
  if (readFileToBuffer(pathBuf.data(), buf.data(), buf.size()) == 0) {
    return rtcTime;
  }

  if (!parseDateString(buf.data(), rtcTime.year, rtcTime.month, rtcTime.day)) {
    return rtcTime;
  }

  // Convert to epoch
  rtcTime.epochSeconds = toEpochSeconds(rtcTime.year, rtcTime.month, rtcTime.day, rtcTime.hour,
                                        rtcTime.minute, rtcTime.second);

  // Get current system time for drift calculation
  rtcTime.systemEpochSec = static_cast<std::int64_t>(::time(nullptr));
  rtcTime.driftSeconds = rtcTime.epochSeconds - rtcTime.systemEpochSec;
  rtcTime.querySucceeded = true;

  return rtcTime;
}

/// Read RTC alarm status from sysfs.
RtcAlarm readRtcAlarmSysfs(const char* rtcName) noexcept {
  RtcAlarm alarm{};
  std::array<char, PATH_BUF_SIZE> pathBuf{};

  // Read wakealarm (Unix epoch timestamp, or 0 if not set)
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/wakealarm", RTC_CLASS_PATH, rtcName);
  if (!pathExists(pathBuf.data())) {
    // No wakealarm support
    return alarm;
  }

  alarm.alarmEpoch = readFileInt64(pathBuf.data(), 0);
  alarm.querySucceeded = true;

  if (alarm.alarmEpoch > 0) {
    alarm.enabled = true;
    const std::int64_t NOW = static_cast<std::int64_t>(::time(nullptr));
    alarm.secondsUntil = alarm.alarmEpoch - NOW;
  }

  return alarm;
}

/// Read RTC capabilities from sysfs.
RtcCapabilities readRtcCapsSysfs(const char* rtcName) noexcept {
  RtcCapabilities caps{};
  std::array<char, PATH_BUF_SIZE> pathBuf{};

  // Check for alarm support
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/device/rtc/%s/alarmN", RTC_CLASS_PATH,
                rtcName, rtcName);
  // Alternatively check via features
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/wakealarm", RTC_CLASS_PATH, rtcName);
  caps.hasWakeAlarm = pathExists(pathBuf.data());
  caps.hasAlarm = caps.hasWakeAlarm; // Simplification: wakealarm implies alarm

  // Check for max/min user freq (indicates periodic IRQ support)
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/max_user_freq", RTC_CLASS_PATH, rtcName);
  if (pathExists(pathBuf.data())) {
    caps.hasPeriodicIrq = true;
    caps.irqFreqMax = readFileInt32(pathBuf.data());
  }

  // Most hardware RTCs are battery-backed (infer from having a valid time on system boot)
  // This is a heuristic since sysfs doesn't expose battery status directly
  caps.hasBattery = true;

  return caps;
}

/// Collect information for a single RTC device.
void collectRtcDevice(const char* rtcName, RtcDevice& device) noexcept {
  std::array<char, PATH_BUF_SIZE> pathBuf{};

  // Set device name
  copyToFixedArray(device.device, rtcName);

  // Parse index
  device.index = parseRtcIndex(rtcName);

  // Read driver/chip name
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/name", RTC_CLASS_PATH, rtcName);
  readFileString(pathBuf.data(), device.name);

  // Check if this is the system RTC (hctosys = "1")
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/hctosys", RTC_CLASS_PATH, rtcName);
  readFileString(pathBuf.data(), device.hctosys);
  device.isSystemRtc = (device.hctosys[0] == '1') || (device.index == 0);

  // Read capabilities
  device.caps = readRtcCapsSysfs(rtcName);

  // Read current time
  device.time = readRtcTimeSysfs(rtcName);

  // Read alarm status
  device.alarm = readRtcAlarmSysfs(rtcName);
}

} // namespace

/* ----------------------------- RtcCapabilities Methods ----------------------------- */

bool RtcCapabilities::canWakeFromSuspend() const noexcept { return hasWakeAlarm; }

/* ----------------------------- RtcTime Methods ----------------------------- */

bool RtcTime::isValid() const noexcept {
  if (!querySucceeded) {
    return false;
  }
  // Basic sanity checks
  return year >= 1970 && year <= 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
         hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

bool RtcTime::isDriftAcceptable() const noexcept {
  if (!querySucceeded) {
    return false;
  }
  return absDrift() <= DRIFT_THRESHOLD_SEC;
}

std::int64_t RtcTime::absDrift() const noexcept {
  return (driftSeconds < 0) ? -driftSeconds : driftSeconds;
}

/* ----------------------------- RtcAlarm Methods ----------------------------- */

bool RtcAlarm::isFutureAlarm() const noexcept { return enabled && secondsUntil > 0; }

/* ----------------------------- RtcDevice Methods ----------------------------- */

bool RtcDevice::isValid() const noexcept { return device[0] != '\0' && index >= 0; }

const char* RtcDevice::healthString() const noexcept {
  if (!isValid()) {
    return "invalid";
  }
  if (!time.querySucceeded) {
    return "unreadable";
  }
  if (!time.isValid()) {
    return "invalid-time";
  }
  if (!time.isDriftAcceptable()) {
    return "drifted";
  }
  return "healthy";
}

/* ----------------------------- RtcStatus Methods ----------------------------- */

const RtcDevice* RtcStatus::findByName(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (std::strcmp(devices[i].device.data(), name) == 0) {
      return &devices[i];
    }
  }
  return nullptr;
}

const RtcDevice* RtcStatus::findByIndex(std::int32_t index) const noexcept {
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].index == index) {
      return &devices[i];
    }
  }
  return nullptr;
}

const RtcDevice* RtcStatus::getSystemRtc() const noexcept {
  // First, look for explicit hctosys
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].hctosys[0] == '1') {
      return &devices[i];
    }
  }
  // Fall back to rtc0
  return findByIndex(0);
}

std::int64_t RtcStatus::maxDriftSeconds() const noexcept {
  std::int64_t maxDrift = 0;
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].time.querySucceeded) {
      const std::int64_t DRIFT = devices[i].time.absDrift();
      if (DRIFT > maxDrift) {
        maxDrift = DRIFT;
      }
    }
  }
  return maxDrift;
}

bool RtcStatus::allDriftAcceptable() const noexcept {
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (devices[i].time.querySucceeded && !devices[i].time.isDriftAcceptable()) {
      return false;
    }
  }
  return true;
}

std::string RtcStatus::toString() const {
  std::string out;
  out.reserve(1024);

  if (!rtcSupported) {
    out = "RTC: Not supported (no /sys/class/rtc)\n";
    return out;
  }

  out += "RTC Status:\n";
  out += fmt::format("  Hardware RTCs: {}\n", deviceCount);
  out += fmt::format("  Wake-capable: {}\n", hasWakeCapable ? "yes" : "no");

  if (deviceCount == 0) {
    out += "  No hardware RTC detected\n";
    return out;
  }

  for (std::size_t i = 0; i < deviceCount; ++i) {
    const RtcDevice& DEV = devices[i];
    out += fmt::format("\n  {}{}:\n", DEV.device.data(), DEV.isSystemRtc ? " [system]" : "");

    if (DEV.name[0] != '\0') {
      out += fmt::format("    Driver: {}\n", DEV.name.data());
    }

    out += fmt::format("    Health: {}\n", DEV.healthString());

    if (DEV.time.querySucceeded) {
      out += fmt::format("    Time: {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}\n", DEV.time.year,
                         DEV.time.month, DEV.time.day, DEV.time.hour, DEV.time.minute,
                         DEV.time.second);
      out += fmt::format("    Drift: {} seconds {}\n", DEV.time.driftSeconds,
                         DEV.time.isDriftAcceptable() ? "[OK]" : "[HIGH]");
    }

    // Capabilities
    out += "    Features: ";
    bool first = true;
    auto addFeature = [&](const char* name) {
      if (!first) {
        out += ", ";
      }
      out += name;
      first = false;
    };

    if (DEV.caps.hasAlarm) {
      addFeature("alarm");
    }
    if (DEV.caps.hasWakeAlarm) {
      addFeature("wakealarm");
    }
    if (DEV.caps.hasPeriodicIrq) {
      addFeature("periodic-irq");
    }
    if (first) {
      out += "(none)";
    }
    out += "\n";

    // Alarm status
    if (DEV.alarm.querySucceeded && DEV.alarm.enabled) {
      out += fmt::format("    Wakealarm: set for {} seconds from now\n", DEV.alarm.secondsUntil);
    }
  }

  return out;
}

std::string RtcStatus::toJson() const {
  std::string out;
  out.reserve(2048);

  out += "{\n";
  out += fmt::format("  \"rtcSupported\": {},\n", rtcSupported ? "true" : "false");
  out += fmt::format("  \"hasHardwareRtc\": {},\n", hasHardwareRtc ? "true" : "false");
  out += fmt::format("  \"hasWakeCapable\": {},\n", hasWakeCapable ? "true" : "false");
  out += fmt::format("  \"deviceCount\": {},\n", deviceCount);
  out += fmt::format("  \"systemRtcIndex\": {},\n", systemRtcIndex);
  out += fmt::format("  \"maxDriftSeconds\": {},\n", maxDriftSeconds());
  out += fmt::format("  \"allDriftAcceptable\": {},\n", allDriftAcceptable() ? "true" : "false");

  out += "  \"devices\": [";
  for (std::size_t i = 0; i < deviceCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    const RtcDevice& DEV = devices[i];
    out += fmt::format(
        "{{\n"
        "    \"device\": \"{}\",\n"
        "    \"index\": {},\n"
        "    \"name\": \"{}\",\n"
        "    \"isSystemRtc\": {},\n"
        "    \"health\": \"{}\",\n"
        "    \"hasAlarm\": {},\n"
        "    \"hasWakeAlarm\": {},\n"
        "    \"hasPeriodicIrq\": {},\n"
        "    \"timeValid\": {},\n"
        "    \"epochSeconds\": {},\n"
        "    \"driftSeconds\": {},\n"
        "    \"driftAcceptable\": {},\n"
        "    \"alarmEnabled\": {},\n"
        "    \"alarmEpoch\": {}\n"
        "  }}",
        DEV.device.data(), DEV.index, DEV.name.data(), DEV.isSystemRtc ? "true" : "false",
        DEV.healthString(), DEV.caps.hasAlarm ? "true" : "false",
        DEV.caps.hasWakeAlarm ? "true" : "false", DEV.caps.hasPeriodicIrq ? "true" : "false",
        DEV.time.isValid() ? "true" : "false", DEV.time.epochSeconds, DEV.time.driftSeconds,
        DEV.time.isDriftAcceptable() ? "true" : "false", DEV.alarm.enabled ? "true" : "false",
        DEV.alarm.alarmEpoch);
  }
  out += "]\n";
  out += "}";

  return out;
}

/* ----------------------------- API ----------------------------- */

bool isRtcSupported() noexcept { return isDirectory(RTC_CLASS_PATH); }

RtcTime getRtcTime(const char* device) noexcept {
  if (device == nullptr) {
    return RtcTime{};
  }

  // Strip path prefix if present
  const char* rtcName = device;
  if (std::strncmp(device, "/dev/", 5) == 0) {
    rtcName = device + 5;
  } else if (std::strncmp(device, "/sys/class/rtc/", 15) == 0) {
    rtcName = device + 15;
  }

  return readRtcTimeSysfs(rtcName);
}

RtcAlarm getRtcAlarm(const char* device) noexcept {
  if (device == nullptr) {
    return RtcAlarm{};
  }

  // Strip path prefix if present
  const char* rtcName = device;
  if (std::strncmp(device, "/dev/", 5) == 0) {
    rtcName = device + 5;
  }

  return readRtcAlarmSysfs(rtcName);
}

RtcStatus getRtcStatus() noexcept {
  RtcStatus status{};

  // Check if RTC subsystem exists
  if (!isDirectory(RTC_CLASS_PATH)) {
    status.rtcSupported = false;
    return status;
  }
  status.rtcSupported = true;

  // Enumerate RTC devices
  DIR* rtcDir = ::opendir(RTC_CLASS_PATH);
  if (rtcDir == nullptr) {
    return status;
  }

  // Collect device names for sorting
  std::array<std::array<char, RTC_DEVICE_NAME_SIZE>, RTC_MAX_DEVICES> rtcNames{};
  std::size_t nameCount = 0;

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(rtcDir)) != nullptr && nameCount < RTC_MAX_DEVICES) {
    if (std::strncmp(entry->d_name, "rtc", 3) != 0) {
      continue;
    }

    // Verify it's a directory/symlink
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s", RTC_CLASS_PATH, entry->d_name);
    if (!isDirectory(pathBuf.data())) {
      continue;
    }

    copyToFixedArray(rtcNames[nameCount], entry->d_name);
    ++nameCount;
  }
  ::closedir(rtcDir);

  // Sort for consistent ordering
  sortFixedStrings(rtcNames, nameCount);

  // Collect data for each RTC
  for (std::size_t i = 0; i < nameCount; ++i) {
    collectRtcDevice(rtcNames[i].data(), status.devices[status.deviceCount]);

    // Track wake capability
    if (status.devices[status.deviceCount].caps.hasWakeAlarm) {
      status.hasWakeCapable = true;
    }

    // Track system RTC
    if (status.devices[status.deviceCount].isSystemRtc && status.systemRtcIndex < 0) {
      status.systemRtcIndex = status.devices[status.deviceCount].index;
    }

    ++status.deviceCount;
  }

  status.hasHardwareRtc = (status.deviceCount > 0);

  return status;
}

} // namespace timing

} // namespace seeker