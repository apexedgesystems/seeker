/**
 * @file mem-info.cpp
 * @brief One-shot memory system identification and status dump.
 *
 * Displays page sizes, memory usage, VM policies, hugepage allocation,
 * NUMA topology, memory locking limits, and ECC/EDAC status. Designed
 * for quick system assessment on RT and HPC systems.
 */

#include "src/memory/inc/EdacStatus.hpp"
#include "src/memory/inc/HugepageStatus.hpp"
#include "src/memory/inc/MemoryLocking.hpp"
#include "src/memory/inc/MemoryStats.hpp"
#include "src/memory/inc/NumaTopology.hpp"
#include "src/memory/inc/PageSizes.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <cstdio>
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
};

constexpr std::string_view DESCRIPTION =
    "Display memory topology, page sizes, hugepage status, VM policies, and ECC/EDAC status.";

seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  return map;
}

/* ----------------------------- Formatting Helpers ----------------------------- */

/// Format bytes as human-readable string (e.g., "16.0 GiB").
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

/// Get page size as string (e.g., "2 MiB", "1 GiB").
const char* pageSizeLabel(std::uint64_t bytes) {
  constexpr std::uint64_t MIB_2 = 2ULL * 1024 * 1024;
  constexpr std::uint64_t GIB_1 = 1024ULL * 1024 * 1024;
  constexpr std::uint64_t MIB_16 = 16ULL * 1024 * 1024;
  constexpr std::uint64_t MIB_32 = 32ULL * 1024 * 1024;
  constexpr std::uint64_t MIB_512 = 512ULL * 1024 * 1024;

  if (bytes == MIB_2)
    return "2 MiB";
  if (bytes == GIB_1)
    return "1 GiB";
  if (bytes == MIB_16)
    return "16 MiB";
  if (bytes == MIB_32)
    return "32 MiB";
  if (bytes == MIB_512)
    return "512 MiB";
  return "custom";
}

/* ----------------------------- Human Output ----------------------------- */

void printPageSizes(const mem::PageSizes& ps) {
  fmt::print("Page Sizes:\n");
  fmt::print("  Base page:    {} bytes ({} KiB)\n", ps.basePageBytes, ps.basePageBytes / 1024);

  if (ps.hugeSizeCount > 0) {
    fmt::print("  Hugepages:    ");
    for (std::size_t i = 0; i < ps.hugeSizeCount; ++i) {
      if (i > 0)
        fmt::print(", ");
      fmt::print("{}", pageSizeLabel(ps.hugeSizes[i]));
    }
    fmt::print("\n");
  } else {
    fmt::print("  Hugepages:    (none available)\n");
  }
}

void printMemoryStats(const mem::MemoryStats& stats) {
  fmt::print("\nMemory Usage:\n");
  fmt::print("  Total:        ");
  printBytesHuman(stats.totalBytes);
  fmt::print("\n");

  fmt::print("  Available:    ");
  printBytesHuman(stats.availableBytes);
  fmt::print(" ({:.1f}%)\n", 100.0 * static_cast<double>(stats.availableBytes) /
                                 static_cast<double>(stats.totalBytes));

  fmt::print("  Used:         ");
  printBytesHuman(stats.usedBytes());
  fmt::print(" ({:.1f}%)\n", stats.utilizationPercent());

  fmt::print("  Buffers:      ");
  printBytesHuman(stats.buffersBytes);
  fmt::print("\n");

  fmt::print("  Cached:       ");
  printBytesHuman(stats.cachedBytes);
  fmt::print("\n");

  if (stats.swapTotalBytes > 0) {
    fmt::print("  Swap Total:   ");
    printBytesHuman(stats.swapTotalBytes);
    fmt::print("\n");

    fmt::print("  Swap Used:    ");
    printBytesHuman(stats.swapUsedBytes());
    fmt::print(" ({:.1f}%)\n", stats.swapUtilizationPercent());
  } else {
    fmt::print("  Swap:         (disabled)\n");
  }

  fmt::print("\nVM Policies:\n");
  if (stats.swappiness >= 0) {
    fmt::print("  Swappiness:   {}", stats.swappiness);
    if (stats.swappiness <= 10) {
      fmt::print(" (RT-friendly)");
    } else if (stats.swappiness >= 60) {
      fmt::print(" (aggressive)");
    }
    fmt::print("\n");
  }

  if (stats.overcommitMemory >= 0) {
    fmt::print("  Overcommit:   {}", stats.overcommitMemory);
    if (stats.overcommitMemory == 0) {
      fmt::print(" (heuristic)");
    } else if (stats.overcommitMemory == 1) {
      fmt::print(" (always)");
    } else if (stats.overcommitMemory == 2) {
      fmt::print(" (never)");
    }
    fmt::print("\n");
  }

  if (stats.zoneReclaimMode >= 0) {
    fmt::print("  Zone Reclaim: {}\n", stats.zoneReclaimMode);
  }

  if (stats.thpEnabled[0] != '\0') {
    fmt::print("  THP Enabled:  {}\n", stats.thpEnabled.data());
  }
  if (stats.thpDefrag[0] != '\0') {
    fmt::print("  THP Defrag:   {}\n", stats.thpDefrag.data());
  }
}

