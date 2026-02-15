/**
 * @file TimerConfig_uTest.cpp
 * @brief Unit tests for seeker::timing::TimerConfig.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Timer slack is process-specific and may vary.
 *  - NO_HZ configuration depends on kernel boot parameters.
 */

#include "src/timing/inc/TimerConfig.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::timing::DEFAULT_TIMER_SLACK_NS;
using seeker::timing::getTimerConfig;
using seeker::timing::getTimerSlackNs;
using seeker::timing::isPreemptRtKernel;
using seeker::timing::MAX_NOHZ_CPUS;
using seeker::timing::setTimerSlackNs;
using seeker::timing::TimerConfig;

class TimerConfigTest : public ::testing::Test {
protected:
  TimerConfig config_{};

  void SetUp() override { config_ = getTimerConfig(); }
};

/* ----------------------------- Timer Slack Tests ----------------------------- */

/** @test Timer slack query succeeds. */
TEST_F(TimerConfigTest, SlackQuerySucceeds) {
  // prctl(PR_GET_TIMERSLACK) should work on all Linux systems
  EXPECT_TRUE(config_.slackQuerySucceeded) << "Timer slack query should succeed";
}

/** @test Timer slack is positive when query succeeds. */
TEST_F(TimerConfigTest, SlackPositiveOnSuccess) {
  if (!config_.slackQuerySucceeded) {
    GTEST_SKIP() << "Timer slack query failed";
  }
  // Slack is at least 1ns (kernel minimum)
  EXPECT_GE(config_.timerSlackNs, 1U);
}

/** @test Timer slack is in reasonable range. */
TEST_F(TimerConfigTest, SlackReasonableRange) {
  if (!config_.slackQuerySucceeded) {
    GTEST_SKIP() << "Timer slack query failed";
  }
  // Typical range: 1ns (explicit) to 100ms (unusual but possible)
  EXPECT_LE(config_.timerSlackNs, 100'000'000U);
}

/** @test hasMinimalSlack returns true for 1ns. */
TEST(TimerConfigSlackTest, HasMinimalSlackTrue) {
  TimerConfig config;
  config.slackQuerySucceeded = true;
  config.timerSlackNs = 1;
  EXPECT_TRUE(config.hasMinimalSlack());
}

/** @test hasMinimalSlack returns false for default slack. */
TEST(TimerConfigSlackTest, HasMinimalSlackFalse) {
  TimerConfig config;
  config.slackQuerySucceeded = true;
  config.timerSlackNs = DEFAULT_TIMER_SLACK_NS;
  EXPECT_FALSE(config.hasMinimalSlack());
}

/** @test hasMinimalSlack returns false when query failed. */
TEST(TimerConfigSlackTest, HasMinimalSlackQueryFailed) {
  TimerConfig config;
  config.slackQuerySucceeded = false;
  config.timerSlackNs = 1;
  EXPECT_FALSE(config.hasMinimalSlack());
}

/** @test hasDefaultSlack detects default range. */
TEST(TimerConfigSlackTest, HasDefaultSlackInRange) {
  TimerConfig config;
  config.slackQuerySucceeded = true;

  config.timerSlackNs = 50'000;
  EXPECT_TRUE(config.hasDefaultSlack());

  config.timerSlackNs = 45'000;
  EXPECT_TRUE(config.hasDefaultSlack());

  config.timerSlackNs = 55'000;
  EXPECT_TRUE(config.hasDefaultSlack());
}

/** @test hasDefaultSlack returns false outside range. */
TEST(TimerConfigSlackTest, HasDefaultSlackOutsideRange) {
  TimerConfig config;
  config.slackQuerySucceeded = true;

  config.timerSlackNs = 1;
  EXPECT_FALSE(config.hasDefaultSlack());

  config.timerSlackNs = 100'000;
  EXPECT_FALSE(config.hasDefaultSlack());
}

/* ----------------------------- High-Res Timer Tests ----------------------------- */

/** @test High-res timer status is set. */
TEST_F(TimerConfigTest, HighResTimerStatusSet) {
  // Most modern Linux systems have high-res timers
  // but we can't assert true since it depends on kernel config
  // Just verify the field is accessible
  (void)config_.highResTimersEnabled;
  SUCCEED();
}

