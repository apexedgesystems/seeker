/**
 * @file mem-rtcheck.cpp
 * @brief Validate memory configuration for real-time readiness.
 *
 * Performs pass/warn/fail checks on memory settings critical for RT systems:
 * hugepage allocation, memory locking limits, THP state, swappiness, and
 * ECC/EDAC memory error status. Returns exit code 0 (pass), 1 (warnings),
 * or 2 (failures).
 */

#include "src/memory/inc/EdacStatus.hpp"
#include "src/memory/inc/HugepageStatus.hpp"
#include "src/memory/inc/MemoryLocking.hpp"
#include "src/memory/inc/MemoryStats.hpp"
#include "src/memory/inc/PageSizes.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace mem = seeker::memory;

namespace {

/* ----------------------------- Argument Handling ----------------------------- */

enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_SIZE = 2,
};

constexpr std::string_view DESCRIPTION = "Validate memory configuration for real-time readiness.";

seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_SIZE] = {"--size", 1, false,
                   "Required lockable memory in bytes (e.g., 1073741824 for 1GiB)"};
  return map;
}

/* ----------------------------- Check Result Types ----------------------------- */

enum class CheckStatus : std::uint8_t {
  PASS = 0,
  WARN = 1,
  FAIL = 2,
  SKIP = 3,
};

struct CheckResult {
  CheckStatus status{CheckStatus::SKIP};
  std::array<char, 256> message{};
  std::array<char, 256> recommendation{};
};

/* ----------------------------- Formatting Helpers ----------------------------- */

const char* statusLabel(CheckStatus s) {
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
  return "UNKNOWN";
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

constexpr const char* RESET = "\033[0m";

void printBytesHuman(std::uint64_t bytes) {
  constexpr std::uint64_t KIB = 1024ULL;
  constexpr std::uint64_t MIB = 1024ULL * 1024;
  constexpr std::uint64_t GIB = 1024ULL * 1024 * 1024;

  if (bytes >= GIB) {
    fmt::print("{:.1f} GiB", static_cast<double>(bytes) / static_cast<double>(GIB));
  } else if (bytes >= MIB) {
    fmt::print("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MIB));
  } else if (bytes >= KIB) {
    fmt::print("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KIB));
  } else {
    fmt::print("{} B", bytes);
  }
}

/* ----------------------------- Check Functions ----------------------------- */

/// Check 1: Hugepages configured and allocated.
CheckResult checkHugepages(const mem::HugepageStatus& hp) {
  CheckResult r{};

  if (!hp.hasHugepages()) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(), "No hugepages configured");
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Configure hugepages: echo N > /proc/sys/vm/nr_hugepages");
    return r;
  }

  // Check if any pages are actually allocated
  std::uint64_t totalPages = 0;
  std::uint64_t freePages = 0;
  for (std::size_t i = 0; i < hp.sizeCount; ++i) {
    totalPages += hp.sizes[i].total;
    freePages += hp.sizes[i].free;
  }

  if (totalPages == 0) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(),
                  "Hugepage sizes available but none allocated");
    std::snprintf(
        r.recommendation.data(), r.recommendation.size(),
        "Allocate hugepages: echo N > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages");
    return r;
  }

  if (freePages == 0) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(), "All %llu hugepages in use (none free)",
                  static_cast<unsigned long long>(totalPages));
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Increase hugepage allocation or free existing allocations");
    return r;
  }

  r.status = CheckStatus::PASS;
  std::snprintf(r.message.data(), r.message.size(), "%llu hugepages configured, %llu free",
                static_cast<unsigned long long>(totalPages),
                static_cast<unsigned long long>(freePages));
  return r;
}

