/**
 * @file CanBusInfo.cpp
 * @brief Implementation of SocketCAN interface enumeration and status queries.
 */

#include "src/device/inc/CanBusInfo.hpp"
#include "src/helpers/inc/Strings.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
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

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* NET_SYS_CLASS_PATH = "/sys/class/net";
constexpr std::size_t PATH_BUFFER_SIZE = 512;
constexpr std::size_t READ_BUFFER_SIZE = 256;

/* ----------------------------- CAN Helpers ----------------------------- */

/**
 * Get interface type by examining interface name and driver.
 */
inline CanInterfaceType detectInterfaceType(const char* name, const char* driver) noexcept {
  if (name == nullptr) {
    return CanInterfaceType::UNKNOWN;
  }

  // Virtual CAN (vcan)
  if (std::strncmp(name, "vcan", 4) == 0) {
    return CanInterfaceType::VIRTUAL;
  }

  // Serial-line CAN (slcan)
  if (std::strncmp(name, "slcan", 5) == 0) {
    return CanInterfaceType::SLCAN;
  }

  // Check driver for known hardware vendors
  if (driver != nullptr && driver[0] != '\0') {
    if (std::strstr(driver, "peak") != nullptr) {
      return CanInterfaceType::PEAK;
    }
    if (std::strstr(driver, "kvaser") != nullptr) {
      return CanInterfaceType::KVASER;
    }
    if (std::strstr(driver, "vcan") != nullptr) {
      return CanInterfaceType::VIRTUAL;
    }
  }

  // Default physical interface
  if (std::strncmp(name, "can", 3) == 0) {
    return CanInterfaceType::PHYSICAL;
  }

  return CanInterfaceType::UNKNOWN;
}

/**
 * Parse CAN bus state from string.
 */
inline CanBusState parseBusState(const char* str) noexcept {
  if (str == nullptr || str[0] == '\0') {
    return CanBusState::UNKNOWN;
  }

  if (std::strcmp(str, "ERROR-ACTIVE") == 0 || std::strcmp(str, "error-active") == 0) {
    return CanBusState::ERROR_ACTIVE;
  }
  if (std::strcmp(str, "ERROR-WARNING") == 0 || std::strcmp(str, "error-warning") == 0) {
    return CanBusState::ERROR_WARNING;
  }
  if (std::strcmp(str, "ERROR-PASSIVE") == 0 || std::strcmp(str, "error-passive") == 0) {
    return CanBusState::ERROR_PASSIVE;
  }
  if (std::strcmp(str, "BUS-OFF") == 0 || std::strcmp(str, "bus-off") == 0) {
    return CanBusState::BUS_OFF;
  }
  if (std::strcmp(str, "STOPPED") == 0 || std::strcmp(str, "stopped") == 0) {
    return CanBusState::STOPPED;
  }

  return CanBusState::UNKNOWN;
}

/**
 * Read interface flags using ioctl.
 */
inline unsigned int getInterfaceFlags(const char* name) noexcept {
  const int SOCK = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (SOCK < 0) {
    return 0;
  }

  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

  if (::ioctl(SOCK, SIOCGIFFLAGS, &ifr) < 0) {
    ::close(SOCK);
    return 0;
  }

  ::close(SOCK);
  return static_cast<unsigned int>(ifr.ifr_flags);
}

/**
 * Get interface index.
 */
inline int getInterfaceIndex(const char* name) noexcept {
  const int SOCK = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (SOCK < 0) {
    return -1;
  }

  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

  if (::ioctl(SOCK, SIOCGIFINDEX, &ifr) < 0) {
    ::close(SOCK);
    return -1;
  }

  ::close(SOCK);
  return ifr.ifr_ifindex;
}

/**
 * Read interface type from sysfs.
 */
inline bool isSysfsCanInterface(const char* name) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/type", NET_SYS_CLASS_PATH, name);

  // Type 280 = ARPHRD_CAN (CAN interface)
  const std::uint64_t TYPE = readFileUint64(path);
  return TYPE == 280;
}

/**
 * Read driver name for interface.
 */
