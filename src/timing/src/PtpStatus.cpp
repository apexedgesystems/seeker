/**
 * @file PtpStatus.cpp
 * @brief PTP hardware clock status collection.
 * @note Reads /sys/class/ptp/ and uses PTP_CLOCK_GETCAPS ioctl.
 */

#include "src/timing/inc/PtpStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>          // opendir, readdir, closedir
#include <fcntl.h>           // open, O_RDONLY
#include <linux/ptp_clock.h> // PTP_CLOCK_GETCAPS, struct ptp_clock_caps
#include <sys/ioctl.h>       // ioctl
#include <sys/stat.h>        // stat
#include <unistd.h>          // read, close

#include <array>   // std::array
#include <cstdlib> // atoi
#include <cstring> // strncmp, strcmp, memcpy

#include <fmt/core.h>

namespace seeker {

namespace timing {

namespace {

using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;
using seeker::helpers::strings::sortFixedStrings;

/* ----------------------------- Constants ----------------------------- */

constexpr const char* PTP_CLASS_PATH = "/sys/class/ptp";
constexpr const char* NET_CLASS_PATH = "/sys/class/net";
constexpr std::size_t PATH_BUF_SIZE = 512;
constexpr std::size_t READ_BUF_SIZE = 128;

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

/// Extract index from directory name like "ptp0".
std::int32_t parsePtpIndex(const char* name) noexcept {
  if (std::strncmp(name, "ptp", 3) != 0) {
    return -1;
  }
  return std::atoi(name + 3);
}

/* ----------------------------- PTP Capabilities Query ----------------------------- */

/// Query PTP clock capabilities via ioctl.
bool queryPtpCaps(const char* devPath, PtpClockCaps& caps) noexcept {
  const int FD = ::open(devPath, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return false;
  }

  struct ptp_clock_caps ptpCaps{};
  const int RET = ::ioctl(FD, PTP_CLOCK_GETCAPS, &ptpCaps);
  ::close(FD);

  if (RET < 0) {
    return false;
  }

  caps.maxAdjPpb = ptpCaps.max_adj;
  caps.nAlarm = ptpCaps.n_alarm;
  caps.nExtTs = ptpCaps.n_ext_ts;
  caps.nPerOut = ptpCaps.n_per_out;
  caps.nPins = ptpCaps.n_pins;
  caps.pps = (ptpCaps.pps != 0);

  // Cross-timestamp and phase adjustment are newer kernel features.
  // Rather than complex compile-time detection, we conservatively
  // report these as unavailable. Systems needing this info can
  // query via sysfs or use newer kernel-specific tools.
  caps.crossTimestamp = false;
  caps.adjustPhase = false;
  caps.maxAdjPhaseNs = 0;

  return true;
}

/* ----------------------------- Interface Binding ----------------------------- */

/// Find network interface bound to a PTP device.
void findBoundInterface(PtpClock& clock) noexcept {
  // Check /sys/class/net/*/device/ptp for matching PTP index
  DIR* netDir = ::opendir(NET_CLASS_PATH);
  if (netDir == nullptr) {
    return;
  }

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(netDir)) != nullptr) {
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Check for PTP device symlink
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/device/ptp/%s", NET_CLASS_PATH,
                  entry->d_name, clock.device.data());

    if (pathExists(pathBuf.data())) {
      copyToFixedArray(clock.boundInterface, entry->d_name);
      clock.hasBoundInterface = true;
      break;
    }

    // Alternative: check phc_index in ethtool info
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/phc_index", NET_CLASS_PATH, entry->d_name);
    const std::int32_t PHC_IDX = readFileInt32(pathBuf.data());
    if (PHC_IDX >= 0 && PHC_IDX == clock.index) {
      copyToFixedArray(clock.boundInterface, entry->d_name);
      clock.hasBoundInterface = true;
      break;
    }
  }

  ::closedir(netDir);
}

/* ----------------------------- Data Collection ----------------------------- */

/// Collect information for a single PTP clock.
void collectPtpClock(const char* ptpName, PtpClock& clock) noexcept {
  std::array<char, PATH_BUF_SIZE> pathBuf{};

  // Set device name
  copyToFixedArray(clock.device, ptpName);

  // Parse index
  clock.index = parsePtpIndex(ptpName);
  clock.phcIndex = clock.index; // PHC index typically matches PTP index

  // Read clock name
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/clock_name", PTP_CLASS_PATH, ptpName);
  readFileString(pathBuf.data(), clock.clockName);

  // Query capabilities via ioctl
  std::snprintf(pathBuf.data(), pathBuf.size(), "/dev/%s", ptpName);
  clock.capsQuerySucceeded = queryPtpCaps(pathBuf.data(), clock.caps);

  // Find bound network interface
  findBoundInterface(clock);
}

} // namespace

/* ----------------------------- PtpClockCaps Methods ----------------------------- */

