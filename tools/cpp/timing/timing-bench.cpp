/**
 * @file timing-bench.cpp
 * @brief Sleep jitter and timer latency benchmark.
 *
 * Measures timer overhead and sleep precision with configurable parameters:
 *  - Measurement budget (duration)
 *  - Sleep target duration
 *  - Optional RT priority elevation
 *  - Optional TIMER_ABSTIME mode
 */

#include "src/timing/inc/LatencyBench.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace timing = seeker::timing;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_BUDGET = 2,
  ARG_TARGET = 3,
  ARG_PRIORITY = 4,
  ARG_ABSTIME = 5,
  ARG_QUICK = 6,
  ARG_THOROUGH = 7,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Measure timer overhead and sleep jitter with detailed statistics.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_BUDGET] = {"--budget", 1, false, "Measurement duration in ms (default: 250)"};
  map[ARG_TARGET] = {"--target", 1, false, "Sleep target in us (default: 1000)"};
  map[ARG_PRIORITY] = {"--priority", 1, false, "SCHED_FIFO priority 1-99 (default: none)"};
  map[ARG_ABSTIME] = {"--abstime", 0, false, "Use TIMER_ABSTIME for reduced jitter"};
  map[ARG_QUICK] = {"--quick", 0, false, "Quick measurement preset (250ms budget)"};
  map[ARG_THOROUGH] = {"--thorough", 0, false, "Thorough measurement preset (5s budget)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printHuman(const timing::LatencyStats& stats) {
  fmt::print("=== Latency Benchmark Results ===\n\n");

  // Configuration
  fmt::print("Configuration:\n");
  fmt::print("  Samples:     {}\n", stats.sampleCount);
  fmt::print("  Target:      {:.0f} us\n", stats.targetNs / 1000.0);
  fmt::print("  Mode:        {}\n", stats.usedAbsoluteTime ? "TIMER_ABSTIME" : "sleep_for");
  if (stats.usedRtPriority) {
    fmt::print("  RT Priority: SCHED_FIFO {}\n", stats.rtPriorityUsed);
  } else {
    fmt::print("  RT Priority: none\n");
  }
  fmt::print("  now() overhead: {:.1f} ns\n", stats.nowOverheadNs);

  // Sleep duration statistics
  fmt::print("\nSleep Duration (us):\n");
  fmt::print("  {:>8}  {:>10}  {:>10}  {:>10}\n", "", "Actual", "Jitter", "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "Min", stats.minNs / 1000.0,
             (stats.minNs - stats.targetNs) / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "Mean", stats.meanNs / 1000.0,
             stats.jitterMeanNs() / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "Median", stats.medianNs / 1000.0,
             (stats.medianNs - stats.targetNs) / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "p90", stats.p90Ns / 1000.0,
             (stats.p90Ns - stats.targetNs) / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "p95", stats.p95Ns / 1000.0,
             stats.jitterP95Ns() / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "p99", stats.p99Ns / 1000.0,
             stats.jitterP99Ns() / 1000.0, stats.isGoodForRt() ? "" : "<-- threshold");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "p99.9", stats.p999Ns / 1000.0,
             (stats.p999Ns - stats.targetNs) / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>+10.1f}  {}\n", "Max", stats.maxNs / 1000.0,
             stats.jitterMaxNs() / 1000.0, "");
  fmt::print("  {:>8}  {:>10.1f}  {:>10}  {}\n", "StdDev", stats.stdDevNs / 1000.0, "", "");

  // Assessment
  fmt::print("\nAssessment:\n");
  fmt::print("  RT Score: {}/100", stats.rtScore());
  if (stats.isGoodForRt()) {
    fmt::print(" \033[32m[GOOD]\033[0m\n");
  } else {
    fmt::print(" \033[33m[NEEDS TUNING]\033[0m\n");
  }

  // Recommendations
  if (!stats.isGoodForRt()) {
    fmt::print("\nRecommendations:\n");
    if (stats.jitterP99Ns() > 100'000) {
      fmt::print("  - p99 jitter > 100us: consider RT priority, TIMER_ABSTIME, or isolcpus\n");
    }
    if (!stats.usedRtPriority) {
      fmt::print("  - Try --priority 90 for SCHED_FIFO scheduling\n");
    }
    if (!stats.usedAbsoluteTime) {
      fmt::print("  - Try --abstime for reduced drift\n");
    }
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const timing::LatencyStats& stats) {
  fmt::print("{{\n");

  // Config
  fmt::print("  \"config\": {{\n");
  fmt::print("    \"sampleCount\": {},\n", stats.sampleCount);
  fmt::print("    \"targetNs\": {:.0f},\n", stats.targetNs);
  fmt::print("    \"usedAbsoluteTime\": {},\n", stats.usedAbsoluteTime);
  fmt::print("    \"usedRtPriority\": {},\n", stats.usedRtPriority);
  fmt::print("    \"rtPriorityUsed\": {},\n", stats.rtPriorityUsed);
  fmt::print("    \"nowOverheadNs\": {:.1f}\n", stats.nowOverheadNs);
  fmt::print("  }},\n");

  // Statistics
  fmt::print("  \"statistics\": {{\n");
  fmt::print("    \"minNs\": {:.1f},\n", stats.minNs);
  fmt::print("    \"maxNs\": {:.1f},\n", stats.maxNs);
  fmt::print("    \"meanNs\": {:.1f},\n", stats.meanNs);
  fmt::print("    \"medianNs\": {:.1f},\n", stats.medianNs);
  fmt::print("    \"p90Ns\": {:.1f},\n", stats.p90Ns);
  fmt::print("    \"p95Ns\": {:.1f},\n", stats.p95Ns);
  fmt::print("    \"p99Ns\": {:.1f},\n", stats.p99Ns);
  fmt::print("    \"p999Ns\": {:.1f},\n", stats.p999Ns);
  fmt::print("    \"stdDevNs\": {:.1f}\n", stats.stdDevNs);
  fmt::print("  }},\n");

  // Jitter
  fmt::print("  \"jitter\": {{\n");
  fmt::print("    \"meanNs\": {:.1f},\n", stats.jitterMeanNs());
  fmt::print("    \"p95Ns\": {:.1f},\n", stats.jitterP95Ns());
  fmt::print("    \"p99Ns\": {:.1f},\n", stats.jitterP99Ns());
  fmt::print("    \"maxNs\": {:.1f},\n", stats.jitterMaxNs());
  fmt::print("    \"undershootNs\": {:.1f}\n", stats.undershootNs());
  fmt::print("  }},\n");

  // Assessment
  fmt::print("  \"assessment\": {{\n");
  fmt::print("    \"rtScore\": {},\n", stats.rtScore());
  fmt::print("    \"isGoodForRt\": {}\n", stats.isGoodForRt());
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;

  // Default config
  timing::BenchConfig config;
  config.budget = std::chrono::milliseconds{250};
  config.sleepTarget = std::chrono::microseconds{1000};
  config.useAbsoluteTime = false;
  config.rtPriority = 0;

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

    // Presets
    if (pargs.count(ARG_QUICK) != 0) {
      config = timing::BenchConfig::quick();
    }
    if (pargs.count(ARG_THOROUGH) != 0) {
      config = timing::BenchConfig::thorough();
    }

    // Custom options override presets
    if (pargs.count(ARG_BUDGET) != 0) {
      const long MS = std::strtol(pargs[ARG_BUDGET][0].data(), nullptr, 10);
      if (MS > 0) {
        config.budget = std::chrono::milliseconds{MS};
      }
    }

    if (pargs.count(ARG_TARGET) != 0) {
      const long US = std::strtol(pargs[ARG_TARGET][0].data(), nullptr, 10);
      if (US > 0) {
        config.sleepTarget = std::chrono::microseconds{US};
      }
    }

    if (pargs.count(ARG_PRIORITY) != 0) {
      const int PRIO = static_cast<int>(std::strtol(pargs[ARG_PRIORITY][0].data(), nullptr, 10));
      if (PRIO >= 1 && PRIO <= 99) {
        config.rtPriority = PRIO;
      } else {
        fmt::print(stderr, "Warning: priority must be 1-99, ignoring\n");
      }
    }

    if (pargs.count(ARG_ABSTIME) != 0) {
      config.useAbsoluteTime = true;
    }
  }

  // Print configuration before running
  if (!jsonOutput) {
    fmt::print("Running latency benchmark...\n");
    fmt::print("  Budget: {} ms, Target: {} us", config.budget.count(), config.sleepTarget.count());
    if (config.useAbsoluteTime) {
      fmt::print(", ABSTIME");
    }
    if (config.rtPriority > 0) {
      fmt::print(", SCHED_FIFO {}", config.rtPriority);
    }
    fmt::print("\n\n");
  }

  // Run benchmark
  const timing::LatencyStats STATS = timing::measureLatency(config);

  if (STATS.sampleCount == 0) {
    fmt::print(stderr, "Error: No samples collected\n");
    return 1;
  }

  if (jsonOutput) {
    printJson(STATS);
  } else {
    printHuman(STATS);
  }

  return 0;
}