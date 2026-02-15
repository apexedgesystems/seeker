/**
 * @file EthtoolInfo_uTest.cpp
 * @brief Unit tests for seeker::network::EthtoolInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Loopback interface typically does not support ethtool queries.
 *  - Physical NICs may or may not be present depending on hardware.
 *  - ethtool support varies by driver - tests handle missing support gracefully.
 */

#include "src/network/inc/EthtoolInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::network::CoalesceConfig;
using seeker::network::EthtoolInfo;
using seeker::network::EthtoolInfoList;
using seeker::network::FEATURE_NAME_SIZE;
using seeker::network::getAllEthtoolInfo;
using seeker::network::getCoalesceConfig;
using seeker::network::getEthtoolInfo;
using seeker::network::getPauseConfig;
using seeker::network::getRingBufferConfig;
using seeker::network::IF_NAME_SIZE;
using seeker::network::LOW_LATENCY_FRAMES_THRESHOLD;
using seeker::network::LOW_LATENCY_USECS_THRESHOLD;
using seeker::network::MAX_FEATURES;
using seeker::network::MAX_INTERFACES;
using seeker::network::NicFeature;
using seeker::network::NicFeatures;
using seeker::network::PauseConfig;
using seeker::network::RingBufferConfig;
using seeker::network::RT_RING_SIZE_WARN_THRESHOLD;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default RingBufferConfig is zeroed and invalid. */
TEST(RingBufferConfigDefaultTest, DefaultZeroed) {
  const RingBufferConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.rxPending, 0U);
  EXPECT_EQ(DEFAULT.rxMax, 0U);
  EXPECT_EQ(DEFAULT.txPending, 0U);
  EXPECT_EQ(DEFAULT.txMax, 0U);
  EXPECT_FALSE(DEFAULT.isValid());
}

/** @test Default CoalesceConfig is zeroed. */
TEST(CoalesceConfigDefaultTest, DefaultZeroed) {
  const CoalesceConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.rxUsecs, 0U);
  EXPECT_EQ(DEFAULT.rxMaxFrames, 0U);
  EXPECT_EQ(DEFAULT.txUsecs, 0U);
  EXPECT_EQ(DEFAULT.txMaxFrames, 0U);
  EXPECT_FALSE(DEFAULT.useAdaptiveRx);
  EXPECT_FALSE(DEFAULT.useAdaptiveTx);
}

/** @test Default PauseConfig is disabled. */
TEST(PauseConfigDefaultTest, DefaultDisabled) {
  const PauseConfig DEFAULT{};

  EXPECT_FALSE(DEFAULT.autoneg);
  EXPECT_FALSE(DEFAULT.rxPause);
  EXPECT_FALSE(DEFAULT.txPause);
  EXPECT_FALSE(DEFAULT.isEnabled());
}

/** @test Default NicFeatures is empty. */
TEST(NicFeaturesDefaultTest, DefaultEmpty) {
  const NicFeatures DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_EQ(DEFAULT.find("anything"), nullptr);
  EXPECT_FALSE(DEFAULT.isEnabled("anything"));
  EXPECT_EQ(DEFAULT.countEnabled(), 0U);
}

/** @test Default EthtoolInfo is zeroed. */
TEST(EthtoolInfoDefaultTest, DefaultZeroed) {
  const EthtoolInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.ifname[0], '\0');
  EXPECT_FALSE(DEFAULT.supportsEthtool);
  EXPECT_EQ(DEFAULT.rtScore(), 50); // Unknown = middle score
}

/** @test Default EthtoolInfoList is empty. */
TEST(EthtoolInfoListDefaultTest, DefaultEmpty) {
  const EthtoolInfoList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_EQ(DEFAULT.find("anything"), nullptr);
}

/* ----------------------------- RingBufferConfig Methods ----------------------------- */

/** @test RingBufferConfig isValid checks max values. */
TEST(RingBufferConfigMethodsTest, IsValidChecksMax) {
  RingBufferConfig cfg{};

  // Default should be invalid
  EXPECT_FALSE(cfg.isValid());

  // Set rxMax
  cfg.rxMax = 256;
  EXPECT_TRUE(cfg.isValid());

  // Reset, set txMax
  cfg = RingBufferConfig{};
  cfg.txMax = 256;
  EXPECT_TRUE(cfg.isValid());
}

/** @test RingBufferConfig isRxAtMax checks pending vs max. */
TEST(RingBufferConfigMethodsTest, IsAtMaxChecks) {
  RingBufferConfig cfg{};
  cfg.rxMax = 4096;
  cfg.rxPending = 2048;
  cfg.txMax = 4096;
  cfg.txPending = 4096;

  EXPECT_FALSE(cfg.isRxAtMax());
  EXPECT_TRUE(cfg.isTxAtMax());
}