inline void queryDriverName(const char* name, char* buf, std::size_t bufSize) noexcept {
  char linkPath[PATH_BUFFER_SIZE];
  std::snprintf(linkPath, sizeof(linkPath), "%s/%s/device/driver", NET_SYS_CLASS_PATH, name);

  char resolved[PATH_BUFFER_SIZE];
  const char* REAL = ::realpath(linkPath, resolved);

  if (REAL == nullptr) {
    buf[0] = '\0';
    return;
  }

  // Extract driver name from path (last component)
  const char* DRIVER = std::strrchr(resolved, '/');
  if (DRIVER != nullptr) {
    ++DRIVER;
  } else {
    DRIVER = resolved;
  }

  std::size_t i = 0;
  for (; i < bufSize - 1 && DRIVER[i] != '\0'; ++i) {
    buf[i] = DRIVER[i];
  }
  buf[i] = '\0';
}

/**
 * Read interface statistics from sysfs.
 */
inline CanInterfaceStats readInterfaceStats(const char* name) noexcept {
  CanInterfaceStats stats{};
  char path[PATH_BUFFER_SIZE];

  std::snprintf(path, sizeof(path), "%s/%s/statistics/tx_packets", NET_SYS_CLASS_PATH, name);
  stats.txFrames = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/rx_packets", NET_SYS_CLASS_PATH, name);
  stats.rxFrames = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/tx_bytes", NET_SYS_CLASS_PATH, name);
  stats.txBytes = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/rx_bytes", NET_SYS_CLASS_PATH, name);
  stats.rxBytes = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/tx_dropped", NET_SYS_CLASS_PATH, name);
  stats.txDropped = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/rx_dropped", NET_SYS_CLASS_PATH, name);
  stats.rxDropped = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/tx_errors", NET_SYS_CLASS_PATH, name);
  stats.txErrors = readFileUint64(path);

  std::snprintf(path, sizeof(path), "%s/%s/statistics/rx_errors", NET_SYS_CLASS_PATH, name);
  stats.rxErrors = readFileUint64(path);

  return stats;
}

/**
 * Read bit timing from sysfs (if available).
 */
inline CanBitTiming readBitTimingFromSysfs(const char* name) noexcept {
  CanBitTiming timing{};
  char path[PATH_BUFFER_SIZE];

  // These paths may or may not exist depending on driver
  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/bitrate", NET_SYS_CLASS_PATH, name);
  timing.bitrate = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/sample_point", NET_SYS_CLASS_PATH, name);
  timing.samplePoint = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/tq", NET_SYS_CLASS_PATH, name);
  timing.tq = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/prop_seg", NET_SYS_CLASS_PATH, name);
  timing.propSeg = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/phase_seg1", NET_SYS_CLASS_PATH, name);
  timing.phaseSeg1 = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/phase_seg2", NET_SYS_CLASS_PATH, name);
  timing.phaseSeg2 = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/sjw", NET_SYS_CLASS_PATH, name);
  timing.sjw = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_bittiming/brp", NET_SYS_CLASS_PATH, name);
  timing.brp = static_cast<std::uint32_t>(readFileUint64(path));

  return timing;
}

/**
 * Read bus state from sysfs.
 */
inline CanBusState readBusStateFromSysfs(const char* name) noexcept {
  char path[PATH_BUFFER_SIZE];
  char buf[READ_BUFFER_SIZE];

  std::snprintf(path, sizeof(path), "%s/%s/can_state", NET_SYS_CLASS_PATH, name);
  if (readFileToBuffer(path, buf, sizeof(buf)) == 0) {
    return CanBusState::UNKNOWN;
  }

  return parseBusState(buf);
}

/**
 * Read error counters from sysfs.
 */
inline CanErrorCounters readErrorCountersFromSysfs(const char* name) noexcept {
  CanErrorCounters errors{};
  char path[PATH_BUFFER_SIZE];

  std::snprintf(path, sizeof(path), "%s/%s/can_berr_counter/tx_errors", NET_SYS_CLASS_PATH, name);
  errors.txErrors = static_cast<std::uint16_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_berr_counter/rx_errors", NET_SYS_CLASS_PATH, name);
  errors.rxErrors = static_cast<std::uint16_t>(readFileUint64(path));

  // Device-specific error statistics (may not exist)
  std::snprintf(path, sizeof(path), "%s/%s/can_stats/bus_error", NET_SYS_CLASS_PATH, name);
  errors.busErrors = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_stats/error_warning", NET_SYS_CLASS_PATH, name);
  errors.errorWarning = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_stats/error_passive", NET_SYS_CLASS_PATH, name);
  errors.errorPassive = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_stats/bus_off", NET_SYS_CLASS_PATH, name);
  errors.busOff = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_stats/arbitration_lost", NET_SYS_CLASS_PATH, name);
  errors.arbitrationLost = static_cast<std::uint32_t>(readFileUint64(path));

  std::snprintf(path, sizeof(path), "%s/%s/can_stats/restarts", NET_SYS_CLASS_PATH, name);
  errors.restarts = static_cast<std::uint32_t>(readFileUint64(path));

  return errors;
}

