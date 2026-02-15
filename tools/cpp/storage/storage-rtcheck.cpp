/**
 * @file storage-rtcheck.cpp
 * @brief Real-time storage configuration validation tool.
 *
 * Checks storage configuration against RT best practices and reports
 * pass/warn/fail status for each check.
 */

#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/storage/inc/IoScheduler.hpp"
#include "src/storage/inc/MountInfo.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace storage = seeker::storage;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_VERBOSE = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION = "Validate storage configuration for real-time systems.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show detailed recommendations"};
  return map;
}

/// Check result status.
enum class CheckStatus : std::uint8_t { PASS, WARN, FAIL };

/// Single check result.
struct CheckResult {
  const char* name;
  CheckStatus status;
  std::string message;
  std::string recommendation;
};

/// Get status string.
const char* statusStr(CheckStatus s) {
  switch (s) {
  case CheckStatus::PASS:
    return "PASS";
  case CheckStatus::WARN:
    return "WARN";
  case CheckStatus::FAIL:
    return "FAIL";
  }
  return "UNKNOWN";
}

/// Get ANSI color for status.
const char* statusColor(CheckStatus s) {
  switch (s) {
  case CheckStatus::PASS:
    return "\033[32m"; // Green
  case CheckStatus::WARN:
    return "\033[33m"; // Yellow
  case CheckStatus::FAIL:
    return "\033[31m"; // Red
  }
  return "";
}

constexpr const char* RESET = "\033[0m";

/* ----------------------------- RT Checks ----------------------------- */

std::vector<CheckResult> runChecks() {
  std::vector<CheckResult> results;

  const storage::BlockDeviceList DEVICES = storage::getBlockDevices();
  const storage::MountTable MOUNTS = storage::getMountTable();

  // Check 1: Device types
  {
    CheckResult r{"device_types", CheckStatus::PASS, "", ""};
    const std::size_t NVME = DEVICES.countNvme();
    const std::size_t SSD = DEVICES.countSsd();
    const std::size_t HDD = DEVICES.countHdd();

    if (NVME > 0) {
      r.message = fmt::format("{} NVMe device(s) detected - optimal for RT", NVME);
    } else if (SSD > 0) {
      r.status = CheckStatus::PASS;
      r.message = fmt::format("{} SSD(s) detected - good for RT", SSD);
    } else if (HDD > 0) {
      r.status = CheckStatus::WARN;
      r.message = fmt::format("{} HDD(s) only - consider SSD/NVMe for RT workloads", HDD);
      r.recommendation = "HDDs have unpredictable seek latency. Use NVMe or SSD for RT.";
    } else {
      r.status = CheckStatus::WARN;
      r.message = "No physical block devices detected";
    }
    results.push_back(r);
  }

  // Check 2: I/O Schedulers
  for (std::size_t i = 0; i < DEVICES.count; ++i) {
    const storage::BlockDevice& DEV = DEVICES.devices[i];
    const storage::IoSchedulerConfig CFG = storage::getIoSchedulerConfig(DEV.name.data());

    if (CFG.current[0] == '\0')
      continue;

    CheckResult r{"scheduler", CheckStatus::PASS, "", ""};
    r.message = fmt::format("{}: scheduler={}", DEV.name.data(), CFG.current.data());

    if (DEV.isNvme() && !CFG.isNoneScheduler()) {
      r.status = CheckStatus::WARN;
      r.message += " (none recommended for NVMe)";
      r.recommendation = fmt::format("echo none > /sys/block/{}/queue/scheduler", DEV.name.data());
    } else if (DEV.isHdd() && !CFG.isMqDeadline()) {
      r.status = CheckStatus::WARN;
      r.message += " (mq-deadline recommended for HDD)";
      r.recommendation =
          fmt::format("echo mq-deadline > /sys/block/{}/queue/scheduler", DEV.name.data());
    } else if (CFG.isRtFriendly()) {
      r.message += " (RT-friendly)";
    }
    results.push_back(r);
  }

  // Check 3: Queue depth
  for (std::size_t i = 0; i < DEVICES.count; ++i) {
    const storage::BlockDevice& DEV = DEVICES.devices[i];
    const storage::IoSchedulerConfig CFG = storage::getIoSchedulerConfig(DEV.name.data());

    if (CFG.nrRequests < 0)
      continue;

    CheckResult r{"queue_depth", CheckStatus::PASS, "", ""};
    r.message = fmt::format("{}: nr_requests={}", DEV.name.data(), CFG.nrRequests);

    if (CFG.nrRequests > 128) {
      r.status = CheckStatus::WARN;
      r.message += " (high - may increase latency variance)";
      r.recommendation =
          fmt::format("echo 32 > /sys/block/{}/queue/nr_requests  # Lower for RT", DEV.name.data());
    } else if (CFG.nrRequests <= 32) {
      r.message += " (optimal for RT)";
    }
    results.push_back(r);
  }

  // Check 4: Read-ahead
  for (std::size_t i = 0; i < DEVICES.count; ++i) {
    const storage::BlockDevice& DEV = DEVICES.devices[i];
    const storage::IoSchedulerConfig CFG = storage::getIoSchedulerConfig(DEV.name.data());

    if (CFG.readAheadKb < 0)
      continue;

    CheckResult r{"read_ahead", CheckStatus::PASS, "", ""};
    r.message = fmt::format("{}: read_ahead_kb={}", DEV.name.data(), CFG.readAheadKb);

    if (CFG.readAheadKb > 128) {
      r.status = CheckStatus::WARN;
      r.message += " (high - wasted I/O for random access)";
      r.recommendation = fmt::format("echo 0 > /sys/block/{}/queue/read_ahead_kb  # Disable for RT",
                                     DEV.name.data());
    } else if (CFG.readAheadKb == 0) {
      r.message += " (disabled - optimal for RT random I/O)";
    }
    results.push_back(r);
  }

  // Check 5: Mount options
  for (std::size_t i = 0; i < MOUNTS.count; ++i) {
    const storage::MountEntry& M = MOUNTS.mounts[i];
    if (!M.isBlockDevice())
      continue;

    // Skip system mounts
    if (std::strcmp(M.mountPoint.data(), "/") == 0 ||
        std::strncmp(M.mountPoint.data(), "/boot", 5) == 0) {
      continue;
    }

    CheckResult r{"mount_options", CheckStatus::PASS, "", ""};
    r.message = fmt::format("{} on {}", M.device.data(), M.mountPoint.data());

    std::string issues;
    if (!M.hasNoAtime() && !M.hasRelAtime()) {
      issues += "atime updates enabled; ";
    }
    if (M.hasNoBarrier()) {
      issues += "barriers disabled (data risk); ";
    }

    if (!issues.empty()) {
      r.status = CheckStatus::WARN;
      r.message += fmt::format(" - {}", issues);
      r.recommendation = "Consider: mount -o remount,noatime " + std::string(M.mountPoint.data());
    } else if (M.hasNoAtime()) {
      r.message += " (noatime - good)";
    }
    results.push_back(r);
  }

  // Check 6: Overall RT score
  {
    int totalScore = 0;
    int deviceCount = 0;

    for (std::size_t i = 0; i < DEVICES.count; ++i) {
      const storage::IoSchedulerConfig CFG =
          storage::getIoSchedulerConfig(DEVICES.devices[i].name.data());
      if (CFG.current[0] != '\0') {
        totalScore += CFG.rtScore();
        ++deviceCount;
      }
    }

    if (deviceCount > 0) {
      const int AVG_SCORE = totalScore / deviceCount;
      CheckResult r{"overall_rt_score", CheckStatus::PASS, "", ""};
      r.message = fmt::format("Average RT score: {}/100", AVG_SCORE);

      if (AVG_SCORE >= 70) {
        r.message += " (good)";
      } else if (AVG_SCORE >= 40) {
        r.status = CheckStatus::WARN;
        r.message += " (room for improvement)";
      } else {
        r.status = CheckStatus::FAIL;
        r.message += " (needs attention)";
      }
      results.push_back(r);
    }
  }

  return results;
}