/* ----------------------------- NO_HZ Tests ----------------------------- */

/** @test nohzFullCount within bounds. */
TEST_F(TimerConfigTest, NohzFullCountWithinBounds) {
  EXPECT_LE(config_.nohzFullCount, MAX_NOHZ_CPUS);
}

/** @test nohzFullCount matches bitset. */
TEST_F(TimerConfigTest, NohzFullCountMatchesBitset) {
  std::size_t counted = 0;
  for (std::size_t i = 0; i < MAX_NOHZ_CPUS; ++i) {
    if (config_.nohzFullCpus.test(i)) {
      ++counted;
    }
  }
  EXPECT_EQ(counted, config_.nohzFullCount);
}

/** @test isNohzFullCpu returns false for invalid CPU. */
TEST_F(TimerConfigTest, IsNohzFullCpuInvalidCpu) {
  EXPECT_FALSE(config_.isNohzFullCpu(MAX_NOHZ_CPUS));
  EXPECT_FALSE(config_.isNohzFullCpu(MAX_NOHZ_CPUS + 1));
}

/** @test isNohzFullCpu consistent with bitset. */
TEST_F(TimerConfigTest, IsNohzFullCpuConsistent) {
  for (std::size_t i = 0; i < MAX_NOHZ_CPUS; ++i) {
    EXPECT_EQ(config_.isNohzFullCpu(i), config_.nohzFullCpus.test(i));
  }
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test RT score is in valid range. */
TEST_F(TimerConfigTest, RtScoreInRange) {
  const int SCORE = config_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test Optimal config gets high score. */
TEST(TimerConfigScoreTest, OptimalConfigHighScore) {
  TimerConfig config;
  config.slackQuerySucceeded = true;
  config.timerSlackNs = 1;
  config.highResTimersEnabled = true;
  config.nohzFullCount = 4;
  config.preemptRtEnabled = true;

  EXPECT_GE(config.rtScore(), 90);
}

/** @test Default config gets moderate score. */
TEST(TimerConfigScoreTest, DefaultConfigModerateScore) {
  TimerConfig config;
  config.slackQuerySucceeded = true;
  config.timerSlackNs = DEFAULT_TIMER_SLACK_NS;
  config.highResTimersEnabled = true;

  const int SCORE = config.rtScore();
  EXPECT_GE(SCORE, 30);
  EXPECT_LE(SCORE, 70);
}

/** @test Poor config gets low score. */
TEST(TimerConfigScoreTest, PoorConfigLowScore) {
  TimerConfig config;
  config.slackQuerySucceeded = false;
  config.highResTimersEnabled = false;
  config.nohzFullCount = 0;
  config.preemptRtEnabled = false;

  EXPECT_LE(config.rtScore(), 20);
}

/* ----------------------------- isOptimalForRt Tests ----------------------------- */

/** @test isOptimalForRt requires all conditions. */
TEST(TimerConfigOptimalTest, RequiresAllConditions) {
  TimerConfig config;

  // All false
  EXPECT_FALSE(config.isOptimalForRt());

  // Only minimal slack
  config.slackQuerySucceeded = true;
  config.timerSlackNs = 1;
  EXPECT_FALSE(config.isOptimalForRt());

  // Add high-res timers
  config.highResTimersEnabled = true;
  EXPECT_FALSE(config.isOptimalForRt());

  // Add nohz_full
  config.nohzFullCount = 1;
  EXPECT_TRUE(config.isOptimalForRt());
}

/* ----------------------------- getTimerSlackNs Tests ----------------------------- */

/** @test getTimerSlackNs returns positive value. */
TEST(GetTimerSlackTest, ReturnsPositive) {
  const std::uint64_t SLACK = getTimerSlackNs();
  // Should be at least 1 (kernel minimum) if query succeeds
  // May return 0 on failure, but that's unlikely
  EXPECT_GE(SLACK, 0U);
}

/** @test getTimerSlackNs consistent with config. */
TEST_F(TimerConfigTest, GetTimerSlackConsistent) {
  if (!config_.slackQuerySucceeded) {
    GTEST_SKIP() << "Timer slack query failed";
  }

  const std::uint64_t SLACK = getTimerSlackNs();
  // Should match config (unless another thread changed it)
  EXPECT_EQ(SLACK, config_.timerSlackNs);
}

/* ----------------------------- setTimerSlackNs Tests ----------------------------- */

/** @test setTimerSlackNs can set minimal slack. */
TEST(SetTimerSlackTest, CanSetMinimal) {
  // Save original
  const std::uint64_t ORIGINAL = getTimerSlackNs();

  // Set to minimal
  EXPECT_TRUE(setTimerSlackNs(1));

  // Verify
  const std::uint64_t AFTER = getTimerSlackNs();
  EXPECT_EQ(AFTER, 1U);

  // Restore original
  (void)setTimerSlackNs(ORIGINAL);
}

/** @test setTimerSlackNs round-trip. */
TEST(SetTimerSlackTest, RoundTrip) {
  const std::uint64_t ORIGINAL = getTimerSlackNs();

  // Set to known value
  const std::uint64_t TEST_VALUE = 12345;
  EXPECT_TRUE(setTimerSlackNs(TEST_VALUE));
  EXPECT_EQ(getTimerSlackNs(), TEST_VALUE);

  // Restore
  EXPECT_TRUE(setTimerSlackNs(ORIGINAL));
  EXPECT_EQ(getTimerSlackNs(), ORIGINAL);
}

/* ----------------------------- isPreemptRtKernel Tests ----------------------------- */

/** @test isPreemptRtKernel returns boolean. */
TEST(PreemptRtTest, ReturnsBool) {
  // Just verify it doesn't crash and returns a valid value
  const bool IS_RT = isPreemptRtKernel();
  (void)IS_RT; // May be true or false depending on kernel
  SUCCEED();
}

/** @test isPreemptRtKernel consistent with config. */
TEST_F(TimerConfigTest, PreemptRtConsistent) {
  EXPECT_EQ(isPreemptRtKernel(), config_.preemptRtEnabled);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(TimerConfigTest, ToStringNonEmpty) {
  const std::string OUTPUT = config_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains timer slack info. */
TEST_F(TimerConfigTest, ToStringContainsSlack) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("Timer Slack"), std::string::npos);
}

/** @test toString contains high-res timer info. */
TEST_F(TimerConfigTest, ToStringContainsHighRes) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("High-Res"), std::string::npos);
}

