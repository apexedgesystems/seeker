/**
 * @file NumaTopology_uTest.cpp
 * @brief Unit tests for seeker::memory::NumaTopology.
 *
 * Notes:
 *  - Live tests are platform-agnostic: assert invariants, not exact values.
 *  - Single-node systems will have nodeCount=1 or 0 (both valid).
 *  - Fake NUMA tests read a tree cut from a board capture through the
 *    filesystem root (FilesystemRootTest), so they run on any host; the
 *    two-node tests read a synthetic physical topology the same way.
 *  - Live tests that assume physical NUMA skip when the kernel lists a CPU
 *    under more than one node (emulated NUMA).
 */

#include "src/memory/inc/NumaTopology.hpp"
#include "src/helpers/utst/inc/FilesystemRootTest.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

using seeker::helpers::files::filesystemRoot;
using seeker::memory::getNumaTopology;
using seeker::memory::MAX_CPUS_PER_NODE;
using seeker::memory::MAX_NUMA_NODES;
using seeker::memory::NUMA_DISTANCE_INVALID;
using seeker::memory::NUMA_DISTANCE_LOCAL;
using seeker::memory::NumaNodeInfo;
using seeker::memory::NumaTopology;
using seeker::test_support::FilesystemRootTest;

namespace {

/// True when the live kernel lists a CPU under more than one node, the
/// signature of emulated NUMA (numa=fake gives each fake node the CPUs of its
/// physical node). Read from the cpulist files directly, not through the
/// reader under test.
bool liveNumaIsEmulated() {
  std::set<int> seen;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/sys/devices/system/node", ec)) {
    const std::string NAME = entry.path().filename().string();
    if (NAME.size() <= 4 || NAME.compare(0, 4, "node") != 0 ||
        NAME.find_first_not_of("0123456789", 4) != std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path() / "cpulist");
    std::string list;
    std::getline(in, list);
    // Comma-separated CPUs and ranges: "0-3,8,10-11"
    std::stringstream items(list);
    std::string item;
    while (std::getline(items, item, ',')) {
      if (item.empty()) {
        continue;
      }
      const std::size_t DASH = item.find('-');
      const int FIRST = std::stoi(item.substr(0, DASH));
      const int LAST = (DASH == std::string::npos) ? FIRST : std::stoi(item.substr(DASH + 1));
      for (int cpu = FIRST; cpu <= LAST; ++cpu) {
        if (!seen.insert(cpu).second) {
          return true;
        }
      }
    }
  }
  return false;
}

} // namespace

class NumaTopologyTest : public ::testing::Test {
protected:
  NumaTopology topo_{};
  bool hasNuma_{false};
  bool emulatedNuma_{false};

  void SetUp() override {
    topo_ = getNumaTopology();
    hasNuma_ = (topo_.nodeCount > 0);
    emulatedNuma_ = liveNumaIsEmulated();
  }
};

/* ----------------------------- Basic Invariants ----------------------------- */

/** @test Node count is within bounds. */
TEST_F(NumaTopologyTest, NodeCountWithinBounds) { EXPECT_LE(topo_.nodeCount, MAX_NUMA_NODES); }

/** @test Empty result is valid (NUMA may not be available). */
TEST_F(NumaTopologyTest, EmptyResultValid) {
  if (topo_.nodeCount == 0) {
    GTEST_LOG_(INFO) << "NUMA not available or single-node system";
  }
  SUCCEED();
}

/** @test Node IDs are non-negative. */
TEST_F(NumaTopologyTest, NodeIdsNonNegative) {
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    EXPECT_GE(topo_.nodes[i].nodeId, 0) << "Node index " << i;
  }
}

/** @test Node IDs are unique. */
TEST_F(NumaTopologyTest, NodeIdsUnique) {
  std::set<int> seen;
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    const auto [IT, INSERTED] = seen.insert(topo_.nodes[i].nodeId);
    EXPECT_TRUE(INSERTED) << "Duplicate node ID: " << topo_.nodes[i].nodeId;
  }
}

/* ----------------------------- Memory Tests ----------------------------- */

/** @test Total memory is positive (when NUMA available). */
TEST_F(NumaTopologyTest, TotalMemoryPositive) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }

  EXPECT_GT(topo_.totalMemoryBytes(), 0U);
}

/** @test Per-node free <= total. */
TEST_F(NumaTopologyTest, FreeNotExceedTotal) {
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    EXPECT_LE(topo_.nodes[i].freeBytes, topo_.nodes[i].totalBytes)
        << "Node " << topo_.nodes[i].nodeId;
  }
}

