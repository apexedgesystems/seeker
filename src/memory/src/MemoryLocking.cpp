/**
 * @file MemoryLocking.cpp
 * @brief Memory locking status collection from /proc and capabilities.
 * @note Uses capget(2) for capability checking.
 */

#include "src/memory/inc/MemoryLocking.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Format.hpp"

#include <fcntl.h>            // open, O_RDONLY
#include <linux/capability.h> // cap_user_header_t, cap_user_data_t
#include <sys/syscall.h>      // SYS_capget
#include <unistd.h>           // read, close, getuid, syscall

#include <array>   // std::array
#include <cstdlib> // strtoull
#include <cstring> // strstr, strncmp, strlen

#include <fmt/core.h>

namespace seeker {

namespace memory {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::format::bytesBinary;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/// Skip leading whitespace.
const char* skipWhitespace(const char* p) noexcept {
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  return p;
}

/// Parse "unlimited" or numeric value from /proc/self/limits.
std::uint64_t parseLimit(const char* str) noexcept {
  str = skipWhitespace(str);

  if (std::strncmp(str, "unlimited", 9) == 0) {
    return MLOCK_UNLIMITED;
  }

  return std::strtoull(str, nullptr, 10);
}

/// Find line starting with prefix and return pointer to rest of line.
const char* findLine(const char* buf, const char* prefix) noexcept {
  const std::size_t PREFIX_LEN = std::strlen(prefix);
  const char* pos = buf;

  while ((pos = std::strstr(pos, prefix)) != nullptr) {
    // Check it's at start of line
    if (pos == buf || *(pos - 1) == '\n') {
      return pos + PREFIX_LEN;
    }
    pos += PREFIX_LEN;
  }
  return nullptr;
}

/* ----------------------------- Formatting ----------------------------- */

/// Format bytes with special handling for MLOCK_UNLIMITED.
std::string formatBytesOrUnlimited(std::uint64_t bytes) {
  if (bytes == MLOCK_UNLIMITED) {
    return "unlimited";
  }
  return bytesBinary(bytes);
}

} // namespace

/* ----------------------------- Capability Check ----------------------------- */

bool hasCapIpcLock() noexcept {
  // Use syscall directly to avoid linking libcap
  struct __user_cap_header_struct header{};
  struct __user_cap_data_struct data[2]{};

  header.version = _LINUX_CAPABILITY_VERSION_3;
  header.pid = 0; // Current process

  if (::syscall(SYS_capget, &header, data) != 0) {
    return false;
  }

  // CAP_IPC_LOCK is capability 14, in the first 32-bit word
  constexpr unsigned int CAP_IPC_LOCK_BIT = 14;
  return (data[0].effective & (1U << CAP_IPC_LOCK_BIT)) != 0;
}

/* ----------------------------- MemoryLockingStatus ----------------------------- */

bool MemoryLockingStatus::isUnlimited() const noexcept {
  return hasCapIpcLock || isRoot || softLimitBytes == MLOCK_UNLIMITED;
}

bool MemoryLockingStatus::canLock(std::uint64_t bytes) const noexcept {
  if (isUnlimited()) {
    return true;
  }

  // Check if current + requested fits within soft limit
  if (bytes > softLimitBytes) {
    return false;
  }

  const std::uint64_t NEEDED = currentLockedBytes + bytes;
  return NEEDED <= softLimitBytes;
}

std::uint64_t MemoryLockingStatus::availableBytes() const noexcept {
  if (isUnlimited()) {
    return MLOCK_UNLIMITED;
  }

  if (currentLockedBytes >= softLimitBytes) {
    return 0;
  }

  return softLimitBytes - currentLockedBytes;
}

std::string MemoryLockingStatus::toString() const {
  std::string out;
  out.reserve(256);

  out += "Memory Locking:\n";
  out += fmt::format("  Soft limit:     {}\n", formatBytesOrUnlimited(softLimitBytes));
  out += fmt::format("  Hard limit:     {}\n", formatBytesOrUnlimited(hardLimitBytes));
  out += fmt::format("  Current locked: {}\n", bytesBinary(currentLockedBytes));
  out += fmt::format("  Available:      {}\n", formatBytesOrUnlimited(availableBytes()));
  out += fmt::format("  CAP_IPC_LOCK:   {}\n", hasCapIpcLock ? "yes" : "no");
  out += fmt::format("  Running as root: {}\n", isRoot ? "yes" : "no");
  out += fmt::format("  Effectively unlimited: {}\n", isUnlimited() ? "yes" : "no");

  return out;
}

/* ----------------------------- MlockallStatus ----------------------------- */

std::string MlockallStatus::toString() const {
  std::string out;
  out.reserve(128);

  out += "mlockall() Status:\n";
  out += fmt::format("  MCL_CURRENT possible: {}\n", canLockCurrent ? "yes" : "no");
  out += fmt::format("  MCL_FUTURE possible:  {}\n", canLockFuture ? "yes" : "no");
  out += fmt::format("  Currently active:     {}\n", isCurrentlyLocked ? "yes" : "no");

  return out;
}

/* ----------------------------- API ----------------------------- */

MemoryLockingStatus getMemoryLockingStatus() noexcept {
  MemoryLockingStatus status{};

  // Check capabilities and root
  status.hasCapIpcLock = hasCapIpcLock();
  status.isRoot = (::getuid() == 0);

  // Parse /proc/self/limits for "Max locked memory"
  std::array<char, 4096> buf{};
  if (readFileToBuffer("/proc/self/limits", buf.data(), buf.size()) > 0) {
    // Format: "Max locked memory         65536                65536                bytes"
    const char* LINE = findLine(buf.data(), "Max locked memory");
    if (LINE != nullptr) {
      // Skip to first number (soft limit)
      const char* p = skipWhitespace(LINE);
      status.softLimitBytes = parseLimit(p);

      // Skip past soft limit to hard limit
      while (*p != '\0' && *p != ' ' && *p != '\t') {
        ++p;
      }
      p = skipWhitespace(p);
      status.hardLimitBytes = parseLimit(p);
    }
  }

  // Parse /proc/self/status for VmLck (currently locked memory)
  if (readFileToBuffer("/proc/self/status", buf.data(), buf.size()) > 0) {
    // Format: "VmLck:         0 kB"
    const char* LINE = findLine(buf.data(), "VmLck:");
    if (LINE != nullptr) {
      const char* p = skipWhitespace(LINE);
      const std::uint64_t KB = std::strtoull(p, nullptr, 10);
      status.currentLockedBytes = KB * 1024ULL;
    }
  }

  return status;
}

MlockallStatus getMlockallStatus() noexcept {
  MlockallStatus status{};

  const MemoryLockingStatus LOCK_STATUS = getMemoryLockingStatus();

  // If unlimited, mlockall should succeed
  if (LOCK_STATUS.isUnlimited()) {
    status.canLockCurrent = true;
    status.canLockFuture = true;
  } else {
    // Heuristic: if soft limit is reasonable (> 64KB), MCL_CURRENT might work
    // MCL_FUTURE is harder to predict as it depends on future allocations
    status.canLockCurrent = (LOCK_STATUS.softLimitBytes >= 64 * 1024);
    status.canLockFuture = LOCK_STATUS.isUnlimited();
  }

  // Check if mlockall is already active by looking at VmLck > 0
  // This is a heuristic - VmLck > 0 could also be from individual mlock calls
  status.isCurrentlyLocked = (LOCK_STATUS.currentLockedBytes > 0);

  return status;
}

} // namespace memory

} // namespace seeker