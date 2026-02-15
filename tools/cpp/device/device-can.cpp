/**
 * @file device-can.cpp
 * @brief CAN interface status, configuration, and statistics display.
 *
 * Shows SocketCAN interfaces with bit timing, error counters, and bus state.
 * Useful for automotive and industrial embedded systems diagnostics.
 */

#include "src/device/inc/CanBusInfo.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace device = seeker::device;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_INTERFACE = 2,
  ARG_STATS = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display CAN interface status, bit timing, error counters, and statistics.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_INTERFACE] = {"--interface", 1, false,
                        "Show details for specific interface (e.g., can0)"};
  map[ARG_STATS] = {"--stats", 0, false, "Include traffic statistics"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printBitTiming(const device::CanBitTiming& timing, const char* label) {
  if (!timing.isConfigured()) {
    return;
  }

  fmt::print("  {} Bit Timing:\n", label);
  fmt::print("    Bitrate:      {} kbps\n", timing.bitrate / 1000);

  if (timing.samplePoint > 0) {
    fmt::print("    Sample point: {:.1f}%\n", timing.samplePoint / 10.0);
  }

  if (timing.tq > 0) {
    fmt::print("    Time quantum: {} ns\n", timing.tq);
    fmt::print("    Segments:     prop={} phase1={} phase2={} sjw={}\n", timing.propSeg,
               timing.phaseSeg1, timing.phaseSeg2, timing.sjw);
  }
}

void printCtrlMode(const device::CanCtrlMode& mode) {
  std::string modes;
  if (mode.loopback)
    modes += "loopback ";
  if (mode.listenOnly)
    modes += "listen-only ";
  if (mode.tripleSampling)
    modes += "triple-sampling ";
  if (mode.oneShot)
    modes += "one-shot ";
  if (mode.berr)
    modes += "berr-reporting ";
  if (mode.fd)
    modes += "FD ";
  if (mode.fdNonIso)
    modes += "FD-non-ISO ";
  if (mode.presumeAck)
    modes += "presume-ack ";

  if (!modes.empty()) {
    fmt::print("  Modes:       {}\n", modes);
  }
}

void printErrorCounters(const device::CanErrorCounters& errors) {
  fmt::print("  Error Counters:\n");
  fmt::print("    TX errors:    {}\n", errors.txErrors);
  fmt::print("    RX errors:    {}\n", errors.rxErrors);
  fmt::print("    Bus errors:   {}\n", errors.busErrors);
  fmt::print("    Restarts:     {}\n", errors.restarts);

  if (errors.hasErrors()) {
    fmt::print("    Status:       WARNING - errors detected\n");
  }
}

void printStats(const device::CanInterfaceStats& stats) {
  fmt::print("  Traffic Statistics:\n");
  fmt::print("    TX:           {} frames, {} bytes\n", stats.txFrames, stats.txBytes);
  fmt::print("    RX:           {} frames, {} bytes\n", stats.rxFrames, stats.rxBytes);
  fmt::print("    TX errors:    {}\n", stats.txErrors);
  fmt::print("    RX errors:    {}\n", stats.rxErrors);
  fmt::print("    TX dropped:   {}\n", stats.txDropped);
  fmt::print("    RX dropped:   {}\n", stats.rxDropped);
}

void printInterfaceDetails(const device::CanInterfaceInfo& iface, bool showStats) {
  fmt::print("=== {} ===\n", iface.name.data());

  if (!iface.exists) {
    fmt::print("  Status: not found\n");
    return;
  }

  // Basic info
  fmt::print("  Type:        {}\n", device::toString(iface.type));
  fmt::print("  State:       {} {}\n", iface.isUp ? "UP" : "DOWN", device::toString(iface.state));
  fmt::print("  Running:     {}\n", iface.isRunning ? "yes" : "no");
  fmt::print("  Usable:      {}\n", iface.isUsable() ? "yes" : "no");

  if (iface.driver[0] != '\0') {
    fmt::print("  Driver:      {}\n", iface.driver.data());
  }

  if (iface.isFd()) {
    fmt::print("  CAN FD:      enabled\n");
  }

  printCtrlMode(iface.ctrlMode);

  // Bit timing
  fmt::print("\n");
  printBitTiming(iface.bitTiming, "Arbitration");

  if (iface.isFd() && iface.dataBitTiming.isConfigured()) {
    printBitTiming(iface.dataBitTiming, "Data");
  }

  // Error counters
  fmt::print("\n");
  printErrorCounters(iface.errors);

  // Traffic stats
  if (showStats) {
    fmt::print("\n");
    printStats(iface.stats);
  }
}

void printAllInterfaces(const device::CanInterfaceList& interfaces, bool showStats) {
  fmt::print("=== CAN Interfaces ({} found) ===\n\n", interfaces.count);

  if (interfaces.count == 0) {
    fmt::print("No CAN interfaces found.\n");
    return;
  }

  // Summary table
  fmt::print("{:<10} {:<10} {:<6} {:<15} {:<12}\n", "INTERFACE", "TYPE", "STATE", "BITRATE",
             "BUS STATE");
  fmt::print("{:-<10} {:-<10} {:-<6} {:-<15} {:-<12}\n", "", "", "", "", "");

  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const device::CanInterfaceInfo& CAN = interfaces.interfaces[i];

    std::string bitrate = "-";
    if (CAN.bitTiming.bitrate > 0) {
      bitrate = fmt::format("{} kbps", CAN.bitTiming.bitrate / 1000);
      if (CAN.isFd() && CAN.dataBitTiming.bitrate > 0) {
        bitrate += fmt::format("/{}", CAN.dataBitTiming.bitrate / 1000);
      }
    }

    fmt::print("{:<10} {:<10} {:<6} {:<15} {:<12}\n", CAN.name.data(), device::toString(CAN.type),
               CAN.isUp ? "UP" : "DOWN", bitrate, device::toString(CAN.state));
  }

  // Detailed output
  if (showStats) {
    fmt::print("\n");
    for (std::size_t i = 0; i < interfaces.count; ++i) {
      if (i > 0)
        fmt::print("\n");
      printInterfaceDetails(interfaces.interfaces[i], true);
    }
  }
}

