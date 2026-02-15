/**
 * @file net-rtcheck.cpp
 * @brief Real-time network configuration validation.
 *
 * Validates network subsystem configuration for RT workloads:
 * - NIC IRQ affinity conflicts with RT cores
 * - Busy polling configuration
 * - Socket buffer sizing
 * - Packet drop rates
 * - Interrupt coalescing settings (via ethtool)
 * - LRO and other latency-impacting features
 *
 * Exit codes: 0=pass, 1=warnings, 2=failures
 */

#include "src/network/inc/EthtoolInfo.hpp"
#include "src/network/inc/InterfaceInfo.hpp"
#include "src/network/inc/InterfaceStats.hpp"
#include "src/network/inc/NetworkIsolation.hpp"
#include "src/network/inc/SocketBufferConfig.hpp"
#include "src/helpers/inc/Args.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/core.h>

namespace net = seeker::network;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_CPUS = 2,
  ARG_VERBOSE = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Validate network configuration for real-time systems.\n\n"
    "Exit codes: 0=all pass, 1=warnings present, 2=failures present";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_CPUS] = {"--cpus", 1, false, "RT CPU list to check (e.g., '2-4,6')"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show detailed check information"};
  return map;
}

/* ----------------------------- Check Results ----------------------------- */

enum class CheckStatus : std::uint8_t {
  PASS = 0,
  WARN = 1,
  FAIL = 2,
  SKIP = 3,
};

struct CheckResult {
  const char* name;
  CheckStatus status;
  std::string message;
  std::string detail;
};

const char* statusStr(CheckStatus s) {
  switch (s) {
  case CheckStatus::PASS:
    return "PASS";
  case CheckStatus::WARN:
    return "WARN";
  case CheckStatus::FAIL:
    return "FAIL";
  case CheckStatus::SKIP:
    return "SKIP";
  }
  return "????";
}

const char* statusColor(CheckStatus s) {
  switch (s) {
  case CheckStatus::PASS:
    return "\033[32m"; // Green
  case CheckStatus::WARN:
    return "\033[33m"; // Yellow
  case CheckStatus::FAIL:
    return "\033[31m"; // Red
  case CheckStatus::SKIP:
    return "\033[90m"; // Gray
  }
  return "";
}

constexpr const char* COLOR_RESET = "\033[0m";

/* ----------------------------- Checks ----------------------------- */

CheckResult checkIrqAffinity(const net::NetworkIsolation& ni, std::uint64_t rtCpuMask) {
  CheckResult result{"NIC IRQ Affinity", CheckStatus::PASS, "", ""};

  if (rtCpuMask == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No RT CPUs specified";
    return result;
  }

  const net::IrqConflictResult CONFLICT = net::checkIrqConflict(ni, rtCpuMask);

  if (CONFLICT.hasConflict) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{} IRQs on RT CPUs", CONFLICT.conflictCount);
    result.detail = fmt::format("Conflicting NICs: {}", CONFLICT.conflictingNics.data());
  } else {
    result.message = "No NIC IRQs on RT CPUs";
  }

  return result;
}

CheckResult checkBusyPolling(const net::SocketBufferConfig& cfg) {
  CheckResult result{"Busy Polling", CheckStatus::PASS, "", ""};

  if (cfg.busyRead < 0 && cfg.busyPoll < 0) {
    result.status = CheckStatus::SKIP;
    result.message = "Cannot read busy polling settings";
    return result;
  }

  if (cfg.isBusyPollingEnabled()) {
    result.message = fmt::format("Enabled (read={}us poll={}us)", cfg.busyRead, cfg.busyPoll);
  } else {
    result.status = CheckStatus::WARN;
    result.message = "Disabled";
    result.detail = "Enable via /proc/sys/net/core/busy_read and busy_poll";
  }

  return result;
}

CheckResult checkSocketBuffers(const net::SocketBufferConfig& cfg) {
  CheckResult result{"Socket Buffers", CheckStatus::PASS, "", ""};

  if (cfg.rmemMax < 0 || cfg.wmemMax < 0) {
    result.status = CheckStatus::SKIP;
    result.message = "Cannot read buffer settings";
    return result;
  }

  constexpr std::int64_t MIN_GOOD = 16 * 1024 * 1024; // 16 MiB
  constexpr std::int64_t MIN_WARN = 4 * 1024 * 1024;  // 4 MiB

  const std::int64_t MIN_MAX = std::min(cfg.rmemMax, cfg.wmemMax);

  if (MIN_MAX >= MIN_GOOD) {
    result.message = fmt::format("rmem_max={} wmem_max={}", net::formatBufferSize(cfg.rmemMax),
                                 net::formatBufferSize(cfg.wmemMax));
  } else if (MIN_MAX >= MIN_WARN) {
    result.status = CheckStatus::WARN;
    result.message =
        fmt::format("Buffers small: rmem_max={} wmem_max={}", net::formatBufferSize(cfg.rmemMax),
                    net::formatBufferSize(cfg.wmemMax));
    result.detail = "Consider increasing to 16+ MiB for high throughput";
  } else {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("Buffers very small: {} / {}", net::formatBufferSize(cfg.rmemMax),
                                 net::formatBufferSize(cfg.wmemMax));
    result.detail = "May limit throughput; increase rmem_max and wmem_max";
  }

  return result;
}