/* ----------------------------- Output ----------------------------- */

void printHuman(const std::vector<CheckResult>& results, bool verbose) {
  fmt::print("=== Storage RT Configuration Check ===\n\n");

  int passed = 0;
  int warned = 0;
  int failed = 0;

  for (const auto& R : results) {
    fmt::print("[{}{}{}] {}: {}\n", statusColor(R.status), statusStr(R.status), RESET, R.name,
               R.message);

    if (verbose && !R.recommendation.empty()) {
      fmt::print("       -> {}\n", R.recommendation);
    }

    switch (R.status) {
    case CheckStatus::PASS:
      ++passed;
      break;
    case CheckStatus::WARN:
      ++warned;
      break;
    case CheckStatus::FAIL:
      ++failed;
      break;
    }
  }

  fmt::print("\n=== Summary ===\n");
  fmt::print("  {}PASS{}: {}  {}WARN{}: {}  {}FAIL{}: {}\n", statusColor(CheckStatus::PASS), RESET,
             passed, statusColor(CheckStatus::WARN), RESET, warned, statusColor(CheckStatus::FAIL),
             RESET, failed);

  if (warned > 0 || failed > 0) {
    fmt::print("\nRun with --verbose for recommendations.\n");
  }
}

void printJson(const std::vector<CheckResult>& results) {
  fmt::print("{{\n  \"checks\": [\n");

  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& R = results[i];
    if (i > 0)
      fmt::print(",\n");

    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", R.name);
    fmt::print("      \"status\": \"{}\",\n", statusStr(R.status));
    fmt::print("      \"message\": \"{}\",\n", R.message);
    fmt::print("      \"recommendation\": \"{}\"\n", R.recommendation);
    fmt::print("    }}");
  }

  fmt::print("\n  ]\n}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool verbose = false;

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
  }

  const std::vector<CheckResult> RESULTS = runChecks();

  if (jsonOutput) {
    printJson(RESULTS);
  } else {
    printHuman(RESULTS, verbose);
  }

  // Exit code: 0=all pass, 1=warnings, 2=failures
  bool hasWarn = false;
  bool hasFail = false;
  for (const auto& R : RESULTS) {
    if (R.status == CheckStatus::WARN)
      hasWarn = true;
    if (R.status == CheckStatus::FAIL)
      hasFail = true;
  }

  if (hasFail)
    return 2;
  if (hasWarn)
    return 1;
  return 0;
}