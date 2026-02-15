/**
 * @file sys-rtcheck.cpp
 * @brief RT readiness validation for system configuration.
 *
 * Checks kernel preemption, capabilities, limits, container restrictions,
 * virtualization environment, RT scheduler config, watchdog, IPC, and
 * file descriptor headroom for real-time suitability.
 * Outputs pass/warn/fail status with recommendations.
 */

#include "src/system/inc/CapabilityStatus.hpp"
#include "src/system/inc/ContainerLimits.hpp"
#include "src/system/inc/FileDescriptorStatus.hpp"
#include "src/system/inc/IpcStatus.hpp"
#include "src/system/inc/KernelInfo.hpp"
#include "src/system/inc/ProcessLimits.hpp"
#include "src/system/inc/RtSchedConfig.hpp"
#include "src/system/inc/VirtualizationInfo.hpp"
#include "src/system/inc/WatchdogStatus.hpp"
#include "src/helpers/inc/Args.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace sys = seeker::system;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_WATCHDOG = 2,
  ARG_IPC = 3,
  ARG_FD = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "RT readiness validation for system configuration.\n"
    "Checks kernel, capabilities, limits, container, virtualization,\n"
    "RT scheduler, watchdog, IPC, and file descriptor resources.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_WATCHDOG] = {"--watchdog", 0, false, "Include watchdog availability check"};
  map[ARG_IPC] = {"--ipc", 0, false, "Include IPC resource limit checks"};
  map[ARG_FD] = {"--fd", 0, false, "Include file descriptor headroom check"};
  return map;
}

/* ----------------------------- Check Result Types ----------------------------- */

enum class CheckResult : unsigned char { PASS = 0, WARN, FAIL, SKIP };

struct CheckStatus {
  std::string name;
  CheckResult result{CheckResult::SKIP};
  std::string message;
  std::string recommendation;
};

/* ----------------------------- Result Formatting ----------------------------- */

const char* resultToString(CheckResult r) noexcept {
  switch (r) {
  case CheckResult::PASS:
    return "PASS";
  case CheckResult::WARN:
    return "WARN";
  case CheckResult::FAIL:
    return "FAIL";
  case CheckResult::SKIP:
    return "SKIP";
  }
  return "UNKNOWN";
}

const char* resultToColor(CheckResult r) noexcept {
  switch (r) {
  case CheckResult::PASS:
    return "\033[32m"; // Green
  case CheckResult::WARN:
    return "\033[33m"; // Yellow
  case CheckResult::FAIL:
    return "\033[31m"; // Red
  case CheckResult::SKIP:
    return "\033[90m"; // Gray
  }
  return "\033[0m";
}

/* ----------------------------- Checks ----------------------------- */

/// Check 1: Kernel preemption model
CheckStatus checkKernelPreempt(const sys::KernelInfo& kernel) {
  CheckStatus status;
  status.name = "Kernel Preemption";

  switch (kernel.preempt) {
  case sys::PreemptModel::PREEMPT_RT:
    status.result = CheckResult::PASS;
    status.message = "PREEMPT_RT kernel (optimal for RT)";
    break;
  case sys::PreemptModel::PREEMPT:
    status.result = CheckResult::WARN;
    status.message = "PREEMPT kernel (acceptable, not optimal)";
    status.recommendation = "Consider using PREEMPT_RT kernel for lowest latency";
    break;
  case sys::PreemptModel::VOLUNTARY:
    status.result = CheckResult::FAIL;
    status.message = "VOLUNTARY preemption (not suitable for RT)";
    status.recommendation = "Use kernel with CONFIG_PREEMPT or CONFIG_PREEMPT_RT";
    break;
  case sys::PreemptModel::NONE:
    status.result = CheckResult::FAIL;
    status.message = "No preemption (server kernel, not suitable for RT)";
    status.recommendation = "Use kernel with CONFIG_PREEMPT or CONFIG_PREEMPT_RT";
    break;
  case sys::PreemptModel::UNKNOWN:
    status.result = CheckResult::WARN;
    status.message = "Could not determine preemption model";
    status.recommendation = "Check kernel config: zcat /proc/config.gz | grep PREEMPT";
    break;
  }

  return status;
}

