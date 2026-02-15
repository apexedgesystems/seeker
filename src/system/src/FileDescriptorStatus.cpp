/**
 * @file FileDescriptorStatus.cpp
 * @brief Implementation of file descriptor status monitoring.
 */

#include "src/system/inc/FileDescriptorStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>       // opendir, readdir, closedir
#include <sys/resource.h> // getrlimit
#include <unistd.h>       // readlink

#include <cstdlib> // strtoul
#include <cstring> // strlen, strncmp, strstr

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;
using seeker::helpers::strings::startsWith;

/* ----------------------------- Constants ----------------------------- */

constexpr const char* PROC_SELF_FD = "/proc/self/fd";
constexpr const char* PROC_SYS_FS_FILE_NR = "/proc/sys/fs/file-nr";
constexpr const char* PROC_SYS_FS_FILE_MAX = "/proc/sys/fs/file-max";
constexpr const char* PROC_SYS_FS_NR_OPEN = "/proc/sys/fs/nr_open";
constexpr const char* PROC_SYS_FS_INODE_MAX = "/proc/sys/fs/inode-max";

constexpr std::size_t LINK_BUF_SIZE = 256;

/* ----------------------------- FD Type Detection ----------------------------- */

/// Classify FD type based on readlink target
FdType classifyFdLink(const char* link) noexcept {
  if (link == nullptr || link[0] == '\0') {
    return FdType::UNKNOWN;
  }

  // Socket
  if (startsWith(link, "socket:")) {
    return FdType::SOCKET;
  }

  // Pipe
  if (startsWith(link, "pipe:")) {
    return FdType::PIPE;
  }

  // Anonymous inode types
  if (startsWith(link, "anon_inode:")) {
    const char* TYPE = link + 11; // Skip "anon_inode:"

    if (startsWith(TYPE, "[eventfd]") || startsWith(TYPE, "eventfd")) {
      return FdType::EVENTFD;
    }
    if (startsWith(TYPE, "[timerfd]") || startsWith(TYPE, "timerfd")) {
      return FdType::TIMERFD;
    }
    if (startsWith(TYPE, "[signalfd]") || startsWith(TYPE, "signalfd")) {
      return FdType::SIGNALFD;
    }
    if (startsWith(TYPE, "[eventpoll]") || startsWith(TYPE, "eventpoll")) {
      return FdType::EPOLL;
    }
    if (startsWith(TYPE, "[inotify]") || startsWith(TYPE, "inotify")) {
      return FdType::INOTIFY;
    }

    return FdType::ANON_INODE;
  }

  // Device files
  if (startsWith(link, "/dev/")) {
    return FdType::DEVICE;
  }

  // Proc/sys filesystem - treat as device-like
  if (startsWith(link, "/proc/") || startsWith(link, "/sys/")) {
    return FdType::DEVICE;
  }

  // Regular file path - check for directory indicator
  // Note: Can't easily distinguish file vs directory from link alone
  // Would need to stat, but we assume file for performance
  if (link[0] == '/') {
    return FdType::REGULAR;
  }

  return FdType::UNKNOWN;
}

/// Increment count for FD type in array
void incrementTypeCount(ProcessFdStatus& status, FdType type) noexcept {
  // Find existing entry
  for (std::size_t i = 0; i < status.typeCount; ++i) {
    if (status.byType[i].type == type) {
      ++status.byType[i].count;
      return;
    }
  }

  // Add new entry if room
  if (status.typeCount < MAX_FD_TYPES) {
    status.byType[status.typeCount].type = type;
    status.byType[status.typeCount].count = 1;
    ++status.typeCount;
  }
}