void printHugepageStatus(const mem::HugepageStatus& hp) {
  if (!hp.hasHugepages()) {
    fmt::print("\nHugepages: (none configured)\n");
    return;
  }

  fmt::print("\nHugepage Allocation:\n");
  for (std::size_t i = 0; i < hp.sizeCount; ++i) {
    const mem::HugepageSizeStatus& S = hp.sizes[i];
    fmt::print("  {}:\n", pageSizeLabel(S.pageSize));
    fmt::print("    Total:      {} pages (", S.total);
    printBytesHuman(S.totalBytes());
    fmt::print(")\n");
    fmt::print("    Free:       {} pages\n", S.free);
    fmt::print("    Used:       {} pages\n", S.used());
    if (S.reserved > 0) {
      fmt::print("    Reserved:   {} pages\n", S.reserved);
    }
    if (S.surplus > 0) {
      fmt::print("    Surplus:    {} pages\n", S.surplus);
    }
  }
}

void printMemoryLocking(const mem::MemoryLockingStatus& ml) {
  fmt::print("\nMemory Locking:\n");

  if (ml.isUnlimited()) {
    fmt::print("  Limit:        unlimited");
    if (ml.hasCapIpcLock) {
      fmt::print(" (CAP_IPC_LOCK)");
    } else if (ml.isRoot) {
      fmt::print(" (root)");
    }
    fmt::print("\n");
  } else {
    fmt::print("  Soft Limit:   ");
    printBytesHuman(ml.softLimitBytes);
    fmt::print("\n");

    fmt::print("  Hard Limit:   ");
    printBytesHuman(ml.hardLimitBytes);
    fmt::print("\n");
  }

  fmt::print("  Current:      ");
  printBytesHuman(ml.currentLockedBytes);
  fmt::print("\n");

  if (!ml.isUnlimited()) {
    fmt::print("  Available:    ");
    printBytesHuman(ml.availableBytes());
    fmt::print("\n");
  }
}

void printNumaTopology(const mem::NumaTopology& numa) {
  if (!numa.isNuma()) {
    fmt::print("\nNUMA: (single node / UMA system)\n");
    return;
  }

  fmt::print("\nNUMA Topology: {} nodes\n", numa.nodeCount);
  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    const mem::NumaNodeInfo& N = numa.nodes[i];
    fmt::print("  Node {}:\n", N.nodeId);
    fmt::print("    Memory:     ");
    printBytesHuman(N.totalBytes);
    fmt::print(" total, ");
    printBytesHuman(N.freeBytes);
    fmt::print(" free\n");

    fmt::print("    CPUs:       ");
    for (std::size_t j = 0; j < N.cpuCount; ++j) {
      if (j > 0)
        fmt::print(",");
      fmt::print("{}", N.cpuIds[j]);
    }
    fmt::print("\n");
  }
}

