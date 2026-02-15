/**
 * @file MemoryStats_uTest.cpp
 * @brief Unit tests for seeker::memory::MemoryStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - VM policy availability varies by kernel configuration.
 */

#include "src/memory/inc/MemoryStats.hpp"

#include <gtest/gtest.h>

#include <cstring> // std::strlen
#include <string>

using seeker::memory::getMemoryStats;
using seeker::memory::MemoryStats;
using seeker::memory::THP_STRING_SIZE;

class MemoryStatsTest : public ::testing::Test {
protected:
  MemoryStats stats_{};

  void SetUp() override { stats_ = getMemoryStats(); }
};

/* ----------------------------- RAM Tests ----------------------------- */

/** @test Total RAM is positive. */
TEST_F(MemoryStatsTest, TotalRamPositive) { EXPECT_GT(stats_.totalBytes, 0U); }

/** @test Total RAM is reasonable (>= 64MB, typical minimum for Linux). */
TEST_F(MemoryStatsTest, TotalRamReasonableMinimum) {
  constexpr std::uint64_t MIN_RAM = 64ULL * 1024 * 1024; // 64 MiB
  EXPECT_GE(stats_.totalBytes, MIN_RAM);
}

/** @test Free RAM is non-negative and <= total. */
TEST_F(MemoryStatsTest, FreeRamValid) {
  EXPECT_GE(stats_.freeBytes, 0U);
  EXPECT_LE(stats_.freeBytes, stats_.totalBytes);
}

/** @test Available RAM is non-negative and <= total. */
TEST_F(MemoryStatsTest, AvailableRamValid) {
  EXPECT_GE(stats_.availableBytes, 0U);
  EXPECT_LE(stats_.availableBytes, stats_.totalBytes);
}

/** @test Buffers is non-negative and <= total. */
TEST_F(MemoryStatsTest, BuffersValid) {
  EXPECT_GE(stats_.buffersBytes, 0U);
  EXPECT_LE(stats_.buffersBytes, stats_.totalBytes);
}

/** @test Cached is non-negative and <= total. */
TEST_F(MemoryStatsTest, CachedValid) {
  EXPECT_GE(stats_.cachedBytes, 0U);
  EXPECT_LE(stats_.cachedBytes, stats_.totalBytes);
}

/** @test Available >= Free (available includes reclaimable). */
TEST_F(MemoryStatsTest, AvailableGreaterThanOrEqualFree) {
  // MemAvailable is typically >= MemFree because it includes reclaimable cache
  // However, on some systems it might not be present (availableBytes = 0)
  if (stats_.availableBytes > 0) {
    EXPECT_GE(stats_.availableBytes, stats_.freeBytes);
  }
}

/* ----------------------------- Swap Tests ----------------------------- */

/** @test Swap total is non-negative. */
TEST_F(MemoryStatsTest, SwapTotalNonNegative) { EXPECT_GE(stats_.swapTotalBytes, 0U); }

/** @test Swap free <= swap total. */
TEST_F(MemoryStatsTest, SwapFreeValid) {
  EXPECT_GE(stats_.swapFreeBytes, 0U);
  EXPECT_LE(stats_.swapFreeBytes, stats_.swapTotalBytes);
}

/** @test Swap disabled means both total and free are zero. */
TEST_F(MemoryStatsTest, SwapDisabledConsistent) {
  if (stats_.swapTotalBytes == 0) {
    EXPECT_EQ(stats_.swapFreeBytes, 0U);
  }
}

/* ----------------------------- Computed Value Tests ----------------------------- */

/** @test usedBytes is consistent with components. */
TEST_F(MemoryStatsTest, UsedBytesConsistent) {
  const std::uint64_t USED = stats_.usedBytes();
  EXPECT_LE(USED, stats_.totalBytes);

  // Used should be approximately total - free - buffers - cached
  const std::uint64_t EXPECTED =
      (stats_.totalBytes > stats_.freeBytes + stats_.buffersBytes + stats_.cachedBytes)
          ? (stats_.totalBytes - stats_.freeBytes - stats_.buffersBytes - stats_.cachedBytes)
          : 0;
  EXPECT_EQ(USED, EXPECTED);
}

/** @test swapUsedBytes is consistent with total and free. */
TEST_F(MemoryStatsTest, SwapUsedBytesConsistent) {
  const std::uint64_t SWAP_USED = stats_.swapUsedBytes();
  EXPECT_LE(SWAP_USED, stats_.swapTotalBytes);

  const std::uint64_t EXPECTED = (stats_.swapTotalBytes > stats_.swapFreeBytes)
                                     ? (stats_.swapTotalBytes - stats_.swapFreeBytes)
                                     : 0;
  EXPECT_EQ(SWAP_USED, EXPECTED);
}

