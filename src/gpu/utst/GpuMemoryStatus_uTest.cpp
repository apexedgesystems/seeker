/**
 * @file GpuMemoryStatus_uTest.cpp
 * @brief Unit tests for seeker::gpu::GpuMemoryStatus.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/GpuMemoryStatus.hpp"

#include <gtest/gtest.h>

using seeker::gpu::EccErrorCounts;
using seeker::gpu::getAllGpuMemoryStatus;
using seeker::gpu::getGpuMemoryStatus;
using seeker::gpu::GpuMemoryStatus;
using seeker::gpu::RetiredPages;

/* ----------------------------- EccErrorCounts Tests ----------------------------- */

/** @test Default EccErrorCounts has no errors. */
TEST(EccErrorCountsTest, DefaultNoErrors) {
  EccErrorCounts counts{};
  EXPECT_EQ(counts.correctedVolatile, 0);
  EXPECT_EQ(counts.uncorrectedVolatile, 0);
  EXPECT_EQ(counts.correctedAggregate, 0);
  EXPECT_EQ(counts.uncorrectedAggregate, 0);
}

/** @test hasUncorrected returns false for default. */
TEST(EccErrorCountsTest, DefaultHasNoUncorrected) {
  EccErrorCounts counts{};
  EXPECT_FALSE(counts.hasUncorrected());
}

/** @test hasUncorrected detects volatile uncorrected. */
TEST(EccErrorCountsTest, DetectsVolatileUncorrected) {
  EccErrorCounts counts{};
  counts.uncorrectedVolatile = 1;
  EXPECT_TRUE(counts.hasUncorrected());
}

/** @test hasUncorrected detects aggregate uncorrected. */
TEST(EccErrorCountsTest, DetectsAggregateUncorrected) {
  EccErrorCounts counts{};
  counts.uncorrectedAggregate = 1;
  EXPECT_TRUE(counts.hasUncorrected());
}

/** @test EccErrorCounts::toString not empty. */
TEST(EccErrorCountsTest, ToStringNotEmpty) {
  EccErrorCounts counts{};
  EXPECT_FALSE(counts.toString().empty());
}

/* ----------------------------- RetiredPages Tests ----------------------------- */

/** @test Default RetiredPages has no retired pages. */
TEST(RetiredPagesTest, DefaultNoRetired) {
  RetiredPages pages{};
  EXPECT_EQ(pages.singleBitEcc, 0);
  EXPECT_EQ(pages.doubleBitEcc, 0);
  EXPECT_EQ(pages.total(), 0);
}

/** @test total() sums SBE and DBE. */
TEST(RetiredPagesTest, TotalSumsCorrectly) {
  RetiredPages pages{};
  pages.singleBitEcc = 3;
  pages.doubleBitEcc = 2;
  EXPECT_EQ(pages.total(), 5);
}

/** @test Default has no pending operations. */
TEST(RetiredPagesTest, DefaultNoPending) {
  RetiredPages pages{};
  EXPECT_FALSE(pages.pendingRetire);
  EXPECT_FALSE(pages.pendingRemapping);
}

/** @test RetiredPages::toString not empty. */
TEST(RetiredPagesTest, ToStringNotEmpty) {
  RetiredPages pages{};
  EXPECT_FALSE(pages.toString().empty());
}

/* ----------------------------- GpuMemoryStatus Tests ----------------------------- */

/** @test Default GpuMemoryStatus has deviceIndex -1. */
TEST(GpuMemoryStatusTest, DefaultDeviceIndex) {
  GpuMemoryStatus status{};
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test Default memory values are zero. */
TEST(GpuMemoryStatusTest, DefaultMemoryZero) {
  GpuMemoryStatus status{};
  EXPECT_EQ(status.totalBytes, 0);
  EXPECT_EQ(status.freeBytes, 0);
  EXPECT_EQ(status.usedBytes, 0);
}

/** @test utilizationPercent returns 0 for default. */
TEST(GpuMemoryStatusTest, DefaultUtilizationZero) {
  GpuMemoryStatus status{};
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 0.0);
}

/** @test utilizationPercent calculates correctly. */
TEST(GpuMemoryStatusTest, UtilizationCalculation) {
  GpuMemoryStatus status{};
  status.totalBytes = 1000;
  status.usedBytes = 250;
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 25.0);
}

/** @test isHealthy returns true for default. */
TEST(GpuMemoryStatusTest, DefaultIsHealthy) {
  GpuMemoryStatus status{};
  EXPECT_TRUE(status.isHealthy());
}

/** @test isHealthy returns false with uncorrected errors. */
TEST(GpuMemoryStatusTest, UnhealthyWithErrors) {
  GpuMemoryStatus status{};
  status.eccErrors.uncorrectedVolatile = 1;
  EXPECT_FALSE(status.isHealthy());
}

/** @test isHealthy returns false with retired pages. */
TEST(GpuMemoryStatusTest, UnhealthyWithRetiredPages) {
  GpuMemoryStatus status{};
  status.retiredPages.doubleBitEcc = 1;
  EXPECT_FALSE(status.isHealthy());
}

/** @test GpuMemoryStatus::toString not empty. */
TEST(GpuMemoryStatusTest, ToStringNotEmpty) {
  GpuMemoryStatus status{};
  EXPECT_FALSE(status.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getGpuMemoryStatus returns default for invalid index. */
TEST(GpuMemoryApiTest, InvalidIndexReturnsDefault) {
  GpuMemoryStatus status = getGpuMemoryStatus(-1);
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test getAllGpuMemoryStatus returns vector. */
TEST(GpuMemoryApiTest, GetAllReturnsVector) {
  std::vector<GpuMemoryStatus> all = getAllGpuMemoryStatus();
  // Vector may be empty if no GPUs
  EXPECT_GE(all.size(), 0);
}
