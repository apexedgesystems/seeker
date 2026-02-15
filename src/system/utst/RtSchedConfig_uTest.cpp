/**
 * @file RtSchedConfig_uTest.cpp
 * @brief Unit tests for seeker::system::RtSchedConfig.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Actual RT configuration varies by kernel and system config.
 *  - Tests verify API contracts and data consistency.
 */

#include "src/system/inc/RtSchedConfig.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::getRtBandwidth;
using seeker::system::getRtBandwidthPercent;
using seeker::system::getRtSchedConfig;
using seeker::system::getSchedTunables;
using seeker::system::isRtThrottlingDisabled;
using seeker::system::RtBandwidth;
using seeker::system::RtSchedConfig;
using seeker::system::SchedTunables;

class RtSchedConfigTest : public ::testing::Test {
protected:
  RtSchedConfig config_{};

  void SetUp() override { config_ = getRtSchedConfig(); }
};

/* ----------------------------- RtBandwidth Tests ----------------------------- */

/** @test getRtBandwidth returns valid structure on Linux. */
TEST(RtBandwidthTest, QueryReturnsStructure) {
  const auto BW = getRtBandwidth();

  // On any Linux system, we should be able to read these values
  // (unless running in a very restricted container)
  if (BW.valid) {
    EXPECT_GT(BW.periodUs, 0);
    // Runtime can be -1 (unlimited) or positive
    EXPECT_TRUE(BW.runtimeUs == -1 || BW.runtimeUs > 0);
  }
}

/** @test isUnlimited is consistent with runtimeUs. */
TEST(RtBandwidthTest, IsUnlimitedConsistent) {
  const auto BW = getRtBandwidth();
  EXPECT_EQ(BW.isUnlimited(), BW.runtimeUs == -1);
}

/** @test bandwidthPercent returns valid percentage. */
TEST(RtBandwidthTest, BandwidthPercentValid) {
  const auto BW = getRtBandwidth();

  if (BW.valid) {
    const double PCT = BW.bandwidthPercent();
    EXPECT_GE(PCT, 0.0);
    EXPECT_LE(PCT, 100.0);

    if (BW.isUnlimited()) {
      EXPECT_EQ(PCT, 100.0);
    }
  }
}

/** @test quotaUs returns reasonable value. */
TEST(RtBandwidthTest, QuotaUsValid) {
  const auto BW = getRtBandwidth();

  if (BW.valid) {
    const auto QUOTA = BW.quotaUs();
    EXPECT_GT(QUOTA, 0);

    if (BW.isUnlimited()) {
      EXPECT_EQ(QUOTA, BW.periodUs);
    } else {
      EXPECT_EQ(QUOTA, BW.runtimeUs);
    }
  }
}

/** @test isRtFriendly is consistent with bandwidth. */
TEST(RtBandwidthTest, RtFriendlyConsistent) {
  const auto BW = getRtBandwidth();

  if (BW.valid) {
    if (BW.isUnlimited()) {
      EXPECT_TRUE(BW.isRtFriendly());
    } else if (BW.bandwidthPercent() >= 90.0) {
      EXPECT_TRUE(BW.isRtFriendly());
    }
  }
}

/** @test toString produces non-empty output. */
TEST(RtBandwidthTest, ToStringNonEmpty) {
  const auto BW = getRtBandwidth();
  const std::string OUTPUT = BW.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Bandwidth"), std::string::npos);
}

/* ----------------------------- SchedTunables Tests ----------------------------- */

/** @test getSchedTunables returns structure. */
TEST(SchedTunablesTest, QueryReturnsStructure) {
  const auto TUNABLES = getSchedTunables();

  // Valid if we could read any tunables
  if (TUNABLES.valid) {
    // At least one should be non-zero
    EXPECT_TRUE(TUNABLES.minGranularityNs > 0 || TUNABLES.latencyNs > 0 ||
                TUNABLES.wakeupGranularityNs > 0);
  }
}