/** @test RingBufferConfig RT-friendly threshold. */
TEST(RingBufferConfigMethodsTest, RtFriendlyThreshold) {
  RingBufferConfig cfg{};
  cfg.rxPending = 1024;
  cfg.txPending = 1024;
  EXPECT_TRUE(cfg.isRtFriendly());

  cfg.rxPending = RT_RING_SIZE_WARN_THRESHOLD + 1;
  EXPECT_FALSE(cfg.isRtFriendly());
}

/* ----------------------------- CoalesceConfig Methods ----------------------------- */

/** @test CoalesceConfig isLowLatency checks thresholds. */
TEST(CoalesceConfigMethodsTest, IsLowLatencyThresholds) {
  CoalesceConfig cfg{};

  // Default zeros should be low latency
  EXPECT_TRUE(cfg.isLowLatency());

  // Just at threshold
  cfg.rxUsecs = LOW_LATENCY_USECS_THRESHOLD;
  cfg.txUsecs = LOW_LATENCY_USECS_THRESHOLD;
  cfg.rxMaxFrames = LOW_LATENCY_FRAMES_THRESHOLD;
  cfg.txMaxFrames = LOW_LATENCY_FRAMES_THRESHOLD;
  EXPECT_TRUE(cfg.isLowLatency());

  // Over threshold
  cfg.rxUsecs = LOW_LATENCY_USECS_THRESHOLD + 1;
  EXPECT_FALSE(cfg.isLowLatency());
}

/** @test CoalesceConfig adaptive detection. */
TEST(CoalesceConfigMethodsTest, AdaptiveDetection) {
  CoalesceConfig cfg{};

  EXPECT_FALSE(cfg.hasAdaptive());

  cfg.useAdaptiveRx = true;
  EXPECT_TRUE(cfg.hasAdaptive());

  cfg.useAdaptiveRx = false;
  cfg.useAdaptiveTx = true;
  EXPECT_TRUE(cfg.hasAdaptive());
}

/** @test CoalesceConfig RT-friendly requires no adaptive and low values. */
TEST(CoalesceConfigMethodsTest, RtFriendlyRequirements) {
  CoalesceConfig cfg{};

  // Default should be RT-friendly
  EXPECT_TRUE(cfg.isRtFriendly());

  // With adaptive, not RT-friendly
  cfg.useAdaptiveRx = true;
  EXPECT_FALSE(cfg.isRtFriendly());

  // Without adaptive but high values, not RT-friendly
  cfg = CoalesceConfig{};
  cfg.rxUsecs = 100;
  EXPECT_FALSE(cfg.isRtFriendly());
}

/* ----------------------------- PauseConfig Methods ----------------------------- */

/** @test PauseConfig isEnabled checks both directions. */
TEST(PauseConfigMethodsTest, IsEnabledChecks) {
  PauseConfig cfg{};

  EXPECT_FALSE(cfg.isEnabled());

  cfg.rxPause = true;
  EXPECT_TRUE(cfg.isEnabled());

  cfg.rxPause = false;
  cfg.txPause = true;
  EXPECT_TRUE(cfg.isEnabled());
}

/* ----------------------------- NicFeatures Methods ----------------------------- */

/** @test NicFeatures find returns nullptr for missing features. */
TEST(NicFeaturesMethodsTest, FindMissing) {
  NicFeatures feats{};

  EXPECT_EQ(feats.find("nonexistent"), nullptr);
  EXPECT_EQ(feats.find(nullptr), nullptr);
  EXPECT_EQ(feats.find(""), nullptr);
}

/** @test NicFeatures find locates existing features. */
TEST(NicFeaturesMethodsTest, FindExisting) {
  NicFeatures feats{};
  std::strcpy(feats.features[0].name.data(), "test-feature");
  feats.features[0].enabled = true;
  feats.count = 1;

  const NicFeature* found = feats.find("test-feature");
  ASSERT_NE(found, nullptr);
  EXPECT_TRUE(found->enabled);
}

/** @test NicFeatures isEnabled convenience method. */
TEST(NicFeaturesMethodsTest, IsEnabledConvenience) {
  NicFeatures feats{};
  std::strcpy(feats.features[0].name.data(), "enabled-feat");
  feats.features[0].enabled = true;
  std::strcpy(feats.features[1].name.data(), "disabled-feat");
  feats.features[1].enabled = false;
  feats.count = 2;

  EXPECT_TRUE(feats.isEnabled("enabled-feat"));
  EXPECT_FALSE(feats.isEnabled("disabled-feat"));
  EXPECT_FALSE(feats.isEnabled("missing-feat"));
}