void printEdacStatus(const mem::EdacStatus& edac) {
  fmt::print("\nECC/EDAC Status:\n");

  if (!edac.edacSupported) {
    fmt::print("  Status:       Not available (no ECC memory or EDAC module not loaded)\n");
    return;
  }

  fmt::print("  ECC Enabled:  {}\n", edac.eccEnabled ? "yes" : "no");
  fmt::print("  Controllers:  {}\n", edac.mcCount);

  if (edac.pollIntervalMs > 0) {
    fmt::print("  Poll Interval: {} ms\n", edac.pollIntervalMs);
  }

  // Error summary
  fmt::print("  Correctable:  {}", edac.totalCeCount);
  if (edac.totalCeCount > 0) {
    fmt::print(" (soft errors - memory still functioning)");
  }
  fmt::print("\n");

  fmt::print("  Uncorrectable: {}", edac.totalUeCount);
  if (edac.totalUeCount > 0) {
    fmt::print(" *** CRITICAL - data corruption possible ***");
  }
  fmt::print("\n");

  // Per-controller details if errors present or multiple controllers
  if (edac.mcCount > 1 || edac.hasErrors()) {
    for (std::size_t i = 0; i < edac.mcCount; ++i) {
      const mem::MemoryController& MC = edac.controllers[i];
      fmt::print("  {}:", MC.name.data());
      if (MC.mcType[0] != '\0') {
        fmt::print(" {}", MC.mcType.data());
      }
      if (MC.memType[0] != '\0') {
        fmt::print(" ({})", MC.memType.data());
      }
      fmt::print(" CE={} UE={}\n", MC.ceCount, MC.ueCount);
    }
  }
}