/** @test Aggregate total matches sum of per-node totals. */
TEST_F(NumaTopologyTest, AggregateTotalMatchesSum) {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    sum += topo_.nodes[i].totalBytes;
  }
  EXPECT_EQ(topo_.totalMemoryBytes(), sum);
}

/** @test Aggregate free matches sum of per-node free. */
TEST_F(NumaTopologyTest, AggregateFreeMatchesSum) {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    sum += topo_.nodes[i].freeBytes;
  }
  EXPECT_EQ(topo_.freeMemoryBytes(), sum);
}

/** @test usedBytes is consistent. */
TEST_F(NumaTopologyTest, UsedBytesConsistent) {
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    const auto& NODE = topo_.nodes[i];
    const std::uint64_t EXPECTED =
        (NODE.totalBytes > NODE.freeBytes) ? (NODE.totalBytes - NODE.freeBytes) : 0;
    EXPECT_EQ(NODE.usedBytes(), EXPECTED) << "Node " << NODE.nodeId;
  }
}

/* ----------------------------- CPU Tests ----------------------------- */

/** @test CPU count per node is within bounds. */
TEST_F(NumaTopologyTest, CpuCountWithinBounds) {
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    EXPECT_LE(topo_.nodes[i].cpuCount, MAX_CPUS_PER_NODE) << "Node " << topo_.nodes[i].nodeId;
  }
}

/** @test CPU IDs are non-negative. */
TEST_F(NumaTopologyTest, CpuIdsNonNegative) {
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = 0; j < topo_.nodes[i].cpuCount; ++j) {
      EXPECT_GE(topo_.nodes[i].cpuIds[j], 0) << "Node " << topo_.nodes[i].nodeId << " CPU " << j;
    }
  }
}

/** @test CPU IDs are unique across all nodes. */
TEST_F(NumaTopologyTest, CpuIdsUniqueAcrossNodes) {
  if (emulatedNuma_) {
    GTEST_SKIP() << "Emulated NUMA: nodes share CPUs (NumaTopologyTwoNodeTest covers this)";
  }

  std::set<int> seen;
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = 0; j < topo_.nodes[i].cpuCount; ++j) {
      const int CPU_ID = topo_.nodes[i].cpuIds[j];
      const auto [IT, INSERTED] = seen.insert(CPU_ID);
      EXPECT_TRUE(INSERTED) << "Duplicate CPU ID: " << CPU_ID;
    }
  }
}

/** @test hasCpu returns correct value. */
TEST_F(NumaTopologyTest, HasCpuCorrect) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    const auto& NODE = topo_.nodes[i];
    for (std::size_t j = 0; j < NODE.cpuCount; ++j) {
      EXPECT_TRUE(NODE.hasCpu(NODE.cpuIds[j]))
          << "Node " << NODE.nodeId << " missing CPU " << NODE.cpuIds[j];
    }
    // Check a CPU that shouldn't be there (use large ID)
    EXPECT_FALSE(NODE.hasCpu(99999));
  }
}

/** @test findNodeForCpu returns correct node. */
TEST_F(NumaTopologyTest, FindNodeForCpuCorrect) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }
  if (emulatedNuma_) {
    GTEST_SKIP() << "Emulated NUMA: nodes share CPUs (NumaTopologyTwoNodeTest covers this)";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    const auto& NODE = topo_.nodes[i];
    for (std::size_t j = 0; j < NODE.cpuCount; ++j) {
      const int FOUND = topo_.findNodeForCpu(NODE.cpuIds[j]);
      EXPECT_EQ(FOUND, static_cast<int>(i)) << "CPU " << NODE.cpuIds[j];
    }
  }
}

/** @test findNodeForCpu returns -1 for unknown CPU. */
TEST_F(NumaTopologyTest, FindNodeForCpuUnknown) {
  EXPECT_EQ(topo_.findNodeForCpu(99999), -1);
  EXPECT_EQ(topo_.findNodeForCpu(-1), -1);
}

/* ----------------------------- Distance Matrix Tests ----------------------------- */

/** @test Local distance is NUMA_DISTANCE_LOCAL (10). */
TEST_F(NumaTopologyTest, LocalDistanceCorrect) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    EXPECT_EQ(topo_.distance[i][i], NUMA_DISTANCE_LOCAL) << "Node " << i << " self-distance";
  }
}