/** @test NicFeatures countEnabled counts correctly. */
TEST(NicFeaturesMethodsTest, CountEnabled) {
  NicFeatures feats{};

  for (int i = 0; i < 5; ++i) {
    std::snprintf(feats.features[i].name.data(), FEATURE_NAME_SIZE, "feat%d", i);
    feats.features[i].enabled = (i % 2 == 0); // 0, 2, 4 enabled
  }
  feats.count = 5;

  EXPECT_EQ(feats.countEnabled(), 3U);
}

/* ----------------------------- EthtoolInfo Feature Helpers ----------------------------- */

class EthtoolInfoFeatureTest : public ::testing::Test {
protected:
  EthtoolInfo info_{};

  void addFeature(const char* name, bool enabled) {
    if (info_.features.count < MAX_FEATURES) {
      NicFeature& f = info_.features.features[info_.features.count];
      std::strncpy(f.name.data(), name, FEATURE_NAME_SIZE - 1);
      f.enabled = enabled;
      f.available = true;
      ++info_.features.count;
    }
  }
};

/** @test EthtoolInfo hasTso detects TSO features. */
TEST_F(EthtoolInfoFeatureTest, HasTsoDetection) {
  EXPECT_FALSE(info_.hasTso());

  addFeature("tx-tcp-segmentation", true);
  EXPECT_TRUE(info_.hasTso());
}

/** @test EthtoolInfo hasGro detects GRO feature. */
TEST_F(EthtoolInfoFeatureTest, HasGroDetection) {
  EXPECT_FALSE(info_.hasGro());

  addFeature("rx-gro", true);
  EXPECT_TRUE(info_.hasGro());
}

/** @test EthtoolInfo hasLro detects LRO feature. */
TEST_F(EthtoolInfoFeatureTest, HasLroDetection) {
  EXPECT_FALSE(info_.hasLro());

  addFeature("rx-lro", true);
  EXPECT_TRUE(info_.hasLro());
}

/** @test EthtoolInfo hasRxChecksum detects RX checksum. */
TEST_F(EthtoolInfoFeatureTest, HasRxChecksumDetection) {
  EXPECT_FALSE(info_.hasRxChecksum());

  addFeature("rx-checksum", true);
  EXPECT_TRUE(info_.hasRxChecksum());
}

/** @test EthtoolInfo hasTxChecksum detects TX checksum variants. */
TEST_F(EthtoolInfoFeatureTest, HasTxChecksumDetection) {
  EXPECT_FALSE(info_.hasTxChecksum());

  addFeature("tx-checksum-ipv4", true);
  EXPECT_TRUE(info_.hasTxChecksum());
}

/* ----------------------------- EthtoolInfo RT Assessment ----------------------------- */

/** @test EthtoolInfo rtScore calculation. */
TEST(EthtoolInfoRtScoreTest, ScoreCalculation) {
  EthtoolInfo info{};

  // Unknown (not supported) = 50
  EXPECT_EQ(info.rtScore(), 50);

  // Supported with good defaults = 100
  info.supportsEthtool = true;
  EXPECT_EQ(info.rtScore(), 100);

  // Adaptive coalescing reduces score
  info.coalesce.useAdaptiveRx = true;
  EXPECT_LT(info.rtScore(), 100);

  // Reset and check high coalescing
  info.coalesce.useAdaptiveRx = false;
  info.coalesce.rxUsecs = 150;
  EXPECT_LT(info.rtScore(), 100);
}