/**
 * Read controller mode flags from sysfs.
 */
inline CanCtrlMode readCtrlModeFromSysfs(const char* name) noexcept {
  CanCtrlMode mode{};
  char path[PATH_BUFFER_SIZE];

  // These files contain "1" or "0" (or don't exist)
  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/loopback", NET_SYS_CLASS_PATH, name);
  mode.loopback = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/listen_only", NET_SYS_CLASS_PATH, name);
  mode.listenOnly = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/triple_sampling", NET_SYS_CLASS_PATH, name);
  mode.tripleSampling = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/one_shot", NET_SYS_CLASS_PATH, name);
  mode.oneShot = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/berr_reporting", NET_SYS_CLASS_PATH, name);
  mode.berr = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/fd", NET_SYS_CLASS_PATH, name);
  mode.fd = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/presume_ack", NET_SYS_CLASS_PATH, name);
  mode.presumeAck = (readFileUint64(path) != 0);

  std::snprintf(path, sizeof(path), "%s/%s/can_ctrlmode/fd_non_iso", NET_SYS_CLASS_PATH, name);
  mode.fdNonIso = (readFileUint64(path) != 0);

  return mode;
}

/**
 * Read TX queue length.
 */
inline std::uint32_t readTxqLen(const char* name) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/tx_queue_len", NET_SYS_CLASS_PATH, name);
  return static_cast<std::uint32_t>(readFileUint64(path));
}

/**
 * Read clock frequency.
 */
inline std::uint32_t readClockFreq(const char* name) noexcept {
  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/can_clock/freq", NET_SYS_CLASS_PATH, name);
  return static_cast<std::uint32_t>(readFileUint64(path));
}

} // anonymous namespace

/* ----------------------------- CanInterfaceType ----------------------------- */

