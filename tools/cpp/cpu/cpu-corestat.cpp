/**
 * @file cpu-corestat.cpp
 * @brief Per-core CPU utilization monitor with snapshot + delta measurement.
 *
 * Displays real-time per-core utilization percentages including user, system,
 * idle, and iowait. Supports continuous monitoring with configurable intervals.
 */

#include "src/cpu/inc/CpuUtilization.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <chrono>
#include <thread>

#include <fmt/core.h>

namespace cpu = seeker::cpu;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_INTERVAL = 2,
  ARG_COUNT = 3,
  ARG_CPUS = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Per-core CPU utilization monitor.\n"
    "Displays user/system/idle/iowait percentages with configurable sampling.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_INTERVAL] = {"--interval", 1, false, "Sampling interval in ms (default: 1000)"};
  map[ARG_COUNT] = {"--count", 1, false, "Number of samples (default: infinite)"};
  map[ARG_CPUS] = {"--cpus", 1, false, "CPU list to display (e.g., 0-3,6)"};
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

/* ----------------------------- Output Functions ----------------------------- */

void printHumanHeader(const cpu::CpuSet& cpuFilter, std::size_t coreCount) {
  fmt::print("CPU Utilization Monitor\n");
  fmt::print("=======================\n");

  if (!cpuFilter.empty()) {
    fmt::print("Monitoring CPUs: {}\n\n", cpuFilter.toString());
  } else {
    fmt::print("Monitoring {} CPUs\n\n", coreCount);
  }

  fmt::print("{:>4}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}\n", "CPU", "user%", "sys%", "idle%", "iowt%",
             "actv%");
  fmt::print("{}\n", std::string(46, '-'));
}

void printHumanSample(const cpu::CpuUtilizationDelta& delta, const cpu::CpuSet& cpuFilter) {
  for (std::size_t i = 0; i < delta.coreCount; ++i) {
    // Filter if specified
    if (!cpuFilter.empty() && !cpuFilter.test(i)) {
      continue;
    }

    const auto& CORE = delta.perCore[i];
    fmt::print("{:>4}  {:>6.1f}  {:>6.1f}  {:>6.1f}  {:>6.1f}  {:>6.1f}\n", i, CORE.user,
               CORE.system, CORE.idle, CORE.iowait, CORE.active());
  }

  // Aggregate line
  const auto& AGG = delta.aggregate;
  fmt::print("{:>4}  {:>6.1f}  {:>6.1f}  {:>6.1f}  {:>6.1f}  {:>6.1f}\n", "ALL", AGG.user,
             AGG.system, AGG.idle, AGG.iowait, AGG.active());
  fmt::print("\n");
}

void printJsonSample(const cpu::CpuUtilizationDelta& delta, const cpu::CpuSet& cpuFilter,
                     int sampleNum) {
  fmt::print("{{");
  fmt::print("\"sample\": {}, ", sampleNum);
  fmt::print("\"intervalMs\": {}, ", delta.intervalNs / 1000000);

  // Aggregate
  fmt::print("\"aggregate\": {{");
  fmt::print("\"user\": {:.2f}, ", delta.aggregate.user);
  fmt::print("\"system\": {:.2f}, ", delta.aggregate.system);
  fmt::print("\"idle\": {:.2f}, ", delta.aggregate.idle);
  fmt::print("\"iowait\": {:.2f}, ", delta.aggregate.iowait);
  fmt::print("\"active\": {:.2f}", delta.aggregate.active());
  fmt::print("}}, ");

  // Per-core
  fmt::print("\"cores\": [");
  bool first = true;
  for (std::size_t i = 0; i < delta.coreCount; ++i) {
    if (!cpuFilter.empty() && !cpuFilter.test(i)) {
      continue;
    }

    if (!first) {
      fmt::print(", ");
    }
    first = false;

    const auto& CORE = delta.perCore[i];
    fmt::print("{{\"cpu\": {}, ", i);
    fmt::print("\"user\": {:.2f}, ", CORE.user);
    fmt::print("\"system\": {:.2f}, ", CORE.system);
    fmt::print("\"idle\": {:.2f}, ", CORE.idle);
    fmt::print("\"iowait\": {:.2f}, ", CORE.iowait);
    fmt::print("\"active\": {:.2f}}}", CORE.active());
  }
  fmt::print("]");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int intervalMs = 1000;
  int count = -1; // Infinite
  cpu::CpuSet cpuFilter;

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
    intervalMs = parseIntArg(pargs, ARG_INTERVAL, 1000);
    count = parseIntArg(pargs, ARG_COUNT, -1);

    if (pargs.count(ARG_CPUS) != 0 && !pargs.at(ARG_CPUS).empty()) {
      cpuFilter = cpu::parseCpuList(std::string(pargs.at(ARG_CPUS)[0]).c_str());
    }
  }

  // Clamp interval to reasonable range
  if (intervalMs < 10) {
    intervalMs = 10;
  }
  if (intervalMs > 60000) {
    intervalMs = 60000;
  }

  // Initial snapshot
  cpu::CpuUtilizationSnapshot prevSnap = cpu::getCpuUtilizationSnapshot();

  if (!jsonOutput) {
    printHumanHeader(cpuFilter, prevSnap.coreCount);
  }

  int sampleNum = 0;
  while (count < 0 || sampleNum < count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

    const cpu::CpuUtilizationSnapshot CURR_SNAP = cpu::getCpuUtilizationSnapshot();
    const cpu::CpuUtilizationDelta DELTA = cpu::computeUtilizationDelta(prevSnap, CURR_SNAP);

    if (jsonOutput) {
      printJsonSample(DELTA, cpuFilter, sampleNum);
    } else {
      printHumanSample(DELTA, cpuFilter);
    }

    prevSnap = CURR_SNAP;
    ++sampleNum;
  }

  return 0;
}