/** @test EthtoolInfo isRtFriendly checks multiple factors. */
TEST(EthtoolInfoRtFriendlyTest, MultipleFactors) {
  EthtoolInfo info{};
  info.supportsEthtool = true;

  // Good defaults should be RT-friendly
  EXPECT_TRUE(info.isRtFriendly());

  // Adaptive coalescing is not RT-friendly
  info.coalesce.useAdaptiveRx = true;
  EXPECT_FALSE(info.isRtFriendly());
  info.coalesce.useAdaptiveRx = false;

  // Large ring buffers are not RT-friendly
  info.rings.rxMax = 8192;
  info.rings.rxPending = RT_RING_SIZE_WARN_THRESHOLD + 1;
  EXPECT_FALSE(info.isRtFriendly());
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Non-existent interface returns empty info. */
TEST(EthtoolInfoErrorTest, NonExistentReturnsEmpty) {
  const EthtoolInfo INFO = getEthtoolInfo("noexist_if99");

  EXPECT_STREQ(INFO.ifname.data(), "noexist_if99");
  EXPECT_FALSE(INFO.supportsEthtool);
}

/** @test Null interface name returns empty info. */
TEST(EthtoolInfoErrorTest, NullReturnsEmpty) {
  const EthtoolInfo INFO = getEthtoolInfo(nullptr);

  EXPECT_EQ(INFO.ifname[0], '\0');
  EXPECT_FALSE(INFO.supportsEthtool);
}

/** @test Empty interface name returns empty info. */
TEST(EthtoolInfoErrorTest, EmptyReturnsEmpty) {
  const EthtoolInfo INFO = getEthtoolInfo("");

  EXPECT_EQ(INFO.ifname[0], '\0');
  EXPECT_FALSE(INFO.supportsEthtool);
}

/** @test Loopback typically doesn't support ethtool. */
TEST(EthtoolInfoErrorTest, LoopbackLimitedSupport) {
  const EthtoolInfo INFO = getEthtoolInfo("lo");

  // Loopback may or may not support ethtool depending on kernel
  // Just verify we don't crash and get reasonable results
  EXPECT_STREQ(INFO.ifname.data(), "lo");
  // RT score should be reasonable regardless
  EXPECT_GE(INFO.rtScore(), 0);
  EXPECT_LE(INFO.rtScore(), 100);
}

/* ----------------------------- Standalone API Functions ----------------------------- */

/** @test getRingBufferConfig handles null/empty. */
TEST(StandaloneApiTest, RingBufferConfigHandlesNull) {
  const RingBufferConfig CFG1 = getRingBufferConfig(nullptr);
  EXPECT_FALSE(CFG1.isValid());

  const RingBufferConfig CFG2 = getRingBufferConfig("");
  EXPECT_FALSE(CFG2.isValid());
}

/** @test getCoalesceConfig handles null/empty. */
TEST(StandaloneApiTest, CoalesceConfigHandlesNull) {
  const CoalesceConfig CFG1 = getCoalesceConfig(nullptr);
  // Can't easily check validity, just ensure no crash
  EXPECT_FALSE(CFG1.hasAdaptive()); // Default

  const CoalesceConfig CFG2 = getCoalesceConfig("");
  EXPECT_FALSE(CFG2.hasAdaptive());
}

/** @test getPauseConfig handles null/empty. */
TEST(StandaloneApiTest, PauseConfigHandlesNull) {
  const PauseConfig CFG1 = getPauseConfig(nullptr);
  EXPECT_FALSE(CFG1.isEnabled());

  const PauseConfig CFG2 = getPauseConfig("");
  EXPECT_FALSE(CFG2.isEnabled());
}

/* ----------------------------- EthtoolInfoList Tests ----------------------------- */

/** @test getAllEthtoolInfo returns list within bounds. */
TEST(EthtoolInfoListTest, ListWithinBounds) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  EXPECT_LE(LIST.count, MAX_INTERFACES);
}

/** @test EthtoolInfoList find returns nullptr for missing. */
TEST(EthtoolInfoListTest, FindMissing) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  EXPECT_EQ(LIST.find("nonexistent_if_xyz"), nullptr);
  EXPECT_EQ(LIST.find(nullptr), nullptr);
}

/** @test EthtoolInfoList excludes loopback. */
TEST(EthtoolInfoListTest, ExcludesLoopback) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  EXPECT_EQ(LIST.find("lo"), nullptr);
}

/** @test All entries in list have ethtool support. */
TEST(EthtoolInfoListTest, AllEntriesSupported) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_TRUE(LIST.nics[i].supportsEthtool)
        << "NIC " << LIST.nics[i].ifname.data() << " in list but supportsEthtool is false";
  }
}

/** @test All entries have non-empty names. */
TEST(EthtoolInfoListTest, AllEntriesHaveNames) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(std::strlen(LIST.nics[i].ifname.data()), 0U) << "Entry " << i << " has empty name";
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test RingBufferConfig toString non-empty. */
TEST(ToStringTest, RingBufferConfigNonEmpty) {
  RingBufferConfig cfg{};
  cfg.rxMax = 4096;
  cfg.rxPending = 256;
  cfg.txMax = 4096;
  cfg.txPending = 256;

  const std::string OUTPUT = cfg.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("256"), std::string::npos);
}

/** @test RingBufferConfig toString handles invalid. */
TEST(ToStringTest, RingBufferConfigInvalid) {
  const RingBufferConfig CFG{};
  const std::string OUTPUT = CFG.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("not available"), std::string::npos);
}

