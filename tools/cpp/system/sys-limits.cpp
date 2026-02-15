/**
 * @file sys-limits.cpp
 * @brief Display process resource limits and capabilities.
 *
 * Shows all rlimits and Linux capabilities for the current process.
 * Useful for diagnosing RT scheduling and memory locking issues.
 */

#include "src/system/inc/CapabilityStatus.hpp"
#include "src/system/inc/ProcessLimits.hpp"
#include "src/helpers/inc/Args.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace sys = seeker::system;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_ALL = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display process resource limits and capabilities.\n"
    "Default shows RT-relevant limits; use --all for complete list.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_ALL] = {"--all", 0, false, "Show all limits (not just RT-relevant)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printLimitRow(const char* name, const sys::RlimitValue& limit, bool isBytes) {
  const std::string SOFT = sys::formatLimit(limit.soft, isBytes);
  const std::string HARD = sys::formatLimit(limit.hard, isBytes);

  fmt::print("  {:<12} {:>14}  {:>14}\n", name, SOFT, HARD);
}

void printHuman(const sys::ProcessLimits& limits, const sys::CapabilityStatus& caps, bool showAll) {
  fmt::print("=== Process Limits ===\n");
  fmt::print("  {:<12} {:>14}  {:>14}\n", "Resource", "Soft", "Hard");
  fmt::print("  {:-<12} {:-^14}  {:-^14}\n", "", "", "");

  // RT-relevant limits (always shown)
  printLimitRow("RTPRIO", limits.rtprio, false);
  printLimitRow("RTTIME", limits.rttime, false);
  printLimitRow("NICE", limits.nice, false);
  printLimitRow("MEMLOCK", limits.memlock, true);

  if (showAll) {
    // All other limits
    printLimitRow("AS", limits.addressSpace, true);
    printLimitRow("DATA", limits.dataSegment, true);
    printLimitRow("STACK", limits.stack, true);
    printLimitRow("CORE", limits.core, true);
    printLimitRow("NOFILE", limits.nofile, false);
    printLimitRow("NPROC", limits.nproc, false);
    printLimitRow("MSGQUEUE", limits.msgqueue, true);
  }

  // RT summary
  fmt::print("\n=== RT Summary ===\n");
  fmt::print("  Max RT priority:    {}\n", limits.rtprioMax());
  fmt::print("  Can use RT sched:   {}\n", limits.canUseRtScheduling() ? "yes" : "no");
  fmt::print("  Unlimited memlock:  {}\n", limits.hasUnlimitedMemlock() ? "yes" : "no");

  // Capabilities
  fmt::print("\n=== Capabilities ===\n");
  fmt::print("  Running as root:    {}\n", caps.isRoot ? "yes" : "no");
  fmt::print("  CAP_SYS_NICE:       {}\n", caps.sysNice ? "yes" : "no");
  fmt::print("  CAP_IPC_LOCK:       {}\n", caps.ipcLock ? "yes" : "no");
  fmt::print("  CAP_SYS_RESOURCE:   {}\n", caps.sysResource ? "yes" : "no");

  if (showAll) {
    fmt::print("  CAP_SYS_RAWIO:      {}\n", caps.sysRawio ? "yes" : "no");
    fmt::print("  CAP_SYS_ADMIN:      {}\n", caps.sysAdmin ? "yes" : "no");
    fmt::print("  CAP_NET_ADMIN:      {}\n", caps.netAdmin ? "yes" : "no");
    fmt::print("  CAP_NET_RAW:        {}\n", caps.netRaw ? "yes" : "no");
    fmt::print("  CAP_SYS_PTRACE:     {}\n", caps.sysPtrace ? "yes" : "no");
    fmt::print("\n  Effective mask:     {:#018x}\n", caps.effective);
    fmt::print("  Permitted mask:     {:#018x}\n", caps.permitted);
    fmt::print("  Inheritable mask:   {:#018x}\n", caps.inheritable);
  }

  // Capability summary
  fmt::print("\n=== Capability Summary ===\n");
  fmt::print("  Can use RT scheduling: {}\n", caps.canUseRtScheduling() ? "yes" : "no");
  fmt::print("  Can lock memory:       {}\n", caps.canLockMemory() ? "yes" : "no");
  fmt::print("  Is privileged:         {}\n", caps.isPrivileged() ? "yes" : "no");
}

/* ----------------------------- JSON Output ----------------------------- */

void printLimitJson(const char* name, const sys::RlimitValue& limit) {
  fmt::print("    \"{}\": {{\n", name);
  fmt::print("      \"soft\": {},\n", limit.soft);
  fmt::print("      \"hard\": {},\n", limit.hard);
  fmt::print("      \"unlimited\": {}\n", limit.unlimited);
  fmt::print("    }}");
}

void printJson(const sys::ProcessLimits& limits, const sys::CapabilityStatus& caps) {
  fmt::print("{{\n");

  // Limits
  fmt::print("  \"limits\": {{\n");
  printLimitJson("rtprio", limits.rtprio);
  fmt::print(",\n");
  printLimitJson("rttime", limits.rttime);
  fmt::print(",\n");
  printLimitJson("nice", limits.nice);
  fmt::print(",\n");
  printLimitJson("memlock", limits.memlock);
  fmt::print(",\n");
  printLimitJson("addressSpace", limits.addressSpace);
  fmt::print(",\n");
  printLimitJson("dataSegment", limits.dataSegment);
  fmt::print(",\n");
  printLimitJson("stack", limits.stack);
  fmt::print(",\n");
  printLimitJson("core", limits.core);
  fmt::print(",\n");
  printLimitJson("nofile", limits.nofile);
  fmt::print(",\n");
  printLimitJson("nproc", limits.nproc);
  fmt::print(",\n");
  printLimitJson("msgqueue", limits.msgqueue);
  fmt::print("\n");
  fmt::print("  }},\n");

  // Derived values
  fmt::print("  \"derived\": {{\n");
  fmt::print("    \"rtprioMax\": {},\n", limits.rtprioMax());
  fmt::print("    \"canUseRtScheduling\": {},\n", limits.canUseRtScheduling());
  fmt::print("    \"hasUnlimitedMemlock\": {}\n", limits.hasUnlimitedMemlock());
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
  fmt::print("    \"effective\": {},\n", caps.effective);
  fmt::print("    \"permitted\": {},\n", caps.permitted);
  fmt::print("    \"inheritable\": {},\n", caps.inheritable);
  fmt::print("    \"canUseRtScheduling\": {},\n", caps.canUseRtScheduling());
  fmt::print("    \"canLockMemory\": {},\n", caps.canLockMemory());
  fmt::print("    \"isPrivileged\": {}\n", caps.isPrivileged());
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool showAll = false;

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
    showAll = (pargs.count(ARG_ALL) != 0);
  }

  // Gather data
  const sys::ProcessLimits LIMITS = sys::getProcessLimits();
  const sys::CapabilityStatus CAPS = sys::getCapabilityStatus();

  if (jsonOutput) {
    printJson(LIMITS, CAPS);
  } else {
    printHuman(LIMITS, CAPS, showAll);
  }

  return 0;
}