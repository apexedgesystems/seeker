/**
 * @file cpu-snapshot.cpp
 * @brief Full CPU diagnostic state dump for bug reports and diffing.
 *
 * Aggregates all CPU diagnostic modules into a single comprehensive output.
 * Ideal for attaching to bug reports, before/after comparisons, and CI baselines.
 */

#include "src/cpu/inc/Affinity.hpp"
#include "src/cpu/inc/CpuFeatures.hpp"
#include "src/cpu/inc/CpuFreq.hpp"
#include "src/cpu/inc/CpuIdle.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/cpu/inc/CpuStats.hpp"
#include "src/cpu/inc/CpuTopology.hpp"
#include "src/cpu/inc/CpuUtilization.hpp"
#include "src/cpu/inc/IrqStats.hpp"
#include "src/cpu/inc/SoftirqStats.hpp"
#include "src/cpu/inc/ThermalStatus.hpp"
#include "src/helpers/inc/Format.hpp"
#include "src/helpers/inc/Args.hpp"
#include "src/helpers/inc/Format.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <chrono>
#include <thread>

#include <fmt/core.h>

namespace cpu = seeker::cpu;

using seeker::helpers::format::bytesBinary;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_OUTPUT = 2,
  ARG_BRIEF = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Full CPU diagnostic snapshot for bug reports and diffing.\n"
    "Aggregates all diagnostic modules into a single output.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format (default)"};
  map[ARG_OUTPUT] = {"--output", 1, false, "Write to file instead of stdout"};
  map[ARG_BRIEF] = {"--brief", 0, false, "Brief human-readable summary"};
  return map;
}

/* ----------------------------- JSON Output ----------------------------- */

