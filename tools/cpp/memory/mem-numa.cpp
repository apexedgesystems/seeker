/**
 * @file mem-numa.cpp
 * @brief NUMA topology display with inter-node distance matrix.
 *
 * Shows per-node memory, CPU affinity, and NUMA distances. Useful for
 * understanding memory locality on multi-socket systems.
 */

#include "src/memory/inc/HugepageStatus.hpp"
#include "src/memory/inc/NumaTopology.hpp"
#include "src/helpers/inc/Args.hpp"

#include <algorithm>
#include <cstdint>
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
  ARG_DISTANCES = 2,
  ARG_HUGEPAGES = 3,
};

constexpr std::string_view DESCRIPTION =
    "Display NUMA topology, per-node memory, and inter-node distances.";

seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DISTANCES] = {"--distances", 0, false, "Show full distance matrix"};
  map[ARG_HUGEPAGES] = {"--hugepages", 0, false, "Show per-node hugepage allocation"};
  return map;
}

/* ----------------------------- Formatting Helpers ----------------------------- */

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

const char* pageSizeLabel(std::uint64_t bytes) {
  constexpr std::uint64_t MIB_2 = 2ULL * 1024 * 1024;
  constexpr std::uint64_t GIB_1 = 1024ULL * 1024 * 1024;

  if (bytes == MIB_2)
    return "2M";
  if (bytes == GIB_1)
    return "1G";
  return "??";
}

/// Format CPU list compactly (e.g., "0-3,8-11").
std::string formatCpuList(const int* cpus, std::size_t count) {
  if (count == 0)
    return "(none)";

  // Sort CPUs for range detection
  std::vector<int> sorted(cpus, cpus + count);
  std::sort(sorted.begin(), sorted.end());

  std::string result;
  std::size_t i = 0;

  while (i < sorted.size()) {
    const int START = sorted[i];
    int end = START;

    // Find contiguous range
    while (i + 1 < sorted.size() && sorted[i + 1] == sorted[i] + 1) {
      end = sorted[++i];
    }

    if (!result.empty())
      result += ",";

    if (end > START) {
      result += std::to_string(START) + "-" + std::to_string(end);
    } else {
      result += std::to_string(START);
    }
    ++i;
  }

  return result;
}

/* ----------------------------- Human Output ----------------------------- */

void printNodeSummary(const mem::NumaTopology& numa) {
  fmt::print("NUMA Topology: {} node{}\n\n", numa.nodeCount, numa.nodeCount != 1 ? "s" : "");

  // Totals
  fmt::print("Total Memory:   ");
  printBytesHuman(numa.totalMemoryBytes());
  fmt::print("\n");

  fmt::print("Free Memory:    ");
  printBytesHuman(numa.freeMemoryBytes());
  fmt::print(" ({:.1f}%)\n\n", 100.0 * static_cast<double>(numa.freeMemoryBytes()) /
                                   static_cast<double>(numa.totalMemoryBytes()));

  // Per-node details
  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    const mem::NumaNodeInfo& N = numa.nodes[i];
    const double UTIL =
        (N.totalBytes > 0)
            ? 100.0 * (1.0 - static_cast<double>(N.freeBytes) / static_cast<double>(N.totalBytes))
            : 0.0;

    fmt::print("Node {}:\n", N.nodeId);
    fmt::print("  Memory:   ");
    printBytesHuman(N.totalBytes);
    fmt::print(" total, ");
    printBytesHuman(N.freeBytes);
    fmt::print(" free ({:.1f}% used)\n", UTIL);

    fmt::print("  CPUs:     {}\n", formatCpuList(N.cpuIds, N.cpuCount));
  }
}

void printDistanceMatrix(const mem::NumaTopology& numa) {
  if (numa.nodeCount <= 1) {
    fmt::print("\nDistance matrix: N/A (single node)\n");
    return;
  }

  fmt::print("\nNUMA Distance Matrix:\n");
  fmt::print("       ");
  for (std::size_t j = 0; j < numa.nodeCount; ++j) {
    fmt::print("  N{:<2}", numa.nodes[j].nodeId);
  }
  fmt::print("\n");

  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    fmt::print("  N{:<2} ", numa.nodes[i].nodeId);
    for (std::size_t j = 0; j < numa.nodeCount; ++j) {
      const std::uint8_t DIST = numa.getDistance(i, j);
      if (DIST == mem::NUMA_DISTANCE_INVALID) {
        fmt::print("   - ");
      } else if (i == j) {
        fmt::print("  {:>2} ", DIST); // Local distance (10)
      } else {
        fmt::print("  {:>2} ", DIST);
      }
    }
    fmt::print("\n");
  }

  fmt::print("\n  (10 = local, higher = more latency)\n");
}

void printPerNodeHugepages(const mem::HugepageStatus& hp) {
  if (!hp.hasHugepages() || hp.nodeCount == 0) {
    fmt::print("\nPer-node hugepages: N/A\n");
    return;
  }

  fmt::print("\nPer-Node Hugepage Allocation:\n");

  for (std::size_t si = 0; si < hp.sizeCount; ++si) {
    const mem::HugepageSizeStatus& S = hp.sizes[si];
    fmt::print("  {} pages:\n", pageSizeLabel(S.pageSize));

    for (std::size_t ni = 0; ni < hp.nodeCount; ++ni) {
      const mem::HugepageNodeStatus& NS = hp.perNode[si][ni];
      if (NS.nodeId < 0)
        continue;

      fmt::print("    Node {}: {} total, {} free", NS.nodeId, NS.total, NS.free);
      if (NS.surplus > 0) {
        fmt::print(", {} surplus", NS.surplus);
      }
      fmt::print("\n");
    }
  }
}