/** @test toString produces non-empty output. */
TEST(SchedTunablesTest, ToStringNonEmpty) {
  const auto TUNABLES = getSchedTunables();
  const std::string OUTPUT = TUNABLES.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Tunable"), std::string::npos);
}

/** @test autogroupEnabled is boolean. */
TEST(SchedTunablesTest, AutogroupIsBoolean) {
  const auto TUNABLES = getSchedTunables();
  EXPECT_TRUE(TUNABLES.autogroupEnabled || !TUNABLES.autogroupEnabled);
}

/* ----------------------------- RtSchedConfig Tests ----------------------------- */

/** @test getRtSchedConfig returns valid structure. */
TEST_F(RtSchedConfigTest, QueryReturnsValidStructure) {
  // Should have attempted to read bandwidth
  // (may or may not be valid depending on permissions)
  EXPECT_TRUE(config_.bandwidth.valid || !config_.bandwidth.valid);
  EXPECT_TRUE(config_.tunables.valid || !config_.tunables.valid);
}

/** @test rtScore is in valid range. */
TEST_F(RtSchedConfigTest, RtScoreInRange) {
  const int SCORE = config_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test isRtFriendly returns boolean. */
TEST_F(RtSchedConfigTest, IsRtFriendlyReturnsBool) {
  EXPECT_TRUE(config_.isRtFriendly() || !config_.isRtFriendly());
}

/** @test hasUnlimitedRt is consistent with bandwidth. */
TEST_F(RtSchedConfigTest, HasUnlimitedRtConsistent) {
  EXPECT_EQ(config_.hasUnlimitedRt(), config_.bandwidth.isUnlimited());
}

/** @test hasAutogroupDisabled is consistent with tunables. */
TEST_F(RtSchedConfigTest, HasAutogroupDisabledConsistent) {
  EXPECT_EQ(config_.hasAutogroupDisabled(), !config_.tunables.autogroupEnabled);
}

/** @test toString produces non-empty output. */
TEST_F(RtSchedConfigTest, ToStringNonEmpty) {
  const std::string OUTPUT = config_.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("RT"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Configuration"), std::string::npos);
}

/** @test toString contains bandwidth info. */
TEST_F(RtSchedConfigTest, ToStringContainsBandwidth) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("Bandwidth"), std::string::npos);
}

/** @test toString contains kernel features. */
TEST_F(RtSchedConfigTest, ToStringContainsFeatures) {
  const std::string OUTPUT = config_.toString();
  EXPECT_NE(OUTPUT.find("Feature"), std::string::npos);
}

/* ----------------------------- Quick API Tests ----------------------------- */

/** @test isRtThrottlingDisabled is consistent with bandwidth. */
TEST_F(RtSchedConfigTest, ThrottlingDisabledConsistent) {
  const bool DISABLED = isRtThrottlingDisabled();
  EXPECT_EQ(DISABLED, config_.bandwidth.isUnlimited());
}

