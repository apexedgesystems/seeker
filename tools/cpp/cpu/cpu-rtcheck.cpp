/**
 * @file cpu-rtcheck.cpp
 * @brief RT readiness validation tool for CPU configuration.
 *
 * Checks isolation, governor, C-states, IRQs, and other RT-critical settings.
 * Outputs pass/warn/fail status for each check with recommendations.
 */

#include "src/cpu/inc/Affinity.hpp"
#include "src/cpu/inc/CpuFeatures.hpp"
#include "src/cpu/inc/CpuFreq.hpp"
#include "src/cpu/inc/CpuIdle.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/cpu/inc/IrqStats.hpp"
#include "src/cpu/inc/SoftirqStats.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdlib>
#include <cstring>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <fmt/core.h>

namespace cpu = seeker::cpu;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_CPUS = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "RT readiness validation for CPU configuration.\n"
    "Checks isolation, governor, C-states, IRQs, and TSC for real-time suitability.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_CPUS] = {"--cpus", 1, false,
                   "CPU list to check (e.g., 2-4,6). Default: isolated CPUs or all"};
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

/* ----------------------------- Individual Checks ----------------------------- */

/// Check 1: CPU isolation configuration
CheckStatus checkIsolation(const cpu::CpuIsolationConfig& config, const cpu::CpuSet& rtCpus) {
  CheckStatus status;
  status.name = "CPU Isolation";

  if (rtCpus.empty()) {
    status.result = CheckResult::WARN;
    status.message = "No isolated CPUs configured";
    status.recommendation =
        "Add isolcpus=<cpulist> nohz_full=<cpulist> rcu_nocbs=<cpulist> to kernel cmdline";
    return status;
  }

  const cpu::IsolationValidation VALIDATION = cpu::validateIsolation(config, rtCpus);

  if (VALIDATION.isValid()) {
    status.result = CheckResult::PASS;
    status.message =
        fmt::format("{} CPU(s) fully isolated (isolcpus + nohz_full + rcu_nocbs)", rtCpus.count());
  } else {
    status.result = CheckResult::WARN;

    std::vector<std::string> issues;
    if (!VALIDATION.missingIsolcpus.empty()) {
      issues.push_back(fmt::format("missing isolcpus: {}", VALIDATION.missingIsolcpus.toString()));
    }
    if (!VALIDATION.missingNohzFull.empty()) {
      issues.push_back(fmt::format("missing nohz_full: {}", VALIDATION.missingNohzFull.toString()));
    }
    if (!VALIDATION.missingRcuNocbs.empty()) {
      issues.push_back(fmt::format("missing rcu_nocbs: {}", VALIDATION.missingRcuNocbs.toString()));
    }

    status.message = "Incomplete isolation: ";
    for (std::size_t i = 0; i < issues.size(); ++i) {
      if (i > 0) {
        status.message += "; ";
      }
      status.message += issues[i];
    }
    status.recommendation = "Ensure all RT CPUs have isolcpus, nohz_full, and rcu_nocbs";
  }

  return status;
}

/// Check 2: CPU governor setting
CheckStatus checkGovernor(const cpu::CpuFrequencySummary& freq, const cpu::CpuSet& rtCpus) {
  CheckStatus status;
  status.name = "CPU Governor";

  if (freq.cores.empty()) {
    status.result = CheckResult::SKIP;
    status.message = "cpufreq not available";
    return status;
  }

  std::vector<int> nonPerformanceCpus;

  for (const auto& CORE : freq.cores) {
    // Check if this CPU is in our RT set (or check all if rtCpus empty)
    if (!rtCpus.empty() && !rtCpus.test(static_cast<std::size_t>(CORE.cpuId))) {
      continue;
    }

    if (std::strcmp(CORE.governor.data(), "performance") != 0) {
      nonPerformanceCpus.push_back(CORE.cpuId);
    }
  }

  if (nonPerformanceCpus.empty()) {
    status.result = CheckResult::PASS;
    status.message = "All checked CPUs using 'performance' governor";
  } else {
    status.result = CheckResult::WARN;
    std::string cpuList;
    for (std::size_t i = 0; i < nonPerformanceCpus.size() && i < 8; ++i) {
      if (i > 0) {
        cpuList += ",";
      }
      cpuList += std::to_string(nonPerformanceCpus[i]);
    }
    if (nonPerformanceCpus.size() > 8) {
      cpuList += ",...";
    }
    status.message =
        fmt::format("{} CPU(s) not using 'performance': [{}]", nonPerformanceCpus.size(), cpuList);
    status.recommendation = "Set governor: cpupower frequency-set -g performance";
  }

  return status;
}

