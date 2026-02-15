/**
 * @file timing-rtcheck.cpp
 * @brief RT timing configuration validation with pass/warn/fail checks.
 *
 * Validates timing settings for real-time suitability:
 *  - Clocksource (TSC preferred)
 *  - High-resolution timers
 *  - Timer slack
 *  - Tickless (NO_HZ) configuration
 *  - RTC drift (optional)
 */

#include "src/timing/inc/ClockSource.hpp"
#include "src/timing/inc/RtcStatus.hpp"
#include "src/timing/inc/TimerConfig.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace timing = seeker::timing;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_VERBOSE = 2,
  ARG_RTC = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION = "Validate timing configuration for real-time suitability.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show detailed recommendations"};
  map[ARG_RTC] = {"--rtc", 0, false, "Include RTC drift validation"};
  return map;
}

/// Check result.
enum class CheckResult { PASS, WARN, FAIL };

/// Individual check outcome.
struct Check {
  const char* name;
  CheckResult result;
  const char* value;
  const char* recommendation;
};

/* ----------------------------- Checks ----------------------------- */

Check checkClockSource(const timing::ClockSource& cs) {
  Check c{"Clocksource", CheckResult::WARN, cs.current.data(), ""};

  if (cs.isTsc()) {
    c.result = CheckResult::PASS;
    c.recommendation = "TSC is optimal for RT";
  } else if (cs.isHpet()) {
    c.result = CheckResult::WARN;
    c.recommendation = "HPET has higher latency than TSC; check if TSC is available";
  } else if (cs.isAcpiPm()) {
    c.result = CheckResult::WARN;
    c.recommendation = "acpi_pm is slow; strongly prefer TSC";
  } else {
    c.result = CheckResult::WARN;
    c.recommendation = "Unknown clocksource; verify RT behavior";
  }

  return c;
}

Check checkHighResTimers(const timing::ClockSource& cs) {
  Check c{"High-Res Timers", CheckResult::FAIL, "", ""};

  if (cs.monotonic.available && cs.monotonic.isHighRes()) {
    c.result = CheckResult::PASS;
    c.value = "enabled";
    c.recommendation = "High-res timers active";
  } else if (cs.monotonic.available) {
    c.result = CheckResult::FAIL;
    c.value = "disabled";
    c.recommendation = "Enable CONFIG_HIGH_RES_TIMERS in kernel";
  } else {
    c.result = CheckResult::FAIL;
    c.value = "unavailable";
    c.recommendation = "CLOCK_MONOTONIC not available";
  }

  return c;
}

Check checkTimerSlack(const timing::TimerConfig& cfg, bool verbose) {
  static std::string valueStr;
  Check c{"Timer Slack", CheckResult::WARN, "", ""};

  if (!cfg.slackQuerySucceeded) {
    c.result = CheckResult::WARN;
    c.value = "unknown";
    c.recommendation = "Could not query timer_slack";
    return c;
  }

  if (cfg.timerSlackNs < 1000) {
    valueStr = fmt::format("{} ns", cfg.timerSlackNs);
  } else {
    valueStr = fmt::format("{:.0f} us", static_cast<double>(cfg.timerSlackNs) / 1000.0);
  }
  c.value = valueStr.c_str();

  if (cfg.hasMinimalSlack()) {
    c.result = CheckResult::PASS;
    c.recommendation = "Minimal slack configured";
  } else if (cfg.hasDefaultSlack()) {
    c.result = CheckResult::WARN;
    c.recommendation = verbose
                           ? "Default slack (~50us) adds jitter; call prctl(PR_SET_TIMERSLACK, 1)"
                           : "Call prctl(PR_SET_TIMERSLACK, 1) for minimal jitter";
  } else {
    c.result = CheckResult::WARN;
    c.recommendation = "Non-standard slack value";
  }

  return c;
}