/// Check 2: Memory locking capability.
CheckResult checkMemoryLocking(const mem::MemoryLockingStatus& ml, std::uint64_t requiredBytes) {
  CheckResult r{};

  if (ml.isUnlimited()) {
    r.status = CheckStatus::PASS;
    if (ml.hasCapIpcLock) {
      std::snprintf(r.message.data(), r.message.size(), "Unlimited mlock via CAP_IPC_LOCK");
    } else if (ml.isRoot) {
      std::snprintf(r.message.data(), r.message.size(), "Unlimited mlock (running as root)");
    } else {
      std::snprintf(r.message.data(), r.message.size(), "Unlimited mlock limit configured");
    }
    return r;
  }

  // Check if required size can be locked
  if (requiredBytes > 0) {
    if (ml.canLock(requiredBytes)) {
      r.status = CheckStatus::PASS;
      std::snprintf(r.message.data(), r.message.size(),
                    "Can lock requested %llu bytes (limit: %llu bytes)",
                    static_cast<unsigned long long>(requiredBytes),
                    static_cast<unsigned long long>(ml.softLimitBytes));
      return r;
    }
    r.status = CheckStatus::FAIL;
    std::snprintf(r.message.data(), r.message.size(),
                  "Cannot lock %llu bytes (available: %llu bytes)",
                  static_cast<unsigned long long>(requiredBytes),
                  static_cast<unsigned long long>(ml.availableBytes()));
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Increase RLIMIT_MEMLOCK or grant CAP_IPC_LOCK capability");
    return r;
  }

  // No specific size requested, check if limit is reasonable (>=64MiB)
  constexpr std::uint64_t MIN_REASONABLE = 64ULL * 1024 * 1024;
  if (ml.softLimitBytes >= MIN_REASONABLE) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "mlock limit: %llu bytes",
                  static_cast<unsigned long long>(ml.softLimitBytes));
    return r;
  }

  r.status = CheckStatus::WARN;
  std::snprintf(r.message.data(), r.message.size(), "mlock limit low: %llu bytes",
                static_cast<unsigned long long>(ml.softLimitBytes));
  std::snprintf(r.recommendation.data(), r.recommendation.size(),
                "Increase limit in /etc/security/limits.conf or grant CAP_IPC_LOCK");
  return r;
}

/// Check 3: Transparent Huge Pages disabled.
CheckResult checkTHP(const mem::MemoryStats& stats) {
  CheckResult r{};

  if (stats.thpEnabled[0] == '\0') {
    r.status = CheckStatus::SKIP;
    std::snprintf(r.message.data(), r.message.size(), "THP status unavailable");
    return r;
  }

  // Parse THP status: "[never]" is ideal, "[madvise]" is acceptable, "[always]" is bad
  const char* thp = stats.thpEnabled.data();

  if (std::strstr(thp, "[never]") != nullptr) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "THP disabled (never)");
    return r;
  }

  if (std::strstr(thp, "[madvise]") != nullptr) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "THP opt-in only (madvise)");
    return r;
  }

  if (std::strstr(thp, "[always]") != nullptr) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(),
                  "THP enabled (always) - may cause latency spikes");
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "echo madvise > /sys/kernel/mm/transparent_hugepage/enabled");
    return r;
  }

  r.status = CheckStatus::WARN;
  std::snprintf(r.message.data(), r.message.size(), "THP status unclear: %s", thp);
  return r;
}

/// Check 4: Swappiness low.
CheckResult checkSwappiness(const mem::MemoryStats& stats) {
  CheckResult r{};

  if (stats.swappiness < 0) {
    r.status = CheckStatus::SKIP;
    std::snprintf(r.message.data(), r.message.size(), "Swappiness unavailable");
    return r;
  }

  if (stats.swappiness <= 10) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "Swappiness: %d (RT-friendly)",
                  stats.swappiness);
    return r;
  }

  if (stats.swappiness <= 30) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "Swappiness: %d (acceptable)",
                  stats.swappiness);
    return r;
  }

  if (stats.swappiness <= 60) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(), "Swappiness: %d (default, may cause jitter)",
                  stats.swappiness);
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Reduce swappiness: sysctl vm.swappiness=10");
    return r;
  }

  r.status = CheckStatus::FAIL;
  std::snprintf(r.message.data(), r.message.size(), "Swappiness: %d (aggressive, not RT-safe)",
                stats.swappiness);
  std::snprintf(r.recommendation.data(), r.recommendation.size(),
                "Reduce swappiness: sysctl vm.swappiness=10");
  return r;
}

/// Check 5: Memory overcommit policy.
CheckResult checkOvercommit(const mem::MemoryStats& stats) {
  CheckResult r{};

  if (stats.overcommitMemory < 0) {
    r.status = CheckStatus::SKIP;
    std::snprintf(r.message.data(), r.message.size(), "Overcommit policy unavailable");
    return r;
  }

  if (stats.overcommitMemory == 2) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "Overcommit: 2 (strict, no overcommit)");
    return r;
  }

  if (stats.overcommitMemory == 0) {
    r.status = CheckStatus::PASS;
    std::snprintf(r.message.data(), r.message.size(), "Overcommit: 0 (heuristic)");
    return r;
  }

  if (stats.overcommitMemory == 1) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(), "Overcommit: 1 (always) - OOM risk");
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Consider sysctl vm.overcommit_memory=0 or 2 for RT safety");
    return r;
  }

  r.status = CheckStatus::WARN;
  std::snprintf(r.message.data(), r.message.size(), "Overcommit: %d (unknown value)",
                stats.overcommitMemory);
  return r;
}