/// Check 2: Virtualization environment
CheckStatus checkVirtualization(const sys::VirtualizationInfo& virt) {
  CheckStatus status;
  status.name = "Virtualization";

  if (virt.isBareMetal()) {
    status.result = CheckResult::PASS;
    status.message = "Bare metal (optimal for RT)";
  } else if (virt.isContainer()) {
    if (virt.rtSuitability >= 70) {
      status.result = CheckResult::PASS;
      status.message = fmt::format("Container ({}, RT score {}%)",
                                   sys::toString(virt.containerRuntime), virt.rtSuitability);
    } else {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Container ({}, RT score {}%)",
                                   sys::toString(virt.containerRuntime), virt.rtSuitability);
      status.recommendation = "Containers add scheduling overhead; consider bare metal for hard RT";
    }
  } else if (virt.isVirtualMachine()) {
    if (virt.nested) {
      status.result = CheckResult::FAIL;
      status.message = fmt::format("Nested VM detected (RT score {}%)", virt.rtSuitability);
      status.recommendation = "Nested virtualization has severe RT latency; use bare metal";
    } else if (virt.rtSuitability >= 50) {
      status.result = CheckResult::WARN;
      status.message =
          fmt::format("VM ({}, RT score {}%)", sys::toString(virt.hypervisor), virt.rtSuitability);
      status.recommendation = "VMs add latency jitter; use bare metal for hard RT requirements";
    } else {
      status.result = CheckResult::FAIL;
      status.message =
          fmt::format("VM ({}, RT score {}%)", sys::toString(virt.hypervisor), virt.rtSuitability);
      status.recommendation = "This hypervisor is not suitable for RT; use bare metal or KVM";
    }
  } else {
    status.result = CheckResult::WARN;
    status.message = "Unknown virtualization environment";
    status.recommendation = "Verify execution environment for RT suitability";
  }

  return status;
}

/// Check 3: RT scheduler bandwidth
CheckStatus checkRtBandwidth(const sys::RtSchedConfig& sched) {
  CheckStatus status;
  status.name = "RT Bandwidth";

  if (sched.bandwidth.isUnlimited()) {
    status.result = CheckResult::PASS;
    status.message = "RT bandwidth unlimited (optimal)";
  } else {
    const double BW = sched.bandwidth.bandwidthPercent();
    if (BW >= 95.0) {
      status.result = CheckResult::PASS;
      status.message = fmt::format("RT bandwidth {:.0f}% (adequate)", BW);
    } else if (BW >= 80.0) {
      status.result = CheckResult::WARN;
      status.message = fmt::format("RT bandwidth {:.0f}% (may throttle under load)", BW);
      status.recommendation = "Set sched_rt_runtime_us=-1 for unlimited RT bandwidth";
    } else {
      status.result = CheckResult::FAIL;
      status.message = fmt::format("RT bandwidth {:.0f}% (will cause throttling)", BW);
      status.recommendation =
          "echo -1 > /proc/sys/kernel/sched_rt_runtime_us for unlimited bandwidth";
    }
  }

  return status;
}

/// Check 4: RT autogroup
CheckStatus checkRtAutogroup(const sys::RtSchedConfig& sched) {
  CheckStatus status;
  status.name = "RT Autogroup";

  if (!sched.tunables.autogroupEnabled) {
    status.result = CheckResult::PASS;
    status.message = "Autogroup disabled (optimal for RT isolation)";
  } else {
    status.result = CheckResult::WARN;
    status.message = "Autogroup enabled (interferes with RT priority)";
    status.recommendation = "echo 0 > /proc/sys/kernel/sched_autogroup_enabled";
  }

  return status;
}

