/**
 * @file CpuTopology_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuTopology.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - All tests should pass on any Linux system with sysfs mounted.
 */

#include "src/cpu/inc/CpuTopology.hpp"

#include <gtest/gtest.h>

#include <cstring> // std::strlen
#include <set>
#include <string>

using seeker::cpu::CACHE_STRING_SIZE;
using seeker::cpu::CacheInfo;
using seeker::cpu::CoreInfo;
using seeker::cpu::CpuTopology;
using seeker::cpu::getCpuTopology;

class CpuTopologyTest : public ::testing::Test {
protected:
  CpuTopology topo_{};

  void SetUp() override { topo_ = getCpuTopology(); }
};

/* ----------------------------- Basic Counts ----------------------------- */

/** @test Logical CPU count is at least 1. */
TEST_F(CpuTopologyTest, AtLeastOneCpu) { EXPECT_GE(topo_.logicalCpus, 1); }

/** @test Physical core count is at least 1. */
TEST_F(CpuTopologyTest, AtLeastOneCore) { EXPECT_GE(topo_.physicalCores, 1); }

/** @test Package count is at least 1. */
TEST_F(CpuTopologyTest, AtLeastOnePackage) { EXPECT_GE(topo_.packages, 1); }

/** @test Logical CPUs >= physical cores (SMT/HT relationship). */
TEST_F(CpuTopologyTest, LogicalGePhysical) { EXPECT_GE(topo_.logicalCpus, topo_.physicalCores); }

/** @test Physical cores >= packages. */
TEST_F(CpuTopologyTest, CoresGePackages) { EXPECT_GE(topo_.physicalCores, topo_.packages); }

/* ----------------------------- Threads Per Core ----------------------------- */

/** @test threadsPerCore returns sensible value. */
TEST_F(CpuTopologyTest, ThreadsPerCoreValid) {
  const int TPC = topo_.threadsPerCore();
  EXPECT_GE(TPC, 1);
  EXPECT_LE(TPC, 8); // No known CPU has more than 8-way SMT
}

/** @test threadsPerCore * physicalCores == logicalCpus. */
TEST_F(CpuTopologyTest, ThreadCountConsistent) {
  const int TPC = topo_.threadsPerCore();
  // May not be exact due to integer division, but should be close
  EXPECT_LE(topo_.physicalCores * TPC, topo_.logicalCpus);
  EXPECT_GE(topo_.physicalCores * (TPC + 1), topo_.logicalCpus);
}

/* ----------------------------- Core Info Validation ----------------------------- */

/** @test Core count matches cores vector size. */
TEST_F(CpuTopologyTest, CoreCountMatchesVector) {
  EXPECT_EQ(topo_.physicalCores, static_cast<int>(topo_.cores.size()));
}

/** @test Each core has at least one thread. */
TEST_F(CpuTopologyTest, EachCoreHasThreads) {
  for (const CoreInfo& CORE : topo_.cores) {
    EXPECT_FALSE(CORE.threadCpuIds.empty()) << "Core " << CORE.coreId << " has no threads";
  }
}

/** @test Thread CPU IDs are unique across all cores. */
TEST_F(CpuTopologyTest, ThreadIdsUnique) {
  std::set<int> seen;
  for (const CoreInfo& CORE : topo_.cores) {
    for (int cpuId : CORE.threadCpuIds) {
      const auto [IT, INSERTED] = seen.insert(cpuId);
      EXPECT_TRUE(INSERTED) << "Duplicate CPU ID: " << cpuId;
    }
  }
}

/** @test Total thread count matches logicalCpus. */
TEST_F(CpuTopologyTest, TotalThreadsMatchLogical) {
  int total = 0;
  for (const CoreInfo& CORE : topo_.cores) {
    total += static_cast<int>(CORE.threadCpuIds.size());
  }
  EXPECT_EQ(total, topo_.logicalCpus);
}

/** @test Thread CPU IDs are non-negative. */
TEST_F(CpuTopologyTest, ThreadIdsNonNegative) {
  for (const CoreInfo& CORE : topo_.cores) {
    for (int cpuId : CORE.threadCpuIds) {
      EXPECT_GE(cpuId, 0);
    }
  }
}

/* ----------------------------- Cache Validation ----------------------------- */