bool PtpClockCaps::hasExtTimestamp() const noexcept { return nExtTs > 0; }

bool PtpClockCaps::hasPeriodicOutput() const noexcept { return nPerOut > 0; }

bool PtpClockCaps::hasHighPrecisionSync() const noexcept { return crossTimestamp && pps; }

/* ----------------------------- PtpClock Methods ----------------------------- */

bool PtpClock::isValid() const noexcept { return device[0] != '\0' && index >= 0; }

int PtpClock::rtScore() const noexcept {
  if (!isValid()) {
    return 0;
  }

  int score = 0;

  // Base score for having a valid clock
  score += 40;

  // Capabilities query succeeded
  if (capsQuerySucceeded) {
    score += 10;

    // High max adjustment range (> 100ppm = 100000ppb)
    if (caps.maxAdjPpb > 100000) {
      score += 10;
    }

    // PPS output support (important for precision timing)
    if (caps.pps) {
      score += 20;
    }

    // External timestamp channels
    if (caps.nExtTs > 0) {
      score += 5;
    }

    // Periodic output channels
    if (caps.nPerOut > 0) {
      score += 5;
    }
  }

  // Bound to network interface
  if (hasBoundInterface) {
    score += 10;
  }

  // Cap at 100
  return (score > 100) ? 100 : score;
}

/* ----------------------------- PtpStatus Methods ----------------------------- */

const PtpClock* PtpStatus::findByDevice(const char* device) const noexcept {
  if (device == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < clockCount; ++i) {
    if (std::strcmp(clocks[i].device.data(), device) == 0) {
      return &clocks[i];
    }
  }
  return nullptr;
}

const PtpClock* PtpStatus::findByIndex(std::int32_t index) const noexcept {
  for (std::size_t i = 0; i < clockCount; ++i) {
    if (clocks[i].index == index) {
      return &clocks[i];
    }
  }
  return nullptr;
}

const PtpClock* PtpStatus::findByInterface(const char* iface) const noexcept {
  if (iface == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < clockCount; ++i) {
    if (clocks[i].hasBoundInterface && std::strcmp(clocks[i].boundInterface.data(), iface) == 0) {
      return &clocks[i];
    }
  }
  return nullptr;
}

const PtpClock* PtpStatus::getBestClock() const noexcept {
  if (clockCount == 0) {
    return nullptr;
  }

  const PtpClock* best = &clocks[0];
  int bestScore = best->rtScore();

  for (std::size_t i = 1; i < clockCount; ++i) {
    const int SCORE = clocks[i].rtScore();
    if (SCORE > bestScore) {
      best = &clocks[i];
      bestScore = SCORE;
    }
  }

  return best;
}

int PtpStatus::rtScore() const noexcept {
  if (!ptpSupported) {
    return 0;
  }
  if (clockCount == 0) {
    return 10; // PTP supported but no hardware clocks
  }

  const PtpClock* BEST = getBestClock();
  return BEST != nullptr ? BEST->rtScore() : 0;
}

std::string PtpStatus::toString() const {
  std::string out;
  out.reserve(1024);

  if (!ptpSupported) {
    out = "PTP: Not supported (no /sys/class/ptp)\n";
    return out;
  }

  out += "PTP Status:\n";
  out += fmt::format("  Hardware clocks: {}\n", clockCount);

  if (clockCount == 0) {
    out += "  No PTP hardware clocks detected\n";
    return out;
  }

  for (std::size_t i = 0; i < clockCount; ++i) {
    const PtpClock& CLK = clocks[i];
    out += fmt::format("\n  {}:\n", CLK.device.data());

    if (CLK.clockName[0] != '\0') {
      out += fmt::format("    Name: {}\n", CLK.clockName.data());
    }

    if (CLK.hasBoundInterface) {
      out += fmt::format("    Interface: {}\n", CLK.boundInterface.data());
    }

    if (CLK.capsQuerySucceeded) {
      out += fmt::format("    Max adjustment: {} ppb\n", CLK.caps.maxAdjPpb);
      out += fmt::format("    Features: ");

      bool first = true;
      auto addFeature = [&](const char* name) {
        if (!first) {
          out += ", ";
        }
        out += name;
        first = false;
      };

      if (CLK.caps.pps) {
        addFeature("PPS");
      }
      if (CLK.caps.crossTimestamp) {
        addFeature("cross-timestamp");
      }
      if (CLK.caps.nExtTs > 0) {
        addFeature(fmt::format("ext-ts({})", CLK.caps.nExtTs).c_str());
      }
      if (CLK.caps.nPerOut > 0) {
        addFeature(fmt::format("per-out({})", CLK.caps.nPerOut).c_str());
      }
      if (CLK.caps.nAlarm > 0) {
        addFeature(fmt::format("alarm({})", CLK.caps.nAlarm).c_str());
      }

      if (first) {
        out += "(none)";
      }
      out += "\n";
    } else {
      out += "    Capabilities: (query failed)\n";
    }

    out += fmt::format("    RT Score: {}/100\n", CLK.rtScore());
  }

  out += fmt::format("\n  Overall RT Score: {}/100\n", rtScore());

  return out;
}

