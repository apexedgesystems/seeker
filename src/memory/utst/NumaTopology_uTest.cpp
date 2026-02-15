/**
 * @file NumaTopology_uTest.cpp
 * @brief Unit tests for seeker::memory::NumaTopology.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Single-node systems will have nodeCount=1 or 0 (both valid).
 */

#include "src/memory/inc/NumaTopology.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

using seeker::memory::getNumaTopology;
using seeker::memory::MAX_CPUS_PER_NODE;
using seeker::memory::MAX_NUMA_NODES;
using seeker::memory::NUMA_DISTANCE_INVALID;
using seeker::memory::NUMA_DISTANCE_LOCAL;
using seeker::memory::NumaNodeInfo;
using seeker::memory::NumaTopology;

class NumaTopologyTest : public ::testing::Test {
protected:
  NumaTopology topo_{};
  bool hasNuma_{false};

  void SetUp() override {
    topo_ = getNumaTopology();
    hasNuma_ = (topo_.nodeCount > 0);
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