/// Check 3: C-state configuration
CheckStatus checkCStates(const cpu::CpuIdleSnapshot& idle, const cpu::CpuSet& rtCpus) {
  CheckStatus status;
  status.name = "C-State Latency";

  if (idle.cpuCount == 0) {
    status.result = CheckResult::SKIP;
    status.message = "cpuidle not available";
    return status;
  }

  constexpr std::uint32_t MAX_LATENCY_US = 10; // 10us threshold for RT
  std::vector<std::string> highLatencyStates;

  for (std::size_t i = 0; i < idle.cpuCount; ++i) {
    const auto& CPU_IDLE = idle.perCpu[i];

    // Check if this CPU is in our RT set
    if (!rtCpus.empty() && !rtCpus.test(static_cast<std::size_t>(CPU_IDLE.cpuId))) {
      continue;
    }

    for (std::size_t s = 0; s < CPU_IDLE.stateCount; ++s) {
      const auto& STATE = CPU_IDLE.states[s];
      if (!STATE.disabled && STATE.latencyUs > MAX_LATENCY_US) {
        highLatencyStates.push_back(
            fmt::format("cpu{}/{} ({}us)", CPU_IDLE.cpuId, STATE.name.data(), STATE.latencyUs));
      }
    }
  }

  if (highLatencyStates.empty()) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("All enabled C-states have <={}us latency", MAX_LATENCY_US);
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("{} high-latency C-state(s) enabled", highLatencyStates.size());
    if (highLatencyStates.size() <= 4) {
      status.message += ": ";
      for (std::size_t i = 0; i < highLatencyStates.size(); ++i) {
        if (i > 0) {
          status.message += ", ";
        }
        status.message += highLatencyStates[i];
      }
    }
    status.recommendation = "Disable deep C-states: processor.max_cstate=1 intel_idle.max_cstate=0";
  }

  return status;
}

/// Check 4: IRQ distribution on RT cores
CheckStatus checkIrqs(const cpu::IrqSnapshot& irq, const cpu::CpuSet& rtCpus) {
  CheckStatus status;
  status.name = "IRQ Affinity";

  if (irq.lineCount == 0) {
    status.result = CheckResult::SKIP;
    status.message = "IRQ stats not available";
    return status;
  }

  if (rtCpus.empty()) {
    status.result = CheckResult::SKIP;
    status.message = "No RT CPUs specified";
    return status;
  }

  // Count total IRQs on RT cores (excluding timer-related which are expected)
  std::uint64_t rtCoreIrqs = 0;
  std::vector<std::string> topSources;

  for (std::size_t line = 0; line < irq.lineCount; ++line) {
    const auto& IRQ_LINE = irq.lines[line];

    // Skip local timer and similar expected IRQs
    const char* name = IRQ_LINE.name.data();
    if (std::strcmp(name, "LOC") == 0 || std::strcmp(name, "RES") == 0 ||
        std::strcmp(name, "CAL") == 0 || std::strcmp(name, "TLB") == 0) {
      continue;
    }

    std::uint64_t lineRtTotal = 0;
    for (std::size_t cpuIdx = 0; cpuIdx < irq.coreCount && cpuIdx < cpu::IRQ_MAX_CPUS; ++cpuIdx) {
      if (rtCpus.test(cpuIdx)) {
        lineRtTotal += IRQ_LINE.perCore[cpuIdx];
      }
    }

    if (lineRtTotal > 0) {
      rtCoreIrqs += lineRtTotal;
      if (topSources.size() < 3) {
        topSources.push_back(fmt::format("{}:{}", IRQ_LINE.name.data(), lineRtTotal));
      }
    }
  }

  if (rtCoreIrqs == 0) {
    status.result = CheckResult::PASS;
    status.message = "No device IRQs on RT cores";
  } else {
    status.result = CheckResult::WARN;
    status.message = fmt::format("{} device IRQs on RT cores", rtCoreIrqs);
    if (!topSources.empty()) {
      status.message += " (top: ";
      for (std::size_t i = 0; i < topSources.size(); ++i) {
        if (i > 0) {
          status.message += ", ";
        }
        status.message += topSources[i];
      }
      status.message += ")";
    }
    status.recommendation = "Move IRQ affinity: echo <mask> > /proc/irq/<n>/smp_affinity";
  }

  return status;
}

/// Check 5: Softirq activity on RT cores (requires delta)
CheckStatus checkSoftirqs(const cpu::SoftirqDelta& delta, const cpu::CpuSet& rtCpus) {
  CheckStatus status;
  status.name = "Softirq Load";

  if (delta.typeCount == 0) {
    status.result = CheckResult::SKIP;
    status.message = "Softirq stats not available";
    return status;
  }

  if (rtCpus.empty()) {
    status.result = CheckResult::SKIP;
    status.message = "No RT CPUs specified";
    return status;
  }

  // Calculate softirq rate on RT cores
  double maxRtRate = 0.0;
  std::size_t maxRtCpu = 0;

  for (std::size_t cpuIdx = 0; cpuIdx < delta.cpuCount; ++cpuIdx) {
    if (!rtCpus.test(cpuIdx)) {
      continue;
    }
    const double RATE = delta.rateForCpu(cpuIdx);
    if (RATE > maxRtRate) {
      maxRtRate = RATE;
      maxRtCpu = cpuIdx;
    }
  }

  constexpr double WARN_THRESHOLD = 1000.0;  // 1000 softirqs/sec
  constexpr double FAIL_THRESHOLD = 10000.0; // 10000 softirqs/sec

  if (maxRtRate < WARN_THRESHOLD) {
    status.result = CheckResult::PASS;
    status.message = fmt::format("RT core softirq rate < {:.0f}/s (max: {:.0f}/s on cpu{})",
                                 WARN_THRESHOLD, maxRtRate, maxRtCpu);
  } else if (maxRtRate < FAIL_THRESHOLD) {
    status.result = CheckResult::WARN;
    status.message = fmt::format("Elevated softirq rate on cpu{}: {:.0f}/s", maxRtCpu, maxRtRate);
    status.recommendation = "Check network/timer activity; consider RPS/XPS tuning";
  } else {
    status.result = CheckResult::FAIL;
    status.message = fmt::format("High softirq rate on cpu{}: {:.0f}/s", maxRtCpu, maxRtRate);
    status.recommendation = "Investigate softirq source; may need driver tuning";
  }

  return status;
}

