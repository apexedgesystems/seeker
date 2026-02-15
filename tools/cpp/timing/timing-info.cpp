/**
 * @file timing-info.cpp
 * @brief One-shot timing system configuration dump.
 *
 * Displays clocksource, timer resolution, timer slack, NO_HZ configuration,
 * and optionally hardware RTC status.
 * Designed for quick system assessment of timing capabilities.
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
  ARG_RTC = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display clocksource, timer resolution, and timing configuration.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_RTC] = {"--rtc", 0, false, "Include hardware RTC status"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printClockSource(const timing::ClockSource& cs) {
  fmt::print("=== Clock Source ===\n");
  fmt::print("  Current:    {}", cs.current.data());
  if (cs.isTsc()) {
    fmt::print(" [optimal]");
  } else if (cs.isHpet()) {
    fmt::print(" [acceptable]");
  } else if (cs.isAcpiPm()) {
    fmt::print(" [slow]");
  }
  fmt::print("\n");

  fmt::print("  Available:  ");
  for (std::size_t i = 0; i < cs.availableCount; ++i) {
    if (i > 0) {
      fmt::print(", ");
    }
    fmt::print("{}", cs.available[i].data());
  }
  fmt::print("\n");
}

void printResolutions(const timing::ClockSource& cs) {
  fmt::print("\n=== Timer Resolutions ===\n");

  auto printRes = [](const char* name, const timing::ClockResolution& res) {
    if (!res.available) {
      fmt::print("  {:20} unavailable\n", name);
      return;
    }
    const char* tag = "";
    if (res.isHighRes()) {
      tag = " [high-res]";
    } else if (res.isCoarse()) {
      tag = " [coarse]";
    }
    fmt::print("  {:20} {} ns{}\n", name, res.resolutionNs, tag);
  };

  printRes("CLOCK_MONOTONIC", cs.monotonic);
  printRes("CLOCK_MONOTONIC_RAW", cs.monotonicRaw);
  printRes("CLOCK_MONOTONIC_COARSE", cs.monotonicCoarse);
  printRes("CLOCK_REALTIME", cs.realtime);
  printRes("CLOCK_REALTIME_COARSE", cs.realtimeCoarse);
  printRes("CLOCK_BOOTTIME", cs.boottime);
}

void printTimerConfig(const timing::TimerConfig& cfg) {
  fmt::print("\n=== Timer Configuration ===\n");

  // Timer slack
  fmt::print("  Timer Slack:     ");
  if (cfg.slackQuerySucceeded) {
    if (cfg.timerSlackNs == 1) {
      fmt::print("1 ns [minimal]\n");
    } else if (cfg.timerSlackNs < 1000) {
      fmt::print("{} ns\n", cfg.timerSlackNs);
    } else if (cfg.timerSlackNs < 1'000'000) {
      fmt::print("{:.1f} us", static_cast<double>(cfg.timerSlackNs) / 1000.0);
      if (cfg.hasDefaultSlack()) {
        fmt::print(" [default]");
      }
      fmt::print("\n");
    } else {
      fmt::print("{:.1f} ms\n", static_cast<double>(cfg.timerSlackNs) / 1'000'000.0);
    }
  } else {
    fmt::print("(query failed)\n");
  }

  fmt::print("  High-Res Timers: {}\n", cfg.highResTimersEnabled ? "enabled" : "disabled");
  fmt::print("  PREEMPT_RT:      {}\n", cfg.preemptRtEnabled ? "yes" : "no");

  // NO_HZ
  fmt::print("\n=== Tickless Configuration ===\n");
  fmt::print("  nohz_idle:       {}\n", cfg.nohzIdleEnabled ? "enabled" : "disabled");
  fmt::print("  nohz_full:       ");
  if (cfg.nohzFullCount > 0) {
    fmt::print("{} CPUs (", cfg.nohzFullCount);
    bool first = true;
    for (std::size_t i = 0; i < timing::MAX_NOHZ_CPUS; ++i) {
      if (cfg.nohzFullCpus.test(i)) {
        if (!first) {
          fmt::print(",");
        }
        fmt::print("{}", i);
        first = false;
      }
    }
    fmt::print(")\n");
  } else {
    fmt::print("(none)\n");
  }
}

void printRtcStatus(const timing::RtcStatus& rtc) {
  fmt::print("\n=== Hardware RTC ===\n");

  if (!rtc.rtcSupported) {
    fmt::print("  (not supported)\n");
    return;
  }

  if (rtc.deviceCount == 0) {
    fmt::print("  (no devices found)\n");
    return;
  }

  fmt::print("  Devices:         {}\n", rtc.deviceCount);
  fmt::print("  Wake-capable:    {}\n", rtc.hasWakeCapable ? "yes" : "no");

  for (std::size_t i = 0; i < rtc.deviceCount; ++i) {
    const timing::RtcDevice& DEV = rtc.devices[i];
    fmt::print("\n  {}{}:\n", DEV.device.data(), DEV.isSystemRtc ? " [system]" : "");

    if (DEV.name[0] != '\0') {
      fmt::print("    Driver:  {}\n", DEV.name.data());
    }

    fmt::print("    Health:  {}\n", DEV.healthString());

    if (DEV.time.querySucceeded && DEV.time.isValid()) {
      fmt::print("    Time:    {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}\n", DEV.time.year,
                 DEV.time.month, DEV.time.day, DEV.time.hour, DEV.time.minute, DEV.time.second);

      fmt::print("    Drift:   {} sec", DEV.time.driftSeconds);
      if (DEV.time.isDriftAcceptable()) {
        fmt::print(" [OK]\n");
      } else {
        fmt::print(" \033[33m[HIGH]\033[0m\n");
      }
    }

    if (DEV.alarm.querySucceeded && DEV.alarm.enabled) {
      fmt::print("    Alarm:   in {} sec\n", DEV.alarm.secondsUntil);
    }
  }
}

void printSummary(const timing::ClockSource& cs, const timing::TimerConfig& cfg) {
  fmt::print("\n=== RT Suitability ===\n");
  fmt::print("  Clock Source Score: {}/100\n", cs.rtScore());
  fmt::print("  Timer Config Score: {}/100\n", cfg.rtScore());

  const int COMBINED = (cs.rtScore() + cfg.rtScore()) / 2;
  fmt::print("  Combined Score:     {}/100", COMBINED);
  if (COMBINED >= 80) {
    fmt::print(" [GOOD]\n");
  } else if (COMBINED >= 50) {
    fmt::print(" [FAIR]\n");
  } else {
    fmt::print(" [NEEDS TUNING]\n");
  }
}

void printHuman(const timing::ClockSource& cs, const timing::TimerConfig& cfg,
                const timing::RtcStatus* rtc) {
  printClockSource(cs);
  printResolutions(cs);
  printTimerConfig(cfg);
  if (rtc != nullptr) {
    printRtcStatus(*rtc);
  }
  printSummary(cs, cfg);
}

/* ----------------------------- JSON Output ----------------------------- */