CheckResult checkLinkState(const net::InterfaceList& interfaces) {
  CheckResult result{"Link State", CheckStatus::PASS, "", ""};

  std::size_t physCount = 0;
  std::size_t upCount = 0;
  std::string downList;

  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const net::InterfaceInfo& iface = interfaces.interfaces[i];
    if (!iface.isPhysical()) {
      continue;
    }

    ++physCount;
    if (iface.hasLink()) {
      ++upCount;
    } else {
      if (!downList.empty()) {
        downList += ", ";
      }
      downList += iface.ifname.data();
    }
  }

  if (physCount == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No physical NICs found";
    return result;
  }

  if (upCount == physCount) {
    result.message = fmt::format("All {} physical NICs have link", physCount);
  } else if (upCount > 0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{}/{} physical NICs up", upCount, physCount);
    result.detail = fmt::format("Down: {}", downList);
  } else {
    result.status = CheckStatus::WARN;
    result.message = "No physical NICs have link";
    result.detail = downList;
  }

  return result;
}

CheckResult checkPacketDrops() {
  CheckResult result{"Packet Drops", CheckStatus::PASS, "", ""};

  // Take two snapshots to check for active drops
  const net::InterfaceStatsSnapshot BEFORE = net::getInterfaceStatsSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const net::InterfaceStatsSnapshot AFTER = net::getInterfaceStatsSnapshot();

  const net::InterfaceStatsDelta DELTA = net::computeStatsDelta(BEFORE, AFTER);

  double totalDropRate = 0.0;
  std::string droppingIfaces;

  for (std::size_t i = 0; i < DELTA.count; ++i) {
    const net::InterfaceRates& rates = DELTA.interfaces[i];
    const double DROP_RATE = rates.rxDroppedPerSec + rates.txDroppedPerSec;

    if (DROP_RATE > 0.0) {
      totalDropRate += DROP_RATE;
      if (!droppingIfaces.empty()) {
        droppingIfaces += ", ";
      }
      droppingIfaces += fmt::format("{}({:.0f}/s)", rates.ifname.data(), DROP_RATE);
    }
  }

  if (totalDropRate == 0.0) {
    result.message = "No drops in sample period";
  } else if (totalDropRate < 10.0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("Low drop rate: {:.1f}/s", totalDropRate);
    result.detail = droppingIfaces;
  } else {
    result.status = CheckStatus::FAIL;
    result.message = fmt::format("High drop rate: {:.0f}/s", totalDropRate);
    result.detail = droppingIfaces;
  }

  return result;
}

CheckResult checkNetdevBacklog(const net::SocketBufferConfig& cfg) {
  CheckResult result{"Netdev Backlog", CheckStatus::PASS, "", ""};

  if (cfg.netdevMaxBacklog < 0) {
    result.status = CheckStatus::SKIP;
    result.message = "Cannot read netdev_max_backlog";
    return result;
  }

  constexpr std::int64_t MIN_GOOD = 10000;
  constexpr std::int64_t MIN_OK = 1000;

  if (cfg.netdevMaxBacklog >= MIN_GOOD) {
    result.message = fmt::format("netdev_max_backlog={}", cfg.netdevMaxBacklog);
  } else if (cfg.netdevMaxBacklog >= MIN_OK) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("netdev_max_backlog={} (consider 10000+)", cfg.netdevMaxBacklog);
  } else {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("netdev_max_backlog={} (too low)", cfg.netdevMaxBacklog);
    result.detail = "Increase to prevent packet drops under load";
  }

  return result;
}

