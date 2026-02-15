/**
 * @file net-stat.cpp
 * @brief Continuous per-interface network statistics monitoring.
 *
 * Displays real-time throughput, packet rates, and error/drop rates
 * for network interfaces. Similar to 'sar -n DEV' but focused on
 * RT-relevant metrics.
 */

#include "src/network/inc/InterfaceInfo.hpp"
#include "src/network/inc/InterfaceStats.hpp"
#include "src/helpers/inc/Args.hpp"

#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/core.h>

namespace net = seeker::network;

namespace {

/* ----------------------------- Signal Handling ----------------------------- */

volatile std::sig_atomic_t g_running = 1;

void signalHandler(int /*signum*/) { g_running = 0; }

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_INTERVAL = 2,
  ARG_COUNT = 3,
  ARG_INTERFACE = 4,
  ARG_PHYSICAL = 5,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION = "Display continuous per-interface network statistics.\n\n"
                                         "Press Ctrl+C to stop.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_INTERVAL] = {"--interval", 1, false, "Sample interval in milliseconds (default: 1000)"};
  map[ARG_COUNT] = {"--count", 1, false, "Number of samples (default: unlimited)"};
  map[ARG_INTERFACE] = {"--interface", 1, false, "Monitor specific interface only"};
  map[ARG_PHYSICAL] = {"--physical", 0, false, "Show only physical interfaces"};
  return map;
}

/* ----------------------------- Formatting ----------------------------- */

/// Format throughput for display.
std::string formatRate(double mbps) {
  if (mbps >= 1000.0) {
    return fmt::format("{:6.2f}G", mbps / 1000.0);
  }
  if (mbps >= 1.0) {
    return fmt::format("{:6.2f}M", mbps);
  }
  if (mbps >= 0.001) {
    return fmt::format("{:6.2f}K", mbps * 1000.0);
  }
  return fmt::format("{:6.0f} ", mbps * 1000000.0);
}

/// Format packet rate for display.
std::string formatPps(double pps) {
  if (pps >= 1000000.0) {
    return fmt::format("{:6.2f}M", pps / 1000000.0);
  }
  if (pps >= 1000.0) {
    return fmt::format("{:6.2f}K", pps / 1000.0);
  }
  return fmt::format("{:6.0f} ", pps);
}

/* ----------------------------- Human Output ----------------------------- */

void printHeader() {
  fmt::print("{:<12} {:>8} {:>8} {:>8} {:>8} {:>7} {:>7}\n", "Interface", "RX Mbps", "TX Mbps",
             "RX pps", "TX pps", "Drops", "Errors");
  fmt::print("{:-<12} {:->8} {:->8} {:->8} {:->8} {:->7} {:->7}\n", "", "", "", "", "", "", "");
}

void printStats(const net::InterfaceStatsDelta& delta, const net::InterfaceList& interfaces,
                bool physicalOnly) {
  for (std::size_t i = 0; i < delta.count; ++i) {
    const net::InterfaceRates& r = delta.interfaces[i];

    // Filter physical only if requested
    if (physicalOnly) {
      const net::InterfaceInfo* info = interfaces.find(r.ifname.data());
      if (info == nullptr || !info->isPhysical()) {
        continue;
      }
    }

    // Calculate totals
    const double DROPS = r.rxDroppedPerSec + r.txDroppedPerSec;
    const double ERRORS = r.rxErrorsPerSec + r.txErrorsPerSec;

    fmt::print("{:<12} {:>8} {:>8} {:>8} {:>8} {:>7} {:>7}\n", r.ifname.data(),
               formatRate(r.rxMbps()), formatRate(r.txMbps()), formatPps(r.rxPacketsPerSec),
               formatPps(r.txPacketsPerSec), formatPps(DROPS), formatPps(ERRORS));
  }
}

void printSeparator() { fmt::print("\n"); }

/* ----------------------------- JSON Output ----------------------------- */

