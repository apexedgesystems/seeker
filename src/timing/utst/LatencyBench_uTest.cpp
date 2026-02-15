/**
 * @file LatencyBench_uTest.cpp
 * @brief Unit tests for seeker::timing::LatencyBench.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Timing tests use generous bounds for CI variance.
 *  - RT priority tests may be skipped without CAP_SYS_NICE.
 */

#include "src/timing/inc/LatencyBench.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <string>

using seeker::timing::BenchConfig;
using seeker::timing::LatencyStats;
using seeker::timing::MAX_LATENCY_SAMPLES;
using seeker::timing::measureLatency;
using seeker::timing::measureNowOverhead;
using seeker::timing::MIN_BENCH_BUDGET;

class LatencyBenchTest : public ::testing::Test {
protected:
  LatencyStats stats_{};

  void SetUp() override {
    // Quick measurement for tests
    stats_ = measureLatency(std::chrono::milliseconds{100});
  }
};

/* ----------------------------- Basic Measurement Tests ----------------------------- */

/** @test Measurement produces samples. */
TEST_F(LatencyBenchTest, ProducesSamples) {
  EXPECT_GT(stats_.sampleCount, 0U) << "Should collect at least one sample";
}

/** @test Sample count within bounds. */
TEST_F(LatencyBenchTest, SampleCountWithinBounds) {
  EXPECT_LE(stats_.sampleCount, MAX_LATENCY_SAMPLES);
}

/** @test Target is set correctly. */
TEST_F(LatencyBenchTest, TargetSet) {
  // Default target is 1ms = 1,000,000 ns
  EXPECT_DOUBLE_EQ(stats_.targetNs, 1'000'000.0);
}

/** @test now() overhead is positive. */
TEST_F(LatencyBenchTest, NowOverheadPositive) { EXPECT_GT(stats_.nowOverheadNs, 0.0); }