void printJsonSnapshot(const cpu::CpuTopology& topo, const cpu::CpuFeatures& feat,
                       const cpu::CpuFrequencySummary& freq, const cpu::CpuStats& stats,
                       const cpu::CpuIsolationConfig& isolation, const cpu::CpuIdleSnapshot& idle,
                       const cpu::ThermalStatus& thermal, const cpu::CpuUtilizationDelta& util,
                       const cpu::IrqSnapshot& irq, const cpu::SoftirqSnapshot& softirq) {
  fmt::print("{{\n");

  // Metadata
  fmt::print("  \"snapshotVersion\": 1,\n");

  // Topology
  fmt::print("  \"topology\": {{\n");
  fmt::print("    \"packages\": {},\n", topo.packages);
  fmt::print("    \"physicalCores\": {},\n", topo.physicalCores);
  fmt::print("    \"logicalCpus\": {},\n", topo.logicalCpus);
  fmt::print("    \"threadsPerCore\": {},\n", topo.threadsPerCore());
  fmt::print("    \"numaNodes\": {}\n", topo.numaNodes);
  fmt::print("  }},\n");

  // Features
  fmt::print("  \"features\": {{\n");
  fmt::print("    \"vendor\": \"{}\",\n", feat.vendor.data());
  fmt::print("    \"brand\": \"{}\",\n", feat.brand.data());
  fmt::print("    \"avx\": {}, \"avx2\": {}, \"avx512f\": {},\n", feat.avx, feat.avx2,
             feat.avx512f);
  fmt::print("    \"invariantTsc\": {}\n", feat.invariantTsc);
  fmt::print("  }},\n");

  // System stats
  fmt::print("  \"system\": {{\n");
  fmt::print("    \"kernel\": \"{}\",\n", stats.kernel.version.data());
  fmt::print("    \"cpuCount\": {},\n", stats.cpuCount.count);
  fmt::print("    \"totalRamBytes\": {},\n", stats.sysinfo.totalRamBytes);
  fmt::print("    \"availableRamBytes\": {},\n", stats.meminfo.availableBytes);
  fmt::print("    \"uptimeSeconds\": {},\n", stats.sysinfo.uptimeSeconds);
  fmt::print("    \"load1\": {:.2f}, \"load5\": {:.2f}, \"load15\": {:.2f}\n", stats.sysinfo.load1,
             stats.sysinfo.load5, stats.sysinfo.load15);
  fmt::print("  }},\n");

  // Isolation
  fmt::print("  \"isolation\": {{\n");
  fmt::print("    \"isolcpus\": \"{}\",\n", isolation.isolcpus.toString());
  fmt::print("    \"nohzFull\": \"{}\",\n", isolation.nohzFull.toString());
  fmt::print("    \"rcuNocbs\": \"{}\",\n", isolation.rcuNocbs.toString());
  fmt::print("    \"fullyIsolated\": \"{}\"\n", isolation.getFullyIsolatedCpus().toString());
  fmt::print("  }},\n");

  // Frequency
  fmt::print("  \"frequency\": {{\n");
  fmt::print("    \"cores\": [\n");
  for (std::size_t i = 0; i < freq.cores.size(); ++i) {
    const auto& C = freq.cores[i];
    fmt::print("      {{\"cpuId\": {}, \"governor\": \"{}\", \"curKHz\": {}}}", C.cpuId,
               C.governor.data(), C.curKHz);
    if (i + 1 < freq.cores.size()) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("    ]\n");
  fmt::print("  }},\n");

  // C-States
  fmt::print("  \"cstates\": {{\n");
  fmt::print("    \"cpuCount\": {},\n", idle.cpuCount);
  fmt::print("    \"perCpu\": [\n");
  for (std::size_t i = 0; i < idle.cpuCount && i < 4; ++i) {
    const auto& CPU_IDLE = idle.perCpu[i];
    fmt::print("      {{\"cpuId\": {}, \"stateCount\": {}}}", CPU_IDLE.cpuId, CPU_IDLE.stateCount);
    if (i + 1 < idle.cpuCount && i + 1 < 4) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  if (idle.cpuCount > 4) {
    fmt::print("      // ... {} more CPUs\n", idle.cpuCount - 4);
  }
  fmt::print("    ]\n");
  fmt::print("  }},\n");

  // Thermal
  fmt::print("  \"thermal\": {{\n");
  fmt::print("    \"throttling\": {{\n");
  fmt::print("      \"thermal\": {},\n", thermal.throttling.thermal);
  fmt::print("      \"powerLimit\": {},\n", thermal.throttling.powerLimit);
  fmt::print("      \"current\": {}\n", thermal.throttling.current);
  fmt::print("    }},\n");
  fmt::print("    \"sensorCount\": {},\n", thermal.sensors.size());
  fmt::print("    \"sensors\": [\n");
  for (std::size_t i = 0; i < thermal.sensors.size(); ++i) {
    const auto& S = thermal.sensors[i];
    fmt::print("      {{\"name\": \"{}\", \"tempCelsius\": {:.1f}}}", S.name.data(), S.tempCelsius);
    if (i + 1 < thermal.sensors.size()) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("    ]\n");
  fmt::print("  }},\n");

  // Utilization (brief sample)
  fmt::print("  \"utilization\": {{\n");
  fmt::print("    \"aggregate\": {{\n");
  fmt::print("      \"user\": {:.1f}, \"system\": {:.1f}, \"idle\": {:.1f}\n", util.aggregate.user,
             util.aggregate.system, util.aggregate.idle);
  fmt::print("    }},\n");
  fmt::print("    \"coreCount\": {}\n", util.coreCount);
  fmt::print("  }},\n");

  // IRQ summary
  fmt::print("  \"irq\": {{\n");
  fmt::print("    \"lineCount\": {},\n", irq.lineCount);
  fmt::print("    \"coreCount\": {},\n", irq.coreCount);
  fmt::print("    \"totalAllCores\": {}\n", irq.totalAllCores());
  fmt::print("  }},\n");

  // Softirq summary
  fmt::print("  \"softirq\": {{\n");
  fmt::print("    \"typeCount\": {},\n", softirq.typeCount);
  fmt::print("    \"cpuCount\": {}\n", softirq.cpuCount);
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

/* ----------------------------- Human Output ----------------------------- */

void printBriefSummary(const cpu::CpuTopology& topo, const cpu::CpuFeatures& feat,
                       const cpu::CpuFrequencySummary& freq, const cpu::CpuStats& stats,
                       const cpu::CpuIsolationConfig& isolation, const cpu::ThermalStatus& thermal,
                       const cpu::CpuUtilizationDelta& util) {
  fmt::print("CPU Diagnostic Snapshot\n");
  fmt::print("=======================\n\n");

  // System
  fmt::print("System:     {} ({} cores, {} threads)\n", feat.brand.data(), topo.physicalCores,
             topo.logicalCpus);
  fmt::print("Kernel:     {}\n", stats.kernel.version.data());
  fmt::print("Memory:     {} total, {} available\n", bytesBinary(stats.sysinfo.totalRamBytes),
             bytesBinary(stats.meminfo.availableBytes));
  fmt::print("Load:       {:.2f} {:.2f} {:.2f}\n", stats.sysinfo.load1, stats.sysinfo.load5,
             stats.sysinfo.load15);

  // Frequency summary
  if (!freq.cores.empty()) {
    const char* gov = freq.cores[0].governor.data();
    bool uniformGov = true;
    for (const auto& C : freq.cores) {
      if (std::strcmp(C.governor.data(), gov) != 0) {
        uniformGov = false;
        break;
      }
    }
    fmt::print("Governor:   {}\n", uniformGov ? gov : "(mixed)");
  }

  // Isolation
  const cpu::CpuSet FULLY_ISOLATED = isolation.getFullyIsolatedCpus();
  if (!FULLY_ISOLATED.empty()) {
    fmt::print("Isolated:   {}\n", FULLY_ISOLATED.toString());
  } else if (isolation.hasAnyIsolation()) {
    fmt::print("Isolated:   (partial - see isolcpus)\n");
  } else {
    fmt::print("Isolated:   (none)\n");
  }

  // Thermal
  fmt::print("\nThermal:    ");
  if (thermal.throttling.thermal) {
    fmt::print("\033[31mTHROTTLING\033[0m");
  } else if (thermal.throttling.powerLimit) {
    fmt::print("\033[33mpower-limited\033[0m");
  } else {
    fmt::print("\033[32mOK\033[0m");
  }

  if (!thermal.sensors.empty()) {
    double maxTemp = 0.0;
    for (const auto& S : thermal.sensors) {
      if (S.tempCelsius > maxTemp) {
        maxTemp = S.tempCelsius;
      }
    }
    fmt::print(" (max {:.0f}C)", maxTemp);
  }
  fmt::print("\n");

  // Utilization
  fmt::print("\nUtilization (1s sample):\n");
  fmt::print("  Aggregate: {:.1f}% active, {:.1f}% idle\n", util.aggregate.active(),
             util.aggregate.idle);

  // Key features for RT
  fmt::print("\nRT-Critical:\n");
  fmt::print("  Invariant TSC: {}\n", feat.invariantTsc ? "yes" : "NO");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool briefOutput = false;
  std::string outputFile;

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

    if (pargs.count(ARG_BRIEF) != 0) {
      briefOutput = true;
    }

    if (pargs.count(ARG_OUTPUT) != 0 && !pargs.at(ARG_OUTPUT).empty()) {
      outputFile = std::string(pargs.at(ARG_OUTPUT)[0]);
    }
  }

  // Collect all data
  const cpu::CpuTopology TOPO = cpu::getCpuTopology();
  const cpu::CpuFeatures FEAT = cpu::getCpuFeatures();
  const cpu::CpuFrequencySummary FREQ = cpu::getCpuFrequencySummary();
  const cpu::CpuStats STATS = cpu::getCpuStats();
  const cpu::CpuIsolationConfig ISOLATION = cpu::getCpuIsolationConfig();
  const cpu::CpuIdleSnapshot IDLE = cpu::getCpuIdleSnapshot();
  const cpu::ThermalStatus THERMAL = cpu::getThermalStatus();
  const cpu::IrqSnapshot IRQ = cpu::getIrqSnapshot();
  const cpu::SoftirqSnapshot SOFTIRQ = cpu::getSoftirqSnapshot();

  // Take utilization sample
  const cpu::CpuUtilizationSnapshot UTIL_BEFORE = cpu::getCpuUtilizationSnapshot();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const cpu::CpuUtilizationSnapshot UTIL_AFTER = cpu::getCpuUtilizationSnapshot();
  const cpu::CpuUtilizationDelta UTIL_DELTA = cpu::computeUtilizationDelta(UTIL_BEFORE, UTIL_AFTER);

  // Redirect to file if specified
  std::streambuf* origBuf = nullptr;
  std::ofstream fileStream;
  if (!outputFile.empty()) {
    fileStream.open(outputFile);
    if (!fileStream) {
      fmt::print(stderr, "Error: Could not open '{}' for writing\n", outputFile);
      return 1;
    }
    origBuf = std::cout.rdbuf(fileStream.rdbuf());
  }

  // Output
  if (briefOutput) {
    printBriefSummary(TOPO, FEAT, FREQ, STATS, ISOLATION, THERMAL, UTIL_DELTA);
  } else {
    printJsonSnapshot(TOPO, FEAT, FREQ, STATS, ISOLATION, IDLE, THERMAL, UTIL_DELTA, IRQ, SOFTIRQ);
  }

  // Restore stdout if redirected
  if (origBuf != nullptr) {
    std::cout.rdbuf(origBuf);
    fmt::print("Snapshot written to: {}\n", outputFile);
  }

  return 0;
}