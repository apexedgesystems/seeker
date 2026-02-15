/**
 * @file GpuBench_uTest.cu
 * @brief Unit tests for seeker::gpu::GpuBench.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 *  - Benchmark tests are kept minimal to avoid long test times.
 */

#include "src/gpu/inc/GpuBench.cuh"

#include <gtest/gtest.h>

using seeker::gpu::BandwidthResult;
using seeker::gpu::BenchmarkOptions;
using seeker::gpu::GpuBenchResult;
using seeker::gpu::runAllGpuBench;
using seeker::gpu::runGpuBench;

/* ----------------------------- BandwidthResult Tests ----------------------------- */

/** @test Default BandwidthResult has zero bandwidth. */
TEST(BandwidthResultTest, DefaultBandwidthZero) {
  BandwidthResult result{};
  EXPECT_DOUBLE_EQ(result.bandwidthMiBps, 0.0);
}

/** @test Default has zero latency. */
TEST(BandwidthResultTest, DefaultLatencyZero) {
  BandwidthResult result{};
  EXPECT_DOUBLE_EQ(result.latencyUs, 0.0);
}

/** @test Default has zero iterations. */
TEST(BandwidthResultTest, DefaultIterations) {
  BandwidthResult result{};
  EXPECT_EQ(result.iterations, 0);
}

/** @test BandwidthResult::toString not empty. */
TEST(BandwidthResultTest, ToStringNotEmpty) {
  BandwidthResult result{};
  EXPECT_FALSE(result.toString().empty());
}

/* ----------------------------- BenchmarkOptions Tests ----------------------------- */

/** @test Default options have reasonable values. */
TEST(BenchmarkOptionsTest, DefaultValues) {
  BenchmarkOptions opts{};
  EXPECT_GT(opts.budget.count(), 0);
  EXPECT_GT(opts.transferSize, 0);
  EXPECT_GT(opts.launchIterations, 0);
}

/** @test Default skip flags are false. */
TEST(BenchmarkOptionsTest, DefaultSkipsFalse) {
  BenchmarkOptions opts{};
  EXPECT_FALSE(opts.skipPageable);
  EXPECT_FALSE(opts.skipAllocation);
  EXPECT_FALSE(opts.skipStreams);
}

/* ----------------------------- GpuBenchResult Tests ----------------------------- */

/** @test Default GpuBenchResult has deviceIndex -1. */
TEST(GpuBenchResultTest, DefaultDeviceIndex) {
  GpuBenchResult result{};
  EXPECT_EQ(result.deviceIndex, -1);
}

/** @test Default has empty name. */
TEST(GpuBenchResultTest, DefaultName) {
  GpuBenchResult result{};
  EXPECT_TRUE(result.name.empty());
}

/** @test Default has zero launch overhead. */
TEST(GpuBenchResultTest, DefaultLaunchOverhead) {
  GpuBenchResult result{};
  EXPECT_DOUBLE_EQ(result.launchOverheadUs, 0.0);
}

/** @test Default has zero allocation times. */
TEST(GpuBenchResultTest, DefaultAllocTimes) {
  GpuBenchResult result{};
  EXPECT_DOUBLE_EQ(result.deviceAllocUs, 0.0);
  EXPECT_DOUBLE_EQ(result.pinnedAllocUs, 0.0);
}

/** @test Default is not completed. */
TEST(GpuBenchResultTest, DefaultNotCompleted) {
  GpuBenchResult result{};
  EXPECT_FALSE(result.completed);
}

/** @test GpuBenchResult::toString not empty. */
TEST(GpuBenchResultTest, ToStringNotEmpty) {
  GpuBenchResult result{};
  EXPECT_FALSE(result.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test runGpuBench returns default for invalid index. */
TEST(GpuBenchApiTest, InvalidIndexReturnsDefault) {
  GpuBenchResult result = runGpuBench(-1);
  EXPECT_EQ(result.deviceIndex, -1);
  EXPECT_FALSE(result.completed);
}

/** @test runGpuBench with budget returns default for invalid index. */
TEST(GpuBenchApiTest, InvalidIndexWithBudget) {
  GpuBenchResult result = runGpuBench(-1, std::chrono::milliseconds{100});
  EXPECT_EQ(result.deviceIndex, -1);
}

/** @test runAllGpuBench returns vector. */
TEST(GpuBenchApiTest, GetAllReturnsVector) {
  BenchmarkOptions opts{};
  opts.budget = std::chrono::milliseconds{50}; // Short budget for test
  std::vector<GpuBenchResult> all = runAllGpuBench(opts);
  // Vector may be empty if no GPUs
  EXPECT_GE(all.size(), 0);
}

/** @test runGpuBench is deterministic for invalid index. */
TEST(GpuBenchApiTest, DeterministicInvalid) {
  GpuBenchResult r1 = runGpuBench(-1);
  GpuBenchResult r2 = runGpuBench(-1);
  EXPECT_EQ(r1.deviceIndex, r2.deviceIndex);
  EXPECT_EQ(r1.completed, r2.completed);
}