void printHuman(const mem::NumaTopology& numa, const mem::HugepageStatus& hp, bool showDistances,
                bool showHugepages) {
  if (!numa.isNuma()) {
    fmt::print("System: UMA (single NUMA node)\n\n");

    if (numa.nodeCount > 0) {
      const mem::NumaNodeInfo& N = numa.nodes[0];
      fmt::print("Memory: ");
      printBytesHuman(N.totalBytes);
      fmt::print(" total, ");
      printBytesHuman(N.freeBytes);
      fmt::print(" free\n");

      fmt::print("CPUs:   {}\n", formatCpuList(N.cpuIds, N.cpuCount));
    }
    return;
  }

  printNodeSummary(numa);

  if (showDistances) {
    printDistanceMatrix(numa);
  }

  if (showHugepages) {
    printPerNodeHugepages(hp);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const mem::NumaTopology& numa, const mem::HugepageStatus& hp, bool showDistances,
               bool showHugepages) {
  fmt::print("{{\n");

  // Summary
  fmt::print("  \"nodeCount\": {},\n", numa.nodeCount);
  fmt::print("  \"isNuma\": {},\n", numa.isNuma() ? "true" : "false");
  fmt::print("  \"totalMemoryBytes\": {},\n", numa.totalMemoryBytes());
  fmt::print("  \"freeMemoryBytes\": {},\n", numa.freeMemoryBytes());

  // Nodes
  fmt::print("  \"nodes\": [\n");
  for (std::size_t i = 0; i < numa.nodeCount; ++i) {
    const mem::NumaNodeInfo& N = numa.nodes[i];
    fmt::print("    {{\n");
    fmt::print("      \"nodeId\": {},\n", N.nodeId);
    fmt::print("      \"totalBytes\": {},\n", N.totalBytes);
    fmt::print("      \"freeBytes\": {},\n", N.freeBytes);
    fmt::print("      \"usedBytes\": {},\n", N.usedBytes());
    fmt::print("      \"cpuCount\": {},\n", N.cpuCount);
    fmt::print("      \"cpus\": [");
    for (std::size_t j = 0; j < N.cpuCount; ++j) {
      if (j > 0)
        fmt::print(", ");
      fmt::print("{}", N.cpuIds[j]);
    }
    fmt::print("]\n");
    fmt::print("    }}{}\n", (i + 1 < numa.nodeCount) ? "," : "");
  }
  fmt::print("  ]");

  // Distance matrix
  if (showDistances && numa.nodeCount > 1) {
    fmt::print(",\n  \"distances\": [\n");
    for (std::size_t i = 0; i < numa.nodeCount; ++i) {
      fmt::print("    [");
      for (std::size_t j = 0; j < numa.nodeCount; ++j) {
        if (j > 0)
          fmt::print(", ");
        const std::uint8_t DIST = numa.getDistance(i, j);
        fmt::print("{}", static_cast<int>(DIST));
      }
      fmt::print("]{}\n", (i + 1 < numa.nodeCount) ? "," : "");
    }
    fmt::print("  ]");
  }

  // Per-node hugepages
  if (showHugepages && hp.hasHugepages() && hp.nodeCount > 0) {
    fmt::print(",\n  \"hugepagesPerNode\": [\n");
    bool firstSize = true;
    for (std::size_t si = 0; si < hp.sizeCount; ++si) {
      const mem::HugepageSizeStatus& S = hp.sizes[si];
      if (!firstSize)
        fmt::print(",\n");
      firstSize = false;

      fmt::print("    {{\n");
      fmt::print("      \"pageSize\": {},\n", S.pageSize);
      fmt::print("      \"nodes\": [");
      bool firstNode = true;
      for (std::size_t ni = 0; ni < hp.nodeCount; ++ni) {
        const mem::HugepageNodeStatus& NS = hp.perNode[si][ni];
        if (NS.nodeId < 0)
          continue;
        if (!firstNode)
          fmt::print(", ");
        firstNode = false;
        fmt::print("{{\"nodeId\": {}, \"total\": {}, \"free\": {}, \"surplus\": {}}}", NS.nodeId,
                   NS.total, NS.free, NS.surplus);
      }
      fmt::print("]\n");
      fmt::print("    }}");
    }
    fmt::print("\n  ]");
  }

  fmt::print("\n}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool showDistances = false;
  bool showHugepages = false;

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
    showDistances = (pargs.count(ARG_DISTANCES) != 0);
    showHugepages = (pargs.count(ARG_HUGEPAGES) != 0);
  }

  // Gather data
  const mem::NumaTopology NUMA = mem::getNumaTopology();
  const mem::HugepageStatus HP = showHugepages ? mem::getHugepageStatus() : mem::HugepageStatus{};

  if (jsonOutput) {
    printJson(NUMA, HP, showDistances, showHugepages);
  } else {
    printHuman(NUMA, HP, showDistances, showHugepages);
  }

  return 0;
}