/** @test Distance is symmetric. */
TEST_F(NumaTopologyTest, DistanceSymmetric) {
  if (topo_.nodeCount < 2) {
    GTEST_SKIP() << "Need multiple NUMA nodes for symmetry test";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = i + 1; j < topo_.nodeCount; ++j) {
      EXPECT_EQ(topo_.distance[i][j], topo_.distance[j][i])
          << "Distance asymmetric between nodes " << i << " and " << j;
    }
  }
}

/** @test Remote distance is greater than local. */
TEST_F(NumaTopologyTest, RemoteDistanceGreaterThanLocal) {
  if (topo_.nodeCount < 2) {
    GTEST_SKIP() << "Need multiple NUMA nodes for remote distance test";
  }
  if (emulatedNuma_) {
    GTEST_SKIP() << "Emulated NUMA: nodes share CPUs (NumaTopologyTwoNodeTest covers this)";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = 0; j < topo_.nodeCount; ++j) {
      if (i != j && topo_.distance[i][j] != NUMA_DISTANCE_INVALID) {
        EXPECT_GT(topo_.distance[i][j], NUMA_DISTANCE_LOCAL)
            << "Remote distance " << i << " -> " << j << " not greater than local";
      }
    }
  }
}

/** @test getDistance returns correct values. */
TEST_F(NumaTopologyTest, GetDistanceCorrect) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }

  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = 0; j < topo_.nodeCount; ++j) {
      EXPECT_EQ(topo_.getDistance(i, j), topo_.distance[i][j]);
    }
  }
}

/** @test getDistance returns INVALID for out-of-bounds. */
TEST_F(NumaTopologyTest, GetDistanceOutOfBounds) {
  EXPECT_EQ(topo_.getDistance(MAX_NUMA_NODES + 1, 0), NUMA_DISTANCE_INVALID);
  EXPECT_EQ(topo_.getDistance(0, MAX_NUMA_NODES + 1), NUMA_DISTANCE_INVALID);
  EXPECT_EQ(topo_.getDistance(topo_.nodeCount, 0), NUMA_DISTANCE_INVALID);
}

/* ----------------------------- isNuma Test ----------------------------- */