/// Check 5: RT scheduling capability
CheckStatus checkRtScheduling(const sys::CapabilityStatus& caps) {
  CheckStatus status;
  status.name = "RT Scheduling";

  if (caps.canUseRtScheduling()) {
    status.result = CheckResult::PASS;
    if (caps.isRoot) {
      status.message = "Running as root (full RT scheduling access)";
    } else {
      status.message = "CAP_SYS_NICE available (RT scheduling permitted)";
    }
  } else {
    status.result = CheckResult::FAIL;
    status.message = "No RT scheduling capability";
    status.recommendation = "Run as root or: setcap cap_sys_nice+ep <binary>";
  }

  return status;
}

/// Check 6: RT priority limit
CheckStatus checkRtprioLimit(const sys::ProcessLimits& limits) {
  CheckStatus status;
  status.name = "RTPRIO Limit";

  const int MAX_RTPRIO = limits.rtprioMax();

  if (MAX_RTPRIO >= 99) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("RTPRIO max = {} (full range)", MAX_RTPRIO);
  } else if (MAX_RTPRIO >= 50) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("RTPRIO max = {} (limited range)", MAX_RTPRIO);
    status.recommendation = "Add to /etc/security/limits.conf: * - rtprio 99";
  } else if (MAX_RTPRIO > 0) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("RTPRIO max = {} (severely limited)", MAX_RTPRIO);
    status.recommendation = "Add to /etc/security/limits.conf: * - rtprio 99";
  } else {
    status.result = CheckResult::FAIL;
    status.message = "RTPRIO max = 0 (no RT scheduling allowed)";
    status.recommendation = "Add to /etc/security/limits.conf: * - rtprio 99";
  }

  return status;
}

/// Check 7: Memory locking capability
CheckStatus checkMemoryLock(const sys::CapabilityStatus& caps, const sys::ProcessLimits& limits) {
  CheckStatus status;
  status.name = "Memory Lock";

  if (caps.canLockMemory()) {
    if (limits.hasUnlimitedMemlock()) {
      status.result = CheckResult::PASS;
      status.message = "Unlimited memory locking available";
    } else {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Memory lock limited to {} bytes", limits.memlock.soft);
      status.recommendation = "Add to /etc/security/limits.conf: * - memlock unlimited";
    }
  } else {
    status.result = CheckResult::FAIL;
    status.message = "No memory locking capability";
    status.recommendation = "Run as root or: setcap cap_ipc_lock+ep <binary>";
  }

  return status;
}

/// Check 8: Kernel taint status
CheckStatus checkKernelTaint(const sys::KernelInfo& kernel) {
  CheckStatus status;
  status.name = "Kernel Taint";

  if (!kernel.tainted) {
    status.result = CheckResult::PASS;
    status.message = "Kernel not tainted";
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("Kernel tainted (mask={})", kernel.taintMask);
    status.recommendation = "Tainted kernels may have unpredictable behavior; review cause";
  }

  return status;
}

/// Check 9: RT-related kernel cmdline flags
CheckStatus checkRtCmdline(const sys::KernelInfo& kernel) {
  CheckStatus status;
  status.name = "RT Cmdline";

  int rtFlags = 0;
  if (kernel.nohzFull)
    ++rtFlags;
  if (kernel.isolCpus)
    ++rtFlags;
  if (kernel.rcuNocbs)
    ++rtFlags;
  if (kernel.skewTick)
    ++rtFlags;
  if (kernel.idlePoll)
    ++rtFlags;

  if (rtFlags >= 3) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("{} RT-related cmdline flags set (well configured)", rtFlags);
  } else if (rtFlags >= 1) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("Only {} RT-related cmdline flags set", rtFlags);
    status.recommendation = "Consider: nohz_full, isolcpus, rcu_nocbs, idle=poll";
  } else {
    status.result = CheckResult::WARN;
    status.message = "No RT-related kernel cmdline flags detected";
    status.recommendation = "Consider: nohz_full, isolcpus, rcu_nocbs for CPU isolation";
  }

  return status;
}

