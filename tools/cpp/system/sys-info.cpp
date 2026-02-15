/**
 * @file sys-info.cpp
 * @brief System identification and configuration display.
 *
 * Displays kernel info, capabilities, process limits, container status,
 * virtualization environment, RT scheduler config, watchdog status, IPC,
 * security (LSM) status, and file descriptor usage.
 * Designed for quick system assessment.
 */

#include "src/system/inc/CapabilityStatus.hpp"
#include "src/system/inc/ContainerLimits.hpp"
#include "src/system/inc/FileDescriptorStatus.hpp"
#include "src/system/inc/IpcStatus.hpp"
#include "src/system/inc/KernelInfo.hpp"
#include "src/system/inc/ProcessLimits.hpp"
#include "src/system/inc/RtSchedConfig.hpp"
#include "src/system/inc/SecurityStatus.hpp"
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
  ARG_SECURITY = 4,
  ARG_FD = 5,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display system identification: kernel, capabilities, limits, container,\n"
    "virtualization, RT scheduler, watchdog, IPC, security, and FD status.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_WATCHDOG] = {"--watchdog", 0, false, "Include watchdog details"};
  map[ARG_IPC] = {"--ipc", 0, false, "Include IPC resource details"};
  map[ARG_SECURITY] = {"--security", 0, false, "Include security (LSM) details"};
  map[ARG_FD] = {"--fd", 0, false, "Include file descriptor details"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printKernel(const sys::KernelInfo& kernel) {
  fmt::print("=== Kernel ===\n");
  fmt::print("  Release:      {}\n", kernel.release.data());
  fmt::print("  Preemption:   {} (RT={})\n", kernel.preemptModelStr(),
             kernel.isPreemptRt() ? "yes" : "no");

  // RT cmdline flags
  std::string flags;
  if (kernel.nohzFull)
    flags += "nohz_full ";
  if (kernel.isolCpus)
    flags += "isolcpus ";
  if (kernel.rcuNocbs)
    flags += "rcu_nocbs ";
  if (kernel.skewTick)
    flags += "skew_tick ";
  if (kernel.tscReliable)
    flags += "tsc=reliable ";
  if (kernel.cstateLimit)
    flags += "cstate_limit ";
  if (kernel.idlePoll)
    flags += "idle=poll ";

  fmt::print("  RT cmdline:   {}\n", flags.empty() ? "(none)" : flags);
  fmt::print("  Tainted:      {} (mask={})\n", kernel.tainted ? "yes" : "no", kernel.taintMask);
}

void printVirtualization(const sys::VirtualizationInfo& virt) {
  fmt::print("\n=== Virtualization ===\n");
  fmt::print("  Type:         {}\n", sys::toString(virt.type));

  if (virt.isBareMetal()) {
    fmt::print("  Environment:  Bare metal (optimal for RT)\n");
  } else if (virt.isVirtualMachine()) {
    fmt::print("  Hypervisor:   {}\n", sys::toString(virt.hypervisor));
    if (virt.productName[0] != '\0') {
      fmt::print("  Product:      {}\n", virt.productName.data());
    }
    fmt::print("  Nested:       {}\n", virt.nested ? "yes" : "no");
  } else if (virt.isContainer()) {
    fmt::print("  Runtime:      {}\n", sys::toString(virt.containerRuntime));
    if (virt.containerName[0] != '\0') {
      fmt::print("  Container:    {}\n", virt.containerName.data());
    }
  }

  fmt::print("  RT Score:     {}%\n", virt.rtSuitability);
}

void printRtSched(const sys::RtSchedConfig& sched) {
  fmt::print("\n=== RT Scheduler ===\n");

  // Bandwidth
  if (sched.bandwidth.isUnlimited()) {
    fmt::print("  RT Bandwidth: Unlimited (optimal)\n");
  } else {
    fmt::print("  RT Bandwidth: {:.1f}% ({} us / {} us)\n", sched.bandwidth.bandwidthPercent(),
               sched.bandwidth.runtimeUs, sched.bandwidth.periodUs);
  }

  // Key settings
  fmt::print("  Autogroup:    {}\n",
             sched.tunables.autogroupEnabled ? "enabled (bad for RT)" : "disabled");
  fmt::print("  DEADLINE:     {}\n", sched.hasSchedDeadline ? "supported" : "not supported");
  fmt::print("  RT Score:     {}/100\n", sched.rtScore());
}

void printCapabilities(const sys::CapabilityStatus& caps) {
  fmt::print("\n=== Capabilities ===\n");
  fmt::print("  Running as root: {}\n", caps.isRoot ? "yes" : "no");
  fmt::print("  CAP_SYS_NICE:    {} (RT scheduling)\n", caps.sysNice ? "yes" : "no");
  fmt::print("  CAP_IPC_LOCK:    {} (memory locking)\n", caps.ipcLock ? "yes" : "no");
  fmt::print("  CAP_SYS_RAWIO:   {} (raw I/O)\n", caps.sysRawio ? "yes" : "no");
  fmt::print("  CAP_SYS_ADMIN:   {} (admin)\n", caps.sysAdmin ? "yes" : "no");
  fmt::print("  CAP_NET_ADMIN:   {} (network admin)\n", caps.netAdmin ? "yes" : "no");
}

void printLimits(const sys::ProcessLimits& limits) {
  fmt::print("\n=== Process Limits ===\n");
  fmt::print("  RTPRIO max:   {}\n", limits.rtprioMax());
  fmt::print("  MEMLOCK:      {}\n", limits.hasUnlimitedMemlock()
                                         ? "unlimited"
                                         : sys::formatLimit(limits.memlock.soft, true));
  fmt::print("  NOFILE:       {}\n", sys::formatLimit(limits.nofile.soft, false));
  fmt::print("  NPROC:        {}\n", sys::formatLimit(limits.nproc.soft, false));
  fmt::print("  STACK:        {}\n", sys::formatLimit(limits.stack.soft, true));
}

void printContainer(const sys::ContainerLimits& container) {
  fmt::print("\n=== Container ===\n");

  if (!container.detected) {
    fmt::print("  Status: Not containerized\n");
    return;
  }

  fmt::print("  Status:   Containerized\n");
  fmt::print("  Runtime:  {}\n",
             container.runtime[0] != '\0' ? container.runtime.data() : "unknown");
  fmt::print("  cgroup:   {}\n", sys::toString(container.cgroupVersion));

  if (container.hasCpuLimit()) {
    fmt::print("  CPU:      {:.1f}%\n", container.cpuQuotaPercent());
  } else {
    fmt::print("  CPU:      unlimited\n");
  }

  if (container.hasMemoryLimit()) {
    fmt::print("  Memory:   {} max\n",
               sys::formatLimit(static_cast<std::uint64_t>(container.memMaxBytes), true));
  } else {
    fmt::print("  Memory:   unlimited\n");
  }

  if (container.hasCpusetLimit()) {
    fmt::print("  CPUset:   {}\n", container.cpusetCpus.data());
  }

  if (container.hasPidLimit()) {
    fmt::print("  PIDs:     {} max\n", container.pidsMax);
  }
}

void printWatchdog(const sys::WatchdogStatus& wd) {
  fmt::print("\n=== Watchdog ===\n");

  if (!wd.hasWatchdog()) {
    fmt::print("  Status:   No watchdog devices found\n");
    return;
  }

  fmt::print("  Devices:  {}\n", wd.deviceCount);

  for (std::size_t i = 0; i < wd.deviceCount && i < sys::MAX_WATCHDOG_DEVICES; ++i) {
    const auto& DEV = wd.devices[i];
    fmt::print("\n  [watchdog{}] {}\n", DEV.index, DEV.identity.data());
    fmt::print("    Timeout:    {} sec (range {}-{})\n", DEV.timeout, DEV.minTimeout,
               DEV.maxTimeout);
    if (DEV.pretimeout > 0) {
      fmt::print("    Pretimeout: {} sec\n", DEV.pretimeout);
    }
    fmt::print("    State:      {}{}{}\n", DEV.active ? "active " : "",
               DEV.nowayout ? "nowayout " : "", DEV.isRtSuitable() ? "(RT-suitable)" : "");
  }
}

void printIpc(const sys::IpcStatus& ipc) {
  fmt::print("\n=== IPC Resources ===\n");

  // Shared memory
  fmt::print("  SHM segments: {}\n", ipc.shm.segmentCount);
  fmt::print("  SHM total:    {} bytes\n", ipc.shm.totalBytes);
  fmt::print("  SHM limit:    {} segments, {} max per segment\n", ipc.shm.limits.shmmni,
             ipc.shm.limits.shmmax);

  // Semaphores
  fmt::print("  SEM arrays:   {}\n", ipc.sem.arraysInUse);
  fmt::print("  SEM total:    {}\n", ipc.sem.semsInUse);
  fmt::print("  SEM limits:   {} arrays, {} sems total\n", ipc.sem.limits.semmni,
             ipc.sem.limits.semmns);

  // Message queues
  fmt::print("  MSG queues:   {}\n", ipc.msg.queuesInUse);
  fmt::print("  MSG limit:    {} queues\n", ipc.msg.limits.msgmni);

  // POSIX MQ
  fmt::print("  POSIX MQ:     {} queues\n", ipc.posixMq.queuesInUse);

  // Status
  if (ipc.isNearAnyLimit()) {
    fmt::print("  Status:       NEAR LIMIT (review usage)\n");
  } else {
    fmt::print("  Status:       OK (RT score {})\n", ipc.rtScore());
  }
}

void printSecurity(const sys::SecurityStatus& sec) {
  fmt::print("\n=== Security (LSM) ===\n");

  // SELinux
  fmt::print("  SELinux:      {}\n", sys::toString(sec.selinux.mode));
  if (sec.selinux.isActive()) {
    fmt::print("    Policy:     {}\n", sec.selinux.policyType.data());
    fmt::print("    Version:    {}\n", sec.selinux.policyVersion);
    if (sec.selinux.mcsEnabled) {
      fmt::print("    MCS:        enabled\n");
    }
    if (sec.selinux.mlsEnabled) {
      fmt::print("    MLS:        enabled\n");
    }
  }

  // AppArmor
  fmt::print("  AppArmor:     {}\n", sys::toString(sec.apparmor.mode));
  if (sec.apparmor.isActive()) {
    fmt::print("    Profiles:   {} loaded ({} enforce, {} complain)\n", sec.apparmor.profilesLoaded,
               sec.apparmor.profilesEnforce, sec.apparmor.profilesComplain);
  }

  // Other LSMs
  fmt::print("  Seccomp:      {}\n", sec.seccompAvailable ? "available" : "not available");
  fmt::print("  Landlock:     {}\n", sec.landLockAvailable ? "available" : "not available");
  fmt::print("  Yama ptrace:  {}\n", sec.yamaPtrace ? "restricted" : "not restricted");

  // Active LSM list
  fmt::print("  Active LSMs:  {}\n", sec.activeLsmList());
}

void printFd(const sys::FileDescriptorStatus& fd) {
  fmt::print("\n=== File Descriptors ===\n");

  // Process FDs
  fmt::print("  Process FDs:  {} open\n", fd.process.openCount);
  fmt::print("    Soft limit: {}\n", fd.process.softLimit);
  fmt::print("    Hard limit: {}\n", fd.process.hardLimit);
  fmt::print("    Available:  {}\n", fd.process.available());
  fmt::print("    Usage:      {:.1f}%{}\n", fd.process.utilizationPercent(),
             fd.process.isCritical()   ? " (CRITICAL)"
             : fd.process.isElevated() ? " (elevated)"
                                       : "");
  fmt::print("    Highest FD: {}\n", fd.process.highestFd);

  // FD types breakdown
  if (fd.process.typeCount > 0) {
    fmt::print("    By type:\n");
    for (std::size_t i = 0; i < fd.process.typeCount && i < sys::MAX_FD_TYPES; ++i) {
      const auto& TYPE = fd.process.byType[i];
      if (TYPE.count > 0) {
        fmt::print("      {}: {}\n", sys::toString(TYPE.type), TYPE.count);
      }
    }
  }

  // System FDs
  fmt::print("\n  System FDs:   {} allocated\n", fd.system.allocated);
  fmt::print("    Maximum:    {}\n", fd.system.maximum);
  fmt::print("    Available:  {}\n", fd.system.available());
  fmt::print("    Usage:      {:.1f}%{}\n", fd.system.utilizationPercent(),
             fd.system.isCritical() ? " (CRITICAL)" : "");
  fmt::print("    nr_open:    {}\n", fd.system.nrOpen);
}

void printHuman(const sys::KernelInfo& kernel, const sys::VirtualizationInfo& virt,
                const sys::RtSchedConfig& sched, const sys::CapabilityStatus& caps,
                const sys::ProcessLimits& limits, const sys::ContainerLimits& container,
                const sys::WatchdogStatus* wd, const sys::IpcStatus* ipc,
                const sys::SecurityStatus* sec, const sys::FileDescriptorStatus* fd) {
  printKernel(kernel);
  printVirtualization(virt);
  printRtSched(sched);
  printCapabilities(caps);
  printLimits(limits);
  printContainer(container);

  if (wd != nullptr) {
    printWatchdog(*wd);
  }
  if (ipc != nullptr) {
    printIpc(*ipc);
  }
  if (sec != nullptr) {
    printSecurity(*sec);
  }
  if (fd != nullptr) {
    printFd(*fd);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const sys::KernelInfo& kernel, const sys::VirtualizationInfo& virt,
               const sys::RtSchedConfig& sched, const sys::CapabilityStatus& caps,
               const sys::ProcessLimits& limits, const sys::ContainerLimits& container,
               const sys::WatchdogStatus* wd, const sys::IpcStatus* ipc,
               const sys::SecurityStatus* sec, const sys::FileDescriptorStatus* fd) {
  fmt::print("{{\n");

  // Kernel
  fmt::print("  \"kernel\": {{\n");
  fmt::print("    \"release\": \"{}\",\n", kernel.release.data());
  fmt::print("    \"version\": \"{}\",\n", kernel.version.data());
  fmt::print("    \"preemptModel\": \"{}\",\n", kernel.preemptModelStr());
  fmt::print("    \"isPreemptRt\": {},\n", kernel.isPreemptRt());
  fmt::print("    \"nohzFull\": {},\n", kernel.nohzFull);
  fmt::print("    \"isolCpus\": {},\n", kernel.isolCpus);
  fmt::print("    \"rcuNocbs\": {},\n", kernel.rcuNocbs);
  fmt::print("    \"skewTick\": {},\n", kernel.skewTick);
  fmt::print("    \"tscReliable\": {},\n", kernel.tscReliable);
  fmt::print("    \"idlePoll\": {},\n", kernel.idlePoll);
  fmt::print("    \"tainted\": {},\n", kernel.tainted);
  fmt::print("    \"taintMask\": {}\n", kernel.taintMask);
  fmt::print("  }},\n");

  // Virtualization
  fmt::print("  \"virtualization\": {{\n");
  fmt::print("    \"type\": \"{}\",\n", sys::toString(virt.type));
  fmt::print("    \"hypervisor\": \"{}\",\n", sys::toString(virt.hypervisor));
  fmt::print("    \"containerRuntime\": \"{}\",\n", sys::toString(virt.containerRuntime));
  fmt::print("    \"productName\": \"{}\",\n", virt.productName.data());
  fmt::print("    \"containerName\": \"{}\",\n", virt.containerName.data());
  fmt::print("    \"nested\": {},\n", virt.nested);
  fmt::print("    \"rtSuitability\": {},\n", virt.rtSuitability);
  fmt::print("    \"isBareMetal\": {},\n", virt.isBareMetal());
  fmt::print("    \"isVirtualMachine\": {},\n", virt.isVirtualMachine());
  fmt::print("    \"isContainer\": {}\n", virt.isContainer());
  fmt::print("  }},\n");

  // RT Scheduler
  fmt::print("  \"rtScheduler\": {{\n");
  fmt::print("    \"rtPeriodUs\": {},\n", sched.bandwidth.periodUs);
  fmt::print("    \"rtRuntimeUs\": {},\n", sched.bandwidth.runtimeUs);
  fmt::print("    \"rtBandwidthPercent\": {:.2f},\n", sched.bandwidth.bandwidthPercent());
  fmt::print("    \"rtBandwidthUnlimited\": {},\n", sched.bandwidth.isUnlimited());
  fmt::print("    \"autogroup\": {},\n", sched.tunables.autogroupEnabled);
  fmt::print("    \"hasSchedDeadline\": {},\n", sched.hasSchedDeadline);
  fmt::print("    \"timerMigration\": {},\n", sched.timerMigration);
  fmt::print("    \"rtScore\": {}\n", sched.rtScore());
  fmt::print("  }},\n");

  // Capabilities
  fmt::print("  \"capabilities\": {{\n");
  fmt::print("    \"isRoot\": {},\n", caps.isRoot);
  fmt::print("    \"sysNice\": {},\n", caps.sysNice);
  fmt::print("    \"ipcLock\": {},\n", caps.ipcLock);
  fmt::print("    \"sysRawio\": {},\n", caps.sysRawio);
  fmt::print("    \"sysResource\": {},\n", caps.sysResource);
  fmt::print("    \"sysAdmin\": {},\n", caps.sysAdmin);
  fmt::print("    \"netAdmin\": {},\n", caps.netAdmin);
  fmt::print("    \"netRaw\": {},\n", caps.netRaw);
  fmt::print("    \"sysPtrace\": {},\n", caps.sysPtrace);
  fmt::print("    \"canUseRtScheduling\": {},\n", caps.canUseRtScheduling());
  fmt::print("    \"canLockMemory\": {}\n", caps.canLockMemory());
  fmt::print("  }},\n");

  // Limits
  fmt::print("  \"limits\": {{\n");
  fmt::print("    \"rtprioSoft\": {},\n", limits.rtprio.soft);
  fmt::print("    \"rtprioHard\": {},\n", limits.rtprio.hard);
  fmt::print("    \"rtprioMax\": {},\n", limits.rtprioMax());
  fmt::print("    \"memlockSoft\": {},\n", limits.memlock.soft);
  fmt::print("    \"memlockHard\": {},\n", limits.memlock.hard);
  fmt::print("    \"memlockUnlimited\": {},\n", limits.hasUnlimitedMemlock());
  fmt::print("    \"nofileSoft\": {},\n", limits.nofile.soft);
  fmt::print("    \"nofileHard\": {},\n", limits.nofile.hard);
  fmt::print("    \"nprocSoft\": {},\n", limits.nproc.soft);
  fmt::print("    \"nprocHard\": {}\n", limits.nproc.hard);
  fmt::print("  }},\n");

  // Container
  fmt::print("  \"container\": {{\n");
  fmt::print("    \"detected\": {},\n", container.detected);
  fmt::print("    \"runtime\": \"{}\",\n", container.runtime.data());
  fmt::print("    \"containerId\": \"{}\",\n", container.containerId.data());
  fmt::print("    \"cgroupVersion\": \"{}\",\n", sys::toString(container.cgroupVersion));
  fmt::print("    \"cpuQuotaUs\": {},\n", container.cpuQuotaUs);
  fmt::print("    \"cpuPeriodUs\": {},\n", container.cpuPeriodUs);
  fmt::print("    \"cpuQuotaPercent\": {:.2f},\n", container.cpuQuotaPercent());
  fmt::print("    \"cpusetCpus\": \"{}\",\n", container.cpusetCpus.data());
  fmt::print("    \"memMaxBytes\": {},\n", container.memMaxBytes);
  fmt::print("    \"memCurrentBytes\": {},\n", container.memCurrentBytes);
  fmt::print("    \"pidsMax\": {},\n", container.pidsMax);
  fmt::print("    \"pidsCurrent\": {}\n", container.pidsCurrent);
  fmt::print("  }}");

  // Watchdog (optional)
  if (wd != nullptr) {
    fmt::print(",\n  \"watchdog\": {{\n");
    fmt::print("    \"deviceCount\": {},\n", wd->deviceCount);
    fmt::print("    \"hasWatchdog\": {},\n", wd->hasWatchdog());
    fmt::print("    \"devices\": [\n");
    for (std::size_t i = 0; i < wd->deviceCount && i < sys::MAX_WATCHDOG_DEVICES; ++i) {
      const auto& DEV = wd->devices[i];
      fmt::print("      {{\n");
      fmt::print("        \"index\": {},\n", DEV.index);
      fmt::print("        \"identity\": \"{}\",\n", DEV.identity.data());
      fmt::print("        \"timeout\": {},\n", DEV.timeout);
      fmt::print("        \"minTimeout\": {},\n", DEV.minTimeout);
      fmt::print("        \"maxTimeout\": {},\n", DEV.maxTimeout);
      fmt::print("        \"pretimeout\": {},\n", DEV.pretimeout);
      fmt::print("        \"active\": {},\n", DEV.active);
      fmt::print("        \"nowayout\": {},\n", DEV.nowayout);
      fmt::print("        \"isRtSuitable\": {}\n", DEV.isRtSuitable());
      fmt::print("      }}{}\n", (i + 1 < wd->deviceCount) ? "," : "");
    }
    fmt::print("    ]\n");
    fmt::print("  }}");
  }

  // IPC (optional)
  if (ipc != nullptr) {
    fmt::print(",\n  \"ipc\": {{\n");
    fmt::print("    \"shm\": {{\n");
    fmt::print("      \"segmentCount\": {},\n", ipc->shm.segmentCount);
    fmt::print("      \"totalBytes\": {},\n", ipc->shm.totalBytes);
    fmt::print("      \"limitShmmni\": {},\n", ipc->shm.limits.shmmni);
    fmt::print("      \"limitShmmax\": {},\n", ipc->shm.limits.shmmax);
    fmt::print("      \"limitShmall\": {},\n", ipc->shm.limits.shmall);
    fmt::print("      \"nearLimit\": {}\n",
               ipc->shm.isNearSegmentLimit() || ipc->shm.isNearMemoryLimit());
    fmt::print("    }},\n");
    fmt::print("    \"sem\": {{\n");
    fmt::print("      \"arraysInUse\": {},\n", ipc->sem.arraysInUse);
    fmt::print("      \"semsInUse\": {},\n", ipc->sem.semsInUse);
    fmt::print("      \"limitSemmni\": {},\n", ipc->sem.limits.semmni);
    fmt::print("      \"limitSemmns\": {},\n", ipc->sem.limits.semmns);
    fmt::print("      \"nearLimit\": {}\n",
               ipc->sem.isNearArrayLimit() || ipc->sem.isNearSemLimit());
    fmt::print("    }},\n");
    fmt::print("    \"msg\": {{\n");
    fmt::print("      \"queuesInUse\": {},\n", ipc->msg.queuesInUse);
    fmt::print("      \"limitMsgmni\": {},\n", ipc->msg.limits.msgmni);
    fmt::print("      \"nearLimit\": {}\n", ipc->msg.isNearQueueLimit());
    fmt::print("    }},\n");
    fmt::print("    \"posixMq\": {{\n");
    fmt::print("      \"queuesInUse\": {},\n", ipc->posixMq.queuesInUse);
    fmt::print("      \"limitQueuesMax\": {}\n", ipc->posixMq.limits.queuesMax);
    fmt::print("    }},\n");
    fmt::print("    \"nearAnyLimit\": {},\n", ipc->isNearAnyLimit());
    fmt::print("    \"rtScore\": {}\n", ipc->rtScore());
    fmt::print("  }}");
  }

  // Security (optional)
  if (sec != nullptr) {
    fmt::print(",\n  \"security\": {{\n");
    fmt::print("    \"selinux\": {{\n");
    fmt::print("      \"mode\": \"{}\",\n", sys::toString(sec->selinux.mode));
    fmt::print("      \"isActive\": {},\n", sec->selinux.isActive());
    fmt::print("      \"isEnforcing\": {},\n", sec->selinux.isEnforcing());
    fmt::print("      \"policyType\": \"{}\",\n", sec->selinux.policyType.data());
    fmt::print("      \"policyVersion\": {},\n", sec->selinux.policyVersion);
    fmt::print("      \"mcsEnabled\": {},\n", sec->selinux.mcsEnabled);
    fmt::print("      \"mlsEnabled\": {}\n", sec->selinux.mlsEnabled);
    fmt::print("    }},\n");
    fmt::print("    \"apparmor\": {{\n");
    fmt::print("      \"mode\": \"{}\",\n", sys::toString(sec->apparmor.mode));
    fmt::print("      \"isActive\": {},\n", sec->apparmor.isActive());
    fmt::print("      \"profilesLoaded\": {},\n", sec->apparmor.profilesLoaded);
    fmt::print("      \"profilesEnforce\": {},\n", sec->apparmor.profilesEnforce);
    fmt::print("      \"profilesComplain\": {}\n", sec->apparmor.profilesComplain);
    fmt::print("    }},\n");
    fmt::print("    \"seccompAvailable\": {},\n", sec->seccompAvailable);
    fmt::print("    \"landLockAvailable\": {},\n", sec->landLockAvailable);
    fmt::print("    \"yamaPtrace\": {},\n", sec->yamaPtrace);
    fmt::print("    \"hasEnforcement\": {},\n", sec->hasEnforcement());
    fmt::print("    \"activeLsms\": \"{}\",\n", sec->activeLsmList());
    fmt::print("    \"lsmCount\": {}\n", sec->lsmCount);
    fmt::print("  }}");
  }

  // File descriptors (optional)
  if (fd != nullptr) {
    fmt::print(",\n  \"fileDescriptors\": {{\n");
    fmt::print("    \"process\": {{\n");
    fmt::print("      \"openCount\": {},\n", fd->process.openCount);
    fmt::print("      \"softLimit\": {},\n", fd->process.softLimit);
    fmt::print("      \"hardLimit\": {},\n", fd->process.hardLimit);
    fmt::print("      \"available\": {},\n", fd->process.available());
    fmt::print("      \"utilizationPercent\": {:.2f},\n", fd->process.utilizationPercent());
    fmt::print("      \"isCritical\": {},\n", fd->process.isCritical());
    fmt::print("      \"isElevated\": {},\n", fd->process.isElevated());
    fmt::print("      \"highestFd\": {},\n", fd->process.highestFd);
    fmt::print("      \"typeCount\": {}\n", fd->process.typeCount);
    fmt::print("    }},\n");
    fmt::print("    \"system\": {{\n");
    fmt::print("      \"allocated\": {},\n", fd->system.allocated);
    fmt::print("      \"maximum\": {},\n", fd->system.maximum);
    fmt::print("      \"available\": {},\n", fd->system.available());
    fmt::print("      \"utilizationPercent\": {:.2f},\n", fd->system.utilizationPercent());
    fmt::print("      \"isCritical\": {},\n", fd->system.isCritical());
    fmt::print("      \"nrOpen\": {}\n", fd->system.nrOpen);
    fmt::print("    }},\n");
    fmt::print("    \"anyCritical\": {}\n", fd->anyCritical());
    fmt::print("  }}");
  }

  fmt::print("\n}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool showWatchdog = false;
  bool showIpc = false;
  bool showSecurity = false;
  bool showFd = false;

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
    showWatchdog = (pargs.count(ARG_WATCHDOG) != 0);
    showIpc = (pargs.count(ARG_IPC) != 0);
    showSecurity = (pargs.count(ARG_SECURITY) != 0);
    showFd = (pargs.count(ARG_FD) != 0);
  }

  // Gather data (always collected)
  const sys::KernelInfo KERNEL = sys::getKernelInfo();
  const sys::VirtualizationInfo VIRT = sys::getVirtualizationInfo();
  const sys::RtSchedConfig SCHED = sys::getRtSchedConfig();
  const sys::CapabilityStatus CAPS = sys::getCapabilityStatus();
  const sys::ProcessLimits LIMITS = sys::getProcessLimits();
  const sys::ContainerLimits CONTAINER = sys::getContainerLimits();

  // Optional data
  sys::WatchdogStatus wd{};
  sys::IpcStatus ipc{};
  sys::SecurityStatus sec{};
  sys::FileDescriptorStatus fd{};

  if (showWatchdog) {
    wd = sys::getWatchdogStatus();
  }
  if (showIpc) {
    ipc = sys::getIpcStatus();
  }
  if (showSecurity) {
    sec = sys::getSecurityStatus();
  }
  if (showFd) {
    fd = sys::getFileDescriptorStatus();
  }

  if (jsonOutput) {
    printJson(KERNEL, VIRT, SCHED, CAPS, LIMITS, CONTAINER, showWatchdog ? &wd : nullptr,
              showIpc ? &ipc : nullptr, showSecurity ? &sec : nullptr, showFd ? &fd : nullptr);
  } else {
    printHuman(KERNEL, VIRT, SCHED, CAPS, LIMITS, CONTAINER, showWatchdog ? &wd : nullptr,
               showIpc ? &ipc : nullptr, showSecurity ? &sec : nullptr, showFd ? &fd : nullptr);
  }

  return 0;
}