/** @test isNuma returns true only for multi-node systems. */
TEST_F(NumaTopologyTest, IsNumaCorrect) {
  if (topo_.nodeCount > 1) {
    EXPECT_TRUE(topo_.isNuma());
  } else {
    EXPECT_FALSE(topo_.isNuma());
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(NumaTopologyTest, ToStringNonEmpty) {
  const std::string OUTPUT = topo_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains NUMA info. */
TEST_F(NumaTopologyTest, ToStringContainsNumaInfo) {
  const std::string OUTPUT = topo_.toString();
  EXPECT_NE(OUTPUT.find("NUMA"), std::string::npos);
}

/** @test NumaNodeInfo::toString produces output. */
TEST_F(NumaTopologyTest, NodeToStringNonEmpty) {
  if (!hasNuma_) {
    GTEST_SKIP() << "NUMA not available";
  }

  const std::string OUTPUT = topo_.nodes[0].toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Node"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default NumaNodeInfo is zeroed. */
TEST(NumaTopologyDefaultTest, NodeDefaultZeroed) {
  const NumaNodeInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.nodeId, -1);
  EXPECT_EQ(DEFAULT.totalBytes, 0U);
  EXPECT_EQ(DEFAULT.freeBytes, 0U);
  EXPECT_EQ(DEFAULT.cpuCount, 0U);
}

/** @test Default NumaTopology is empty. */
TEST(NumaTopologyDefaultTest, TopologyDefaultEmpty) {
  const NumaTopology DEFAULT{};

  EXPECT_EQ(DEFAULT.nodeCount, 0U);
  EXPECT_FALSE(DEFAULT.isNuma());
  EXPECT_EQ(DEFAULT.totalMemoryBytes(), 0U);
  EXPECT_EQ(DEFAULT.freeMemoryBytes(), 0U);
}

/** @test Default usedBytes is zero. */
TEST(NumaTopologyDefaultTest, DefaultUsedBytesZero) {
  const NumaNodeInfo DEFAULT{};
  EXPECT_EQ(DEFAULT.usedBytes(), 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Repeated calls return consistent results. */
TEST(NumaTopologyDeterminismTest, ConsistentResults) {
  const NumaTopology TOPO1 = getNumaTopology();
  const NumaTopology TOPO2 = getNumaTopology();

  // Node count and IDs should be stable
  EXPECT_EQ(TOPO1.nodeCount, TOPO2.nodeCount);

  for (std::size_t i = 0; i < TOPO1.nodeCount; ++i) {
    EXPECT_EQ(TOPO1.nodes[i].nodeId, TOPO2.nodes[i].nodeId);
    EXPECT_EQ(TOPO1.nodes[i].cpuCount, TOPO2.nodes[i].cpuCount);
    // Memory totals should be stable (free may vary)
    EXPECT_EQ(TOPO1.nodes[i].totalBytes, TOPO2.nodes[i].totalBytes);
  }

  // Distance matrix should be identical
  for (std::size_t i = 0; i < TOPO1.nodeCount; ++i) {
    for (std::size_t j = 0; j < TOPO1.nodeCount; ++j) {
      EXPECT_EQ(TOPO1.distance[i][j], TOPO2.distance[i][j]);
    }
  }
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test hasCpu handles negative CPU ID. */
TEST(NumaNodeInfoEdgeCaseTest, HasCpuNegative) {
  NumaNodeInfo node{};
  node.cpuIds[0] = 0;
  node.cpuIds[1] = 1;
  node.cpuCount = 2;

  EXPECT_FALSE(node.hasCpu(-1));
}

/** @test usedBytes handles free > total (doesn't underflow). */
TEST(NumaNodeInfoEdgeCaseTest, UsedBytesNoUnderflow) {
  NumaNodeInfo node{};
  node.totalBytes = 100;
  node.freeBytes = 200; // Invalid but shouldn't crash

  EXPECT_EQ(node.usedBytes(), 0U);
}
/* ----------------------------- Fake NUMA Tree Tests ----------------------------- */

/// /sys/devices/system/node of a Raspberry Pi 4 booted with numa=fake=2
/// (captured 2026-10-10): two nodes over the same four CPUs with local
/// distances; each meminfo holds the two lines the reader parses.
class NumaTopologyFakeNumaTest : public FilesystemRootTest {
protected:
  NumaTopology topo_{};

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(FilesystemRootTest::SetUp());
    put("/sys/devices/system/node/node0/cpulist", "0-3\n");
    put("/sys/devices/system/node/node0/distance", "10 10\n");
    put("/sys/devices/system/node/node0/meminfo", "Node 0 MemTotal:        3959444 kB\n"
                                                  "Node 0 MemFree:         2653064 kB\n");
    put("/sys/devices/system/node/node1/cpulist", "0-3\n");
    put("/sys/devices/system/node/node1/distance", "10 10\n");
    put("/sys/devices/system/node/node1/meminfo", "Node 1 MemTotal:        4048016 kB\n"
                                                  "Node 1 MemFree:         2768476 kB\n");
    topo_ = getNumaTopology();
  }
};

/** @test Both fake nodes are reported, in node order */
TEST_F(NumaTopologyFakeNumaTest, ReportsBothNodes) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  EXPECT_EQ(topo_.nodes[0].nodeId, 0);
  EXPECT_EQ(topo_.nodes[1].nodeId, 1);
  EXPECT_TRUE(topo_.isNuma());
}

/** @test Both nodes list the same four CPUs: fake nodes overlap */
TEST_F(NumaTopologyFakeNumaTest, BothNodesListTheSameFourCpus) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  for (std::size_t n = 0; n < topo_.nodeCount; ++n) {
    SCOPED_TRACE(::testing::Message() << "node " << n);
    ASSERT_EQ(topo_.nodes[n].cpuCount, 4U);
    for (int cpu = 0; cpu < 4; ++cpu) {
      EXPECT_EQ(topo_.nodes[n].cpuIds[cpu], cpu);
    }
  }
}

/** @test Every distance between the fake nodes is the local distance */
TEST_F(NumaTopologyFakeNumaTest, AllDistancesAreLocal) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  for (std::size_t from = 0; from < topo_.nodeCount; ++from) {
    for (std::size_t to = 0; to < topo_.nodeCount; ++to) {
      EXPECT_EQ(topo_.getDistance(from, to), NUMA_DISTANCE_LOCAL) << from << " -> " << to;
    }
  }
}

/** @test Node memory comes from each node's meminfo */
TEST_F(NumaTopologyFakeNumaTest, NodeMemoryFromNodeMeminfo) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  constexpr std::uint64_t KIB = 1024;
  EXPECT_EQ(topo_.nodes[0].totalBytes, 3959444 * KIB);
  EXPECT_EQ(topo_.nodes[0].freeBytes, 2653064 * KIB);
  EXPECT_EQ(topo_.nodes[1].totalBytes, 4048016 * KIB);
  EXPECT_EQ(topo_.nodes[1].freeBytes, 2768476 * KIB);
}

/* ----------------------------- Two-Node Tree Tests ----------------------------- */

/// A physical two-node topology: node0 owns CPUs 0-3 and node1 CPUs 4-7, each
/// node 10 from itself and 20 from the other. Synthetic: written to the
/// kernel's sysfs node ABI (cpulist, distance and meminfo per node), since no
/// capture of a physical two-node machine exists to cut it from.
class NumaTopologyTwoNodeTest : public FilesystemRootTest {
protected:
  static constexpr std::uint8_t REMOTE_DISTANCE = 20;
  NumaTopology topo_{};

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(FilesystemRootTest::SetUp());
    put("/sys/devices/system/node/node0/cpulist", "0-3\n");
    put("/sys/devices/system/node/node0/distance", "10 20\n");
    put("/sys/devices/system/node/node0/meminfo", "Node 0 MemTotal:        8388608 kB\n"
                                                  "Node 0 MemFree:         4194304 kB\n");
    put("/sys/devices/system/node/node1/cpulist", "4-7\n");
    put("/sys/devices/system/node/node1/distance", "20 10\n");
    put("/sys/devices/system/node/node1/meminfo", "Node 1 MemTotal:        8388608 kB\n"
                                                  "Node 1 MemFree:         6291456 kB\n");
    topo_ = getNumaTopology();
  }
};