Check checkNohzFull(const timing::TimerConfig& cfg) {
  static std::string valueStr;
  Check c{"NO_HZ Full", CheckResult::WARN, "", ""};

  if (cfg.nohzFullCount > 0) {
    valueStr = fmt::format("{} CPUs", cfg.nohzFullCount);
    c.value = valueStr.c_str();
    c.result = CheckResult::PASS;
    c.recommendation = "Tickless CPUs configured for RT";
  } else {
    c.value = "none";
    c.result = CheckResult::WARN;
    c.recommendation = "Add nohz_full= kernel parameter for dedicated RT cores";
  }

  return c;
}

Check checkPreemptRt(const timing::TimerConfig& cfg) {
  Check c{"PREEMPT_RT", CheckResult::WARN, "", ""};

  if (cfg.preemptRtEnabled) {
    c.result = CheckResult::PASS;
    c.value = "yes";
    c.recommendation = "Running PREEMPT_RT kernel";
  } else {
    c.value = "no";
    c.result = CheckResult::WARN;
    c.recommendation = "Consider PREEMPT_RT kernel for hard RT requirements";
  }

  return c;
}

Check checkRtcDrift(const timing::RtcStatus& rtc) {
  static std::string valueStr;
  Check c{"RTC Drift", CheckResult::WARN, "", ""};

  if (!rtc.rtcSupported) {
    c.value = "no RTC";
    c.result = CheckResult::WARN;
    c.recommendation = "No hardware RTC detected";
    return c;
  }

  if (rtc.deviceCount == 0) {
    c.value = "no devices";
    c.result = CheckResult::WARN;
    c.recommendation = "No RTC devices found";
    return c;
  }

  // Check the system RTC or first available
  const timing::RtcDevice* sysRtc = rtc.getSystemRtc();
  if (sysRtc == nullptr && rtc.deviceCount > 0) {
    sysRtc = &rtc.devices[0];
  }

  if (sysRtc == nullptr) {
    c.value = "unknown";
    c.result = CheckResult::WARN;
    c.recommendation = "Could not identify system RTC";
    return c;
  }

  if (!sysRtc->time.querySucceeded) {
    c.value = "unreadable";
    c.result = CheckResult::WARN;
    c.recommendation = "Could not read RTC time";
    return c;
  }

  if (!sysRtc->time.isValid()) {
    c.value = "invalid";
    c.result = CheckResult::WARN;
    c.recommendation = "RTC time appears invalid; check battery or set time";
    return c;
  }

  const std::int64_t DRIFT = sysRtc->time.absDrift();
  valueStr = fmt::format("{} sec", DRIFT);
  c.value = valueStr.c_str();

  if (sysRtc->time.isDriftAcceptable()) {
    c.result = CheckResult::PASS;
    c.recommendation = "RTC within acceptable drift";
  } else {
    c.result = CheckResult::WARN;
    c.recommendation = "RTC significantly drifted; run hwclock --systohc to sync";
  }

  return c;
}

/* ----------------------------- Human Output ----------------------------- */

const char* resultStr(CheckResult r) {
  switch (r) {
  case CheckResult::PASS:
    return "PASS";
  case CheckResult::WARN:
    return "WARN";
  case CheckResult::FAIL:
    return "FAIL";
  }
  return "????";
}

const char* resultColor(CheckResult r) {
  switch (r) {
  case CheckResult::PASS:
    return "\033[32m"; // green
  case CheckResult::WARN:
    return "\033[33m"; // yellow
  case CheckResult::FAIL:
    return "\033[31m"; // red
  }
  return "";
}