/// Check 10: Container CPU limits
CheckStatus checkContainerCpu(const sys::ContainerLimits& container) {
  CheckStatus status;
  status.name = "Container CPU";

  if (!container.detected) {
    status.result = CheckResult::PASS;
    status.message = "Not containerized";
    return status;
  }

  if (!container.hasCpuLimit()) {
    status.result = CheckResult::PASS;
    status.message = "Container CPU unlimited";
  } else {
    const double QUOTA = container.cpuQuotaPercent();
    if (QUOTA >= 100.0) {
      status.result = CheckResult::PASS;
      status.message = fmt::format("Container CPU quota {:.0f}% (adequate)", QUOTA);
    } else if (QUOTA >= 50.0) {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Container CPU quota {:.0f}% (may throttle)", QUOTA);
      status.recommendation = "Increase CPU quota or remove limit for RT workloads";
    } else {
      status.result = CheckResult::FAIL;
      status.message = fmt::format("Container CPU quota {:.0f}% (will throttle)", QUOTA);
      status.recommendation = "Remove CPU limit: docker run --cpu-quota=0";
    }
  }

  return status;
}

/// Check 11: Container memory limits
CheckStatus checkContainerMemory(const sys::ContainerLimits& container) {
  CheckStatus status;
  status.name = "Container Memory";

  if (!container.detected) {
    status.result = CheckResult::PASS;
    status.message = "Not containerized";
    return status;
  }

  if (!container.hasMemoryLimit()) {
    status.result = CheckResult::PASS;
    status.message = "Container memory unlimited";
  } else {
    const std::int64_t LIMIT_MB = container.memMaxBytes / (1024 * 1024);
    if (LIMIT_MB >= 4096) {
      status.result = CheckResult::PASS;
      status.message = fmt::format("Container memory limit {} MB (adequate)", LIMIT_MB);
    } else if (LIMIT_MB >= 1024) {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Container memory limit {} MB (may be tight)", LIMIT_MB);
      status.recommendation = "Ensure memory limit exceeds locked memory requirements";
    } else {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Container memory limit {} MB (low)", LIMIT_MB);
      status.recommendation = "Increase memory limit for RT applications";
    }
  }

  return status;
}

/// Check 12: Watchdog availability (optional)
CheckStatus checkWatchdog(const sys::WatchdogStatus& wd) {
  CheckStatus status;
  status.name = "Watchdog";

  if (!wd.hasWatchdog()) {
    status.result = CheckResult::WARN;
    status.message = "No watchdog devices found";
    status.recommendation = "Hardware watchdog recommended for RT systems";
    return status;
  }

  const sys::WatchdogDevice* suitable = wd.findRtSuitable();
  if (suitable != nullptr) {
    status.result = CheckResult::PASS;
    status.message =
        fmt::format("RT-suitable watchdog: {} (timeout {}-{}s)", suitable->identity.data(),
                    suitable->minTimeout, suitable->maxTimeout);
  } else {
    status.result = CheckResult::WARN;
    status.message =
        fmt::format("Watchdog available but not RT-suitable ({} devices)", wd.deviceCount);
    status.recommendation = "Consider hardware watchdog with configurable timeout";
  }

  return status;
}

/// Check 13: IPC resource limits (optional)
CheckStatus checkIpcLimits(const sys::IpcStatus& ipc) {
  CheckStatus status;
  status.name = "IPC Limits";

  if (ipc.isNearAnyLimit()) {
    status.result = CheckResult::WARN;
    status.message = "Near IPC resource limit(s)";

    std::string details;
    if (ipc.shm.isNearSegmentLimit() || ipc.shm.isNearMemoryLimit()) {
      details += "shm ";
    }
    if (ipc.sem.isNearArrayLimit() || ipc.sem.isNearSemLimit()) {
      details += "sem ";
    }
    if (ipc.msg.isNearQueueLimit()) {
      details += "msg ";
    }
    status.message += " (" + details + ")";
    status.recommendation = "Increase IPC limits in /proc/sys/kernel/";
  } else {
    const int SCORE = ipc.rtScore();
    if (SCORE >= 90) {
      status.result = CheckResult::PASS;
      status.message = fmt::format("IPC resources adequate (score {})", SCORE);
    } else {
      status.result = CheckResult::PASS;
      status.message = fmt::format("IPC resources available (score {})", SCORE);
    }
  }

  return status;
}