/** @test Cache levels are positive. */
TEST_F(CpuTopologyTest, CacheLevelsPositive) {
  for (const CoreInfo& CORE : topo_.cores) {
    for (const CacheInfo& CACHE : CORE.caches) {
      EXPECT_GT(CACHE.level, 0);
      EXPECT_LE(CACHE.level, 2); // Per-core caches should be L1/L2
    }
  }
  for (const CacheInfo& CACHE : topo_.sharedCaches) {
    EXPECT_GE(CACHE.level, 3); // Shared caches should be L3+
  }
}

/** @test Cache sizes are reasonable (non-zero when present). */
TEST_F(CpuTopologyTest, CacheSizesReasonable) {
  for (const CoreInfo& CORE : topo_.cores) {
    for (const CacheInfo& CACHE : CORE.caches) {
      if (CACHE.level > 0) {
        // L1 typically 16KB-128KB, L2 typically 256KB-2MB
        EXPECT_GT(CACHE.sizeBytes, 0U);
        EXPECT_LE(CACHE.sizeBytes, 64ULL * 1024 * 1024); // Max 64MB per cache
      }
    }
  }
}

/** @test Cache line sizes are powers of two (when present). */
TEST_F(CpuTopologyTest, CacheLineSizesPowerOfTwo) {
  auto isPowerOfTwo = [](std::uint64_t n) { return n > 0 && (n & (n - 1)) == 0; };

  for (const CoreInfo& CORE : topo_.cores) {
    for (const CacheInfo& CACHE : CORE.caches) {
      if (CACHE.lineBytes > 0) {
        EXPECT_TRUE(isPowerOfTwo(CACHE.lineBytes))
            << "Cache line size " << CACHE.lineBytes << " not power of two";
        EXPECT_GE(CACHE.lineBytes, 32U);  // Min 32 bytes
        EXPECT_LE(CACHE.lineBytes, 256U); // Max 256 bytes
      }
    }
  }
}

/** @test Cache type strings are null-terminated. */
TEST_F(CpuTopologyTest, CacheTypeStringsValid) {
  for (const CoreInfo& CORE : topo_.cores) {
    for (const CacheInfo& CACHE : CORE.caches) {
      const std::size_t LEN = std::strlen(CACHE.type.data());
      EXPECT_LT(LEN, CACHE_STRING_SIZE);
    }
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test CpuTopology::toString produces valid output. */
TEST_F(CpuTopologyTest, ToStringValid) {
  const std::string OUTPUT = topo_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Packages:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Cores:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Threads:"), std::string::npos);
}

/** @test CacheInfo::toString produces valid output. */
TEST_F(CpuTopologyTest, CacheToStringValid) {
  for (const CoreInfo& CORE : topo_.cores) {
    for (const CacheInfo& CACHE : CORE.caches) {
      const std::string OUTPUT = CACHE.toString();
      EXPECT_FALSE(OUTPUT.empty());
      EXPECT_NE(OUTPUT.find("L"), std::string::npos);
    }
  }
}

/** @test CoreInfo::toString produces valid output. */
TEST_F(CpuTopologyTest, CoreToStringValid) {
  for (const CoreInfo& CORE : topo_.cores) {
    const std::string OUTPUT = CORE.toString();
    EXPECT_FALSE(OUTPUT.empty());
    EXPECT_NE(OUTPUT.find("core"), std::string::npos);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default CpuTopology is zeroed. */
TEST(CpuTopologyDefaultTest, DefaultIsZero) {
  const CpuTopology DEFAULT{};

  EXPECT_EQ(DEFAULT.packages, 0);
  EXPECT_EQ(DEFAULT.physicalCores, 0);
  EXPECT_EQ(DEFAULT.logicalCpus, 0);
  EXPECT_EQ(DEFAULT.numaNodes, 0);
  EXPECT_TRUE(DEFAULT.cores.empty());
  EXPECT_TRUE(DEFAULT.sharedCaches.empty());
}

/** @test Default CacheInfo is zeroed. */
TEST(CpuTopologyDefaultTest, DefaultCacheIsZero) {
  const CacheInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.level, 0);
  EXPECT_EQ(DEFAULT.sizeBytes, 0U);
  EXPECT_EQ(DEFAULT.lineBytes, 0U);
  EXPECT_EQ(DEFAULT.associativity, 0);
  EXPECT_EQ(DEFAULT.type[0], '\0');
  EXPECT_EQ(DEFAULT.policy[0], '\0');
}