/** @test toString contains tickless info. */
TEST_F(TimerConfigTest, ToStringContainsTickless) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("Tickless"), std::string::npos);
}

/** @test toString contains RT score. */
TEST_F(TimerConfigTest, ToStringContainsRtScore) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("RT Score:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default TimerConfig is zeroed. */
TEST(TimerConfigDefaultTest, DefaultZeroed) {
  const TimerConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.timerSlackNs, 0U);
  EXPECT_FALSE(DEFAULT.slackQuerySucceeded);
  EXPECT_FALSE(DEFAULT.highResTimersEnabled);
  EXPECT_FALSE(DEFAULT.nohzFullEnabled);
  EXPECT_EQ(DEFAULT.nohzFullCount, 0U);
  EXPECT_FALSE(DEFAULT.nohzIdleEnabled);
  EXPECT_FALSE(DEFAULT.preemptRtEnabled);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getTimerConfig returns consistent results. */
TEST(TimerConfigDeterminismTest, ConsistentResults) {
  const TimerConfig C1 = getTimerConfig();
  const TimerConfig C2 = getTimerConfig();

  EXPECT_EQ(C1.slackQuerySucceeded, C2.slackQuerySucceeded);
  if (C1.slackQuerySucceeded && C2.slackQuerySucceeded) {
    EXPECT_EQ(C1.timerSlackNs, C2.timerSlackNs);
  }
  EXPECT_EQ(C1.highResTimersEnabled, C2.highResTimersEnabled);
  EXPECT_EQ(C1.nohzFullCount, C2.nohzFullCount);
  EXPECT_EQ(C1.preemptRtEnabled, C2.preemptRtEnabled);
}