/** @test utilizationPercent is in valid range. */
TEST_F(MemoryStatsTest, UtilizationPercentValid) {
  const double PCT = stats_.utilizationPercent();
  EXPECT_GE(PCT, 0.0);
  EXPECT_LE(PCT, 100.0);
}

/** @test swapUtilizationPercent is in valid range. */
TEST_F(MemoryStatsTest, SwapUtilizationPercentValid) {
  const double PCT = stats_.swapUtilizationPercent();
  EXPECT_GE(PCT, 0.0);
  EXPECT_LE(PCT, 100.0);
}

/** @test swapUtilizationPercent is zero when swap disabled. */
TEST_F(MemoryStatsTest, SwapUtilizationZeroWhenDisabled) {
  if (stats_.swapTotalBytes == 0) {
    EXPECT_EQ(stats_.swapUtilizationPercent(), 0.0);
  }
}

/* ----------------------------- VM Policy Tests ----------------------------- */

/** @test Swappiness is in valid range or unavailable. */
TEST_F(MemoryStatsTest, SwappinessValid) {
  // -1 means unavailable, otherwise 0-100
  if (stats_.swappiness >= 0) {
    EXPECT_LE(stats_.swappiness, 200); // Technically max is 200 on some kernels
  }
}

/** @test zoneReclaimMode is in valid range or unavailable. */
TEST_F(MemoryStatsTest, ZoneReclaimModeValid) {
  // -1 means unavailable, otherwise 0-7 (bitmask)
  if (stats_.zoneReclaimMode >= 0) {
    EXPECT_LE(stats_.zoneReclaimMode, 7);
  }
}

/** @test overcommitMemory is in valid range or unavailable. */
TEST_F(MemoryStatsTest, OvercommitMemoryValid) {
  // -1 means unavailable, otherwise 0, 1, or 2
  if (stats_.overcommitMemory >= 0) {
    EXPECT_LE(stats_.overcommitMemory, 2);
  }
}

/** @test isSwappinessLow returns correct value. */
TEST_F(MemoryStatsTest, IsSwappinessLowCorrect) {
  if (stats_.swappiness >= 0 && stats_.swappiness <= 10) {
    EXPECT_TRUE(stats_.isSwappinessLow());
  } else if (stats_.swappiness > 10) {
    EXPECT_FALSE(stats_.isSwappinessLow());
  } else {
    // Unavailable (-1)
    EXPECT_FALSE(stats_.isSwappinessLow());
  }
}

/* ----------------------------- THP Tests ----------------------------- */