CheckResult checkCoalescing(const net::EthtoolInfoList& ethtoolList) {
  CheckResult result{"IRQ Coalescing", CheckStatus::PASS, "", ""};

  if (ethtoolList.count == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No NICs with ethtool support";
    return result;
  }

  std::string adaptiveNics;
  std::string highCoalesceNics;
  int adaptiveCount = 0;
  int highCoalesceCount = 0;

  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const net::EthtoolInfo& eth = ethtoolList.nics[i];

    // Check for adaptive coalescing (bad for RT)
    if (eth.coalesce.hasAdaptive()) {
      ++adaptiveCount;
      if (!adaptiveNics.empty()) {
        adaptiveNics += ", ";
      }
      adaptiveNics += eth.ifname.data();
    }

    // Check for high coalescing values (>50us is questionable for RT)
    if (eth.coalesce.rxUsecs > 50 || eth.coalesce.txUsecs > 50) {
      ++highCoalesceCount;
      if (!highCoalesceNics.empty()) {
        highCoalesceNics += ", ";
      }
      highCoalesceNics += fmt::format("{}(rx={}us)", eth.ifname.data(), eth.coalesce.rxUsecs);
    }
  }

  if (adaptiveCount > 0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{} NICs have adaptive coalescing", adaptiveCount);
    result.detail = fmt::format("Disable adaptive on: {}", adaptiveNics);
  } else if (highCoalesceCount > 0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{} NICs have high coalescing (>50us)", highCoalesceCount);
    result.detail = highCoalesceNics;
  } else {
    result.message = fmt::format("All {} NICs have low coalescing", ethtoolList.count);
  }

  return result;
}

CheckResult checkLro(const net::EthtoolInfoList& ethtoolList) {
  CheckResult result{"LRO Status", CheckStatus::PASS, "", ""};

  if (ethtoolList.count == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No NICs with ethtool support";
    return result;
  }

  std::string lroNics;
  int lroCount = 0;

  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const net::EthtoolInfo& eth = ethtoolList.nics[i];

    if (eth.hasLro()) {
      ++lroCount;
      if (!lroNics.empty()) {
        lroNics += ", ";
      }
      lroNics += eth.ifname.data();
    }
  }

  if (lroCount > 0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{} NICs have LRO enabled", lroCount);
    result.detail = fmt::format("LRO adds latency variance. Disable on: {}", lroNics);
  } else {
    result.message = "LRO disabled on all NICs";
  }

  return result;
}

CheckResult checkPauseFrames(const net::EthtoolInfoList& ethtoolList) {
  CheckResult result{"Pause Frames", CheckStatus::PASS, "", ""};

  if (ethtoolList.count == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No NICs with ethtool support";
    return result;
  }

  std::string pauseNics;
  int pauseCount = 0;

  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const net::EthtoolInfo& eth = ethtoolList.nics[i];

    if (eth.pause.isEnabled()) {
      ++pauseCount;
      if (!pauseNics.empty()) {
        pauseNics += ", ";
      }
      pauseNics += eth.ifname.data();
      if (eth.pause.rxPause && eth.pause.txPause) {
        pauseNics += "(RX+TX)";
      } else if (eth.pause.rxPause) {
        pauseNics += "(RX)";
      } else {
        pauseNics += "(TX)";
      }
    }
  }

  if (pauseCount > 0) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("{} NICs have pause frames enabled", pauseCount);
    result.detail = fmt::format("Can cause latency spikes: {}", pauseNics);
  } else {
    result.message = "Pause frames disabled on all NICs";
  }

  return result;
}

CheckResult checkOverallRtScore(const net::EthtoolInfoList& ethtoolList) {
  CheckResult result{"NIC RT Score", CheckStatus::PASS, "", ""};

  if (ethtoolList.count == 0) {
    result.status = CheckStatus::SKIP;
    result.message = "No NICs with ethtool support";
    return result;
  }

  int totalScore = 0;
  int minScore = 100;
  std::string worstNic;

  for (std::size_t i = 0; i < ethtoolList.count; ++i) {
    const net::EthtoolInfo& eth = ethtoolList.nics[i];
    const int SCORE = eth.rtScore();
    totalScore += SCORE;

    if (SCORE < minScore) {
      minScore = SCORE;
      worstNic = eth.ifname.data();
    }
  }

  const int AVG_SCORE = totalScore / static_cast<int>(ethtoolList.count);

  if (AVG_SCORE >= 80) {
    result.message = fmt::format("Average RT score: {}/100", AVG_SCORE);
  } else if (AVG_SCORE >= 60) {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("Average RT score: {}/100 (fair)", AVG_SCORE);
    result.detail = fmt::format("Lowest: {} with score {}", worstNic, minScore);
  } else {
    result.status = CheckStatus::WARN;
    result.message = fmt::format("Average RT score: {}/100 (needs tuning)", AVG_SCORE);
    result.detail = fmt::format("Lowest: {} with score {}", worstNic, minScore);
  }

  return result;
}

/* ----------------------------- Output ----------------------------- */

