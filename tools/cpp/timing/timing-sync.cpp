/**
 * @file timing-sync.cpp
 * @brief Time synchronization status display.
 *
 * Shows NTP/PTP/chrony status, PTP hardware devices, and kernel time state.
 * Essential for distributed RT systems requiring coordinated timing.
 */

#include "src/timing/inc/PtpStatus.hpp"
#include "src/timing/inc/TimeSyncStatus.hpp"
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
  ARG_PTP = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION = "Display time synchronization status (NTP, PTP, chrony).";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show detailed kernel time status"};
  map[ARG_PTP] = {"--ptp", 0, false, "Show detailed PTP hardware capabilities"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printDaemons(const timing::TimeSyncStatus& status) {
  fmt::print("=== Sync Daemons ===\n");

  auto printDaemon = [](const char* name, bool detected) {
    if (detected) {
      fmt::print("  {:20} \033[32mdetected\033[0m\n", name);
    } else {
      fmt::print("  {:20} not found\n", name);
    }
  };

  printDaemon("chrony", status.chronyDetected);
  printDaemon("ntpd", status.ntpdDetected);
  printDaemon("systemd-timesyncd", status.systemdTimesyncDetected);
  printDaemon("linuxptp (ptp4l)", status.ptpLinuxDetected);

  fmt::print("\n  Primary method:    {}\n", status.primarySyncMethod());
}

void printPtpDevices(const timing::TimeSyncStatus& status) {
  fmt::print("\n=== PTP Hardware ===\n");

  if (status.ptpDeviceCount == 0) {
    fmt::print("  (no PTP devices found)\n");
    return;
  }

  fmt::print("  {} device(s) found:\n", status.ptpDeviceCount);

  for (std::size_t i = 0; i < status.ptpDeviceCount; ++i) {
    const auto& DEV = status.ptpDevices[i];
    fmt::print("    {}:", DEV.name.data());

    if (DEV.clock[0] != '\0') {
      fmt::print(" {}", DEV.clock.data());
    }

    if (DEV.maxAdjPpb > 0) {
      fmt::print(" (max adj: {} ppb)", DEV.maxAdjPpb);
    }

    if (DEV.ppsAvailable == 1) {
      fmt::print(" [PPS]");
    }

    fmt::print("\n");
  }
}

void printPtpDetailed(const timing::PtpStatus& ptp) {
  fmt::print("\n=== PTP Hardware (Detailed) ===\n");

  if (!ptp.ptpSupported) {
    fmt::print("  (PTP not supported)\n");
    return;
  }

  if (ptp.clockCount == 0) {
    fmt::print("  (no PTP clocks found)\n");
    return;
  }

  fmt::print("  {} clock(s) found:\n", ptp.clockCount);

  for (std::size_t i = 0; i < ptp.clockCount; ++i) {
    const timing::PtpClock& CLK = ptp.clocks[i];

    fmt::print("\n  {}:\n", CLK.device.data());

    if (CLK.clockName[0] != '\0') {
      fmt::print("    Name:           {}\n", CLK.clockName.data());
    }

    if (CLK.hasBoundInterface) {
      fmt::print("    Interface:      {}\n", CLK.boundInterface.data());
    }

    if (CLK.capsQuerySucceeded) {
      fmt::print("    Max Adjustment: {} ppb ({:.1f} ppm)\n", CLK.caps.maxAdjPpb,
                 static_cast<double>(CLK.caps.maxAdjPpb) / 1000.0);

      fmt::print("    Capabilities:   ");
      bool first = true;

      auto addCap = [&](const char* name) {
        if (!first) {
          fmt::print(", ");
        }
        fmt::print("{}", name);
        first = false;
      };

      if (CLK.caps.pps) {
        addCap("PPS");
      }
      if (CLK.caps.nAlarm > 0) {
        addCap(fmt::format("alarms({})", CLK.caps.nAlarm).c_str());
      }
      if (CLK.caps.nExtTs > 0) {
        addCap(fmt::format("ext-ts({})", CLK.caps.nExtTs).c_str());
      }
      if (CLK.caps.nPerOut > 0) {
        addCap(fmt::format("per-out({})", CLK.caps.nPerOut).c_str());
      }
      if (CLK.caps.nPins > 0) {
        addCap(fmt::format("pins({})", CLK.caps.nPins).c_str());
      }

      if (first) {
        fmt::print("(none)");
      }
      fmt::print("\n");

      fmt::print("    RT Score:       {}/100\n", CLK.rtScore());
    } else {
      fmt::print("    Capabilities:   (query failed - check permissions on /dev/{})\n",
                 CLK.device.data());
    }
  }

  const timing::PtpClock* BEST = ptp.getBestClock();
  if (BEST != nullptr) {
    fmt::print("\n  Best clock for RT: {} (score: {}/100)\n", BEST->device.data(), BEST->rtScore());
  }
}

void printKernelTime(const timing::TimeSyncStatus& status, bool verbose) {
  fmt::print("\n=== Kernel Time Status ===\n");

  const auto& K = status.kernel;

  if (!K.querySucceeded) {
    fmt::print("  (adjtimex query failed)\n");
    return;
  }

  // Sync status with color
  fmt::print("  Synchronized:  ");
  if (K.synced) {
    fmt::print("\033[32myes\033[0m\n");
  } else {
    fmt::print("\033[33mno\033[0m\n");
  }

  fmt::print("  Quality:       {}\n", K.qualityString());

  // Offset
  if (K.offsetUs >= 0) {
    fmt::print("  Offset:        +{} us\n", K.offsetUs);
  } else {
    fmt::print("  Offset:        {} us\n", K.offsetUs);
  }

  // Error estimates
  fmt::print("  Est. Error:    {} us\n", K.estErrorUs);
  fmt::print("  Max Error:     {} us\n", K.maxErrorUs);

  if (verbose) {
    // Frequency adjustment
    fmt::print("  Freq Adjust:   {} ppb\n", K.freqPpb);

    // PLL/PPS status
    fmt::print("  PLL mode:      {}\n", K.pll ? "yes" : "no");
    if (K.ppsFreq || K.ppsTime) {
      fmt::print("  PPS discipline:");
      if (K.ppsFreq) {
        fmt::print(" freq");
      }
      if (K.ppsTime) {
        fmt::print(" time");
      }
      fmt::print("\n");
    }
    if (K.freqHold) {
      fmt::print("  Freq hold:     yes\n");
    }

    fmt::print("  Clock state:   {}\n", K.clockState);
  }
}

void printSummary(const timing::TimeSyncStatus& status) {
  fmt::print("\n=== Assessment ===\n");
  fmt::print("  RT Score: {}/100", status.rtScore());

  const int SCORE = status.rtScore();
  if (SCORE >= 80) {
    fmt::print(" \033[32m[GOOD]\033[0m\n");
  } else if (SCORE >= 50) {
    fmt::print(" \033[33m[FAIR]\033[0m\n");
  } else {
    fmt::print(" \033[31m[POOR]\033[0m\n");
  }

  // Recommendations
  if (!status.hasAnySyncDaemon()) {
    fmt::print("\n  ! No sync daemon detected - time may drift\n");
  }

  if (!status.kernel.synced) {
    fmt::print("  ! Kernel clock not synchronized\n");
  }

  if (status.hasPtpHardware() && !status.ptpLinuxDetected) {
    fmt::print("  * PTP hardware available but linuxptp not detected\n");
  }
}

void printHuman(const timing::TimeSyncStatus& status, const timing::PtpStatus* ptp, bool verbose) {
  printDaemons(status);

  if (ptp != nullptr) {
    printPtpDetailed(*ptp);
  } else {
    printPtpDevices(status);
  }

  printKernelTime(status, verbose);
  printSummary(status);
}

/* ----------------------------- JSON Output ----------------------------- */

void printPtpJson(const timing::PtpStatus& ptp) {
  fmt::print("  \"ptpDetailed\": {{\n");
  fmt::print("    \"supported\": {},\n", ptp.ptpSupported ? "true" : "false");
  fmt::print("    \"clockCount\": {},\n", ptp.clockCount);
  fmt::print("    \"rtScore\": {},\n", ptp.rtScore());

  fmt::print("    \"clocks\": [");
  for (std::size_t i = 0; i < ptp.clockCount; ++i) {
    if (i > 0) {
      fmt::print(", ");
    }
    const timing::PtpClock& CLK = ptp.clocks[i];
    fmt::print("{{\n");
    fmt::print("      \"device\": \"{}\",\n", CLK.device.data());
    fmt::print("      \"index\": {},\n", CLK.index);
    fmt::print("      \"clockName\": \"{}\",\n", CLK.clockName.data());
    fmt::print("      \"boundInterface\": \"{}\",\n", CLK.boundInterface.data());
    fmt::print("      \"hasBoundInterface\": {},\n", CLK.hasBoundInterface ? "true" : "false");
    fmt::print("      \"capsQuerySucceeded\": {},\n", CLK.capsQuerySucceeded ? "true" : "false");
    fmt::print("      \"maxAdjPpb\": {},\n", CLK.caps.maxAdjPpb);
    fmt::print("      \"nAlarm\": {},\n", CLK.caps.nAlarm);
    fmt::print("      \"nExtTs\": {},\n", CLK.caps.nExtTs);
    fmt::print("      \"nPerOut\": {},\n", CLK.caps.nPerOut);
    fmt::print("      \"nPins\": {},\n", CLK.caps.nPins);
    fmt::print("      \"pps\": {},\n", CLK.caps.pps ? "true" : "false");
    fmt::print("      \"rtScore\": {}\n", CLK.rtScore());
    fmt::print("    }}");
  }
  fmt::print("]\n");
  fmt::print("  }},\n");
}

void printJson(const timing::TimeSyncStatus& status, const timing::PtpStatus* ptp) {
  fmt::print("{{\n");

  // Daemons
  fmt::print("  \"daemons\": {{\n");
  fmt::print("    \"chrony\": {},\n", status.chronyDetected);
  fmt::print("    \"ntpd\": {},\n", status.ntpdDetected);
  fmt::print("    \"systemdTimesyncd\": {},\n", status.systemdTimesyncDetected);
  fmt::print("    \"linuxptp\": {},\n", status.ptpLinuxDetected);
  fmt::print("    \"primaryMethod\": \"{}\"\n", status.primarySyncMethod());
  fmt::print("  }},\n");

  // PTP devices (basic)
  fmt::print("  \"ptpDevices\": [\n");
  for (std::size_t i = 0; i < status.ptpDeviceCount; ++i) {
    const auto& DEV = status.ptpDevices[i];
    fmt::print(
        "    {{\"name\": \"{}\", \"clock\": \"{}\", \"maxAdjPpb\": {}, \"ppsAvailable\": {}}}",
        DEV.name.data(), DEV.clock.data(), DEV.maxAdjPpb, DEV.ppsAvailable);
    if (i + 1 < status.ptpDeviceCount) {
      fmt::print(",");
    }
    fmt::print("\n");
  }
  fmt::print("  ],\n");

  // PTP detailed (if requested)
  if (ptp != nullptr) {
    printPtpJson(*ptp);
  }

  // Kernel time status
  const auto& K = status.kernel;
  fmt::print("  \"kernelTime\": {{\n");
  fmt::print("    \"querySucceeded\": {},\n", K.querySucceeded);
  fmt::print("    \"synced\": {},\n", K.synced);
  fmt::print("    \"quality\": \"{}\",\n", K.qualityString());
  fmt::print("    \"offsetUs\": {},\n", K.offsetUs);
  fmt::print("    \"freqPpb\": {},\n", K.freqPpb);
  fmt::print("    \"maxErrorUs\": {},\n", K.maxErrorUs);
  fmt::print("    \"estErrorUs\": {},\n", K.estErrorUs);
  fmt::print("    \"pll\": {},\n", K.pll);
  fmt::print("    \"ppsFreq\": {},\n", K.ppsFreq);
  fmt::print("    \"ppsTime\": {},\n", K.ppsTime);
  fmt::print("    \"freqHold\": {},\n", K.freqHold);
  fmt::print("    \"clockState\": {}\n", K.clockState);
  fmt::print("  }},\n");

  // Assessment
  fmt::print("  \"assessment\": {{\n");
  fmt::print("    \"rtScore\": {},\n", status.rtScore());
  fmt::print("    \"hasAnySyncDaemon\": {},\n", status.hasAnySyncDaemon());
  fmt::print("    \"hasPtpHardware\": {},\n", status.hasPtpHardware());
  fmt::print("    \"isWellSynced\": {}\n", K.isWellSynced());
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
  bool showPtpDetailed = false;

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
    showPtpDetailed = (pargs.count(ARG_PTP) != 0);
  }

  // Gather data
  const timing::TimeSyncStatus STATUS = timing::getTimeSyncStatus();

  // Conditionally gather detailed PTP data
  timing::PtpStatus ptpStatus;
  const timing::PtpStatus* ptpPtr = nullptr;
  if (showPtpDetailed) {
    ptpStatus = timing::getPtpStatus();
    ptpPtr = &ptpStatus;
  }

  if (jsonOutput) {
    printJson(STATUS, ptpPtr);
  } else {
    printHuman(STATUS, ptpPtr, verbose);
  }

  return 0;
}