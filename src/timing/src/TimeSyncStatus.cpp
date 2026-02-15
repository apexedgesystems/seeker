/**
 * @file TimeSyncStatus.cpp
 * @brief Implementation of time synchronization status queries.
 */

#include "src/timing/inc/TimeSyncStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <fcntl.h>
#include <sys/timex.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

#include <fmt/core.h>

namespace seeker {

namespace timing {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

/// Check if any file matching prefix exists in directory.
bool hasFileWithPrefix(const char* dirPath, const char* prefix) noexcept {
  DIR* dir = ::opendir(dirPath);
  if (dir == nullptr) {
    return false;
  }

  const std::size_t PREFIX_LEN = std::strlen(prefix);
  bool found = false;

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr) {
    if (std::strncmp(entry->d_name, prefix, PREFIX_LEN) == 0) {
      found = true;
      break;
    }
  }

  ::closedir(dir);
  return found;
}

/// Enumerate PTP devices from /sys/class/ptp/.
std::size_t enumeratePtpDevices(PtpDevice* devices, std::size_t maxDevices) noexcept {
  const char* PTP_PATH = "/sys/class/ptp";
  DIR* dir = ::opendir(PTP_PATH);
  if (dir == nullptr) {
    return 0;
  }

  std::size_t count = 0;
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(dir)) != nullptr && count < maxDevices) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Only process ptp* entries
    if (std::strncmp(entry->d_name, "ptp", 3) != 0) {
      continue;
    }

    PtpDevice& dev = devices[count];

    // Copy device name
    copyToFixedArray(dev.name, entry->d_name);

    // Read clock name
    std::array<char, 512> attrPath{};
    std::snprintf(attrPath.data(), attrPath.size(), "%s/%s/clock_name", PTP_PATH, entry->d_name);
    (void)readFileToBuffer(attrPath.data(), dev.clock.data(), PTP_CLOCK_NAME_SIZE);

    // Read max_adjustment
    std::snprintf(attrPath.data(), attrPath.size(), "%s/%s/max_adjustment", PTP_PATH,
                  entry->d_name);
    std::array<char, 32> adjBuf{};
    if (readFileToBuffer(attrPath.data(), adjBuf.data(), adjBuf.size()) > 0) {
      dev.maxAdjPpb = std::strtoll(adjBuf.data(), nullptr, 10);
    }

    // Check PPS availability
    std::snprintf(attrPath.data(), attrPath.size(), "%s/%s/pps_available", PTP_PATH, entry->d_name);
    std::array<char, 8> ppsBuf{};
    if (readFileToBuffer(attrPath.data(), ppsBuf.data(), ppsBuf.size()) > 0) {
      dev.ppsAvailable = (ppsBuf[0] == '1') ? 1 : 0;
    }

    ++count;
  }

  ::closedir(dir);
  return count;
}

} // namespace

/* ----------------------------- PtpDevice Methods ----------------------------- */

bool PtpDevice::isValid() const noexcept { return name[0] != '\0'; }

/* ----------------------------- KernelTimeStatus Methods ----------------------------- */

bool KernelTimeStatus::isWellSynced() const noexcept {
  if (!querySucceeded || !synced) {
    return false;
  }
  // Well-synced: offset < 1ms and estimated error < 10ms
  const std::int64_t OFFSET_THRESHOLD_US = 1000;
  const std::int64_t ERROR_THRESHOLD_US = 10000;

  const std::int64_t ABS_OFFSET = (offsetUs < 0) ? -offsetUs : offsetUs;
  return ABS_OFFSET < OFFSET_THRESHOLD_US && estErrorUs < ERROR_THRESHOLD_US;
}

const char* KernelTimeStatus::qualityString() const noexcept {
  if (!querySucceeded) {
    return "unknown";
  }
  if (!synced) {
    return "unsynchronized";
  }

  const std::int64_t ABS_OFFSET = (offsetUs < 0) ? -offsetUs : offsetUs;

  if (ABS_OFFSET < 100 && estErrorUs < 1000) {
    return "excellent";
  }
  if (ABS_OFFSET < 1000 && estErrorUs < 10000) {
    return "good";
  }
  if (ABS_OFFSET < 10000 && estErrorUs < 100000) {
    return "fair";
  }
  return "poor";
}

