/**
 * @file KernelInfo.cpp
 * @brief Implementation of kernel info collection from /proc and /sys.
 */

#include "src/system/inc/KernelInfo.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <cstdlib> // strtol
#include <cstring> // strstr, strncpy, strlen, strcmp

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileToBuffer;

/// Check if cmdline contains parameter (word boundary aware).
bool hasCmdlineParam(const char* cmdline, const char* param) noexcept {
  const std::size_t PARAM_LEN = std::strlen(param);
  const char* pos = cmdline;

  while ((pos = std::strstr(pos, param)) != nullptr) {
    // Check word boundary: start of string or preceded by space
    const bool AT_START = (pos == cmdline) || (*(pos - 1) == ' ');

    // Check followed by '=' or space or end
    const char NEXT = pos[PARAM_LEN];
    const bool VALID_END = (NEXT == '=' || NEXT == ' ' || NEXT == '\0');

    if (AT_START && VALID_END) {
      return true;
    }
    pos += PARAM_LEN;
  }
  return false;
}

/// Detect preemption model from /proc/version string.
PreemptModel detectPreemptFromVersion(const char* version) noexcept {
  // PREEMPT_RT kernels have "PREEMPT_RT" in version string
  if (std::strstr(version, "PREEMPT_RT") != nullptr) {
    return PreemptModel::PREEMPT_RT;
  }

  // Standard preemptible kernels have "PREEMPT" without "_RT"
  if (std::strstr(version, "PREEMPT") != nullptr) {
    return PreemptModel::PREEMPT;
  }

  // Check for VOLUNTARY (older kernels might indicate this)
  if (std::strstr(version, "VOLUNTARY") != nullptr) {
    return PreemptModel::VOLUNTARY;
  }

  // Default server kernels have no preemption indicator
  return PreemptModel::NONE;
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

} // namespace

/* ----------------------------- PreemptModel toString ----------------------------- */

const char* toString(PreemptModel model) noexcept {
  switch (model) {
  case PreemptModel::NONE:
    return "none";
  case PreemptModel::VOLUNTARY:
    return "voluntary";
  case PreemptModel::PREEMPT:
    return "preempt";
  case PreemptModel::PREEMPT_RT:
    return "preempt_rt";
  case PreemptModel::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- KernelInfo Methods ----------------------------- */

bool KernelInfo::isRtKernel() const noexcept {
  return preempt == PreemptModel::PREEMPT_RT || preempt == PreemptModel::PREEMPT;
}

bool KernelInfo::isPreemptRt() const noexcept {
  return preempt == PreemptModel::PREEMPT_RT || rtPreemptPatched;
}

bool KernelInfo::hasRtCmdlineFlags() const noexcept { return nohzFull || isolCpus || rcuNocbs; }

const char* KernelInfo::preemptModelStr() const noexcept {
  if (preemptStr[0] != '\0') {
    return preemptStr.data();
  }
  return seeker::system::toString(preempt);
}

std::string KernelInfo::toString() const {
  std::string out;
  out.reserve(512);

  out += "Kernel Info:\n";
  out += fmt::format("  Release:     {}\n", release.data());
  out += fmt::format("  Preemption:  {} (RT-PREEMPT={})\n", preemptModelStr(),
                     rtPreemptPatched ? "yes" : "no");

  out += "  RT cmdline:  ";
  if (hasRtCmdlineFlags()) {
    bool first = true;
    if (nohzFull) {
      out += "nohz_full";
      first = false;
    }
    if (isolCpus) {
      if (!first) {
        out += ", ";
      }
      out += "isolcpus";
      first = false;
    }
    if (rcuNocbs) {
      if (!first) {
        out += ", ";
      }
      out += "rcu_nocbs";
      first = false;
    }
    if (skewTick) {
      if (!first) {
        out += ", ";
      }
      out += "skew_tick";
      first = false;
    }
    if (cstateLimit) {
      if (!first) {
        out += ", ";
      }
      out += "cstate_limit";
      first = false;
    }
    if (idlePoll) {
      if (!first) {
        out += ", ";
      }
      out += "idle=poll";
    }
    out += "\n";
  } else {
    out += "(none detected)\n";
  }

  out += fmt::format("  TSC reliable: {}\n", tscReliable ? "yes" : "no");
  out += fmt::format("  Tainted:     {} (mask={})\n", tainted ? "yes" : "no", taintMask);

  return out;
}

/* ----------------------------- API ----------------------------- */

KernelInfo getKernelInfo() noexcept {
  KernelInfo info{};

  // Stack buffers for file reads
  std::array<char, 256> smallBuf{};
  std::array<char, 4096> cmdlineBuf{};

  // Kernel release from /proc/sys/kernel/osrelease
  if (readFileToBuffer("/proc/sys/kernel/osrelease", smallBuf.data(), smallBuf.size()) > 0) {
    safeCopy(info.release, smallBuf.data());
  }

  // Full version from /proc/version (includes preempt model in most distros)
  if (readFileToBuffer("/proc/version", info.version.data(), info.version.size()) > 0) {
    // Detect preemption model from version string
    info.preempt = detectPreemptFromVersion(info.version.data());

    // Set human-readable preempt string
    safeCopy(info.preemptStr, toString(info.preempt));

    // Check for RT-PREEMPT specifically
    info.rtPreemptPatched = (std::strstr(info.version.data(), "PREEMPT_RT") != nullptr);
  }

  // Check /sys/kernel/realtime for explicit RT indicator (present in some RT kernels)
  if (pathExists("/sys/kernel/realtime")) {
    if (readFileToBuffer("/sys/kernel/realtime", smallBuf.data(), smallBuf.size()) > 0) {
      if (smallBuf[0] == '1') {
        info.rtPreemptPatched = true;
        info.preempt = PreemptModel::PREEMPT_RT;
        safeCopy(info.preemptStr, "preempt_rt");
      }
    }
  }

  // Parse /proc/cmdline for RT-relevant flags
  if (readFileToBuffer("/proc/cmdline", cmdlineBuf.data(), cmdlineBuf.size()) > 0) {
    const char* CMD = cmdlineBuf.data();

    info.nohzFull = hasCmdlineParam(CMD, "nohz_full");
    info.isolCpus = hasCmdlineParam(CMD, "isolcpus");
    info.rcuNocbs = hasCmdlineParam(CMD, "rcu_nocbs");
    info.skewTick = hasCmdlineParam(CMD, "skew_tick");
    info.idlePoll = hasCmdlineParam(CMD, "idle=poll");

    // TSC reliability
    info.tscReliable =
        hasCmdlineParam(CMD, "tsc=reliable") || hasCmdlineParam(CMD, "tsc=nowatchdog");

    // C-state limiting (various forms)
    info.cstateLimit = hasCmdlineParam(CMD, "intel_idle.max_cstate") ||
                       hasCmdlineParam(CMD, "processor.max_cstate") ||
                       hasCmdlineParam(CMD, "intel_pstate=disable");
  }

  // Kernel taint status
  if (readFileToBuffer("/proc/sys/kernel/tainted", smallBuf.data(), smallBuf.size()) > 0) {
    info.taintMask = static_cast<int>(std::strtol(smallBuf.data(), nullptr, 10));
    info.tainted = (info.taintMask != 0);
  }

  return info;
}

} // namespace system

} // namespace seeker