/** @test THP enabled string is null-terminated. */
TEST_F(MemoryStatsTest, THPEnabledNullTerminated) {
  bool foundNull = false;
  for (std::size_t i = 0; i < THP_STRING_SIZE; ++i) {
    if (stats_.thpEnabled[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

/** @test THP defrag string is null-terminated. */
TEST_F(MemoryStatsTest, THPDefragNullTerminated) {
  bool foundNull = false;
  for (std::size_t i = 0; i < THP_STRING_SIZE; ++i) {
    if (stats_.thpDefrag[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

/** @test THP enabled string within bounds. */
TEST_F(MemoryStatsTest, THPEnabledWithinBounds) {
  const std::size_t LEN = std::strlen(stats_.thpEnabled.data());
  EXPECT_LT(LEN, THP_STRING_SIZE);
}

/** @test THP defrag string within bounds. */
TEST_F(MemoryStatsTest, THPDefragWithinBounds) {
  const std::size_t LEN = std::strlen(stats_.thpDefrag.data());
  EXPECT_LT(LEN, THP_STRING_SIZE);
}

/** @test isTHPEnabled detects [never] correctly. */
TEST(MemoryStatsTHPTest, DetectsNeverDisabled) {
  MemoryStats stats{};

  // Simulate "[never]" - THP disabled
  const char* NEVER = "always madvise [never]";
  std::strncpy(stats.thpEnabled.data(), NEVER, THP_STRING_SIZE - 1);
  stats.thpEnabled[THP_STRING_SIZE - 1] = '\0';
  EXPECT_FALSE(stats.isTHPEnabled());
}

/** @test isTHPEnabled detects [always] correctly. */
TEST(MemoryStatsTHPTest, DetectsAlwaysEnabled) {
  MemoryStats stats{};

  // Simulate "[always]" - THP enabled
  const char* ALWAYS = "[always] madvise never";
  std::strncpy(stats.thpEnabled.data(), ALWAYS, THP_STRING_SIZE - 1);
  stats.thpEnabled[THP_STRING_SIZE - 1] = '\0';
  EXPECT_TRUE(stats.isTHPEnabled());
}

/** @test isTHPEnabled detects [madvise] correctly. */
TEST(MemoryStatsTHPTest, DetectsMadviseEnabled) {
  MemoryStats stats{};

  // Simulate "[madvise]" - THP enabled on demand
  const char* MADVISE = "always [madvise] never";
  std::strncpy(stats.thpEnabled.data(), MADVISE, THP_STRING_SIZE - 1);
  stats.thpEnabled[THP_STRING_SIZE - 1] = '\0';
  EXPECT_TRUE(stats.isTHPEnabled());
}

/** @test isTHPEnabled handles empty string. */
TEST(MemoryStatsTHPTest, HandlesEmptyString) {
  MemoryStats stats{};
  stats.thpEnabled[0] = '\0';
  EXPECT_FALSE(stats.isTHPEnabled());
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(MemoryStatsTest, ToStringNonEmpty) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains RAM info. */
TEST_F(MemoryStatsTest, ToStringContainsRamInfo) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("RAM:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("total"), std::string::npos);
  EXPECT_NE(OUTPUT.find("used"), std::string::npos);
}

/** @test toString contains swap info. */
TEST_F(MemoryStatsTest, ToStringContainsSwapInfo) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("Swap:"), std::string::npos);
}

/** @test toString contains VM policy info. */
TEST_F(MemoryStatsTest, ToStringContainsVMPolicies) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("swappiness"), std::string::npos);
  EXPECT_NE(OUTPUT.find("THP"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default MemoryStats is zeroed. */
TEST(MemoryStatsDefaultTest, DefaultZeroed) {
  const MemoryStats DEFAULT{};

  EXPECT_EQ(DEFAULT.totalBytes, 0U);
  EXPECT_EQ(DEFAULT.freeBytes, 0U);
  EXPECT_EQ(DEFAULT.availableBytes, 0U);
  EXPECT_EQ(DEFAULT.buffersBytes, 0U);
  EXPECT_EQ(DEFAULT.cachedBytes, 0U);
  EXPECT_EQ(DEFAULT.swapTotalBytes, 0U);
  EXPECT_EQ(DEFAULT.swapFreeBytes, 0U);
  EXPECT_EQ(DEFAULT.swappiness, -1);
  EXPECT_EQ(DEFAULT.zoneReclaimMode, -1);
  EXPECT_EQ(DEFAULT.overcommitMemory, -1);
  EXPECT_EQ(DEFAULT.thpEnabled[0], '\0');
  EXPECT_EQ(DEFAULT.thpDefrag[0], '\0');
}

/** @test Default stats have zero utilization. */
TEST(MemoryStatsDefaultTest, DefaultUtilizationZero) {
  const MemoryStats DEFAULT{};

  EXPECT_EQ(DEFAULT.usedBytes(), 0U);
  EXPECT_EQ(DEFAULT.swapUsedBytes(), 0U);
  EXPECT_EQ(DEFAULT.utilizationPercent(), 0.0);
  EXPECT_EQ(DEFAULT.swapUtilizationPercent(), 0.0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Repeated calls return consistent static values. */
TEST(MemoryStatsDeterminismTest, TotalRamConsistent) {
  const MemoryStats STATS1 = getMemoryStats();
  const MemoryStats STATS2 = getMemoryStats();

  // Total RAM should not change between calls
  EXPECT_EQ(STATS1.totalBytes, STATS2.totalBytes);
  EXPECT_EQ(STATS1.swapTotalBytes, STATS2.swapTotalBytes);

  // VM policies should be consistent
  EXPECT_EQ(STATS1.swappiness, STATS2.swappiness);
  EXPECT_EQ(STATS1.zoneReclaimMode, STATS2.zoneReclaimMode);
  EXPECT_EQ(STATS1.overcommitMemory, STATS2.overcommitMemory);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test usedBytes handles edge case where components exceed total. */
TEST(MemoryStatsEdgeCaseTest, UsedBytesNoUnderflow) {
  MemoryStats stats{};
  stats.totalBytes = 1000;
  stats.freeBytes = 500;
  stats.buffersBytes = 300;
  stats.cachedBytes = 300; // Sum = 1100 > total

  // Should not underflow
  EXPECT_EQ(stats.usedBytes(), 0U);
}

/** @test swapUsedBytes handles edge case where free exceeds total. */
TEST(MemoryStatsEdgeCaseTest, SwapUsedBytesNoUnderflow) {
  MemoryStats stats{};
  stats.swapTotalBytes = 100;
  stats.swapFreeBytes = 200; // Free > total (invalid but shouldn't crash)

  EXPECT_EQ(stats.swapUsedBytes(), 0U);
}