/// Parse /proc/sys/fs/file-nr format: "allocated  free  maximum"
void parseFileNr(SystemFdStatus& status) noexcept {
  std::array<char, 128> buf{};
  if (readFileToBuffer(PROC_SYS_FS_FILE_NR, buf.data(), buf.size()) == 0) {
    return;
  }

  // Parse three whitespace-separated values
  char* ptr = buf.data();
  char* end = nullptr;

  // First value: allocated
  status.allocated = std::strtoull(ptr, &end, 10);
  if (end == ptr) {
    return;
  }
  ptr = end;

  // Skip whitespace
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }

  // Second value: free (in kernel free list, often 0)
  status.free = std::strtoull(ptr, &end, 10);
  if (end == ptr) {
    return;
  }
  ptr = end;

  // Skip whitespace
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }

  // Third value: maximum
  status.maximum = std::strtoull(ptr, &end, 10);
}

} // namespace

/* ----------------------------- FdType ----------------------------- */

const char* toString(FdType type) noexcept {
  switch (type) {
  case FdType::UNKNOWN:
    return "unknown";
  case FdType::REGULAR:
    return "file";
  case FdType::DIRECTORY:
    return "directory";
  case FdType::PIPE:
    return "pipe";
  case FdType::SOCKET:
    return "socket";
  case FdType::DEVICE:
    return "device";
  case FdType::EVENTFD:
    return "eventfd";
  case FdType::TIMERFD:
    return "timerfd";
  case FdType::SIGNALFD:
    return "signalfd";
  case FdType::EPOLL:
    return "epoll";
  case FdType::INOTIFY:
    return "inotify";
  case FdType::ANON_INODE:
    return "anon_inode";
  }
  return "unknown";
}

/* ----------------------------- FdTypeCount Methods ----------------------------- */

std::string FdTypeCount::toString() const {
  return fmt::format("{}: {}", seeker::system::toString(type), count);
}

/* ----------------------------- ProcessFdStatus Methods ----------------------------- */

std::uint64_t ProcessFdStatus::available() const noexcept {
  if (softLimit <= openCount) {
    return 0;
  }
  return softLimit - openCount;
}

double ProcessFdStatus::utilizationPercent() const noexcept {
  if (softLimit == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(openCount) / static_cast<double>(softLimit);
}

bool ProcessFdStatus::isCritical() const noexcept { return utilizationPercent() > 90.0; }

bool ProcessFdStatus::isElevated() const noexcept { return utilizationPercent() > 75.0; }

std::uint32_t ProcessFdStatus::countByType(FdType type) const noexcept {
  for (std::size_t i = 0; i < typeCount; ++i) {
    if (byType[i].type == type) {
      return byType[i].count;
    }
  }
  return 0;
}

std::string ProcessFdStatus::toString() const {
  std::string out;
  out += fmt::format("Process FDs: {} open (limit: {}/{}, {:.1f}% used)\n", openCount, softLimit,
                     hardLimit, utilizationPercent());

  if (isCritical()) {
    out += "  WARNING: FD usage is critical (>90%)\n";
  } else if (isElevated()) {
    out += "  NOTE: FD usage is elevated (>75%)\n";
  }

  out += fmt::format("  Available: {}, Highest FD: {}\n", available(), highestFd);

  if (typeCount > 0) {
    out += "  By type: ";
    for (std::size_t i = 0; i < typeCount; ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += fmt::format("{}={}", seeker::system::toString(byType[i].type), byType[i].count);
    }
    out += "\n";
  }

  return out;
}

/* ----------------------------- SystemFdStatus Methods ----------------------------- */

std::uint64_t SystemFdStatus::available() const noexcept {
  if (maximum <= allocated) {
    return 0;
  }
  return maximum - allocated;
}

double SystemFdStatus::utilizationPercent() const noexcept {
  if (maximum == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(allocated) / static_cast<double>(maximum);
}

bool SystemFdStatus::isCritical() const noexcept { return utilizationPercent() > 90.0; }

std::string SystemFdStatus::toString() const {
  std::string out;
  out += fmt::format("System FDs: {} allocated / {} max ({:.1f}% used)\n", allocated, maximum,
                     utilizationPercent());

  if (isCritical()) {
    out += "  WARNING: System-wide FD usage is critical\n";
  }

  out += fmt::format("  Available: {}\n", available());
  out += fmt::format("  Per-process max (nr_open): {}\n", nrOpen);

  return out;
}

/* ----------------------------- FileDescriptorStatus Methods ----------------------------- */

bool FileDescriptorStatus::anyCritical() const noexcept {
  return process.isCritical() || system.isCritical();
}

std::string FileDescriptorStatus::toString() const {
  std::string out;
  out += process.toString();
  out += system.toString();
  return out;
}

/* ----------------------------- API ----------------------------- */

std::uint64_t getFdSoftLimit() noexcept {
  struct rlimit rl{};
  if (::getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(rl.rlim_cur);
}

std::uint64_t getFdHardLimit() noexcept {
  struct rlimit rl{};
  if (::getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(rl.rlim_max);
}

std::uint32_t getOpenFdCount() noexcept {
  DIR* dir = ::opendir(PROC_SELF_FD);
  if (dir == nullptr) {
    return 0;
  }

  std::uint32_t count = 0;
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(dir)) != nullptr) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }
    ++count;
  }

  ::closedir(dir);

  // Subtract 1 for the directory FD used by opendir itself
  return (count > 0) ? count - 1 : 0;
}