const char* toString(CanInterfaceType type) noexcept {
  switch (type) {
  case CanInterfaceType::PHYSICAL:
    return "physical";
  case CanInterfaceType::VIRTUAL:
    return "virtual";
  case CanInterfaceType::SLCAN:
    return "slcan";
  case CanInterfaceType::SOCKETCAND:
    return "socketcand";
  case CanInterfaceType::PEAK:
    return "peak";
  case CanInterfaceType::KVASER:
    return "kvaser";
  case CanInterfaceType::VECTOR:
    return "vector";
  case CanInterfaceType::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- CanBusState ----------------------------- */

const char* toString(CanBusState state) noexcept {
  switch (state) {
  case CanBusState::ERROR_ACTIVE:
    return "error-active";
  case CanBusState::ERROR_WARNING:
    return "error-warning";
  case CanBusState::ERROR_PASSIVE:
    return "error-passive";
  case CanBusState::BUS_OFF:
    return "bus-off";
  case CanBusState::STOPPED:
    return "stopped";
  case CanBusState::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- CanCtrlMode Methods ----------------------------- */

bool CanCtrlMode::hasSpecialModes() const noexcept {
  return loopback || listenOnly || tripleSampling || oneShot || fd;
}

std::string CanCtrlMode::toString() const {
  std::string out;

  if (fd)
    out += "fd ";
  if (loopback)
    out += "loopback ";
  if (listenOnly)
    out += "listen-only ";
  if (tripleSampling)
    out += "triple-sampling ";
  if (oneShot)
    out += "one-shot ";
  if (berr)
    out += "berr ";
  if (presumeAck)
    out += "presume-ack ";
  if (fdNonIso)
    out += "fd-non-iso ";

  if (out.empty()) {
    return "normal";
  }

  // Remove trailing space
  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }

  return out;
}

/* ----------------------------- CanBitTiming Methods ----------------------------- */

bool CanBitTiming::isConfigured() const noexcept { return bitrate > 0; }

double CanBitTiming::samplePointPercent() const noexcept {
  return static_cast<double>(samplePoint) / 10.0;
}

std::string CanBitTiming::toString() const {
  if (!isConfigured()) {
    return "not configured";
  }

  std::string out;

  if (bitrate >= 1000000) {
    out = fmt::format("{} Mbps", bitrate / 1000000);
  } else if (bitrate >= 1000) {
    out = fmt::format("{} kbps", bitrate / 1000);
  } else {
    out = fmt::format("{} bps", bitrate);
  }

  if (samplePoint > 0) {
    out += fmt::format(" (SP: {:.1f}%)", samplePointPercent());
  }

  return out;
}

/* ----------------------------- CanErrorCounters Methods ----------------------------- */

bool CanErrorCounters::hasErrors() const noexcept {
  return txErrors > 0 || rxErrors > 0 || busErrors > 0 || errorWarning > 0 || errorPassive > 0 ||
         busOff > 0;
}

std::uint32_t CanErrorCounters::totalErrors() const noexcept {
  return static_cast<std::uint32_t>(txErrors) + static_cast<std::uint32_t>(rxErrors) + busErrors +
         errorWarning + errorPassive + busOff + arbitrationLost;
}

std::string CanErrorCounters::toString() const {
  if (!hasErrors()) {
    return "no errors";
  }

  std::string out = fmt::format("TEC: {}, REC: {}", txErrors, rxErrors);

  if (busErrors > 0) {
    out += fmt::format(", bus-errors: {}", busErrors);
  }
  if (busOff > 0) {
    out += fmt::format(", bus-off: {}", busOff);
  }
  if (arbitrationLost > 0) {
    out += fmt::format(", arb-lost: {}", arbitrationLost);
  }
  if (restarts > 0) {
    out += fmt::format(", restarts: {}", restarts);
  }

  return out;
}

/* ----------------------------- CanInterfaceStats Methods ----------------------------- */

std::string CanInterfaceStats::toString() const {
  return fmt::format("TX: {} frames ({} bytes), RX: {} frames ({} bytes)", txFrames, txBytes,
                     rxFrames, rxBytes);
}

/* ----------------------------- CanInterfaceInfo Methods ----------------------------- */

bool CanInterfaceInfo::isUsable() const noexcept {
  return exists && isUp && isRunning && state != CanBusState::BUS_OFF &&
         state != CanBusState::STOPPED;
}

bool CanInterfaceInfo::isFd() const noexcept { return ctrlMode.fd; }

bool CanInterfaceInfo::hasErrors() const noexcept {
  return errors.hasErrors() || state == CanBusState::ERROR_WARNING ||
         state == CanBusState::ERROR_PASSIVE || state == CanBusState::BUS_OFF;
}

std::string CanInterfaceInfo::toString() const {
  std::string out = fmt::format("{}: ", name.data());

  if (!exists) {
    out += "not found";
    return out;
  }

  // Type and state
  out += seeker::device::toString(type);
  if (isFd()) {
    out += " (FD)";
  }
  out += fmt::format(", {}", seeker::device::toString(state));

  // UP/DOWN status
  if (isUp) {
    out += ", UP";
  } else {
    out += ", DOWN";
  }

  // Bit timing
  if (bitTiming.isConfigured()) {
    out += fmt::format("\n  Bitrate: {}", bitTiming.toString());
  }

  // Data bit timing for CAN FD
  if (isFd() && dataBitTiming.isConfigured()) {
    out += fmt::format("\n  Data bitrate: {}", dataBitTiming.toString());
  }

  // Controller mode
  if (ctrlMode.hasSpecialModes()) {
    out += fmt::format("\n  Mode: {}", ctrlMode.toString());
  }

  // Errors
  if (errors.hasErrors()) {
    out += fmt::format("\n  Errors: {}", errors.toString());
  }

  // Driver
  if (driver[0] != '\0') {
    out += fmt::format("\n  Driver: {}", driver.data());
  }

  return out;
}

/* ----------------------------- CanInterfaceList Methods ----------------------------- */

const CanInterfaceInfo* CanInterfaceList::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (std::strcmp(interfaces[i].name.data(), name) == 0) {
      return &interfaces[i];
    }
  }

  return nullptr;
}

bool CanInterfaceList::empty() const noexcept { return count == 0; }

std::size_t CanInterfaceList::countUp() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (interfaces[i].isUp) {
      ++n;
    }
  }
  return n;
}

std::size_t CanInterfaceList::countPhysical() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (interfaces[i].type == CanInterfaceType::PHYSICAL ||
        interfaces[i].type == CanInterfaceType::PEAK ||
        interfaces[i].type == CanInterfaceType::KVASER ||
        interfaces[i].type == CanInterfaceType::VECTOR) {
      ++n;
    }
  }
  return n;
}