/// Check 6: ECC/EDAC memory error status.
CheckResult checkEdac(const mem::EdacStatus& edac) {
  CheckResult r{};

  if (!edac.edacSupported) {
    r.status = CheckStatus::SKIP;
    std::snprintf(r.message.data(), r.message.size(),
                  "EDAC unavailable (no ECC memory or module not loaded)");
    return r;
  }

  if (!edac.eccEnabled) {
    r.status = CheckStatus::WARN;
    std::snprintf(r.message.data(), r.message.size(),
                  "EDAC present but no memory controllers found");
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Verify ECC is enabled in BIOS and EDAC driver is loaded");
    return r;
  }

  // Check for uncorrectable errors (critical - data corruption)
  if (edac.totalUeCount > 0) {
    r.status = CheckStatus::FAIL;
    std::snprintf(r.message.data(), r.message.size(),
                  "CRITICAL: %llu uncorrectable memory errors detected!",
                  static_cast<unsigned long long>(edac.totalUeCount));
    std::snprintf(r.recommendation.data(), r.recommendation.size(),
                  "Memory hardware failure - replace faulty DIMMs immediately");
    return r;
  }

  // Check for correctable errors (warning - degraded but functional)
  if (edac.totalCeCount > 0) {
    // High CE count is more concerning
    if (edac.totalCeCount > 100) {
      r.status = CheckStatus::WARN;
      std::snprintf(r.message.data(), r.message.size(),
                    "ECC: %llu correctable errors (high count, monitor closely)",
                    static_cast<unsigned long long>(edac.totalCeCount));
      std::snprintf(r.recommendation.data(), r.recommendation.size(),
                    "Monitor for increase - may indicate failing DIMM");
    } else {
      r.status = CheckStatus::PASS;
      std::snprintf(r.message.data(), r.message.size(),
                    "ECC enabled, %llu correctable errors (within normal range)",
                    static_cast<unsigned long long>(edac.totalCeCount));
    }
    return r;
  }

  // No errors - ideal
  r.status = CheckStatus::PASS;
  std::snprintf(r.message.data(), r.message.size(),
                "ECC enabled, no memory errors (%zu controller%s)", edac.mcCount,
                edac.mcCount == 1 ? "" : "s");
  return r;
}

/* ----------------------------- Output Functions ----------------------------- */

void printCheckHuman(const char* name, const CheckResult& result) {
  fmt::print("[{}{}{}] {}: {}\n", statusColor(result.status), statusLabel(result.status), RESET,
             name, result.message.data());

  if (result.recommendation[0] != '\0') {
    fmt::print("         -> {}\n", result.recommendation.data());
  }
}

void printSummaryHuman(int passes, int warnings, int failures, int skips) {
  fmt::print("\n");

  if (failures > 0) {
    fmt::print("{}MEMORY NOT RT-READY{}: {} failures, {} warnings\n", "\033[31m", RESET, failures,
               warnings);
  } else if (warnings > 0) {
    fmt::print("{}MEMORY PARTIALLY RT-READY{}: {} warnings\n", "\033[33m", RESET, warnings);
  } else {
    fmt::print("{}MEMORY RT-READY{}: All checks passed\n", "\033[32m", RESET);
  }

  fmt::print("Summary: {} pass, {} warn, {} fail, {} skip\n", passes, warnings, failures, skips);
}