/// Check 14: File descriptor headroom (optional)
CheckStatus checkFdHeadroom(const sys::FileDescriptorStatus& fd) {
  CheckStatus status;
  status.name = "FD Headroom";

  const double PROCESS_UTIL = fd.process.utilizationPercent();
  const double SYSTEM_UTIL = fd.system.utilizationPercent();

  // Check process FD usage first (more likely to be the bottleneck)
  if (fd.process.isCritical()) {
    status.result = CheckResult::FAIL;
    status.message = fmt::format("Process FD usage critical: {:.1f}% ({}/{} used)", PROCESS_UTIL,
                                 fd.process.openCount, fd.process.softLimit);
    status.recommendation = fmt::format(
        "Increase NOFILE limit: ulimit -n {} or increase in limits.conf", fd.process.hardLimit);
    return status;
  }

  if (fd.process.isElevated()) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("Process FD usage elevated: {:.1f}% ({}/{} used)", PROCESS_UTIL,
                                 fd.process.openCount, fd.process.softLimit);
    status.recommendation = "Monitor FD usage; consider increasing NOFILE limit";
    return status;
  }

  // Check system-wide FD usage
  if (fd.system.isCritical()) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("System FD usage high: {:.1f}% ({}/{} allocated)", SYSTEM_UTIL,
                                 fd.system.allocated, fd.system.maximum);
    status.recommendation = "Increase fs.file-max sysctl";
    return status;
  }

  // All good
  status.result = CheckResult::PASS;
  status.message = fmt::format("FD headroom adequate: process {:.1f}%, system {:.1f}%",
                               PROCESS_UTIL, SYSTEM_UTIL);

  return status;
}

/* ----------------------------- Output Functions ----------------------------- */

void printHumanOutput(const std::vector<CheckStatus>& checks) {
  fmt::print("System RT Readiness Check\n");
  fmt::print("=========================\n\n");

  // Determine column widths
  std::size_t maxNameLen = 0;
  for (const auto& CHECK : checks) {
    if (CHECK.name.size() > maxNameLen) {
      maxNameLen = CHECK.name.size();
    }
  }

  // Print each check
  int passCount = 0;
  int warnCount = 0;
  int failCount = 0;

  for (const auto& CHECK : checks) {
    const char* COLOR = resultToColor(CHECK.result);
    const char* RESET = "\033[0m";

    fmt::print("[{}{}{}] {:<{}}  {}\n", COLOR, resultToString(CHECK.result), RESET, CHECK.name,
               maxNameLen, CHECK.message);

    if (!CHECK.recommendation.empty()) {
      fmt::print("      -> {}\n", CHECK.recommendation);
    }

    switch (CHECK.result) {
    case CheckResult::PASS:
      ++passCount;
      break;
    case CheckResult::WARN:
      ++warnCount;
      break;
    case CheckResult::FAIL:
      ++failCount;
      break;
    case CheckResult::SKIP:
      break;
    }
  }

  // Summary
  fmt::print("\n");
  fmt::print("Summary: {} passed, {} warnings, {} failed\n", passCount, warnCount, failCount);

  if (failCount > 0) {
    fmt::print("\n\033[31mVerdict: NOT RT-READY\033[0m\n");
  } else if (warnCount > 0) {
    fmt::print("\n\033[33mVerdict: PARTIALLY RT-READY (review warnings)\033[0m\n");
  } else {
    fmt::print("\n\033[32mVerdict: RT-READY\033[0m\n");
  }
}