void printJsonSample(const net::InterfaceStatsDelta& delta, const net::InterfaceList& interfaces,
                     bool physicalOnly, std::size_t sampleNum) {
  if (sampleNum > 0) {
    fmt::print(",\n");
  }

  fmt::print("  {{\n");
  fmt::print("    \"sample\": {},\n", sampleNum);
  fmt::print("    \"durationSec\": {:.6f},\n", delta.durationSec);
  fmt::print("    \"interfaces\": [\n");

  bool first = true;
  for (std::size_t i = 0; i < delta.count; ++i) {
    const net::InterfaceRates& r = delta.interfaces[i];

    // Filter physical only if requested
    if (physicalOnly) {
      const net::InterfaceInfo* info = interfaces.find(r.ifname.data());
      if (info == nullptr || !info->isPhysical()) {
        continue;
      }
    }

    if (!first) {
      fmt::print(",\n");
    }
    first = false;

    fmt::print("      {{\n");
    fmt::print("        \"name\": \"{}\",\n", r.ifname.data());
    fmt::print("        \"rxMbps\": {:.6f},\n", r.rxMbps());
    fmt::print("        \"txMbps\": {:.6f},\n", r.txMbps());
    fmt::print("        \"rxPps\": {:.2f},\n", r.rxPacketsPerSec);
    fmt::print("        \"txPps\": {:.2f},\n", r.txPacketsPerSec);
    fmt::print("        \"rxDropsPerSec\": {:.2f},\n", r.rxDroppedPerSec);
    fmt::print("        \"txDropsPerSec\": {:.2f},\n", r.txDroppedPerSec);
    fmt::print("        \"rxErrorsPerSec\": {:.2f},\n", r.rxErrorsPerSec);
    fmt::print("        \"txErrorsPerSec\": {:.2f}\n", r.txErrorsPerSec);
    fmt::print("      }}");
  }

  fmt::print("\n    ]\n");
  fmt::print("  }}");
}

/* ----------------------------- Main Loop ----------------------------- */

int runMonitor(int intervalMs, int maxCount, const char* interfaceName, bool physicalOnly,
               bool jsonOutput) {

  // Get interface list for filtering
  const net::InterfaceList INTERFACES = net::getAllInterfaces();

  // Initial snapshot
  net::InterfaceStatsSnapshot prevSnap;
  if (interfaceName != nullptr) {
    prevSnap = net::getInterfaceStatsSnapshot(interfaceName);
    if (prevSnap.count == 0) {
      fmt::print(stderr, "Error: Interface '{}' not found\n", interfaceName);
      return 1;
    }
  } else {
    prevSnap = net::getInterfaceStatsSnapshot();
  }

  // JSON preamble
  if (jsonOutput) {
    fmt::print("{{\n");
    fmt::print("  \"intervalMs\": {},\n", intervalMs);
    if (interfaceName != nullptr) {
      fmt::print("  \"interface\": \"{}\",\n", interfaceName);
    }
    fmt::print("  \"samples\": [\n");
  } else {
    if (interfaceName != nullptr) {
      fmt::print("Monitoring interface: {}\n", interfaceName);
    }
    fmt::print("Interval: {} ms\n\n", intervalMs);
    printHeader();
  }

  const auto INTERVAL = std::chrono::milliseconds(intervalMs);
  int sampleCount = 0;

  while (g_running != 0 && (maxCount <= 0 || sampleCount < maxCount)) {
    std::this_thread::sleep_for(INTERVAL);

    if (g_running == 0) {
      break;
    }

    // Take new snapshot
    net::InterfaceStatsSnapshot curSnap;
    if (interfaceName != nullptr) {
      curSnap = net::getInterfaceStatsSnapshot(interfaceName);
    } else {
      curSnap = net::getInterfaceStatsSnapshot();
    }

    // Compute delta
    const net::InterfaceStatsDelta DELTA = net::computeStatsDelta(prevSnap, curSnap);

    // Output
    if (jsonOutput) {
      printJsonSample(DELTA, INTERFACES, physicalOnly, static_cast<std::size_t>(sampleCount));
    } else {
      printStats(DELTA, INTERFACES, physicalOnly);

      // Print header periodically (every 20 samples)
      if ((sampleCount + 1) % 20 == 0 && (maxCount <= 0 || sampleCount + 1 < maxCount)) {
        printSeparator();
        printHeader();
      }
    }

    prevSnap = curSnap;
    ++sampleCount;
  }

  // JSON epilogue
  if (jsonOutput) {
    fmt::print("\n  ],\n");
    fmt::print("  \"totalSamples\": {}\n", sampleCount);
    fmt::print("}}\n");
  } else {
    fmt::print("\n{} samples collected\n", sampleCount);
  }

  return 0;
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  // Install signal handler
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool physicalOnly = false;
  int intervalMs = 1000;
  int maxCount = 0;
  const char* interfaceName = nullptr;

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
    physicalOnly = (pargs.count(ARG_PHYSICAL) != 0);

    if (pargs.count(ARG_INTERVAL) != 0) {
      intervalMs = std::atoi(pargs[ARG_INTERVAL][0].data());
      if (intervalMs < 10) {
        fmt::print(stderr, "Error: Interval must be >= 10 ms\n");
        return 1;
      }
    }

    if (pargs.count(ARG_COUNT) != 0) {
      maxCount = std::atoi(pargs[ARG_COUNT][0].data());
      if (maxCount < 1) {
        fmt::print(stderr, "Error: Count must be >= 1\n");
        return 1;
      }
    }

    if (pargs.count(ARG_INTERFACE) != 0) {
      interfaceName = pargs[ARG_INTERFACE][0].data();
    }
  }

  return runMonitor(intervalMs, maxCount, interfaceName, physicalOnly, jsonOutput);
}