void printHuman(const std::vector<CheckResult>& results, bool verbose) {
  fmt::print("=== Network RT Configuration Check ===\n\n");

  int passCount = 0;
  int warnCount = 0;
  int failCount = 0;

  for (const auto& R : results) {
    fmt::print("  [{}{}{}] {}: {}\n", statusColor(R.status), statusStr(R.status), COLOR_RESET,
               R.name, R.message);

    if (verbose && !R.detail.empty()) {
      fmt::print("         {}\n", R.detail);
    }

    switch (R.status) {
    case CheckStatus::PASS:
      ++passCount;
      break;
    case CheckStatus::WARN:
      ++warnCount;
      break;
    case CheckStatus::FAIL:
      ++failCount;
      break;
    case CheckStatus::SKIP:
      break;
    }
  }

  fmt::print("\n=== Summary ===\n");
  fmt::print("  Pass: {}  Warn: {}  Fail: {}\n", passCount, warnCount, failCount);

  if (failCount > 0) {
    fmt::print("\n  Result: {}FAIL{} - Address failures before RT operation\n",
               statusColor(CheckStatus::FAIL), COLOR_RESET);
  } else if (warnCount > 0) {
    fmt::print("\n  Result: {}WARN{} - Review warnings for optimal RT performance\n",
               statusColor(CheckStatus::WARN), COLOR_RESET);
  } else {
    fmt::print("\n  Result: {}PASS{} - Network configuration looks good for RT\n",
               statusColor(CheckStatus::PASS), COLOR_RESET);
  }
}

void printJson(const std::vector<CheckResult>& results, std::uint64_t rtCpuMask) {
  fmt::print("{{\n");
  fmt::print("  \"rtCpuMask\": \"{}\",\n", net::formatCpuMask(rtCpuMask));

  fmt::print("  \"checks\": [\n");
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& R = results[i];
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", R.name);
    fmt::print("      \"status\": \"{}\",\n", statusStr(R.status));
    fmt::print("      \"message\": \"{}\",\n", R.message);
    fmt::print("      \"detail\": \"{}\"\n", R.detail);
    fmt::print("    }}{}\n", (i + 1 < results.size()) ? "," : "");
  }
  fmt::print("  ],\n");

  int passCount = 0;
  int warnCount = 0;
  int failCount = 0;
  for (const auto& R : results) {
    switch (R.status) {
    case CheckStatus::PASS:
      ++passCount;
      break;
    case CheckStatus::WARN:
      ++warnCount;
      break;
    case CheckStatus::FAIL:
      ++failCount;
      break;
    case CheckStatus::SKIP:
      break;
    }
  }

  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"pass\": {},\n", passCount);
  fmt::print("    \"warn\": {},\n", warnCount);
  fmt::print("    \"fail\": {},\n", failCount);
  fmt::print("    \"overallStatus\": \"{}\"\n",
             failCount > 0 ? "FAIL" : (warnCount > 0 ? "WARN" : "PASS"));
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
  std::uint64_t rtCpuMask = 0;

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

    if (pargs.count(ARG_CPUS) != 0) {
      rtCpuMask = net::parseCpuListToMask(pargs[ARG_CPUS][0].data());
    }
  }

  // Gather data
  const net::InterfaceList INTERFACES = net::getAllInterfaces();
  const net::SocketBufferConfig BUF_CFG = net::getSocketBufferConfig();
  const net::NetworkIsolation NET_ISO = net::getNetworkIsolation();
  const net::EthtoolInfoList ETHTOOL_LIST = net::getAllEthtoolInfo();

  // Run checks
  std::vector<CheckResult> results;
  results.reserve(12);

  // Original checks
  results.push_back(checkIrqAffinity(NET_ISO, rtCpuMask));
  results.push_back(checkBusyPolling(BUF_CFG));
  results.push_back(checkSocketBuffers(BUF_CFG));
  results.push_back(checkNetdevBacklog(BUF_CFG));
  results.push_back(checkLinkState(INTERFACES));
  results.push_back(checkPacketDrops());

  // Ethtool-based checks
  results.push_back(checkCoalescing(ETHTOOL_LIST));
  results.push_back(checkLro(ETHTOOL_LIST));
  results.push_back(checkPauseFrames(ETHTOOL_LIST));
  results.push_back(checkOverallRtScore(ETHTOOL_LIST));

  // Output results
  if (jsonOutput) {
    printJson(results, rtCpuMask);
  } else {
    printHuman(results, verbose);
  }

  // Determine exit code
  bool hasFail = false;
  bool hasWarn = false;
  for (const auto& R : results) {
    if (R.status == CheckStatus::FAIL) {
      hasFail = true;
    }
    if (R.status == CheckStatus::WARN) {
      hasWarn = true;
    }
  }

  if (hasFail) {
    return 2;
  }
  if (hasWarn) {
    return 1;
  }
  return 0;
}