void printJsonOutput(const std::vector<CheckStatus>& checks) {
  fmt::print("{{\n");

  // Checks array
  fmt::print("  \"checks\": [\n");
  for (std::size_t i = 0; i < checks.size(); ++i) {
    const auto& CHECK = checks[i];
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", CHECK.name);
    fmt::print("      \"result\": \"{}\",\n", resultToString(CHECK.result));
    fmt::print("      \"message\": \"{}\",\n", CHECK.message);
    fmt::print("      \"recommendation\": \"{}\"\n", CHECK.recommendation);
    fmt::print("    }}{}\n", (i + 1 < checks.size()) ? "," : "");
  }
  fmt::print("  ],\n");

  // Summary
  int passCount = 0;
  int warnCount = 0;
  int failCount = 0;
  for (const auto& CHECK : checks) {
    switch (CHECK.result) {
    case CheckResult::PASS:
      ++passCount;
      break;
    case CheckResult::WARN:
      ++warnCount;
      break;
    case CheckResult::FAIL:
      ++failCount;
      break;
    case CheckResult::SKIP:
      break;
    }
  }

  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"pass\": {},\n", passCount);
  fmt::print("    \"warn\": {},\n", warnCount);
  fmt::print("    \"fail\": {}\n", failCount);
  fmt::print("  }},\n");

  // Verdict
  const char* verdict = "RT_READY";
  if (failCount > 0) {
    verdict = "NOT_RT_READY";
  } else if (warnCount > 0) {
    verdict = "PARTIAL";
  }
  fmt::print("  \"verdict\": \"{}\"\n", verdict);

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool checkWatchdogFlag = false;
  bool checkIpcFlag = false;
  bool checkFdFlag = false;

  if (argc > 1) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    std::string error;
    if (!seeker::helpers::args::parseArgs(args, ARG_MAP, pargs, error)) {
      fmt::print(stderr, "Error: {}\n\n", error);
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 1;
    }

    if (pargs.count(ARG_HELP) != 0) {
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 0;
    }

    jsonOutput = (pargs.count(ARG_JSON) != 0);
    checkWatchdogFlag = (pargs.count(ARG_WATCHDOG) != 0);
    checkIpcFlag = (pargs.count(ARG_IPC) != 0);
    checkFdFlag = (pargs.count(ARG_FD) != 0);
  }

  // Gather system state
  const sys::KernelInfo KERNEL = sys::getKernelInfo();
  const sys::VirtualizationInfo VIRT = sys::getVirtualizationInfo();
  const sys::RtSchedConfig SCHED = sys::getRtSchedConfig();
  const sys::CapabilityStatus CAPS = sys::getCapabilityStatus();
  const sys::ProcessLimits LIMITS = sys::getProcessLimits();
  const sys::ContainerLimits CONTAINER = sys::getContainerLimits();

  // Run checks
  std::vector<CheckStatus> checks;
  checks.push_back(checkKernelPreempt(KERNEL));
  checks.push_back(checkVirtualization(VIRT));
  checks.push_back(checkRtBandwidth(SCHED));
  checks.push_back(checkRtAutogroup(SCHED));
  checks.push_back(checkRtScheduling(CAPS));
  checks.push_back(checkRtprioLimit(LIMITS));
  checks.push_back(checkMemoryLock(CAPS, LIMITS));
  checks.push_back(checkKernelTaint(KERNEL));
  checks.push_back(checkRtCmdline(KERNEL));
  checks.push_back(checkContainerCpu(CONTAINER));
  checks.push_back(checkContainerMemory(CONTAINER));

  // Optional checks
  if (checkWatchdogFlag) {
    const sys::WatchdogStatus WD = sys::getWatchdogStatus();
    checks.push_back(checkWatchdog(WD));
  }

  if (checkIpcFlag) {
    const sys::IpcStatus IPC = sys::getIpcStatus();
    checks.push_back(checkIpcLimits(IPC));
  }

  if (checkFdFlag) {
    const sys::FileDescriptorStatus FD = sys::getFileDescriptorStatus();
    checks.push_back(checkFdHeadroom(FD));
  }

  // Output results
  if (jsonOutput) {
    printJsonOutput(checks);
  } else {
    printHumanOutput(checks);
  }

  // Exit code: 0=pass, 1=warn, 2=fail
  for (const auto& CHECK : checks) {
    if (CHECK.result == CheckResult::FAIL) {
      return 2;
    }
  }
  for (const auto& CHECK : checks) {
    if (CHECK.result == CheckResult::WARN) {
      return 1;
    }
  }

  return 0;
}