void printHuman(const device::CanInterfaceList& interfaces, const char* ifaceFilter,
                bool showStats) {
  if (ifaceFilter != nullptr) {
    const device::CanInterfaceInfo IFACE = device::getCanInterfaceInfo(ifaceFilter);
    if (!IFACE.exists) {
      fmt::print(stderr, "Error: Interface '{}' not found\n", ifaceFilter);
      return;
    }
    printInterfaceDetails(IFACE, showStats);
  } else {
    printAllInterfaces(interfaces, showStats);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printInterfaceJson(const device::CanInterfaceInfo& iface, bool showStats) {
  fmt::print("  {{\n");
  fmt::print("    \"name\": \"{}\",\n", iface.name.data());
  fmt::print("    \"type\": \"{}\",\n", device::toString(iface.type));
  fmt::print("    \"exists\": {},\n", iface.exists);
  fmt::print("    \"isUp\": {},\n", iface.isUp);
  fmt::print("    \"isRunning\": {},\n", iface.isRunning);
  fmt::print("    \"state\": \"{}\",\n", device::toString(iface.state));
  fmt::print("    \"driver\": \"{}\",\n", iface.driver.data());
  fmt::print("    \"isFd\": {},\n", iface.isFd());
  fmt::print("    \"isUsable\": {},\n", iface.isUsable());

  // Bit timing
  fmt::print("    \"bitTiming\": {{\n");
  fmt::print("      \"bitrate\": {},\n", iface.bitTiming.bitrate);
  fmt::print("      \"samplePoint\": {},\n", iface.bitTiming.samplePoint);
  fmt::print("      \"tq\": {},\n", iface.bitTiming.tq);
  fmt::print("      \"propSeg\": {},\n", iface.bitTiming.propSeg);
  fmt::print("      \"phaseSeg1\": {},\n", iface.bitTiming.phaseSeg1);
  fmt::print("      \"phaseSeg2\": {},\n", iface.bitTiming.phaseSeg2);
  fmt::print("      \"sjw\": {}\n", iface.bitTiming.sjw);
  fmt::print("    }},\n");

  // Data bit timing (CAN FD)
  fmt::print("    \"dataBitTiming\": {{\n");
  fmt::print("      \"bitrate\": {},\n", iface.dataBitTiming.bitrate);
  fmt::print("      \"samplePoint\": {}\n", iface.dataBitTiming.samplePoint);
  fmt::print("    }},\n");

  // Error counters
  fmt::print("    \"errors\": {{\n");
  fmt::print("      \"txErrors\": {},\n", iface.errors.txErrors);
  fmt::print("      \"rxErrors\": {},\n", iface.errors.rxErrors);
  fmt::print("      \"busErrors\": {},\n", iface.errors.busErrors);
  fmt::print("      \"restarts\": {}\n", iface.errors.restarts);
  fmt::print("    }}");

  // Stats
  if (showStats) {
    fmt::print(",\n    \"stats\": {{\n");
    fmt::print("      \"txFrames\": {},\n", iface.stats.txFrames);
    fmt::print("      \"rxFrames\": {},\n", iface.stats.rxFrames);
    fmt::print("      \"txBytes\": {},\n", iface.stats.txBytes);
    fmt::print("      \"rxBytes\": {},\n", iface.stats.rxBytes);
    fmt::print("      \"txErrors\": {},\n", iface.stats.txErrors);
    fmt::print("      \"rxErrors\": {}\n", iface.stats.rxErrors);
    fmt::print("    }}\n");
  } else {
    fmt::print("\n");
  }

  fmt::print("  }}");
}

void printJson(const device::CanInterfaceList& interfaces, const char* ifaceFilter,
               bool showStats) {
  fmt::print("{{\n");
  fmt::print("\"canInterfaces\": [\n");

  if (ifaceFilter != nullptr) {
    const device::CanInterfaceInfo IFACE = device::getCanInterfaceInfo(ifaceFilter);
    printInterfaceJson(IFACE, showStats);
  } else {
    for (std::size_t i = 0; i < interfaces.count; ++i) {
      if (i > 0)
        fmt::print(",\n");
      printInterfaceJson(interfaces.interfaces[i], showStats);
    }
  }

  fmt::print("\n]\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool showStats = false;
  const char* ifaceFilter = nullptr;

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
    showStats = (pargs.count(ARG_STATS) != 0);

    if (pargs.count(ARG_INTERFACE) != 0) {
      ifaceFilter = pargs[ARG_INTERFACE][0].data();
    }
  }

  // Gather data
  const device::CanInterfaceList INTERFACES = device::getAllCanInterfaces();

  if (jsonOutput) {
    printJson(INTERFACES, ifaceFilter, showStats);
  } else {
    printHuman(INTERFACES, ifaceFilter, showStats);
  }

  return 0;
}