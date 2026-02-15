/**
 * @file gpu-rtcheck.cpp
 * @brief GPU RT readiness validation tool.
 *
 * Checks GPU configuration for real-time suitability:
 *  - Persistence mode
 *  - Compute exclusivity
 *  - ECC status and retired pages
 *  - Driver/runtime version compatibility
 *  - Thermal/power throttling
 *  - PCIe link status
 *  - Process isolation
 */

#include "src/gpu/inc/GpuDriverStatus.hpp"
#include "src/gpu/inc/GpuIsolation.hpp"
#include "src/gpu/inc/GpuMemoryStatus.hpp"
#include "src/gpu/inc/GpuTelemetry.hpp"
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/gpu/inc/PcieStatus.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace gpu = seeker::gpu;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_DEVICE = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "GPU RT readiness validation.\n"
    "Checks persistence mode, compute exclusivity, ECC, throttling, and PCIe links.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DEVICE] = {"--device", 1, false, "GPU device index (default: all)"};
  return map;
}

/* ----------------------------- Check Types ----------------------------- */

enum class CheckResult : unsigned char { PASS = 0, WARN, FAIL, SKIP };

struct CheckStatus {
  std::string name;
  CheckResult result{CheckResult::SKIP};
  std::string message;
  std::string recommendation;
};

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
    return "\033[32m";
  case CheckResult::WARN:
    return "\033[33m";
  case CheckResult::FAIL:
    return "\033[31m";
  case CheckResult::SKIP:
    return "\033[90m";
  }
  return "\033[0m";
}

/* ----------------------------- Individual Checks ----------------------------- */

/// Check 1: Persistence mode
CheckStatus checkPersistence(const gpu::GpuDriverStatus& drv) {
  CheckStatus status;
  status.name = "Persistence Mode";

  if (drv.persistenceMode) {
    status.result = CheckResult::PASS;
    status.message = "Persistence mode enabled (reduced initialization latency)";
  } else {
    status.result = CheckResult::WARN;
    status.message = "Persistence mode disabled";
    status.recommendation = "Enable with: nvidia-smi -pm 1";
  }

  return status;
}

/// Check 2: Compute mode
CheckStatus checkComputeMode(const gpu::GpuDriverStatus& drv) {
  CheckStatus status;
  status.name = "Compute Mode";

  switch (drv.computeMode) {
  case gpu::ComputeMode::ExclusiveProcess:
    status.result = CheckResult::PASS;
    status.message = "Exclusive process mode (recommended for RT)";
    break;
  case gpu::ComputeMode::Default:
    status.result = CheckResult::WARN;
    status.message = "Default mode (shared access)";
    status.recommendation = "Set exclusive mode: nvidia-smi -c EXCLUSIVE_PROCESS";
    break;
  case gpu::ComputeMode::Prohibited:
    status.result = CheckResult::FAIL;
    status.message = "CUDA access prohibited";
    status.recommendation = "Allow compute: nvidia-smi -c DEFAULT";
    break;
  default:
    status.result = CheckResult::WARN;
    status.message = fmt::format("{}", gpu::toString(drv.computeMode));
    break;
  }

  return status;
}

/// Check 3: Throttling
CheckStatus checkThrottling(const gpu::GpuTelemetry& telem) {
  CheckStatus status;
  status.name = "Throttling";

  if (!telem.isThrottling()) {
    status.result = CheckResult::PASS;
    status.message = "No throttling detected";
  } else {
    if (telem.throttleReasons.isThermalThrottling()) {
      status.result = CheckResult::FAIL;
      status.message = "Thermal throttling active";
      status.recommendation = "Improve cooling or reduce GPU load";
    } else if (telem.throttleReasons.isPowerThrottling()) {
      status.result = CheckResult::WARN;
      status.message = "Power throttling active";
      status.recommendation = "Increase power limit: nvidia-smi -pl <watts>";
    } else {
      status.result = CheckResult::WARN;
      status.message = fmt::format("Throttling: {}", telem.throttleReasons.toString());
    }
  }

  return status;
}

