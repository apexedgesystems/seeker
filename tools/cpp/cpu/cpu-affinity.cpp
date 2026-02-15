/**
 * @file cpu-affinity.cpp
 * @brief Query and set CPU affinity for processes/threads.
 *
 * Displays current affinity mask or modifies affinity for a target process.
 * Useful for pinning RT threads to isolated cores.
 */

#include "src/cpu/inc/Affinity.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <sched.h>
#include <unistd.h>

#include <fmt/core.h>

namespace cpu = seeker::cpu;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_PID = 2,
  ARG_SET = 3,
  ARG_GET = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION = "Query or set CPU affinity for processes.\n"
                                         "Without --pid, operates on the current process.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_PID] = {"--pid", 1, false, "Target process ID (default: self)"};
  map[ARG_SET] = {"--set", 1, false, "Set affinity to CPU list (e.g., 0-3,6)"};
  map[ARG_GET] = {"--get", 0, false, "Get current affinity (default action)"};
  return map;
}

/// Parse integer argument with default.
int parseIntArg(const seeker::helpers::args::ParsedArgs& pargs, ArgKey key, int defaultVal) {
  if (pargs.count(key) != 0 && !pargs.at(key).empty()) {
    char* end = nullptr;
    const long VAL = std::strtol(std::string(pargs.at(key)[0]).c_str(), &end, 10);
    if (end != pargs.at(key)[0].data()) {
      return static_cast<int>(VAL);
    }
  }
  return defaultVal;
}

/* ----------------------------- Affinity Operations ----------------------------- */

/// Get affinity for a specific PID using sched_getaffinity.
cpu::CpuSet getAffinityForPid(pid_t pid) {
  cpu::CpuSet result;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  if (sched_getaffinity(pid, sizeof(cpuset), &cpuset) == 0) {
    for (std::size_t i = 0; i < cpu::MAX_CPUS && i < CPU_SETSIZE; ++i) {
      if (CPU_ISSET(i, &cpuset)) {
        result.set(i);
      }
    }
  }

  return result;
}

/// Set affinity for a specific PID using sched_setaffinity.
bool setAffinityForPid(pid_t pid, const cpu::CpuSet& cpuSet) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  for (std::size_t i = 0; i < cpu::MAX_CPUS && i < CPU_SETSIZE; ++i) {
    if (cpuSet.test(i)) {
      CPU_SET(i, &cpuset);
    }
  }

  return sched_setaffinity(pid, sizeof(cpuset), &cpuset) == 0;
}

/* ----------------------------- Output Functions ----------------------------- */

void printHumanOutput(pid_t pid, const cpu::CpuSet& affinity,
                      const cpu::CpuIsolationConfig& isolation) {
  fmt::print("CPU Affinity\n");
  fmt::print("============\n\n");

  if (pid == 0) {
    fmt::print("Process:    self (PID {})\n", getpid());
  } else {
    fmt::print("Process:    PID {}\n", pid);
  }

  fmt::print("Affinity:   {}\n", affinity.toString());
  fmt::print("CPU count:  {} of {} configured\n", affinity.count(), cpu::getConfiguredCpuCount());

  // Show isolation context
  if (isolation.hasAnyIsolation()) {
    fmt::print("\n--- Isolation Context ---\n");
    if (!isolation.isolcpus.empty()) {
      fmt::print("isolcpus:       {}\n", isolation.isolcpus.toString());
    }

    const cpu::CpuSet FULLY_ISOLATED = isolation.getFullyIsolatedCpus();
    if (!FULLY_ISOLATED.empty()) {
      fmt::print("Fully isolated: {}\n", FULLY_ISOLATED.toString());

      // Check overlap
      bool hasOverlap = false;
      for (std::size_t i = 0; i < cpu::MAX_CPUS; ++i) {
        if (affinity.test(i) && FULLY_ISOLATED.test(i)) {
          hasOverlap = true;
          break;
        }
      }

      if (hasOverlap) {
        fmt::print("\n\033[33mNote: Affinity includes isolated CPUs.\033[0m\n");
      }
    }
  }
}

void printJsonOutput(pid_t pid, const cpu::CpuSet& affinity,
                     const cpu::CpuIsolationConfig& isolation) {
  fmt::print("{{\n");
  fmt::print("  \"pid\": {},\n", (pid == 0) ? getpid() : pid);

  // Affinity as list
  fmt::print("  \"affinity\": [");
  bool first = true;
  for (std::size_t i = 0; i < cpu::MAX_CPUS; ++i) {
    if (affinity.test(i)) {
      if (!first) {
        fmt::print(", ");
      }
      first = false;
      fmt::print("{}", i);
    }
  }
  fmt::print("],\n");

  fmt::print("  \"affinityString\": \"{}\",\n", affinity.toString());
  fmt::print("  \"cpuCount\": {},\n", affinity.count());
  fmt::print("  \"configuredCpus\": {},\n", cpu::getConfiguredCpuCount());

  // Isolation info
  fmt::print("  \"isolation\": {{\n");
  fmt::print("    \"isolcpus\": \"{}\",\n", isolation.isolcpus.toString());
  fmt::print("    \"fullyIsolated\": \"{}\"\n", isolation.getFullyIsolatedCpus().toString());
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  pid_t targetPid = 0; // 0 means self
  std::string setCpuList;
  bool doSet = false;

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
    targetPid = static_cast<pid_t>(parseIntArg(pargs, ARG_PID, 0));

    if (pargs.count(ARG_SET) != 0 && !pargs.at(ARG_SET).empty()) {
      setCpuList = std::string(pargs.at(ARG_SET)[0]);
      doSet = true;
    }
  }

  // Get isolation config for context
  const cpu::CpuIsolationConfig ISOLATION = cpu::getCpuIsolationConfig();

  if (doSet) {
    // Set affinity
    const cpu::CpuSet NEW_AFFINITY = cpu::parseCpuList(setCpuList.c_str());

    if (NEW_AFFINITY.empty()) {
      fmt::print(stderr, "Error: Invalid CPU list '{}'\n", setCpuList);
      return 1;
    }

    if (!setAffinityForPid(targetPid, NEW_AFFINITY)) {
      fmt::print(stderr, "Error: Failed to set affinity: {}\n", std::strerror(errno));
      return 1;
    }

    // Verify and display
    const cpu::CpuSet CURRENT = getAffinityForPid(targetPid);

    if (jsonOutput) {
      fmt::print("{{\"status\": \"ok\", \"affinity\": \"{}\"}}\n", CURRENT.toString());
    } else {
      fmt::print("Affinity set successfully.\n");
      fmt::print("New affinity: {}\n", CURRENT.toString());
    }
  } else {
    // Get affinity (default action)
    const cpu::CpuSet AFFINITY = getAffinityForPid(targetPid);

    if (AFFINITY.empty()) {
      if (targetPid != 0) {
        fmt::print(stderr, "Error: Could not get affinity for PID {}: {}\n", targetPid,
                   std::strerror(errno));
        return 1;
      }
      // Fallback to current thread affinity
      const cpu::CpuSet THREAD_AFFINITY = cpu::getCurrentThreadAffinity();
      if (jsonOutput) {
        printJsonOutput(targetPid, THREAD_AFFINITY, ISOLATION);
      } else {
        printHumanOutput(targetPid, THREAD_AFFINITY, ISOLATION);
      }
    } else {
      if (jsonOutput) {
        printJsonOutput(targetPid, AFFINITY, ISOLATION);
      } else {
        printHumanOutput(targetPid, AFFINITY, ISOLATION);
      }
    }
  }

  return 0;
}