/* ----------------------------- TimeSyncStatus Methods ----------------------------- */

bool TimeSyncStatus::hasAnySyncDaemon() const noexcept {
  return chronyDetected || ntpdDetected || systemdTimesyncDetected || ptpLinuxDetected;
}

bool TimeSyncStatus::hasPtpHardware() const noexcept { return ptpDeviceCount > 0; }

const char* TimeSyncStatus::primarySyncMethod() const noexcept {
  // Priority order: PTP > chrony > ntpd > systemd-timesyncd
  if (ptpLinuxDetected && ptpDeviceCount > 0) {
    return "ptp";
  }
  if (chronyDetected) {
    return "chrony";
  }
  if (ntpdDetected) {
    return "ntpd";
  }
  if (systemdTimesyncDetected) {
    return "systemd-timesyncd";
  }
  return "none";
}

int TimeSyncStatus::rtScore() const noexcept {
  int score = 0;

  // Sync daemon component (0-30 points)
  if (ptpLinuxDetected && ptpDeviceCount > 0) {
    score += 30; // PTP with hardware is best
  } else if (ptpLinuxDetected) {
    score += 25; // Software PTP
  } else if (chronyDetected) {
    score += 20; // chrony (good NTP implementation)
  } else if (ntpdDetected) {
    score += 15; // ntpd
  } else if (systemdTimesyncDetected) {
    score += 10; // Basic SNTP
  }

  // PTP hardware component (0-20 points)
  if (ptpDeviceCount > 0) {
    score += 20;
  }

  // Kernel sync status component (0-50 points)
  if (kernel.querySucceeded && kernel.synced) {
    const std::int64_t ABS_OFFSET = (kernel.offsetUs < 0) ? -kernel.offsetUs : kernel.offsetUs;

    if (ABS_OFFSET < 100) {
      score += 50; // < 100us offset
    } else if (ABS_OFFSET < 1000) {
      score += 40; // < 1ms offset
    } else if (ABS_OFFSET < 10000) {
      score += 25; // < 10ms offset
    } else {
      score += 10; // Synced but high offset
    }
  }

  // Cap at 100
  if (score > 100) {
    score = 100;
  }

  return score;
}

std::string TimeSyncStatus::toString() const {
  std::string out;
  out.reserve(1024);

  out += "Time Synchronization Status:\n";

  // Sync daemons
  out += "  Sync Daemons:\n";
  out += fmt::format("    chrony: {}\n", chronyDetected ? "detected" : "not found");
  out += fmt::format("    ntpd: {}\n", ntpdDetected ? "detected" : "not found");
  out += fmt::format("    systemd-timesyncd: {}\n",
                     systemdTimesyncDetected ? "detected" : "not found");
  out += fmt::format("    linuxptp: {}\n", ptpLinuxDetected ? "detected" : "not found");
  out += fmt::format("    Primary method: {}\n", primarySyncMethod());

  // PTP devices
  out += fmt::format("  PTP Hardware: {} device(s)\n", ptpDeviceCount);
  for (std::size_t i = 0; i < ptpDeviceCount; ++i) {
    const PtpDevice& dev = ptpDevices[i];
    out += fmt::format("    {}: {}", dev.name.data(),
                       dev.clock[0] != '\0' ? dev.clock.data() : "(unknown)");
    if (dev.ppsAvailable == 1) {
      out += " [PPS]";
    }
    out += "\n";
  }

  // Kernel time status
  out += "  Kernel Time Status:\n";
  if (kernel.querySucceeded) {
    out += fmt::format("    Synchronized: {}\n", kernel.synced ? "yes" : "no");
    out += fmt::format("    Quality: {}\n", kernel.qualityString());
    out += fmt::format("    Offset: {} us\n", kernel.offsetUs);
    out += fmt::format("    Est. Error: {} us\n", kernel.estErrorUs);
    out += fmt::format("    Freq Adj: {} ppb\n", kernel.freqPpb);
    if (kernel.ppsTime || kernel.ppsFreq) {
      out += "    PPS discipline: active\n";
    }
  } else {
    out += "    (query failed)\n";
  }

  out += fmt::format("  RT Score: {}/100\n", rtScore());

  return out;
}