/// Check 4: ECC memory
CheckStatus checkEcc(const gpu::GpuMemoryStatus& mem) {
  CheckStatus status;
  status.name = "ECC Memory";

  if (!mem.eccSupported) {
    status.result = CheckResult::SKIP;
    status.message = "ECC not supported by hardware";
    return status;
  }

  if (!mem.eccEnabled) {
    status.result = CheckResult::WARN;
    status.message = "ECC disabled";
    status.recommendation = "Enable for mission-critical: nvidia-smi -e 1";
  } else if (mem.eccErrors.hasUncorrected()) {
    status.result = CheckResult::FAIL;
    status.message =
        fmt::format("Uncorrected ECC errors detected ({} volatile, {} aggregate)",
                    mem.eccErrors.uncorrectedVolatile, mem.eccErrors.uncorrectedAggregate);
    status.recommendation = "Hardware may be failing; consider GPU replacement";
  } else {
    status.result = CheckResult::PASS;
    status.message = "ECC enabled, no uncorrected errors";
  }

  return status;
}

/// Check 5: Retired pages
CheckStatus checkRetiredPages(const gpu::GpuMemoryStatus& mem) {
  CheckStatus status;
  status.name = "Retired Pages";

  if (!mem.eccSupported) {
    status.result = CheckResult::SKIP;
    status.message = "ECC not supported (no page retirement tracking)";
    return status;
  }

  if (mem.retiredPages.total() == 0) {
    status.result = CheckResult::PASS;
    status.message = "No retired pages";
  } else if (mem.retiredPages.doubleBitEcc > 0) {
    status.result = CheckResult::FAIL;
    status.message = fmt::format("{} pages retired ({} double-bit ECC)", mem.retiredPages.total(),
                                 mem.retiredPages.doubleBitEcc);
    status.recommendation = "GPU memory may be failing; consider replacement";
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("{} pages retired (single-bit ECC)", mem.retiredPages.total());
    status.recommendation = "Monitor for increasing retired page count";
  }

  if (mem.retiredPages.pendingRetire) {
    status.message += " [retirement pending]";
    status.recommendation = "Reboot required to complete page retirement";
  }

  return status;
}

/// Check 6: Driver/runtime version compatibility
CheckStatus checkDriverVersions(const gpu::GpuDriverStatus& drv) {
  CheckStatus status;
  status.name = "Driver Versions";

  if (drv.cudaDriverVersion == 0 || drv.cudaRuntimeVersion == 0) {
    status.result = CheckResult::SKIP;
    status.message = "Version info not available";
    return status;
  }

  if (drv.versionsCompatible()) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("Driver {} supports runtime {} ",
                                 gpu::GpuDriverStatus::formatCudaVersion(drv.cudaDriverVersion),
                                 gpu::GpuDriverStatus::formatCudaVersion(drv.cudaRuntimeVersion));
  } else {
    status.result = CheckResult::FAIL;
    status.message = fmt::format("Driver {} incompatible with runtime {}",
                                 gpu::GpuDriverStatus::formatCudaVersion(drv.cudaDriverVersion),
                                 gpu::GpuDriverStatus::formatCudaVersion(drv.cudaRuntimeVersion));
    status.recommendation = "Update NVIDIA driver to match or exceed CUDA runtime version";
  }

  return status;
}

/// Check 7: PCIe link
CheckStatus checkPcieLink(const gpu::PcieStatus& pcie) {
  CheckStatus status;
  status.name = "PCIe Link";

  if (pcie.bdf.empty()) {
    status.result = CheckResult::SKIP;
    status.message = "PCIe info not available";
    return status;
  }

  if (pcie.isAtMaxLink()) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("Running at x{} Gen{} (maximum)", pcie.currentWidth,
                                 static_cast<int>(pcie.currentGen));
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("Running at x{} Gen{} (max: x{} Gen{})", pcie.currentWidth,
                                 static_cast<int>(pcie.currentGen), pcie.maxWidth,
                                 static_cast<int>(pcie.maxGen));
    status.recommendation = "Check slot seating and motherboard slot capability";
  }

  return status;
}