void printJson(const CheckResult& hugepages, const CheckResult& mlock, const CheckResult& thp,
               const CheckResult& swappiness, const CheckResult& overcommit,
               const CheckResult& edac) {
  fmt::print("{{\n");
  fmt::print("  \"checks\": [\n");

  auto printCheck = [](const char* name, const CheckResult& r, bool last) {
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", name);
    fmt::print("      \"status\": \"{}\",\n", statusLabel(r.status));
    fmt::print("      \"message\": \"{}\",\n", r.message.data());
    fmt::print("      \"recommendation\": \"{}\"\n", r.recommendation.data());
    fmt::print("    }}{}\n", last ? "" : ",");
  };

  printCheck("hugepages", hugepages, false);
  printCheck("memoryLocking", mlock, false);
  printCheck("transparentHugepages", thp, false);
  printCheck("swappiness", swappiness, false);
  printCheck("overcommit", overcommit, false);
  printCheck("eccMemory", edac, true);

  fmt::print("  ],\n");

  // Summary
  int passes = 0, warnings = 0, failures = 0, skips = 0;
  auto count = [&](const CheckResult& r) {
    switch (r.status) {
    case CheckStatus::PASS:
      ++passes;
      break;
    case CheckStatus::WARN:
      ++warnings;
      break;
    case CheckStatus::FAIL:
      ++failures;
      break;
    case CheckStatus::SKIP:
      ++skips;
      break;
    }
  };
  count(hugepages);
  count(mlock);
  count(thp);
  count(swappiness);
  count(overcommit);
  count(edac);

  const char* verdict = "RT_READY";
  if (failures > 0) {
    verdict = "NOT_RT_READY";
  } else if (warnings > 0) {
    verdict = "PARTIALLY_RT_READY";
  }

  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"verdict\": \"{}\",\n", verdict);
  fmt::print("    \"passes\": {},\n", passes);
  fmt::print("    \"warnings\": {},\n", warnings);
  fmt::print("    \"failures\": {},\n", failures);
  fmt::print("    \"skips\": {}\n", skips);
  fmt::print("  }}\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  std::uint64_t requiredSize = 0;

  if (argc > 1) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    std::string error;
    if (!seeker::helpers::args::parseArgs(args, ARG_MAP, pargs, error)) {
      fmt::print(stderr, "Error: {}\n", error);
      return 1;
    }

    if (pargs.count(ARG_HELP) != 0) {
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 0;
    }

    jsonOutput = (pargs.count(ARG_JSON) != 0);

    if (pargs.count(ARG_SIZE) != 0) {
      const std::string_view SZ = pargs[ARG_SIZE][0];
      char* end = nullptr;
      requiredSize = std::strtoull(SZ.data(), &end, 10);
      if (end == SZ.data() || requiredSize == 0) {
        fmt::print(stderr, "Error: Invalid --size value: {}\n", SZ);
        return 1;
      }
    }
  }

  // Gather data
  const mem::HugepageStatus HP = mem::getHugepageStatus();
  const mem::MemoryLockingStatus ML = mem::getMemoryLockingStatus();
  const mem::MemoryStats STATS = mem::getMemoryStats();
  const mem::EdacStatus EDAC = mem::getEdacStatus();

  // Run checks
  const CheckResult HUGEPAGES = checkHugepages(HP);
  const CheckResult MLOCK = checkMemoryLocking(ML, requiredSize);
  const CheckResult THP = checkTHP(STATS);
  const CheckResult SWAPPINESS = checkSwappiness(STATS);
  const CheckResult OVERCOMMIT = checkOvercommit(STATS);
  const CheckResult ECC = checkEdac(EDAC);

  if (jsonOutput) {
    printJson(HUGEPAGES, MLOCK, THP, SWAPPINESS, OVERCOMMIT, ECC);
  } else {
    fmt::print("=== Memory RT Readiness Check ===\n\n");

    if (requiredSize > 0) {
      fmt::print("Required lockable memory: ");
      printBytesHuman(requiredSize);
      fmt::print("\n\n");
    }

    printCheckHuman("Hugepages", HUGEPAGES);
    printCheckHuman("Memory Locking", MLOCK);
    printCheckHuman("Transparent Hugepages", THP);
    printCheckHuman("Swappiness", SWAPPINESS);
    printCheckHuman("Overcommit Policy", OVERCOMMIT);
    printCheckHuman("ECC Memory", ECC);

    int passes = 0, warnings = 0, failures = 0, skips = 0;
    auto count = [&](const CheckResult& r) {
      switch (r.status) {
      case CheckStatus::PASS:
        ++passes;
        break;
      case CheckStatus::WARN:
        ++warnings;
        break;
      case CheckStatus::FAIL:
        ++failures;
        break;
      case CheckStatus::SKIP:
        ++skips;
        break;
      }
    };
    count(HUGEPAGES);
    count(MLOCK);
    count(THP);
    count(SWAPPINESS);
    count(OVERCOMMIT);
    count(ECC);

    printSummaryHuman(passes, warnings, failures, skips);
  }

  // Exit code: 0=pass, 1=warn, 2=fail
  int passes = 0, warnings = 0, failures = 0;
  auto count = [&](const CheckResult& r) {
    switch (r.status) {
    case CheckStatus::PASS:
      ++passes;
      break;
    case CheckStatus::WARN:
      ++warnings;
      break;
    case CheckStatus::FAIL:
      ++failures;
      break;
    default:
      break;
    }
  };
  count(HUGEPAGES);
  count(MLOCK);
  count(THP);
  count(SWAPPINESS);
  count(OVERCOMMIT);
  count(ECC);

  if (failures > 0)
    return 2;
  if (warnings > 0)
    return 1;
  return 0;
}