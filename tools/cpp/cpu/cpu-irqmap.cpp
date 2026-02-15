/**
 * @file cpu-irqmap.cpp
 * @brief IRQ and softirq distribution across CPU cores.
 *
 * Displays hardware and software interrupt counts/rates per core, with
 * optional filtering to show top interrupt sources.
 */

#include "src/cpu/inc/IrqStats.hpp"
#include "src/cpu/inc/SoftirqStats.hpp"
#include "src/helpers/inc/Args.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
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
  ARG_TOP = 3,
  ARG_SOFTIRQ = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "IRQ and softirq distribution across CPU cores.\n"
    "Shows interrupt counts and rates with optional filtering.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_INTERVAL] = {"--interval", 1, false, "Measurement interval in ms (default: 1000)"};
  map[ARG_TOP] = {"--top", 1, false, "Show top N interrupt sources (default: all)"};
  map[ARG_SOFTIRQ] = {"--softirq", 0, false, "Include softirq breakdown"};
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

/* ----------------------------- Human Output ----------------------------- */

void printHumanIrq(const cpu::IrqDelta& delta, int topN) {
  fmt::print("=== Hardware Interrupts ===\n\n");

  // Calculate per-core totals
  fmt::print("Per-core IRQ rates (IRQs/sec):\n");
  for (std::size_t c = 0; c < delta.coreCount && c < 16; ++c) {
    fmt::print("  CPU {:2}: {:>8.1f}\n", c, delta.rateForCore(c));
  }
  if (delta.coreCount > 16) {
    fmt::print("  ... and {} more cores\n", delta.coreCount - 16);
  }
  fmt::print("\n");

  // Build sorted list of IRQs by total delta
  std::vector<std::pair<std::size_t, std::uint64_t>> irqRanked;
  for (std::size_t i = 0; i < delta.lineCount; ++i) {
    if (delta.lineTotals[i] > 0) {
      irqRanked.emplace_back(i, delta.lineTotals[i]);
    }
  }

  std::sort(irqRanked.begin(), irqRanked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Apply top filter
  std::size_t showCount = irqRanked.size();
  if (topN > 0 && static_cast<std::size_t>(topN) < showCount) {
    showCount = static_cast<std::size_t>(topN);
  }

  if (showCount > 0) {
    fmt::print("Top {} active IRQ sources:\n", showCount);
    fmt::print("{:>8}  {:>10}  {:>10}  {}\n", "IRQ", "Count", "Rate/s", "Distribution");
    fmt::print("{}\n", std::string(60, '-'));

    const double INTERVAL_SEC =
        (delta.intervalNs > 0) ? static_cast<double>(delta.intervalNs) / 1e9 : 1.0;

    for (std::size_t rank = 0; rank < showCount; ++rank) {
      const std::size_t IDX = irqRanked[rank].first;
      const std::uint64_t TOTAL = irqRanked[rank].second;
      const double RATE = TOTAL / INTERVAL_SEC;

      // Build distribution string showing top 4 cores
      std::vector<std::pair<std::size_t, std::uint64_t>> coreRanked;
      for (std::size_t c = 0; c < delta.coreCount; ++c) {
        if (delta.perCoreDelta[IDX][c] > 0) {
          coreRanked.emplace_back(c, delta.perCoreDelta[IDX][c]);
        }
      }
      std::sort(coreRanked.begin(), coreRanked.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });

      std::string dist;
      for (std::size_t i = 0; i < coreRanked.size() && i < 4; ++i) {
        if (i > 0) {
          dist += " ";
        }
        dist += fmt::format("cpu{}:{}", coreRanked[i].first, coreRanked[i].second);
      }
      if (coreRanked.size() > 4) {
        dist += " ...";
      }

      fmt::print("{:>8}  {:>10}  {:>10.1f}  {}\n", delta.names[IDX].data(), TOTAL, RATE, dist);
    }
  } else {
    fmt::print("No hardware interrupts during measurement interval.\n");
  }
}