/// Check 8: Process isolation
CheckStatus checkIsolation(const gpu::GpuIsolation& iso) {
  CheckStatus status;
  status.name = "Process Isolation";

  const int TOTAL_PROCS = iso.computeProcessCount + iso.graphicsProcessCount;

  if (TOTAL_PROCS == 0) {
    status.result = CheckResult::PASS;
    status.message = "No other processes using GPU";
  } else if (iso.computeMode == gpu::GpuIsolation::ComputeMode::ExclusiveProcess) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("{} process(es) active, exclusive mode enforced", TOTAL_PROCS);
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("{} process(es) active ({} compute, {} graphics)", TOTAL_PROCS,
                                 iso.computeProcessCount, iso.graphicsProcessCount);
    status.recommendation = "Consider exclusive mode or dedicated GPU for RT workloads";
  }

  return status;
}

/// Check 9: Temperature
CheckStatus checkTemperature(const gpu::GpuTelemetry& telem) {
  CheckStatus status;
  status.name = "Temperature";

  if (telem.temperatureC <= 0) {
    status.result = CheckResult::SKIP;
    status.message = "Temperature reading not available";
    return status;
  }

  constexpr int WARN_TEMP = 75;
  constexpr int FAIL_TEMP = 85;

  if (telem.temperatureC < WARN_TEMP) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("{} C (good)", telem.temperatureC);
  } else if (telem.temperatureC < FAIL_TEMP) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("{} C (elevated)", telem.temperatureC);
    status.recommendation = "Monitor cooling; may throttle under sustained load";
  } else {
    status.result = CheckResult::FAIL;
    status.message = fmt::format("{} C (critical)", telem.temperatureC);
    status.recommendation = "Improve cooling immediately";
  }

  return status;
}

/* ----------------------------- Output Functions ----------------------------- */