/** @test CoalesceConfig toString includes key values. */
TEST(ToStringTest, CoalesceConfigIncludesValues) {
  CoalesceConfig cfg{};
  cfg.rxUsecs = 50;
  cfg.txUsecs = 25;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("50"), std::string::npos);
  EXPECT_NE(OUTPUT.find("25"), std::string::npos);
}

/** @test CoalesceConfig toString shows adaptive status. */
TEST(ToStringTest, CoalesceConfigShowsAdaptive) {
  CoalesceConfig cfg{};
  cfg.useAdaptiveRx = true;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("adaptive"), std::string::npos);
}

/** @test PauseConfig toString handles disabled. */
TEST(ToStringTest, PauseConfigDisabled) {
  const PauseConfig CFG{};
  const std::string OUTPUT = CFG.toString();

  EXPECT_NE(OUTPUT.find("disabled"), std::string::npos);
}

/** @test PauseConfig toString shows enabled directions. */
TEST(ToStringTest, PauseConfigEnabled) {
  PauseConfig cfg{};
  cfg.rxPause = true;
  cfg.txPause = true;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("RX"), std::string::npos);
  EXPECT_NE(OUTPUT.find("TX"), std::string::npos);
}

/** @test NicFeatures toString handles empty. */
TEST(ToStringTest, NicFeaturesEmpty) {
  const NicFeatures FEATS{};
  const std::string OUTPUT = FEATS.toString();

  EXPECT_NE(OUTPUT.find("not available"), std::string::npos);
}

/** @test EthtoolInfo toString includes RT score. */
TEST(ToStringTest, EthtoolInfoIncludesRtScore) {
  EthtoolInfo info{};
  std::strcpy(info.ifname.data(), "eth0");
  info.supportsEthtool = true;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("RT score"), std::string::npos);
}

/** @test EthtoolInfoList toString handles empty. */
TEST(ToStringTest, EthtoolInfoListEmpty) {
  const EthtoolInfoList EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("No ethtool"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getEthtoolInfo returns consistent results. */
TEST(DeterminismTest, ConsistentResults) {
  const EthtoolInfo INFO1 = getEthtoolInfo("lo");
  const EthtoolInfo INFO2 = getEthtoolInfo("lo");

  EXPECT_STREQ(INFO1.ifname.data(), INFO2.ifname.data());
  EXPECT_EQ(INFO1.supportsEthtool, INFO2.supportsEthtool);
  EXPECT_EQ(INFO1.rtScore(), INFO2.rtScore());
}

/** @test getAllEthtoolInfo returns consistent count. */
TEST(DeterminismTest, ConsistentCount) {
  const EthtoolInfoList LIST1 = getAllEthtoolInfo();
  const EthtoolInfoList LIST2 = getAllEthtoolInfo();

  EXPECT_EQ(LIST1.count, LIST2.count);
}

/* ----------------------------- Physical NIC Tests (Conditional) ----------------------------- */

/** @test Physical NICs have reasonable RT scores. */
TEST(PhysicalNicTest, ReasonableRtScores) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const EthtoolInfo& NIC = LIST.nics[i];

    EXPECT_GE(NIC.rtScore(), 0) << "NIC " << NIC.ifname.data() << " has negative RT score";
    EXPECT_LE(NIC.rtScore(), 100) << "NIC " << NIC.ifname.data() << " has RT score > 100";
  }
}

/** @test Physical NICs with rings have valid config. */
TEST(PhysicalNicTest, ValidRingConfig) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const EthtoolInfo& NIC = LIST.nics[i];

    if (NIC.rings.isValid()) {
      // Pending should not exceed max
      EXPECT_LE(NIC.rings.rxPending, NIC.rings.rxMax)
          << "NIC " << NIC.ifname.data() << " RX pending > max";
      EXPECT_LE(NIC.rings.txPending, NIC.rings.txMax)
          << "NIC " << NIC.ifname.data() << " TX pending > max";
    }
  }
}

/** @test Physical NICs have consistent feature counts. */
TEST(PhysicalNicTest, ConsistentFeatureCounts) {
  const EthtoolInfoList LIST = getAllEthtoolInfo();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const EthtoolInfo& NIC = LIST.nics[i];

    EXPECT_LE(NIC.features.count, MAX_FEATURES)
        << "NIC " << NIC.ifname.data() << " has too many features";

    EXPECT_LE(NIC.features.countEnabled(), NIC.features.count)
        << "NIC " << NIC.ifname.data() << " enabled > total";
  }
}