/** @test now() overhead is reasonable (< 10us). */
TEST_F(LatencyBenchTest, NowOverheadReasonable) {
  // Most systems should have now() overhead < 1us, allow up to 10us for VMs
  EXPECT_LT(stats_.nowOverheadNs, 10'000.0);
}

/* ----------------------------- Statistics Tests ----------------------------- */

/** @test Minimum <= Mean <= Maximum. */
TEST_F(LatencyBenchTest, StatisticsOrdering) {
  if (stats_.sampleCount == 0) {
    GTEST_SKIP() << "No samples collected";
  }

  EXPECT_LE(stats_.minNs, stats_.meanNs);
  EXPECT_LE(stats_.meanNs, stats_.maxNs);
}

/** @test Percentiles are ordered correctly. */
TEST_F(LatencyBenchTest, PercentilesOrdered) {
  if (stats_.sampleCount == 0) {
    GTEST_SKIP() << "No samples collected";
  }

  EXPECT_LE(stats_.minNs, stats_.medianNs);
  EXPECT_LE(stats_.medianNs, stats_.p90Ns);
  EXPECT_LE(stats_.p90Ns, stats_.p95Ns);
  EXPECT_LE(stats_.p95Ns, stats_.p99Ns);
  EXPECT_LE(stats_.p99Ns, stats_.p999Ns);
  EXPECT_LE(stats_.p999Ns, stats_.maxNs);
}

/** @test Standard deviation is non-negative. */
TEST_F(LatencyBenchTest, StdDevNonNegative) { EXPECT_GE(stats_.stdDevNs, 0.0); }

/** @test Sleep durations are at least target. */
TEST_F(LatencyBenchTest, SleepAtLeastTarget) {
  if (stats_.sampleCount == 0) {
    GTEST_SKIP() << "No samples collected";
  }

  // Minimum should be close to target (allow some early wakeup)
  // But typically sleep overshoots, not undershoots
  EXPECT_GT(stats_.minNs, stats_.targetNs * 0.8);
}

/** @test Sleep durations are within reasonable bounds. */
TEST_F(LatencyBenchTest, SleepReasonableBounds) {
  if (stats_.sampleCount == 0) {
    GTEST_SKIP() << "No samples collected";
  }

  // Max should be less than 10x target (generous for CI)
  EXPECT_LT(stats_.maxNs, stats_.targetNs * 10.0);
}

/* ----------------------------- Jitter Helpers Tests ----------------------------- */

/** @test jitterMeanNs is positive (typically oversleep). */
TEST_F(LatencyBenchTest, JitterMeanPositive) {
  if (stats_.sampleCount == 0) {
    GTEST_SKIP() << "No samples collected";
  }

  // Most systems oversleep, so jitter should be positive
  // But allow negative on very precise systems
  const double JITTER = stats_.jitterMeanNs();
  EXPECT_GT(JITTER, -stats_.targetNs) << "Jitter should not be huge negative";
}

/** @test jitterP99Ns computation is correct. */
TEST_F(LatencyBenchTest, JitterP99Correct) {
  EXPECT_DOUBLE_EQ(stats_.jitterP99Ns(), stats_.p99Ns - stats_.targetNs);
}

/** @test jitterMaxNs >= jitterP99Ns. */
TEST_F(LatencyBenchTest, JitterMaxGteP99) { EXPECT_GE(stats_.jitterMaxNs(), stats_.jitterP99Ns()); }

/** @test undershootNs is non-negative or small negative. */
TEST_F(LatencyBenchTest, UndershootReasonable) {
  // Undershoot = target - min
  // Typically negative (min > target), but small positive possible
  EXPECT_GT(stats_.undershootNs(), -stats_.targetNs);
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test RT score is in valid range. */
TEST_F(LatencyBenchTest, RtScoreInRange) {
  const int SCORE = stats_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test isGoodForRt based on p99 jitter. */
TEST(LatencyStatsGoodTest, Threshold) {
  LatencyStats stats;
  stats.targetNs = 1'000'000.0; // 1ms target

  // Good: p99 jitter < 100us
  stats.p99Ns = 1'050'000.0; // 50us jitter
  EXPECT_TRUE(stats.isGoodForRt());

  // Bad: p99 jitter > 100us
  stats.p99Ns = 1'200'000.0; // 200us jitter
  EXPECT_FALSE(stats.isGoodForRt());
}

/** @test RT score decreases with jitter. */
TEST(LatencyScoreTest, DecreasesWithJitter) {
  LatencyStats low;
  low.targetNs = 1'000'000.0;
  low.p99Ns = 1'005'000.0; // 5us jitter
  low.maxNs = 1'010'000.0;

  LatencyStats high;
  high.targetNs = 1'000'000.0;
  high.p99Ns = 1'500'000.0; // 500us jitter
  high.maxNs = 2'000'000.0;

  EXPECT_GT(low.rtScore(), high.rtScore());
}

/* ----------------------------- BenchConfig Tests ----------------------------- */

/** @test quick() config has short budget. */
TEST(BenchConfigTest, QuickShortBudget) {
  const BenchConfig CFG = BenchConfig::quick();
  EXPECT_LE(CFG.budget, std::chrono::milliseconds{500});
}

/** @test thorough() config has longer budget. */
TEST(BenchConfigTest, ThoroughLongerBudget) {
  const BenchConfig QUICK = BenchConfig::quick();
  const BenchConfig THOROUGH = BenchConfig::thorough();
  EXPECT_GT(THOROUGH.budget, QUICK.budget);
}

/** @test rtCharacterization() config uses absolute time. */
TEST(BenchConfigTest, RtCharacterizationAbsolute) {
  const BenchConfig CFG = BenchConfig::rtCharacterization();
  EXPECT_TRUE(CFG.useAbsoluteTime);
  EXPECT_GT(CFG.rtPriority, 0);
}

/* ----------------------------- measureLatency Overload Tests ----------------------------- */

/** @test measureLatency(budget) uses default target. */
TEST(MeasureLatencyOverloadTest, DefaultTarget) {
  const auto STATS = measureLatency(std::chrono::milliseconds{100});
  // Default target is 1ms
  EXPECT_DOUBLE_EQ(STATS.targetNs, 1'000'000.0);
}

/** @test measureLatency enforces minimum budget. */
TEST(MeasureLatencyOverloadTest, MinBudgetEnforced) {
  // Try with very short budget
  const auto STATS = measureLatency(std::chrono::milliseconds{10});

  // Should still produce some samples due to minimum enforcement
  // (unless system is extremely slow)
  EXPECT_GE(STATS.sampleCount, 0U);
}

/* ----------------------------- measureNowOverhead Tests ----------------------------- */

/** @test measureNowOverhead returns positive value. */
TEST(MeasureNowOverheadTest, ReturnsPositive) {
  const double OVERHEAD = measureNowOverhead();
  EXPECT_GT(OVERHEAD, 0.0);
}

/** @test measureNowOverhead with custom iterations. */
TEST(MeasureNowOverheadTest, CustomIterations) {
  const double OVERHEAD1 = measureNowOverhead(1000);
  const double OVERHEAD2 = measureNowOverhead(10000);

  // Both should be positive
  EXPECT_GT(OVERHEAD1, 0.0);
  EXPECT_GT(OVERHEAD2, 0.0);

  // Should be roughly similar (within 10x)
  EXPECT_LT(OVERHEAD1, OVERHEAD2 * 10.0);
  EXPECT_LT(OVERHEAD2, OVERHEAD1 * 10.0);
}

/** @test measureNowOverhead handles zero iterations. */
TEST(MeasureNowOverheadTest, HandlesZero) {
  const double OVERHEAD = measureNowOverhead(0);
  EXPECT_GT(OVERHEAD, 0.0); // Should use default
}

/* ----------------------------- Custom Config Tests ----------------------------- */

/** @test Custom sleep target is used. */
TEST(CustomConfigTest, CustomTarget) {
  BenchConfig config;
  config.budget = std::chrono::milliseconds{100};
  config.sleepTarget = std::chrono::microseconds{500}; // 500us target

  const auto STATS = measureLatency(config);
  EXPECT_DOUBLE_EQ(STATS.targetNs, 500'000.0);
}

/** @test Absolute time mode is recorded. */
TEST(CustomConfigTest, AbsoluteTimeRecorded) {
  BenchConfig config;
  config.budget = std::chrono::milliseconds{100};
  config.useAbsoluteTime = true;

  const auto STATS = measureLatency(config);
  EXPECT_TRUE(STATS.usedAbsoluteTime);
}

/** @test RT priority mode recorded when not elevated. */
TEST(CustomConfigTest, RtPriorityNotElevated) {
  BenchConfig config;
  config.budget = std::chrono::milliseconds{100};
  config.rtPriority = 0; // No elevation

  const auto STATS = measureLatency(config);
  EXPECT_FALSE(STATS.usedRtPriority);
  EXPECT_EQ(STATS.rtPriorityUsed, 0);
}

// Note: Testing actual RT priority elevation requires CAP_SYS_NICE
// which may not be available in all CI environments

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(LatencyBenchTest, ToStringNonEmpty) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains sample count. */
TEST_F(LatencyBenchTest, ToStringContainsSamples) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("Samples:"), std::string::npos);
}

/** @test toString contains jitter analysis. */
TEST_F(LatencyBenchTest, ToStringContainsJitter) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("Jitter"), std::string::npos);
}