ProcessFdStatus getProcessFdStatus() noexcept {
  ProcessFdStatus status{};

  // Get limits
  status.softLimit = getFdSoftLimit();
  status.hardLimit = getFdHardLimit();

  // Iterate /proc/self/fd to count and classify FDs
  DIR* dir = ::opendir(PROC_SELF_FD);
  if (dir == nullptr) {
    return status;
  }

  std::array<char, FD_PATH_SIZE> pathBuf{};
  std::array<char, LINK_BUF_SIZE> linkBuf{};

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(dir)) != nullptr) {
    // Skip . and ..
    if (entry->d_name[0] == '.') {
      continue;
    }

    // Parse FD number
    char* end = nullptr;
    const unsigned long FD_NUM = std::strtoul(entry->d_name, &end, 10);
    if (end == entry->d_name) {
      continue;
    }

    ++status.openCount;

    // Track highest FD
    if (FD_NUM > status.highestFd) {
      status.highestFd = static_cast<std::uint32_t>(FD_NUM);
    }

    // Read link target to classify
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s", PROC_SELF_FD, entry->d_name);
    const ssize_t LINK_LEN = ::readlink(pathBuf.data(), linkBuf.data(), linkBuf.size() - 1);

    if (LINK_LEN > 0) {
      linkBuf[static_cast<std::size_t>(LINK_LEN)] = '\0';
      const FdType TYPE = classifyFdLink(linkBuf.data());
      incrementTypeCount(status, TYPE);
    } else {
      incrementTypeCount(status, FdType::UNKNOWN);
    }
  }

  ::closedir(dir);

  // Subtract 1 for opendir's FD
  if (status.openCount > 0) {
    --status.openCount;
  }

  return status;
}

SystemFdStatus getSystemFdStatus() noexcept {
  SystemFdStatus status{};

  // Parse /proc/sys/fs/file-nr
  parseFileNr(status);

  // Read individual values for verification/additional info
  const std::uint64_t FILE_MAX = readFileUint64(PROC_SYS_FS_FILE_MAX, 0);
  if (FILE_MAX > 0 && status.maximum == 0) {
    status.maximum = FILE_MAX;
  }

  status.nrOpen = readFileUint64(PROC_SYS_FS_NR_OPEN, 0);

  // inode-max may not exist on all systems
  status.inodeMax = readFileUint64(PROC_SYS_FS_INODE_MAX, 0);

  return status;
}

FileDescriptorStatus getFileDescriptorStatus() noexcept {
  FileDescriptorStatus status{};

  status.process = getProcessFdStatus();
  status.system = getSystemFdStatus();

  return status;
}

} // namespace system

} // namespace seeker