/// Check 6: Invariant TSC
CheckStatus checkTsc(const cpu::CpuFeatures& features) {
  CheckStatus status;
  status.name = "Invariant TSC";

  if (features.invariantTsc) {
    status.result = CheckResult::PASS;
    status.message = "Invariant TSC available (reliable high-resolution timing)";
  } else {
    status.result = CheckResult::WARN;
    status.message = "Invariant TSC not available";
    status.recommendation = "TSC-based timing may drift; use HPET or external clock source";
  }

  return status;
}

/* ----------------------------- Output Functions ----------------------------- */

void printHumanOutput(const std::vector<CheckStatus>& checks, const cpu::CpuSet& rtCpus) {
  fmt::print("RT Readiness Check\n");
  fmt::print("==================\n\n");

  if (rtCpus.empty()) {
    fmt::print("Target CPUs: (all)\n\n");
  } else {
    fmt::print("Target CPUs: {}\n\n", rtCpus.toString());
  }

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

void printJsonOutput(const std::vector<CheckStatus>& checks, const cpu::CpuSet& rtCpus) {
  fmt::print("{{\n");

  // Target CPUs
  fmt::print("  \"targetCpus\": [");
  bool first = true;
  for (std::size_t i = 0; i < cpu::MAX_CPUS; ++i) {
    if (rtCpus.test(i)) {
      if (!first) {
        fmt::print(",");
      }
      fmt::print("{}", i);
      first = false;
    }
  }
  fmt::print("],\n");

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
  std::string cpuListArg;

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

    if (pargs.count(ARG_CPUS) != 0 && !pargs.at(ARG_CPUS).empty()) {
      cpuListArg = std::string(pargs.at(ARG_CPUS)[0]);
    }
  }

  const bool JSON_OUTPUT = jsonOutput;

  // Determine target CPUs
  cpu::CpuSet rtCpus;

  if (!cpuListArg.empty()) {
    // User-specified CPU list
    rtCpus = cpu::parseCpuList(cpuListArg.c_str());
  } else {
    // Default: use isolated CPUs if any, otherwise empty (check all)
    const cpu::CpuIsolationConfig CONFIG = cpu::getCpuIsolationConfig();
    rtCpus = CONFIG.getFullyIsolatedCpus();
    if (rtCpus.empty()) {
      // Fall back to isolcpus if no fully isolated
      rtCpus = CONFIG.isolcpus;
    }
  }

  // Collect system state
  const cpu::CpuIsolationConfig ISOLATION = cpu::getCpuIsolationConfig();
  const cpu::CpuFrequencySummary FREQ = cpu::getCpuFrequencySummary();
  const cpu::CpuIdleSnapshot IDLE = cpu::getCpuIdleSnapshot();
  const cpu::CpuFeatures FEATURES = cpu::getCpuFeatures();
  const cpu::IrqSnapshot IRQ = cpu::getIrqSnapshot();

  // Softirq needs delta measurement
  const cpu::SoftirqSnapshot SOFTIRQ_BEFORE = cpu::getSoftirqSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const cpu::SoftirqSnapshot SOFTIRQ_AFTER = cpu::getSoftirqSnapshot();
  const cpu::SoftirqDelta SOFTIRQ_DELTA = cpu::computeSoftirqDelta(SOFTIRQ_BEFORE, SOFTIRQ_AFTER);

  // Run checks
  std::vector<CheckStatus> checks;
  checks.push_back(checkIsolation(ISOLATION, rtCpus));
  checks.push_back(checkGovernor(FREQ, rtCpus));
  checks.push_back(checkCStates(IDLE, rtCpus));
  checks.push_back(checkIrqs(IRQ, rtCpus));
  checks.push_back(checkSoftirqs(SOFTIRQ_DELTA, rtCpus));
  checks.push_back(checkTsc(FEATURES));

  // Output results
  if (JSON_OUTPUT) {
    printJsonOutput(checks, rtCpus);
  } else {
    printHumanOutput(checks, rtCpus);
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