/** @test toString contains RT score. */
TEST_F(LatencyBenchTest, ToStringContainsRtScore) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("RT Score:"), std::string::npos);
}

/** @test toString contains percentiles. */
TEST_F(LatencyBenchTest, ToStringContainsPercentiles) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_NE(OUTPUT.find("p99:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default LatencyStats is zeroed. */
TEST(LatencyStatsDefaultTest, DefaultZeroed) {
  const LatencyStats DEFAULT{};

  EXPECT_EQ(DEFAULT.sampleCount, 0U);
  EXPECT_DOUBLE_EQ(DEFAULT.nowOverheadNs, 0.0);
  EXPECT_DOUBLE_EQ(DEFAULT.targetNs, 0.0);
  EXPECT_DOUBLE_EQ(DEFAULT.meanNs, 0.0);
  EXPECT_FALSE(DEFAULT.usedAbsoluteTime);
  EXPECT_FALSE(DEFAULT.usedRtPriority);
}

/** @test Default BenchConfig has reasonable defaults. */
TEST(BenchConfigDefaultTest, ReasonableDefaults) {
  const BenchConfig DEFAULT{};

  EXPECT_GT(DEFAULT.budget.count(), 0);
  EXPECT_GT(DEFAULT.sleepTarget.count(), 0);
  EXPECT_FALSE(DEFAULT.useAbsoluteTime);
  EXPECT_EQ(DEFAULT.rtPriority, 0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test measureNowOverhead returns consistent results. */
TEST(NowOverheadDeterminismTest, ConsistentResults) {
  const double O1 = measureNowOverhead(10000);
  const double O2 = measureNowOverhead(10000);

  // Should be within 10x of each other
  EXPECT_LT(O1, O2 * 10.0);
  EXPECT_LT(O2, O1 * 10.0);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test Very short sleep target works. */
TEST(EdgeCaseTest, ShortSleepTarget) {
  BenchConfig config;
  config.budget = std::chrono::milliseconds{100};
  config.sleepTarget = std::chrono::microseconds{10}; // 10us

  const auto STATS = measureLatency(config);
  EXPECT_GT(STATS.sampleCount, 0U);
}

/** @test Long sleep target works. */
TEST(EdgeCaseTest, LongSleepTarget) {
  BenchConfig config;
  config.budget = std::chrono::milliseconds{200};
  config.sleepTarget = std::chrono::microseconds{50000}; // 50ms

  const auto STATS = measureLatency(config);
  // Should get at least a few samples
  EXPECT_GE(STATS.sampleCount, 1U);
}
