/**
 * @file ProcessLimits.cpp
 * @brief Implementation of process resource limit queries.
 */

#include "src/system/inc/ProcessLimits.hpp"
#include "src/helpers/inc/Format.hpp"

#include <sys/resource.h> // getrlimit, RLIMIT_*

#include <array>

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

/* ----------------------------- Limit Helpers ----------------------------- */

/// Convert rlim_t to uint64_t, mapping RLIM_INFINITY to our sentinel.
inline std::uint64_t convertLimit(rlim_t value) noexcept {
  if (value == RLIM_INFINITY) {
    return RLIMIT_UNLIMITED_VALUE;
  }
  return static_cast<std::uint64_t>(value);
}

/// Check if limit value represents unlimited.
inline bool isUnlimited(std::uint64_t value) noexcept {
  // RLIM_INFINITY is typically ~0ULL, but we use our sentinel
  return value == RLIMIT_UNLIMITED_VALUE;
}

/* ----------------------------- Formatting ----------------------------- */

/// Format bytes as human-readable string.
std::string formatBytesInternal(std::uint64_t bytes) {
  if (isUnlimited(bytes)) {
    return "unlimited";
  }

  return seeker::helpers::format::bytesBinary(bytes);
}

/// Format count value.
std::string formatCountInternal(std::uint64_t value) {
  if (isUnlimited(value)) {
    return "unlimited";
  }
  return fmt::format("{}", value);
}

} // namespace

/* ----------------------------- RlimitValue Methods ----------------------------- */

bool RlimitValue::canIncreaseTo(std::uint64_t value) const noexcept {
  if (unlimited || hard == RLIMIT_UNLIMITED_VALUE) {
    return true;
  }
  return value <= hard;
}

bool RlimitValue::hasAtLeast(std::uint64_t value) const noexcept {
  if (unlimited || soft == RLIMIT_UNLIMITED_VALUE) {
    return true;
  }
  return soft >= value;
}

/* ----------------------------- ProcessLimits Methods ----------------------------- */

int ProcessLimits::rtprioMax() const noexcept {
  if (rtprio.unlimited || rtprio.soft == RLIMIT_UNLIMITED_VALUE) {
    return 99; // Max RT priority
  }
  // RLIMIT_RTPRIO value is the max priority directly
  return static_cast<int>(rtprio.soft > 99 ? 99 : rtprio.soft);
}

bool ProcessLimits::hasUnlimitedMemlock() const noexcept {
  return memlock.unlimited || memlock.soft == RLIMIT_UNLIMITED_VALUE;
}

bool ProcessLimits::canUseRtPriority(int priority) const noexcept {
  if (priority <= 0 || priority > 99) {
    return false;
  }
  return static_cast<int>(rtprioMax()) >= priority;
}

bool ProcessLimits::canUseRtScheduling() const noexcept { return rtprioMax() > 0; }

bool ProcessLimits::canLockMemory(std::uint64_t bytes) const noexcept {
  if (hasUnlimitedMemlock()) {
    return true;
  }
  return memlock.soft >= bytes;
}

std::string ProcessLimits::toString() const {
  std::string out;
  out.reserve(1024);

  out += "Process Limits:\n";
  out += "  RT Scheduling:\n";
  out += fmt::format("    RTPRIO:  soft={} hard={}\n", formatCountInternal(rtprio.soft),
                     formatCountInternal(rtprio.hard));
  out += fmt::format("    RTTIME:  soft={} hard={} (microseconds)\n",
                     formatCountInternal(rttime.soft), formatCountInternal(rttime.hard));
  out += fmt::format("    NICE:    soft={} hard={}\n", formatCountInternal(nice.soft),
                     formatCountInternal(nice.hard));

  out += "  Memory:\n";
  out += fmt::format("    MEMLOCK: soft={} hard={}\n", formatBytesInternal(memlock.soft),
                     formatBytesInternal(memlock.hard));
  out += fmt::format("    AS:      soft={} hard={}\n", formatBytesInternal(addressSpace.soft),
                     formatBytesInternal(addressSpace.hard));
  out += fmt::format("    DATA:    soft={} hard={}\n", formatBytesInternal(dataSegment.soft),
                     formatBytesInternal(dataSegment.hard));
  out += fmt::format("    STACK:   soft={} hard={}\n", formatBytesInternal(stack.soft),
                     formatBytesInternal(stack.hard));

  out += "  File/Process:\n";
  out += fmt::format("    NOFILE:  soft={} hard={}\n", formatCountInternal(nofile.soft),
                     formatCountInternal(nofile.hard));
  out += fmt::format("    NPROC:   soft={} hard={}\n", formatCountInternal(nproc.soft),
                     formatCountInternal(nproc.hard));
  out += fmt::format("    CORE:    soft={} hard={}\n", formatBytesInternal(core.soft),
                     formatBytesInternal(core.hard));
  out += fmt::format("    MSGQUEUE: soft={} hard={}\n", formatBytesInternal(msgqueue.soft),
                     formatBytesInternal(msgqueue.hard));

  return out;
}

std::string ProcessLimits::toRtSummary() const {
  std::string out;
  out.reserve(256);

  out += "RT-Relevant Limits:\n";
  out += fmt::format("  Max RT priority:  {}\n", rtprioMax());
  out += fmt::format("  RT scheduling:    {}\n", canUseRtScheduling() ? "allowed" : "NOT allowed");
  out += fmt::format("  Memory locking:   {}\n",
                     hasUnlimitedMemlock() ? "unlimited" : formatBytesInternal(memlock.soft));
  out += fmt::format("  Max open files:   {}\n", formatCountInternal(nofile.soft));

  return out;
}

/* ----------------------------- API ----------------------------- */

RlimitValue getRlimit(int resource) noexcept {
  RlimitValue result{};
  struct rlimit lim{};

  if (::getrlimit(resource, &lim) == 0) {
    result.soft = convertLimit(lim.rlim_cur);
    result.hard = convertLimit(lim.rlim_max);
    result.unlimited = (lim.rlim_cur == RLIM_INFINITY);
  }

  return result;
}

ProcessLimits getProcessLimits() noexcept {
  ProcessLimits limits{};

  // RT scheduling limits
  limits.rtprio = getRlimit(RLIMIT_RTPRIO);
  limits.rttime = getRlimit(RLIMIT_RTTIME);
  limits.nice = getRlimit(RLIMIT_NICE);

  // Memory limits
  limits.memlock = getRlimit(RLIMIT_MEMLOCK);
  limits.addressSpace = getRlimit(RLIMIT_AS);
  limits.dataSegment = getRlimit(RLIMIT_DATA);
  limits.stack = getRlimit(RLIMIT_STACK);

  // File/process limits
  limits.nofile = getRlimit(RLIMIT_NOFILE);
  limits.nproc = getRlimit(RLIMIT_NPROC);
  limits.core = getRlimit(RLIMIT_CORE);
  limits.msgqueue = getRlimit(RLIMIT_MSGQUEUE);

  return limits;
}

std::string formatLimit(std::uint64_t value, bool isBytes) {
  if (isBytes) {
    return formatBytesInternal(value);
  }
  return formatCountInternal(value);
}

} // namespace system

} // namespace seeker