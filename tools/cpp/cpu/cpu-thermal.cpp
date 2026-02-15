/**
 * @file cpu-thermal.cpp
 * @brief CPU temperature, throttling, and power limit status.
 *
 * Displays sensor temperatures, RAPL power limits, and throttling indicators.
 * Supports continuous watch mode for monitoring thermal behavior.
 */

#include "src/cpu/inc/ThermalStatus.hpp"
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
  ARG_WATCH = 2,
  ARG_INTERVAL = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "CPU thermal, throttling, and power status.\n"
    "Displays temperatures, RAPL power limits, and throttle indicators.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_WATCH] = {"--watch", 0, false, "Continuous monitoring mode"};
  map[ARG_INTERVAL] = {"--interval", 1, false, "Watch interval in ms (default: 2000)"};
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

void printHumanOutput(const cpu::ThermalStatus& status, bool showHeader) {
  if (showHeader) {
    fmt::print("Thermal Status\n");
    fmt::print("==============\n\n");
  }

  // Throttling status (most important - show first)
  fmt::print("=== Throttling ===\n");
  if (status.throttling.thermal || status.throttling.powerLimit || status.throttling.current) {
    if (status.throttling.thermal) {
      fmt::print("  \033[31mTHERMAL THROTTLING ACTIVE\033[0m\n");
    }
    if (status.throttling.powerLimit) {
      fmt::print("  \033[33mPower limit throttling active\033[0m\n");
    }
    if (status.throttling.current) {
      fmt::print("  \033[33mCurrent limit throttling active\033[0m\n");
    }
  } else {
    fmt::print("  \033[32mNo throttling detected\033[0m\n");
  }

  // Temperature sensors
  fmt::print("\n=== Temperatures ===\n");
  if (status.sensors.empty()) {
    fmt::print("  (no temperature sensors detected)\n");
  } else {
    // Find max temperature for highlighting
    double maxTemp = 0.0;
    for (const auto& SENSOR : status.sensors) {
      if (SENSOR.tempCelsius > maxTemp) {
        maxTemp = SENSOR.tempCelsius;
      }
    }

    for (const auto& SENSOR : status.sensors) {
      const char* color = "\033[0m"; // Default
      if (SENSOR.tempCelsius >= 90.0) {
        color = "\033[31m"; // Red: critical
      } else if (SENSOR.tempCelsius >= 80.0) {
        color = "\033[33m"; // Yellow: warning
      } else if (SENSOR.tempCelsius >= 70.0) {
        color = "\033[0m"; // Normal
      } else {
        color = "\033[32m"; // Green: cool
      }

      fmt::print("  {:<24} {}{}{}C\n", SENSOR.name.data(), color, SENSOR.tempCelsius, "\033[0m");
    }
  }

  // Power limits
  fmt::print("\n=== Power Limits ===\n");
  if (status.powerLimits.empty()) {
    fmt::print("  (no RAPL power limits detected)\n");
  } else {
    for (const auto& LIMIT : status.powerLimits) {
      fmt::print("  {:<24} {:.1f}W {}\n", LIMIT.domain.data(), LIMIT.watts,
                 LIMIT.enforced ? "(enforced)" : "");
    }
  }

  fmt::print("\n");
}

void printJsonOutput(const cpu::ThermalStatus& status) {
  fmt::print("{{\n");

  // Throttling
  fmt::print("  \"throttling\": {{\n");
  fmt::print("    \"thermal\": {},\n", status.throttling.thermal);
  fmt::print("    \"powerLimit\": {},\n", status.throttling.powerLimit);
  fmt::print("    \"current\": {}\n", status.throttling.current);
  fmt::print("  }},\n");

  // Sensors
  fmt::print("  \"sensors\": [\n");
  for (std::size_t i = 0; i < status.sensors.size(); ++i) {
    const auto& SENSOR = status.sensors[i];
    fmt::print("    {{\"name\": \"{}\", \"tempCelsius\": {:.1f}}}", SENSOR.name.data(),
               SENSOR.tempCelsius);
    if (i + 1 < status.sensors.size()) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("  ],\n");

  // Power limits
  fmt::print("  \"powerLimits\": [\n");
  for (std::size_t i = 0; i < status.powerLimits.size(); ++i) {
    const auto& LIMIT = status.powerLimits[i];
    fmt::print("    {{\"domain\": \"{}\", \"watts\": {:.1f}, \"enforced\": {}}}",
               LIMIT.domain.data(), LIMIT.watts, LIMIT.enforced);
    if (i + 1 < status.powerLimits.size()) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("  ]\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool watchMode = false;
  int intervalMs = 2000;

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
    watchMode = (pargs.count(ARG_WATCH) != 0);
    intervalMs = parseIntArg(pargs, ARG_INTERVAL, 2000);
  }

  // Clamp interval
  if (intervalMs < 500) {
    intervalMs = 500;
  }
  if (intervalMs > 60000) {
    intervalMs = 60000;
  }

  if (watchMode) {
    // Continuous monitoring
    bool firstIteration = true;
    while (true) {
      const cpu::ThermalStatus STATUS = cpu::getThermalStatus();

      if (jsonOutput) {
        printJsonOutput(STATUS);
      } else {
        // Clear screen for watch mode (ANSI escape)
        if (!firstIteration) {
          fmt::print("\033[2J\033[H");
        }
        printHumanOutput(STATUS, true);
        fmt::print("(refreshing every {}ms, Ctrl+C to exit)\n", intervalMs);
      }

      firstIteration = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
  } else {
    // Single shot
    const cpu::ThermalStatus STATUS = cpu::getThermalStatus();

    if (jsonOutput) {
      printJsonOutput(STATUS);
    } else {
      printHumanOutput(STATUS, true);
    }
  }

  return 0;
}