std::size_t CanInterfaceList::countWithErrors() const noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (interfaces[i].hasErrors()) {
      ++n;
    }
  }
  return n;
}

std::string CanInterfaceList::toString() const {
  if (count == 0) {
    return "No CAN interfaces found";
  }

  std::string out = fmt::format("CAN interfaces: {} found ({} up, {} physical", count, countUp(),
                                countPhysical());

  const std::size_t ERR_COUNT = countWithErrors();
  if (ERR_COUNT > 0) {
    out += fmt::format(", {} with errors", ERR_COUNT);
  }
  out += ")\n";

  for (std::size_t i = 0; i < count; ++i) {
    out += "\n" + interfaces[i].toString() + "\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

CanInterfaceInfo getCanInterfaceInfo(const char* name) noexcept {
  CanInterfaceInfo info{};

  if (name == nullptr || name[0] == '\0') {
    return info;
  }

  copyToFixedArray(info.name, name);

  // Build sysfs path
  std::snprintf(info.sysfsPath.data(), info.sysfsPath.size(), "%s/%s", NET_SYS_CLASS_PATH, name);

  // Check existence
  info.exists = pathExists(info.sysfsPath.data());
  if (!info.exists) {
    return info;
  }

  // Verify it's a CAN interface
  if (!isSysfsCanInterface(name)) {
    info.exists = false;
    return info;
  }

  // Get driver
  queryDriverName(name, info.driver.data(), info.driver.size());

  // Determine interface type
  info.type = detectInterfaceType(name, info.driver.data());

  // Get interface flags
  const unsigned int FLAGS = getInterfaceFlags(name);
  info.isUp = ((FLAGS & IFF_UP) != 0);
  info.isRunning = ((FLAGS & IFF_RUNNING) != 0);

  // Get interface index
  info.ifindex = getInterfaceIndex(name);

  // Get bit timing
  info.bitTiming = readBitTimingFromSysfs(name);

  // Get bus state
  info.state = readBusStateFromSysfs(name);

  // Get error counters
  info.errors = readErrorCountersFromSysfs(name);

  // Get controller mode
  info.ctrlMode = readCtrlModeFromSysfs(name);

  // Get statistics
  info.stats = readInterfaceStats(name);

  // Get TX queue length
  info.txqLen = readTxqLen(name);

  // Get clock frequency
  info.clockFreq = readClockFreq(name);

  // Get data bit timing if FD mode
  if (info.ctrlMode.fd) {
    char path[PATH_BUFFER_SIZE];
    std::snprintf(path, sizeof(path), "%s/%s/can_data_bittiming/bitrate", NET_SYS_CLASS_PATH, name);
    info.dataBitTiming.bitrate = static_cast<std::uint32_t>(readFileUint64(path));

    std::snprintf(path, sizeof(path), "%s/%s/can_data_bittiming/sample_point", NET_SYS_CLASS_PATH,
                  name);
    info.dataBitTiming.samplePoint = static_cast<std::uint32_t>(readFileUint64(path));
  }

  return info;
}

CanBitTiming getCanBitTiming(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return CanBitTiming{};
  }
  return readBitTimingFromSysfs(name);
}

CanErrorCounters getCanErrorCounters(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return CanErrorCounters{};
  }
  return readErrorCountersFromSysfs(name);
}

CanBusState getCanBusState(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return CanBusState::UNKNOWN;
  }
  return readBusStateFromSysfs(name);
}

CanInterfaceList getAllCanInterfaces() noexcept {
  CanInterfaceList list{};

  DIR* dir = ::opendir(NET_SYS_CLASS_PATH);
  if (dir == nullptr) {
    return list;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr && list.count < MAX_CAN_INTERFACES) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Check if it's a CAN interface
    if (!isSysfsCanInterface(entry->d_name)) {
      continue;
    }

    list.interfaces[list.count] = getCanInterfaceInfo(entry->d_name);
    if (list.interfaces[list.count].exists) {
      ++list.count;
    }
  }

  ::closedir(dir);
  return list;
}

bool isCanInterface(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  return isSysfsCanInterface(name);
}

bool canInterfaceExists(const char* name) noexcept {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  char path[PATH_BUFFER_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s", NET_SYS_CLASS_PATH, name);

  return pathExists(path) && isSysfsCanInterface(name);
}

} // namespace device

} // namespace seeker