/** @test CPU IDs are unique across nodes that own their CPUs */
TEST_F(NumaTopologyTwoNodeTest, CpuIdsUniqueAcrossNodes) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  std::set<int> seen;
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    for (std::size_t j = 0; j < topo_.nodes[i].cpuCount; ++j) {
      const int CPU_ID = topo_.nodes[i].cpuIds[j];
      EXPECT_TRUE(seen.insert(CPU_ID).second) << "Duplicate CPU ID: " << CPU_ID;
    }
  }
  EXPECT_EQ(seen.size(), 8U);
}

/** @test findNodeForCpu returns the node that owns the CPU, for every CPU of each node */
TEST_F(NumaTopologyTwoNodeTest, FindNodeForCpuCorrect) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  EXPECT_EQ(topo_.findNodeForCpu(2), 0);
  EXPECT_EQ(topo_.findNodeForCpu(6), 1);
  for (std::size_t i = 0; i < topo_.nodeCount; ++i) {
    const auto& NODE = topo_.nodes[i];
    for (std::size_t j = 0; j < NODE.cpuCount; ++j) {
      EXPECT_EQ(topo_.findNodeForCpu(NODE.cpuIds[j]), static_cast<int>(i))
          << "CPU " << NODE.cpuIds[j];
    }
  }
}

/** @test The remote distance is greater than the local one, both ways */
TEST_F(NumaTopologyTwoNodeTest, RemoteDistanceGreaterThanLocal) {
  ASSERT_EQ(topo_.nodeCount, 2U);
  EXPECT_EQ(topo_.getDistance(0, 0), NUMA_DISTANCE_LOCAL);
  EXPECT_EQ(topo_.getDistance(1, 1), NUMA_DISTANCE_LOCAL);
  EXPECT_EQ(topo_.getDistance(0, 1), REMOTE_DISTANCE);
  EXPECT_EQ(topo_.getDistance(1, 0), REMOTE_DISTANCE);
  EXPECT_GT(topo_.getDistance(0, 1), NUMA_DISTANCE_LOCAL);
  EXPECT_GT(topo_.getDistance(1, 0), NUMA_DISTANCE_LOCAL);
}

/* ----------------------------- Live System Tests ----------------------------- */

/** @test With the root unset the node count matches the live node directories */
TEST(NumaTopologyLiveTest, NodeCountMatchesTheLiveNodeDirectories) {
  ASSERT_STREQ(filesystemRoot(), "");
  const std::filesystem::path NODE_DIR{"/sys/devices/system/node"};
  std::error_code ec;
  if (!std::filesystem::is_directory(NODE_DIR, ec)) {
    GTEST_SKIP() << NODE_DIR << " is absent here";
  }

  std::size_t liveNodes = 0;
  for (const auto& entry : std::filesystem::directory_iterator(NODE_DIR, ec)) {
    const std::string NAME = entry.path().filename().string();
    if (NAME.size() > 4 && NAME.compare(0, 4, "node") == 0 &&
        NAME.find_first_not_of("0123456789", 4) == std::string::npos && entry.is_directory(ec)) {
      ++liveNodes;
    }
  }

  EXPECT_EQ(getNumaTopology().nodeCount, std::min<std::size_t>(liveNodes, MAX_NUMA_NODES));
}