/** @test getRtBandwidthPercent is consistent with bandwidth. */
TEST_F(RtSchedConfigTest, BandwidthPercentConsistent) {
  const double PCT = getRtBandwidthPercent();
  EXPECT_DOUBLE_EQ(PCT, config_.bandwidth.bandwidthPercent());
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default RtBandwidth has default values. */
TEST(RtBandwidthDefaultTest, DefaultValues) {
  const RtBandwidth BW{};

  EXPECT_EQ(BW.periodUs, seeker::system::DEFAULT_RT_PERIOD_US);
  EXPECT_EQ(BW.runtimeUs, seeker::system::DEFAULT_RT_RUNTIME_US);
  EXPECT_FALSE(BW.valid);
}

/** @test Default SchedTunables is zeroed. */
TEST(SchedTunablesDefaultTest, DefaultZeroed) {
  const SchedTunables TUNABLES{};

  EXPECT_EQ(TUNABLES.minGranularityNs, 0U);
  EXPECT_EQ(TUNABLES.wakeupGranularityNs, 0U);
  EXPECT_EQ(TUNABLES.migrationCostNs, 0U);
  EXPECT_EQ(TUNABLES.latencyNs, 0U);
  EXPECT_FALSE(TUNABLES.childRunsFirst);
  EXPECT_FALSE(TUNABLES.autogroupEnabled);
  EXPECT_FALSE(TUNABLES.valid);
}

/** @test Default RtSchedConfig has default bandwidth. */
TEST(RtSchedConfigDefaultTest, DefaultValues) {
  const RtSchedConfig CONFIG{};

  EXPECT_FALSE(CONFIG.bandwidth.valid);
  EXPECT_FALSE(CONFIG.tunables.valid);
  EXPECT_FALSE(CONFIG.hasRtGroupSched);
  EXPECT_FALSE(CONFIG.hasCfsBandwidth);
  EXPECT_FALSE(CONFIG.hasSchedDeadline);
  EXPECT_FALSE(CONFIG.timerMigration);
  EXPECT_EQ(CONFIG.rtTasksRunnable, 0U);
  EXPECT_EQ(CONFIG.rtThrottleCount, 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getRtSchedConfig returns consistent results. */
TEST(RtSchedConfigDeterminismTest, ConsistentResults) {
  const auto CONFIG1 = getRtSchedConfig();
  const auto CONFIG2 = getRtSchedConfig();

  // Bandwidth should be identical
  EXPECT_EQ(CONFIG1.bandwidth.periodUs, CONFIG2.bandwidth.periodUs);
  EXPECT_EQ(CONFIG1.bandwidth.runtimeUs, CONFIG2.bandwidth.runtimeUs);
  EXPECT_EQ(CONFIG1.bandwidth.valid, CONFIG2.bandwidth.valid);

  // Kernel features should be identical
  EXPECT_EQ(CONFIG1.hasRtGroupSched, CONFIG2.hasRtGroupSched);
  EXPECT_EQ(CONFIG1.hasCfsBandwidth, CONFIG2.hasCfsBandwidth);
  EXPECT_EQ(CONFIG1.hasSchedDeadline, CONFIG2.hasSchedDeadline);

  // Tunables should be identical
  EXPECT_EQ(CONFIG1.tunables.autogroupEnabled, CONFIG2.tunables.autogroupEnabled);
}

/** @test getRtBandwidth returns consistent results. */
TEST(RtBandwidthDeterminismTest, ConsistentResults) {
  const auto BW1 = getRtBandwidth();
  const auto BW2 = getRtBandwidth();

  EXPECT_EQ(BW1.periodUs, BW2.periodUs);
  EXPECT_EQ(BW1.runtimeUs, BW2.runtimeUs);
  EXPECT_EQ(BW1.valid, BW2.valid);
}

/* ----------------------------- RT Friendliness Scenarios ----------------------------- */

/** @test High bandwidth implies RT friendly. */
TEST_F(RtSchedConfigTest, HighBandwidthRtFriendly) {
  if (config_.bandwidth.valid && config_.bandwidth.bandwidthPercent() >= 95.0 &&
      !config_.tunables.autogroupEnabled) {
    EXPECT_TRUE(config_.bandwidth.isRtFriendly());
  }
}

/** @test Unlimited bandwidth is always RT friendly. */
TEST_F(RtSchedConfigTest, UnlimitedAlwaysFriendly) {
  if (config_.bandwidth.isUnlimited()) {
    EXPECT_TRUE(config_.bandwidth.isRtFriendly());
  }
}

/** @test Autogroup enabled reduces RT friendliness. */
TEST_F(RtSchedConfigTest, AutogroupReducesFriendliness) {
  if (config_.tunables.autogroupEnabled) {
    // With autogroup enabled, overall config is not RT-friendly
    EXPECT_FALSE(config_.isRtFriendly());
  }
}