/**
 * @file gpu-stat.cpp
 * @brief GPU telemetry and status display.
 *
 * Shows real-time GPU metrics: temperature, power, clocks, memory usage,
 * throttling status, and process information.
 */

#include "src/gpu/inc/GpuIsolation.hpp"
#include "src/gpu/inc/GpuMemoryStatus.hpp"
#include "src/gpu/inc/GpuTelemetry.hpp"
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/helpers/inc/Format.hpp"
#include "src/helpers/inc/Args.hpp"
#include "src/helpers/inc/Format.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace gpu = seeker::gpu;

using seeker::helpers::format::bytesBinary;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_DEVICE = 2,
  ARG_PROCS = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display GPU telemetry: temperature, power, clocks, memory, and throttling status.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DEVICE] = {"--device", 1, false, "GPU device index (default: all)"};
  map[ARG_PROCS] = {"--procs", 0, false, "Show GPU processes"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printDeviceStatus(const gpu::GpuTelemetry& telem, const gpu::GpuMemoryStatus& mem,
                       const gpu::GpuIsolation& iso, bool showProcs) {
  fmt::print("=== GPU {} ===\n", telem.deviceIndex);

  // Temperature and power
  fmt::print("  Temperature: {} C", telem.temperatureC);
  if (telem.temperatureSlowdownC > 0) {
    fmt::print(" (slowdown: {} C)", telem.temperatureSlowdownC);
  }
  fmt::print("\n");

  if (telem.powerMilliwatts > 0) {
    fmt::print("  Power:       {:.1f} W", telem.powerMilliwatts / 1000.0);
    if (telem.powerLimitMilliwatts > 0) {
      fmt::print(" / {:.0f} W ({:.0f}%%)", telem.powerLimitMilliwatts / 1000.0,
                 100.0 * static_cast<double>(telem.powerMilliwatts) /
                     static_cast<double>(telem.powerLimitMilliwatts));
    }
    fmt::print("\n");
  }

  // Clocks
  if (telem.smClockMHz > 0) {
    fmt::print("  SM Clock:    {} MHz\n", telem.smClockMHz);
    fmt::print("  Mem Clock:   {} MHz\n", telem.memClockMHz);
    fmt::print("  Perf State:  P{}\n", telem.perfState);
  }

  // Fan
  if (telem.fanSpeedPercent >= 0) {
    fmt::print("  Fan:         {}%%\n", telem.fanSpeedPercent);
  }

  // Throttling
  if (telem.isThrottling()) {
    fmt::print("  \033[33mThrottling:  {}\033[0m\n", telem.throttleReasons.toString());
  }

  // Memory
  fmt::print("  Memory:      {} / {} ({:.1f}%% used)\n", bytesBinary(mem.usedBytes),
             bytesBinary(mem.totalBytes), mem.utilizationPercent());

  // ECC status
  if (mem.eccEnabled) {
    fmt::print("  ECC:         enabled");
    if (mem.eccErrors.hasUncorrected()) {
      fmt::print(" \033[31m[UNCORRECTED ERRORS]\033[0m");
    }
    fmt::print("\n");
  }

  // Isolation info
  if (iso.migModeEnabled) {
    fmt::print("  MIG:         enabled ({} instances)\n", iso.migInstances.size());
  }
  if (iso.mpsServerActive) {
    fmt::print("  MPS:         active\n");
  }

  // Processes
  if (showProcs && !iso.processes.empty()) {
    fmt::print("  Processes:\n");
    for (const auto& PROC : iso.processes) {
      const char* type = (PROC.type == gpu::GpuProcess::Type::Compute) ? "compute" : "graphics";
      fmt::print("    PID {}: {} ({}, {})\n", PROC.pid, PROC.name, type,
                 bytesBinary(PROC.usedMemoryBytes));
    }
  } else if (!showProcs) {
    if (iso.computeProcessCount > 0 || iso.graphicsProcessCount > 0) {
      fmt::print("  Processes:   {} compute, {} graphics\n", iso.computeProcessCount,
                 iso.graphicsProcessCount);
    }
  }
}

void printHuman(const std::vector<gpu::GpuTelemetry>& telemList,
                const std::vector<gpu::GpuMemoryStatus>& memList,
                const std::vector<gpu::GpuIsolation>& isoList, int targetDevice, bool showProcs) {
  if (telemList.empty()) {
    fmt::print("No GPUs detected.\n");
    return;
  }

  bool first = true;
  for (std::size_t i = 0; i < telemList.size(); ++i) {
    const auto& TELEM = telemList[i];

    if (targetDevice >= 0 && TELEM.deviceIndex != targetDevice) {
      continue;
    }

    // Find matching memory and isolation info
    gpu::GpuMemoryStatus mem;
    gpu::GpuIsolation iso;
    for (const auto& MEM_ENTRY : memList) {
      if (MEM_ENTRY.deviceIndex == TELEM.deviceIndex) {
        mem = MEM_ENTRY;
        break;
      }
    }
    for (const auto& ISO_ENTRY : isoList) {
      if (ISO_ENTRY.deviceIndex == TELEM.deviceIndex) {
        iso = ISO_ENTRY;
        break;
      }
    }

    if (!first) {
      fmt::print("\n");
    }
    first = false;
    printDeviceStatus(TELEM, mem, iso, showProcs);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const std::vector<gpu::GpuTelemetry>& telemList,
               const std::vector<gpu::GpuMemoryStatus>& memList,
               const std::vector<gpu::GpuIsolation>& isoList, int targetDevice, bool showProcs) {
  fmt::print("{{\n");
  fmt::print("  \"devices\": [\n");

  bool first = true;
  for (const auto& TELEM : telemList) {
    if (targetDevice >= 0 && TELEM.deviceIndex != targetDevice) {
      continue;
    }

    // Find matching info
    gpu::GpuMemoryStatus mem;
    gpu::GpuIsolation iso;
    for (const auto& MEM_ENTRY : memList) {
      if (MEM_ENTRY.deviceIndex == TELEM.deviceIndex) {
        mem = MEM_ENTRY;
        break;
      }
    }
    for (const auto& ISO_ENTRY : isoList) {
      if (ISO_ENTRY.deviceIndex == TELEM.deviceIndex) {
        iso = ISO_ENTRY;
        break;
      }
    }

    if (!first) {
      fmt::print(",\n");
    }
    first = false;

    fmt::print("    {{\n");
    fmt::print("      \"deviceIndex\": {},\n", TELEM.deviceIndex);

    // Telemetry
    fmt::print("      \"telemetry\": {{\n");
    fmt::print("        \"temperatureC\": {},\n", TELEM.temperatureC);
    fmt::print("        \"powerMilliwatts\": {},\n", TELEM.powerMilliwatts);
    fmt::print("        \"smClockMHz\": {},\n", TELEM.smClockMHz);
    fmt::print("        \"memClockMHz\": {},\n", TELEM.memClockMHz);
    fmt::print("        \"perfState\": {},\n", TELEM.perfState);
    fmt::print("        \"fanSpeedPercent\": {},\n", TELEM.fanSpeedPercent);
    fmt::print("        \"isThrottling\": {},\n", TELEM.isThrottling());
    fmt::print("        \"throttleReasons\": \"{}\"\n", TELEM.throttleReasons.toString());
    fmt::print("      }},\n");

    // Memory
    fmt::print("      \"memory\": {{\n");
    fmt::print("        \"totalBytes\": {},\n", mem.totalBytes);
    fmt::print("        \"usedBytes\": {},\n", mem.usedBytes);
    fmt::print("        \"freeBytes\": {},\n", mem.freeBytes);
    fmt::print("        \"eccEnabled\": {},\n", mem.eccEnabled);
    fmt::print("        \"hasUncorrectedErrors\": {}\n", mem.eccErrors.hasUncorrected());
    fmt::print("      }},\n");

    // Isolation
    fmt::print("      \"isolation\": {{\n");
    fmt::print("        \"migModeEnabled\": {},\n", iso.migModeEnabled);
    fmt::print("        \"mpsServerActive\": {},\n", iso.mpsServerActive);
    fmt::print("        \"computeProcessCount\": {},\n", iso.computeProcessCount);
    fmt::print("        \"graphicsProcessCount\": {}", iso.graphicsProcessCount);

    if (showProcs && !iso.processes.empty()) {
      fmt::print(",\n        \"processes\": [\n");
      for (std::size_t p = 0; p < iso.processes.size(); ++p) {
        const auto& PROC = iso.processes[p];
        if (p > 0) {
          fmt::print(",\n");
        }
        fmt::print("          {{\"pid\": {}, \"name\": \"{}\", \"usedBytes\": {}}}", PROC.pid,
                   PROC.name, PROC.usedMemoryBytes);
      }
      fmt::print("\n        ]");
    }
    fmt::print("\n      }}\n");

    fmt::print("    }}");
  }

  fmt::print("\n  ]\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int targetDevice = -1;
  bool showProcs = false;

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
    showProcs = (pargs.count(ARG_PROCS) != 0);

    if (pargs.count(ARG_DEVICE) != 0 && !pargs[ARG_DEVICE].empty()) {
      targetDevice = static_cast<int>(std::strtol(pargs[ARG_DEVICE][0].data(), nullptr, 10));
    }
  }

  // Gather data
  const std::vector<gpu::GpuTelemetry> TELEM_LIST = gpu::getAllGpuTelemetry();
  const std::vector<gpu::GpuMemoryStatus> MEM_LIST = gpu::getAllGpuMemoryStatus();
  const std::vector<gpu::GpuIsolation> ISO_LIST = gpu::getAllGpuIsolation();

  if (jsonOutput) {
    printJson(TELEM_LIST, MEM_LIST, ISO_LIST, targetDevice, showProcs);
  } else {
    printHuman(TELEM_LIST, MEM_LIST, ISO_LIST, targetDevice, showProcs);
  }

  return 0;
}