void printHuman(const mem::PageSizes& ps, const mem::MemoryStats& stats,
                const mem::HugepageStatus& hp, const mem::MemoryLockingStatus& ml,
                const mem::NumaTopology& numa, const mem::EdacStatus& edac) {
  printPageSizes(ps);
  printMemoryStats(stats);
  printHugepageStatus(hp);
  printMemoryLocking(ml);
  printNumaTopology(numa);
  printEdacStatus(edac);
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const mem::PageSizes& ps, const mem::MemoryStats& stats,
               const mem::HugepageStatus& hp, const mem::MemoryLockingStatus& ml,
               const mem::NumaTopology& numa, const mem::EdacStatus& edac) {
  fmt::print("{{\n");

  // Page sizes
  fmt::print("  \"pageSizes\": {{\n");
  fmt::print("    \"basePageBytes\": {},\n", ps.basePageBytes);
  fmt::print("    \"hugepageSizes\": [");
  for (std::size_t i = 0; i < ps.hugeSizeCount; ++i) {
    if (i > 0)
      fmt::print(", ");
    fmt::print("{}", ps.hugeSizes[i]);
  }
  fmt::print("]\n  }},\n");

  // Memory stats
  fmt::print("  \"memory\": {{\n");
  fmt::print("    \"totalBytes\": {},\n", stats.totalBytes);
  fmt::print("    \"freeBytes\": {},\n", stats.freeBytes);
  fmt::print("    \"availableBytes\": {},\n", stats.availableBytes);
  fmt::print("    \"usedBytes\": {},\n", stats.usedBytes());
  fmt::print("    \"buffersBytes\": {},\n", stats.buffersBytes);
  fmt::print("    \"cachedBytes\": {},\n", stats.cachedBytes);
  fmt::print("    \"swapTotalBytes\": {},\n", stats.swapTotalBytes);
  fmt::print("    \"swapFreeBytes\": {},\n", stats.swapFreeBytes);
  fmt::print("    \"swapUsedBytes\": {},\n", stats.swapUsedBytes());
  fmt::print("    \"utilizationPercent\": {:.2f}\n", stats.utilizationPercent());
  fmt::print("  }},\n");

  // VM policies
  fmt::print("  \"vmPolicies\": {{\n");
  fmt::print("    \"swappiness\": {},\n", stats.swappiness);
  fmt::print("    \"overcommitMemory\": {},\n", stats.overcommitMemory);
  fmt::print("    \"zoneReclaimMode\": {},\n", stats.zoneReclaimMode);
  fmt::print("    \"thpEnabled\": \"{}\",\n", stats.thpEnabled.data());
  fmt::print("    \"thpDefrag\": \"{}\"\n", stats.thpDefrag.data());
  fmt::print("  }},\n");

  // Hugepages
  fmt::print("  \"hugepages\": {{\n");
  fmt::print("    \"configured\": {},\n", hp.hasHugepages() ? "true" : "false");
  fmt::print("    \"totalBytes\": {},\n", hp.totalBytes());
  fmt::print("    \"freeBytes\": {},\n", hp.freeBytes());
  fmt::print("    \"usedBytes\": {},\n", hp.usedBytes());
  fmt::print("    \"sizes\": [");
  for (std::size_t i = 0; i < hp.sizeCount; ++i) {
    if (i > 0)
      fmt::print(", ");
    const mem::HugepageSizeStatus& S = hp.sizes[i];
    fmt::print("{{\"pageSize\": {}, \"total\": {}, \"free\": {}, \"used\": {}, \"reserved\": {}, "
               "\"surplus\": {}}}",
               S.pageSize, S.total, S.free, S.used(), S.reserved, S.surplus);
  }
  fmt::print("]\n  }},\n");

  // Memory locking
  fmt::print("  \"memoryLocking\": {{\n");
  fmt::print("    \"softLimitBytes\": {},\n", ml.softLimitBytes);
  fmt::print("    \"hardLimitBytes\": {},\n", ml.hardLimitBytes);
  fmt::print("    \"currentLockedBytes\": {},\n", ml.currentLockedBytes);
  fmt::print("    \"availableBytes\": {},\n", ml.availableBytes());
  fmt::print("    \"unlimited\": {},\n", ml.isUnlimited() ? "true" : "false");
  fmt::print("    \"hasCapIpcLock\": {},\n", ml.hasCapIpcLock ? "true" : "false");
  fmt::print("    \"isRoot\": {}\n", ml.isRoot ? "true" : "false");
  fmt::print("  }},\n");

  // NUMA topology
  fmt::print("  \"numa\": {{\n");
  fmt::print("    \"nodeCount\": {},\n", numa.nodeCount);
  fmt::print("    \"isNuma\": {},\n", numa.isNuma() ? "true" : "false");
  fmt::print("    \"totalMemoryBytes\": {},\n", numa.totalMemoryBytes());
  fmt::print("    \"freeMemoryBytes\": {},\n", numa.freeMemoryBytes());
  fmt::print("    \"nodes\": [");
  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    if (i > 0)
      fmt::print(", ");
    const mem::NumaNodeInfo& N = numa.nodes[i];
    fmt::print(
        "{{\"nodeId\": {}, \"totalBytes\": {}, \"freeBytes\": {}, \"cpuCount\": {}, \"cpus\": [",
        N.nodeId, N.totalBytes, N.freeBytes, N.cpuCount);
    for (std::size_t j = 0; j < N.cpuCount; ++j) {
      if (j > 0)
        fmt::print(", ");
      fmt::print("{}", N.cpuIds[j]);
    }
    fmt::print("]}}");
  }
  fmt::print("]\n  }},\n");

  // EDAC status
  fmt::print("  \"edac\": {{\n");
  fmt::print("    \"supported\": {},\n", edac.edacSupported ? "true" : "false");
  fmt::print("    \"eccEnabled\": {},\n", edac.eccEnabled ? "true" : "false");
  fmt::print("    \"mcCount\": {},\n", edac.mcCount);
  fmt::print("    \"totalCeCount\": {},\n", edac.totalCeCount);
  fmt::print("    \"totalUeCount\": {},\n", edac.totalUeCount);
  fmt::print("    \"hasErrors\": {},\n", edac.hasErrors() ? "true" : "false");
  fmt::print("    \"hasCriticalErrors\": {},\n", edac.hasCriticalErrors() ? "true" : "false");
  fmt::print("    \"pollIntervalMs\": {},\n", edac.pollIntervalMs);
  fmt::print("    \"controllers\": [");
  for (std::size_t i = 0; i < edac.mcCount; ++i) {
    if (i > 0)
      fmt::print(", ");
    const mem::MemoryController& MC = edac.controllers[i];
    fmt::print("{{\"name\": \"{}\", \"mcIndex\": {}, \"mcType\": \"{}\", \"memType\": \"{}\", "
               "\"edacMode\": \"{}\", \"sizeMb\": {}, \"ceCount\": {}, \"ueCount\": {}}}",
               MC.name.data(), MC.mcIndex, MC.mcType.data(), MC.memType.data(), MC.edacMode.data(),
               MC.sizeMb, MC.ceCount, MC.ueCount);
  }
  fmt::print("]\n  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;

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
  }

  // Gather data
  const mem::PageSizes PS = mem::getPageSizes();
  const mem::MemoryStats STATS = mem::getMemoryStats();
  const mem::HugepageStatus HP = mem::getHugepageStatus();
  const mem::MemoryLockingStatus ML = mem::getMemoryLockingStatus();
  const mem::NumaTopology NUMA = mem::getNumaTopology();
  const mem::EdacStatus EDAC = mem::getEdacStatus();

  if (jsonOutput) {
    printJson(PS, STATS, HP, ML, NUMA, EDAC);
  } else {
    printHuman(PS, STATS, HP, ML, NUMA, EDAC);
  }

  return 0;
}