void printRtcJson(const timing::RtcStatus& rtc) {
  fmt::print("  \"rtc\": {{\n");
  fmt::print("    \"supported\": {},\n", rtc.rtcSupported ? "true" : "false");
  fmt::print("    \"deviceCount\": {},\n", rtc.deviceCount);
  fmt::print("    \"hasWakeCapable\": {},\n", rtc.hasWakeCapable ? "true" : "false");
  fmt::print("    \"allDriftAcceptable\": {},\n", rtc.allDriftAcceptable() ? "true" : "false");
  fmt::print("    \"maxDriftSeconds\": {},\n", rtc.maxDriftSeconds());

  fmt::print("    \"devices\": [");
  for (std::size_t i = 0; i < rtc.deviceCount; ++i) {
    if (i > 0) {
      fmt::print(", ");
    }
    const timing::RtcDevice& DEV = rtc.devices[i];
    fmt::print("{{\n");
    fmt::print("      \"device\": \"{}\",\n", DEV.device.data());
    fmt::print("      \"name\": \"{}\",\n", DEV.name.data());
    fmt::print("      \"isSystemRtc\": {},\n", DEV.isSystemRtc ? "true" : "false");
    fmt::print("      \"health\": \"{}\",\n", DEV.healthString());
    fmt::print("      \"driftSeconds\": {},\n", DEV.time.driftSeconds);
    fmt::print("      \"driftAcceptable\": {},\n", DEV.time.isDriftAcceptable() ? "true" : "false");
    fmt::print("      \"hasWakeAlarm\": {},\n", DEV.caps.hasWakeAlarm ? "true" : "false");
    fmt::print("      \"alarmEnabled\": {}\n", DEV.alarm.enabled ? "true" : "false");
    fmt::print("    }}");
  }
  fmt::print("]\n");
  fmt::print("  }}\n");
}