void printHuman(const std::vector<Check>& checks, bool verbose) {
  constexpr const char* RESET = "\033[0m";

  fmt::print("=== Timing RT Validation ===\n\n");

  int passes = 0;
  int warnings = 0;
  int failures = 0;

  for (const auto& C : checks) {
    fmt::print("  {}{:4}{} {:20} {}\n", resultColor(C.result), resultStr(C.result), RESET, C.name,
               C.value);

    if (verbose && C.recommendation[0] != '\0') {
      fmt::print("       -> {}\n", C.recommendation);
    }

    switch (C.result) {
    case CheckResult::PASS:
      ++passes;
      break;
    case CheckResult::WARN:
      ++warnings;
      break;
    case CheckResult::FAIL:
      ++failures;
      break;
    }
  }

  fmt::print("\n=== Summary ===\n");
  fmt::print("  {}PASS{}: {}  {}WARN{}: {}  {}FAIL{}: {}\n", resultColor(CheckResult::PASS), RESET,
             passes, resultColor(CheckResult::WARN), RESET, warnings,
             resultColor(CheckResult::FAIL), RESET, failures);

  if (failures > 0) {
    fmt::print("\n  Status: {}FAIL{} - Critical issues found\n", resultColor(CheckResult::FAIL),
               RESET);
  } else if (warnings > 0) {
    fmt::print("\n  Status: {}WARN{} - Improvements recommended\n", resultColor(CheckResult::WARN),
               RESET);
  } else {
    fmt::print("\n  Status: {}PASS{} - Timing configuration looks good for RT\n",
               resultColor(CheckResult::PASS), RESET);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

const char* resultJsonStr(CheckResult r) {
  switch (r) {
  case CheckResult::PASS:
    return "pass";
  case CheckResult::WARN:
    return "warn";
  case CheckResult::FAIL:
    return "fail";
  }
  return "unknown";
}

void printJson(const std::vector<Check>& checks) {
  int passes = 0;
  int warnings = 0;
  int failures = 0;

  for (const auto& C : checks) {
    switch (C.result) {
    case CheckResult::PASS:
      ++passes;
      break;
    case CheckResult::WARN:
      ++warnings;
      break;
    case CheckResult::FAIL:
      ++failures;
      break;
    }
  }

  fmt::print("{{\n");
  fmt::print("  \"checks\": [\n");

  for (std::size_t i = 0; i < checks.size(); ++i) {
    const auto& C = checks[i];
    fmt::print("    {{\"name\": \"{}\", \"result\": \"{}\", \"value\": \"{}\", \"recommendation\": "
               "\"{}\"}}",
               C.name, resultJsonStr(C.result), C.value, C.recommendation);
    if (i + 1 < checks.size()) {
      fmt::print(",");
    }
    fmt::print("\n");
  }

  fmt::print("  ],\n");
  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"pass\": {},\n", passes);
  fmt::print("    \"warn\": {},\n", warnings);
  fmt::print("    \"fail\": {},\n", failures);
  fmt::print("    \"overall\": \"{}\"\n", failures > 0 ? "fail" : (warnings > 0 ? "warn" : "pass"));
  fmt::print("  }}\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool verbose = false;
  bool checkRtc = false;

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
    verbose = (pargs.count(ARG_VERBOSE) != 0);
    checkRtc = (pargs.count(ARG_RTC) != 0);
  }

  // Gather data
  const timing::ClockSource CS = timing::getClockSource();
  const timing::TimerConfig CFG = timing::getTimerConfig();

  // Run checks
  std::vector<Check> checks;
  checks.push_back(checkClockSource(CS));
  checks.push_back(checkHighResTimers(CS));
  checks.push_back(checkTimerSlack(CFG, verbose));
  checks.push_back(checkNohzFull(CFG));
  checks.push_back(checkPreemptRt(CFG));

  // Optionally add RTC drift check
  if (checkRtc) {
    const timing::RtcStatus RTC = timing::getRtcStatus();
    checks.push_back(checkRtcDrift(RTC));
  }

  if (jsonOutput) {
    printJson(checks);
  } else {
    printHuman(checks, verbose);
  }

  // Return non-zero if any failures
  for (const auto& C : checks) {
    if (C.result == CheckResult::FAIL) {
      return 1;
    }
  }

  return 0;
}