void printHumanSoftirq(const cpu::SoftirqDelta& delta) {
  fmt::print("\n=== Software Interrupts ===\n\n");

  const double INTERVAL_SEC =
      (delta.intervalNs > 0) ? static_cast<double>(delta.intervalNs) / 1e9 : 1.0;

  // Per-core totals
  fmt::print("Per-core softirq rates (softirqs/sec):\n");
  for (std::size_t c = 0; c < delta.cpuCount && c < 16; ++c) {
    fmt::print("  CPU {:2}: {:>8.1f}\n", c, delta.rateForCpu(c));
  }
  if (delta.cpuCount > 16) {
    fmt::print("  ... and {} more cores\n", delta.cpuCount - 16);
  }
  fmt::print("\n");

  // Per-type breakdown
  fmt::print("Softirq type breakdown:\n");
  fmt::print("{:>10}  {:>10}  {:>10}\n", "Type", "Count", "Rate/s");
  fmt::print("{}\n", std::string(35, '-'));

  for (std::size_t t = 0; t < delta.typeCount; ++t) {
    if (delta.typeTotals[t] > 0) {
      const double RATE = static_cast<double>(delta.typeTotals[t]) / INTERVAL_SEC;
      fmt::print("{:>10}  {:>10}  {:>10.1f}\n", delta.names[t].data(), delta.typeTotals[t], RATE);
    }
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJsonOutput(const cpu::IrqDelta& irqDelta, const cpu::SoftirqDelta* softirqDelta,
                     int topN) {
  fmt::print("{{\n");
  fmt::print("  \"intervalMs\": {},\n", irqDelta.intervalNs / 1000000);

  // Hardware IRQs
  fmt::print("  \"hardwareIrq\": {{\n");

  // Per-core rates
  fmt::print("    \"perCoreRates\": [");
  for (std::size_t c = 0; c < irqDelta.coreCount; ++c) {
    if (c > 0) {
      fmt::print(", ");
    }
    fmt::print("{:.2f}", irqDelta.rateForCore(c));
  }
  fmt::print("],\n");

  // Top sources
  std::vector<std::pair<std::size_t, std::uint64_t>> irqRanked;
  for (std::size_t i = 0; i < irqDelta.lineCount; ++i) {
    if (irqDelta.lineTotals[i] > 0) {
      irqRanked.emplace_back(i, irqDelta.lineTotals[i]);
    }
  }
  std::sort(irqRanked.begin(), irqRanked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::size_t showCount = irqRanked.size();
  if (topN > 0 && static_cast<std::size_t>(topN) < showCount) {
    showCount = static_cast<std::size_t>(topN);
  }

  const double IRQ_INTERVAL_SEC =
      (irqDelta.intervalNs > 0) ? static_cast<double>(irqDelta.intervalNs) / 1e9 : 1.0;

  fmt::print("    \"sources\": [\n");
  for (std::size_t rank = 0; rank < showCount; ++rank) {
    const std::size_t IDX = irqRanked[rank].first;
    const std::uint64_t TOTAL = irqRanked[rank].second;
    fmt::print("      {{\"name\": \"{}\", \"count\": {}, \"rate\": {:.2f}}}",
               irqDelta.names[IDX].data(), TOTAL, TOTAL / IRQ_INTERVAL_SEC);
    if (rank + 1 < showCount) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("    ]\n");
  fmt::print("  }}");

  // Softirq (optional)
  if (softirqDelta != nullptr) {
    fmt::print(",\n");
    fmt::print("  \"softirq\": {{\n");

    // Per-core rates
    fmt::print("    \"perCoreRates\": [");
    for (std::size_t c = 0; c < softirqDelta->cpuCount; ++c) {
      if (c > 0) {
        fmt::print(", ");
      }
      fmt::print("{:.2f}", softirqDelta->rateForCpu(c));
    }
    fmt::print("],\n");

    // Per-type breakdown
    const double SOFT_INTERVAL_SEC =
        (softirqDelta->intervalNs > 0) ? static_cast<double>(softirqDelta->intervalNs) / 1e9 : 1.0;

    fmt::print("    \"types\": [\n");
    bool first = true;
    for (std::size_t t = 0; t < softirqDelta->typeCount; ++t) {
      if (softirqDelta->typeTotals[t] > 0) {
        if (!first) {
          fmt::print(",\n");
        }
        first = false;
        const double RATE = static_cast<double>(softirqDelta->typeTotals[t]) / SOFT_INTERVAL_SEC;
        fmt::print("      {{\"name\": \"{}\", \"count\": {}, \"rate\": {:.2f}}}",
                   softirqDelta->names[t].data(), softirqDelta->typeTotals[t], RATE);
      }
    }
    fmt::print("\n    ]\n");
    fmt::print("  }}\n");
  } else {
    fmt::print("\n");
  }

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int intervalMs = 1000;
  int topN = -1;
  bool showSoftirq = false;

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
    topN = parseIntArg(pargs, ARG_TOP, -1);
    showSoftirq = (pargs.count(ARG_SOFTIRQ) != 0);
  }

  // Clamp interval
  if (intervalMs < 100) {
    intervalMs = 100;
  }
  if (intervalMs > 60000) {
    intervalMs = 60000;
  }

  // Take before snapshots
  const cpu::IrqSnapshot IRQ_BEFORE = cpu::getIrqSnapshot();
  cpu::SoftirqSnapshot softirqBefore;
  if (showSoftirq) {
    softirqBefore = cpu::getSoftirqSnapshot();
  }

  // Wait for measurement interval
  std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

  // Take after snapshots and compute deltas
  const cpu::IrqSnapshot IRQ_AFTER = cpu::getIrqSnapshot();
  const cpu::IrqDelta IRQ_DELTA = cpu::computeIrqDelta(IRQ_BEFORE, IRQ_AFTER);

  cpu::SoftirqDelta softirqDelta;
  if (showSoftirq) {
    const cpu::SoftirqSnapshot SOFTIRQ_AFTER = cpu::getSoftirqSnapshot();
    softirqDelta = cpu::computeSoftirqDelta(softirqBefore, SOFTIRQ_AFTER);
  }

  // Output results
  if (jsonOutput) {
    printJsonOutput(IRQ_DELTA, showSoftirq ? &softirqDelta : nullptr, topN);
  } else {
    fmt::print("IRQ Distribution ({}ms sample)\n", intervalMs);
    fmt::print("==============================\n\n");

    printHumanIrq(IRQ_DELTA, topN);

    if (showSoftirq) {
      printHumanSoftirq(softirqDelta);
    }
  }

  return 0;
}