std::string PtpStatus::toJson() const {
  std::string out;
  out.reserve(2048);

  out += "{\n";
  out += fmt::format("  \"ptpSupported\": {},\n", ptpSupported ? "true" : "false");
  out += fmt::format("  \"hasHardwareClock\": {},\n", hasHardwareClock ? "true" : "false");
  out += fmt::format("  \"clockCount\": {},\n", clockCount);
  out += fmt::format("  \"rtScore\": {},\n", rtScore());

  out += "  \"clocks\": [";
  for (std::size_t i = 0; i < clockCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    const PtpClock& CLK = clocks[i];
    out += fmt::format(
        "{{\n"
        "    \"device\": \"{}\",\n"
        "    \"index\": {},\n"
        "    \"clockName\": \"{}\",\n"
        "    \"boundInterface\": \"{}\",\n"
        "    \"hasBoundInterface\": {},\n"
        "    \"capsQuerySucceeded\": {},\n"
        "    \"maxAdjPpb\": {},\n"
        "    \"nAlarm\": {},\n"
        "    \"nExtTs\": {},\n"
        "    \"nPerOut\": {},\n"
        "    \"nPins\": {},\n"
        "    \"pps\": {},\n"
        "    \"crossTimestamp\": {},\n"
        "    \"rtScore\": {}\n"
        "  }}",
        CLK.device.data(), CLK.index, CLK.clockName.data(), CLK.boundInterface.data(),
        CLK.hasBoundInterface ? "true" : "false", CLK.capsQuerySucceeded ? "true" : "false",
        CLK.caps.maxAdjPpb, CLK.caps.nAlarm, CLK.caps.nExtTs, CLK.caps.nPerOut, CLK.caps.nPins,
        CLK.caps.pps ? "true" : "false", CLK.caps.crossTimestamp ? "true" : "false", CLK.rtScore());
  }
  out += "]\n";
  out += "}";

  return out;
}

/* ----------------------------- API ----------------------------- */

bool isPtpSupported() noexcept { return isDirectory(PTP_CLASS_PATH); }

PtpClockCaps getPtpClockCaps(const char* device) noexcept {
  PtpClockCaps caps{};

  if (device == nullptr) {
    return caps;
  }

  // Build device path if not already a path
  std::array<char, PATH_BUF_SIZE> devPath{};
  if (device[0] == '/') {
    std::snprintf(devPath.data(), devPath.size(), "%s", device);
  } else {
    std::snprintf(devPath.data(), devPath.size(), "/dev/%s", device);
  }

  queryPtpCaps(devPath.data(), caps);
  return caps;
}

std::int32_t getPhcIndexForInterface(const char* iface) noexcept {
  if (iface == nullptr) {
    return -1;
  }

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s/phc_index", NET_CLASS_PATH, iface);

  std::array<char, READ_BUF_SIZE> buf{};
  if (readFileToBuffer(pathBuf.data(), buf.data(), buf.size()) == 0) {
    return -1;
  }

  return std::atoi(buf.data());
}

PtpStatus getPtpStatus() noexcept {
  PtpStatus status{};

  // Check if PTP subsystem exists
  if (!isDirectory(PTP_CLASS_PATH)) {
    status.ptpSupported = false;
    return status;
  }
  status.ptpSupported = true;

  // Enumerate PTP devices
  DIR* ptpDir = ::opendir(PTP_CLASS_PATH);
  if (ptpDir == nullptr) {
    return status;
  }

  // Collect device names for sorting
  std::array<std::array<char, PTP_DEVICE_NAME_SIZE>, PTP_MAX_CLOCKS> ptpNames{};
  std::size_t nameCount = 0;

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(ptpDir)) != nullptr && nameCount < PTP_MAX_CLOCKS) {
    if (std::strncmp(entry->d_name, "ptp", 3) != 0) {
      continue;
    }

    // Verify it's a directory/symlink
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s", PTP_CLASS_PATH, entry->d_name);
    if (!isDirectory(pathBuf.data())) {
      continue;
    }

    copyToFixedArray(ptpNames[nameCount], entry->d_name);
    ++nameCount;
  }
  ::closedir(ptpDir);

  // Sort for consistent ordering
  sortFixedStrings(ptpNames, nameCount);

  // Collect data for each PTP clock
  for (std::size_t i = 0; i < nameCount; ++i) {
    collectPtpClock(ptpNames[i].data(), status.clocks[status.clockCount]);
    ++status.clockCount;
  }

  status.hasHardwareClock = (status.clockCount > 0);

  return status;
}

} // namespace timing

} // namespace seeker