void printHumanDevice(int deviceIndex, const std::string& name,
                      const std::vector<CheckStatus>& checks) {
  fmt::print("GPU {}: {}\n", deviceIndex, name);
  fmt::print("{}\n", std::string(40, '-'));

  int passCount = 0;
  int warnCount = 0;
  int failCount = 0;

  for (const auto& CHECK : checks) {
    const char* COLOR = resultToColor(CHECK.result);
    const char* RESET = "\033[0m";

    fmt::print("[{}{}{}] {:<18}  {}\n", COLOR, resultToString(CHECK.result), RESET, CHECK.name,
               CHECK.message);

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

  fmt::print("\nResult: {} passed, {} warnings, {} failed\n", passCount, warnCount, failCount);

  if (failCount > 0) {
    fmt::print("\033[31mVerdict: NOT RT-READY\033[0m\n");
  } else if (warnCount > 0) {
    fmt::print("\033[33mVerdict: PARTIAL (review warnings)\033[0m\n");
  } else {
    fmt::print("\033[32mVerdict: RT-READY\033[0m\n");
  }
}

void printJsonDevice(int deviceIndex, const std::string& name,
                     const std::vector<CheckStatus>& checks, bool isFirst) {
  if (!isFirst) {
    fmt::print(",\n");
  }

  fmt::print("    {{\n");
  fmt::print("      \"deviceIndex\": {},\n", deviceIndex);
  fmt::print("      \"name\": \"{}\",\n", name);

  fmt::print("      \"checks\": [\n");
  for (std::size_t i = 0; i < checks.size(); ++i) {
    const auto& CHECK = checks[i];
    fmt::print("        {{\n");
    fmt::print("          \"name\": \"{}\",\n", CHECK.name);
    fmt::print("          \"result\": \"{}\",\n", resultToString(CHECK.result));
    fmt::print("          \"message\": \"{}\",\n", CHECK.message);
    fmt::print("          \"recommendation\": \"{}\"\n", CHECK.recommendation);
    fmt::print("        }}{}\n", (i + 1 < checks.size()) ? "," : "");
  }
  fmt::print("      ],\n");

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

  const char* verdict = "RT_READY";
  if (failCount > 0) {
    verdict = "NOT_RT_READY";
  } else if (warnCount > 0) {
    verdict = "PARTIAL";
  }

  fmt::print("      \"summary\": {{\"pass\": {}, \"warn\": {}, \"fail\": {}}},\n", passCount,
             warnCount, failCount);
  fmt::print("      \"verdict\": \"{}\"\n", verdict);
  fmt::print("    }}");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int targetDevice = -1;

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

    if (pargs.count(ARG_DEVICE) != 0 && !pargs[ARG_DEVICE].empty()) {
      targetDevice = static_cast<int>(std::strtol(pargs[ARG_DEVICE][0].data(), nullptr, 10));
    }
  }

  // Gather data
  const gpu::GpuTopology TOPO = gpu::getGpuTopology();
  const std::vector<gpu::GpuDriverStatus> DRV_LIST = gpu::getAllGpuDriverStatus();
  const std::vector<gpu::GpuTelemetry> TELEM_LIST = gpu::getAllGpuTelemetry();
  const std::vector<gpu::GpuMemoryStatus> MEM_LIST = gpu::getAllGpuMemoryStatus();
  const std::vector<gpu::PcieStatus> PCIE_LIST = gpu::getAllPcieStatus();
  const std::vector<gpu::GpuIsolation> ISO_LIST = gpu::getAllGpuIsolation();

  if (TOPO.deviceCount == 0) {
    if (jsonOutput) {
      fmt::print("{{\"error\": \"No GPUs detected\"}}\n");
    } else {
      fmt::print("No GPUs detected.\n");
    }
    return 1;
  }

  if (jsonOutput) {
    fmt::print("{{\n");
    fmt::print("  \"gpuRtCheck\": [\n");
  }

  int overallExitCode = 0;
  bool firstDevice = true;

  for (const auto& DEV : TOPO.devices) {
    if (targetDevice >= 0 && DEV.deviceIndex != targetDevice) {
      continue;
    }

    // Find matching data
    gpu::GpuDriverStatus drv;
    gpu::GpuTelemetry telem;
    gpu::GpuMemoryStatus mem;
    gpu::PcieStatus pcie;
    gpu::GpuIsolation iso;

    for (const auto& D : DRV_LIST) {
      if (D.deviceIndex == DEV.deviceIndex) {
        drv = D;
        break;
      }
    }
    for (const auto& T : TELEM_LIST) {
      if (T.deviceIndex == DEV.deviceIndex) {
        telem = T;
        break;
      }
    }
    for (const auto& M : MEM_LIST) {
      if (M.deviceIndex == DEV.deviceIndex) {
        mem = M;
        break;
      }
    }
    for (const auto& P : PCIE_LIST) {
      if (P.deviceIndex == DEV.deviceIndex) {
        pcie = P;
        break;
      }
    }
    for (const auto& I : ISO_LIST) {
      if (I.deviceIndex == DEV.deviceIndex) {
        iso = I;
        break;
      }
    }

    // Run checks
    std::vector<CheckStatus> checks;
    checks.push_back(checkPersistence(drv));
    checks.push_back(checkComputeMode(drv));
    checks.push_back(checkTemperature(telem));
    checks.push_back(checkThrottling(telem));
    checks.push_back(checkEcc(mem));
    checks.push_back(checkRetiredPages(mem));
    checks.push_back(checkDriverVersions(drv));
    checks.push_back(checkPcieLink(pcie));
    checks.push_back(checkIsolation(iso));

    // Determine exit code for this device
    for (const auto& CHECK : checks) {
      if (CHECK.result == CheckResult::FAIL) {
        overallExitCode = 2;
      } else if (CHECK.result == CheckResult::WARN && overallExitCode < 1) {
        overallExitCode = 1;
      }
    }

    // Output
    if (jsonOutput) {
      printJsonDevice(DEV.deviceIndex, DEV.name, checks, firstDevice);
    } else {
      if (!firstDevice) {
        fmt::print("\n");
      }
      printHumanDevice(DEV.deviceIndex, DEV.name, checks);
    }

    firstDevice = false;
  }

  if (jsonOutput) {
    fmt::print("\n  ]\n");
    fmt::print("}}\n");
  }

  return overallExitCode;
}