/* ----------------------------- API ----------------------------- */

TimeSyncStatus getTimeSyncStatus() noexcept {
  TimeSyncStatus status;

  // Detect sync daemons by checking runtime directories and PIDs
  status.chronyDetected = pathExists("/run/chrony") || pathExists("/var/run/chrony") ||
                          pathExists("/run/chrony/chronyd.pid");

  status.ntpdDetected =
      pathExists("/var/lib/ntp") || pathExists("/run/ntpd.pid") || pathExists("/var/run/ntpd.pid");

  status.systemdTimesyncDetected = pathExists("/run/systemd/timesync");

  // Check for linuxptp (ptp4l)
  status.ptpLinuxDetected = pathExists("/run/ptp4l") || hasFileWithPrefix("/run", "ptp4l") ||
                            pathExists("/var/run/ptp4l.pid");

  // Enumerate PTP devices
  status.ptpDeviceCount = enumeratePtpDevices(status.ptpDevices, MAX_PTP_DEVICES);

  // Query kernel time status
  status.kernel = getKernelTimeStatus();

  return status;
}

KernelTimeStatus getKernelTimeStatus() noexcept {
  KernelTimeStatus status;

  struct timex tx{};
  tx.modes = 0; // Query only, don't modify

  const int RESULT = ::adjtimex(&tx);
  if (RESULT < 0) {
    return status;
  }

  status.querySucceeded = true;
  status.clockState = RESULT;

  // Parse status flags
  status.synced = (tx.status & STA_UNSYNC) == 0;
  status.pll = (tx.status & STA_PLL) != 0;
  status.ppsFreq = (tx.status & STA_PPSFREQ) != 0;
  status.ppsTime = (tx.status & STA_PPSTIME) != 0;
  status.freqHold = (tx.status & STA_FREQHOLD) != 0;

  // Time offset (tx.offset is in microseconds when STA_NANO not set)
  // Modern kernels use nanoseconds when STA_NANO is set
  if ((tx.status & STA_NANO) != 0) {
    status.offsetUs = tx.offset / 1000;
  } else {
    status.offsetUs = tx.offset;
  }

  // Frequency adjustment: tx.freq is in ppm with 16-bit fraction (scaled by 2^16)
  // Convert to ppb: (freq / 65536) * 1000
  status.freqPpb = (static_cast<std::int64_t>(tx.freq) * 1000) / 65536;

  // Error estimates
  status.maxErrorUs = static_cast<std::int64_t>(tx.maxerror);
  status.estErrorUs = static_cast<std::int64_t>(tx.esterror);

  return status;
}

bool isSyncDaemonRunning(const char* daemon) noexcept {
  if (daemon == nullptr) {
    return false;
  }

  if (std::strcmp(daemon, "chrony") == 0) {
    return pathExists("/run/chrony") || pathExists("/var/run/chrony") ||
           pathExists("/run/chrony/chronyd.pid");
  }

  if (std::strcmp(daemon, "ntpd") == 0) {
    return pathExists("/run/ntpd.pid") || pathExists("/var/run/ntpd.pid") ||
           pathExists("/var/lib/ntp/ntp.drift");
  }

  if (std::strcmp(daemon, "systemd-timesyncd") == 0) {
    return pathExists("/run/systemd/timesync");
  }

  if (std::strcmp(daemon, "ptp4l") == 0 || std::strcmp(daemon, "linuxptp") == 0) {
    return pathExists("/run/ptp4l") || hasFileWithPrefix("/run", "ptp4l") ||
           pathExists("/var/run/ptp4l.pid");
  }

  return false;
}

} // namespace timing

} // namespace seeker