void printJson(const timing::ClockSource& cs, const timing::TimerConfig& cfg,
               const timing::RtcStatus* rtc) {
  fmt::print("{{\n");

  // Clock source
  fmt::print("  \"clockSource\": {{\n");
  fmt::print("    \"current\": \"{}\",\n", cs.current.data());
  fmt::print("    \"available\": [");
  for (std::size_t i = 0; i < cs.availableCount; ++i) {
    if (i > 0) {
      fmt::print(", ");
    }
    fmt::print("\"{}\"", cs.available[i].data());
  }
  fmt::print("],\n");
  fmt::print("    \"isTsc\": {},\n", cs.isTsc());
  fmt::print("    \"rtScore\": {}\n", cs.rtScore());
  fmt::print("  }},\n");

  // Resolutions
  fmt::print("  \"resolutions\": {{\n");
  fmt::print("    \"monotonic\": {{\"ns\": {}, \"available\": {}}},\n", cs.monotonic.resolutionNs,
             cs.monotonic.available);
  fmt::print("    \"monotonicRaw\": {{\"ns\": {}, \"available\": {}}},\n",
             cs.monotonicRaw.resolutionNs, cs.monotonicRaw.available);
  fmt::print("    \"monotonicCoarse\": {{\"ns\": {}, \"available\": {}}},\n",
             cs.monotonicCoarse.resolutionNs, cs.monotonicCoarse.available);
  fmt::print("    \"realtime\": {{\"ns\": {}, \"available\": {}}},\n", cs.realtime.resolutionNs,
             cs.realtime.available);
  fmt::print("    \"realtimeCoarse\": {{\"ns\": {}, \"available\": {}}},\n",
             cs.realtimeCoarse.resolutionNs, cs.realtimeCoarse.available);
  fmt::print("    \"boottime\": {{\"ns\": {}, \"available\": {}}}\n", cs.boottime.resolutionNs,
             cs.boottime.available);
  fmt::print("  }},\n");

  // Timer config
  fmt::print("  \"timerConfig\": {{\n");
  fmt::print("    \"timerSlackNs\": {},\n", cfg.timerSlackNs);
  fmt::print("    \"slackQuerySucceeded\": {},\n", cfg.slackQuerySucceeded);
  fmt::print("    \"highResTimersEnabled\": {},\n", cfg.highResTimersEnabled);
  fmt::print("    \"preemptRtEnabled\": {},\n", cfg.preemptRtEnabled);
  fmt::print("    \"nohzIdleEnabled\": {},\n", cfg.nohzIdleEnabled);
  fmt::print("    \"nohzFullEnabled\": {},\n", cfg.nohzFullEnabled);
  fmt::print("    \"nohzFullCount\": {},\n", cfg.nohzFullCount);
  fmt::print("    \"nohzFullCpus\": [");
  bool first = true;
  for (std::size_t i = 0; i < timing::MAX_NOHZ_CPUS; ++i) {
    if (cfg.nohzFullCpus.test(i)) {
      if (!first) {
        fmt::print(", ");
      }
      fmt::print("{}", i);
      first = false;
    }
  }
  fmt::print("],\n");
  fmt::print("    \"rtScore\": {}\n", cfg.rtScore());
  fmt::print("  }}");

  // RTC (optional)
  if (rtc != nullptr) {
    fmt::print(",\n");
    printRtcJson(*rtc);
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
  bool showRtc = false;

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
    showRtc = (pargs.count(ARG_RTC) != 0);
  }

  // Gather data
  const timing::ClockSource CS = timing::getClockSource();
  const timing::TimerConfig CFG = timing::getTimerConfig();

  // Conditionally gather RTC data
  timing::RtcStatus rtcStatus;
  const timing::RtcStatus* rtcPtr = nullptr;
  if (showRtc) {
    rtcStatus = timing::getRtcStatus();
    rtcPtr = &rtcStatus;
  }

  if (jsonOutput) {
    printJson(CS, CFG, rtcPtr);
  } else {
    printHuman(CS, CFG, rtcPtr);
  }

  return 0;
}