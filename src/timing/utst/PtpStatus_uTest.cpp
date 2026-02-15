/**
 * @file PtpStatus_uTest.cpp
 * @brief Unit tests for seeker::timing::PtpStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - PTP hardware availability varies by system.
 *  - Capability queries require /dev/ptp* access (may need permissions).
 */

#include "src/timing/inc/PtpStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::timing::getPhcIndexForInterface;
using seeker::timing::getPtpClockCaps;
using seeker::timing::getPtpStatus;
using seeker::timing::isPtpSupported;
using seeker::timing::PTP_MAX_CLOCKS;
using seeker::timing::PtpClock;
using seeker::timing::PtpClockCaps;
using seeker::timing::PtpStatus;

class PtpStatusTest : public ::testing::Test {
protected:
  PtpStatus status_{};

  void SetUp() override { status_ = getPtpStatus(); }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default PtpClockCaps is zeroed. */
TEST(PtpClockCapsDefaultTest, DefaultZeroed) {
  const PtpClockCaps DEFAULT{};

  EXPECT_EQ(DEFAULT.maxAdjPpb, 0);
  EXPECT_EQ(DEFAULT.nAlarm, 0);
  EXPECT_EQ(DEFAULT.nExtTs, 0);
  EXPECT_EQ(DEFAULT.nPerOut, 0);
  EXPECT_EQ(DEFAULT.nPins, 0);
  EXPECT_FALSE(DEFAULT.pps);
  EXPECT_FALSE(DEFAULT.crossTimestamp);
  EXPECT_FALSE(DEFAULT.adjustPhase);
}

/** @test Default PtpClock is zeroed. */
TEST(PtpClockDefaultTest, DefaultZeroed) {
  const PtpClock DEFAULT{};

  EXPECT_EQ(DEFAULT.device[0], '\0');
  EXPECT_EQ(DEFAULT.clockName[0], '\0');
  EXPECT_EQ(DEFAULT.index, -1);
  EXPECT_EQ(DEFAULT.phcIndex, -1);
  EXPECT_FALSE(DEFAULT.capsQuerySucceeded);
  EXPECT_FALSE(DEFAULT.hasBoundInterface);
}

/** @test Default PtpStatus is zeroed. */
TEST(PtpStatusDefaultTest, DefaultZeroed) {
  const PtpStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.clockCount, 0U);
  EXPECT_FALSE(DEFAULT.ptpSupported);
  EXPECT_FALSE(DEFAULT.hasHardwareClock);
}

/* ----------------------------- PtpClockCaps Method Tests ----------------------------- */

/** @test hasExtTimestamp requires nExtTs > 0. */
TEST(PtpClockCapsTest, HasExtTimestamp) {
  PtpClockCaps caps{};
  EXPECT_FALSE(caps.hasExtTimestamp());

  caps.nExtTs = 1;
  EXPECT_TRUE(caps.hasExtTimestamp());

  caps.nExtTs = 4;
  EXPECT_TRUE(caps.hasExtTimestamp());
}

/** @test hasPeriodicOutput requires nPerOut > 0. */
TEST(PtpClockCapsTest, HasPeriodicOutput) {
  PtpClockCaps caps{};
  EXPECT_FALSE(caps.hasPeriodicOutput());

  caps.nPerOut = 1;
  EXPECT_TRUE(caps.hasPeriodicOutput());
}

/** @test hasHighPrecisionSync requires both crossTimestamp and pps. */
TEST(PtpClockCapsTest, HasHighPrecisionSync) {
  PtpClockCaps caps{};
  EXPECT_FALSE(caps.hasHighPrecisionSync());

  // Note: crossTimestamp detection is conservative (not detected at compile time)
  // so hasHighPrecisionSync will typically be false unless manually set
  caps.crossTimestamp = true;
  EXPECT_FALSE(caps.hasHighPrecisionSync()); // Still need PPS

  caps.pps = true;
  EXPECT_TRUE(caps.hasHighPrecisionSync());

  caps.crossTimestamp = false;
  EXPECT_FALSE(caps.hasHighPrecisionSync()); // Need both
}

/* ----------------------------- PtpClock Method Tests ----------------------------- */

/** @test Default PtpClock is not valid. */
TEST(PtpClockTest, DefaultNotValid) {
  const PtpClock DEFAULT{};
  EXPECT_FALSE(DEFAULT.isValid());
}

/** @test PtpClock with device and index is valid. */
TEST(PtpClockTest, WithDeviceAndIndexIsValid) {
  PtpClock clock{};
  std::strcpy(clock.device.data(), "ptp0");
  clock.index = 0;
  EXPECT_TRUE(clock.isValid());
}

/** @test PtpClock with device but negative index is not valid. */
TEST(PtpClockTest, NegativeIndexNotValid) {
  PtpClock clock{};
  std::strcpy(clock.device.data(), "ptp0");
  clock.index = -1;
  EXPECT_FALSE(clock.isValid());
}

/** @test rtScore returns 0 for invalid clock. */
TEST(PtpClockTest, InvalidClockZeroScore) {
  const PtpClock CLOCK{};
  EXPECT_EQ(CLOCK.rtScore(), 0);
}

/** @test rtScore increases with capabilities. */
TEST(PtpClockTest, ScoreIncreasesWithCaps) {
  PtpClock clock{};
  std::strcpy(clock.device.data(), "ptp0");
  clock.index = 0;

  const int BASE_SCORE = clock.rtScore();

  clock.capsQuerySucceeded = true;
  const int WITH_CAPS = clock.rtScore();
  EXPECT_GT(WITH_CAPS, BASE_SCORE);

  clock.caps.pps = true;
  const int WITH_PPS = clock.rtScore();
  EXPECT_GT(WITH_PPS, WITH_CAPS);

  clock.caps.nExtTs = 2;
  const int WITH_EXTTS = clock.rtScore();
  EXPECT_GT(WITH_EXTTS, WITH_PPS);

  clock.hasBoundInterface = true;
  const int WITH_IFACE = clock.rtScore();
  EXPECT_GT(WITH_IFACE, WITH_EXTTS);
}

/** @test rtScore capped at 100. */
TEST(PtpClockTest, ScoreCappedAt100) {
  PtpClock clock{};
  std::strcpy(clock.device.data(), "ptp0");
  clock.index = 0;
  clock.capsQuerySucceeded = true;
  clock.caps.maxAdjPpb = 500000000; // Very high
  clock.caps.pps = true;
  clock.caps.crossTimestamp = true;
  clock.caps.nExtTs = 8;
  clock.caps.nPerOut = 8;
  clock.hasBoundInterface = true;

  EXPECT_LE(clock.rtScore(), 100);
}

/* ----------------------------- PtpStatus Method Tests ----------------------------- */

/** @test Clock count within bounds. */
TEST_F(PtpStatusTest, ClockCountWithinBounds) { EXPECT_LE(status_.clockCount, PTP_MAX_CLOCKS); }

/** @test hasHardwareClock consistent with clockCount. */
TEST_F(PtpStatusTest, HasHardwareClockConsistent) {
  EXPECT_EQ(status_.hasHardwareClock, status_.clockCount > 0);
}

/** @test findByDevice returns nullptr for unknown device. */
TEST_F(PtpStatusTest, FindByDeviceUnknown) {
  EXPECT_EQ(status_.findByDevice("definitely_not_a_ptp_device"), nullptr);
}

/** @test findByDevice returns nullptr for null. */
TEST_F(PtpStatusTest, FindByDeviceNull) { EXPECT_EQ(status_.findByDevice(nullptr), nullptr); }

/** @test findByIndex returns nullptr for invalid index. */
TEST_F(PtpStatusTest, FindByIndexInvalid) {
  EXPECT_EQ(status_.findByIndex(-1), nullptr);
  EXPECT_EQ(status_.findByIndex(9999), nullptr);
}

/** @test findByInterface returns nullptr for null. */
TEST_F(PtpStatusTest, FindByInterfaceNull) { EXPECT_EQ(status_.findByInterface(nullptr), nullptr); }

/** @test findByInterface returns nullptr for unknown interface. */
TEST_F(PtpStatusTest, FindByInterfaceUnknown) {
  EXPECT_EQ(status_.findByInterface("definitely_not_an_interface"), nullptr);
}

/** @test getBestClock returns nullptr when no clocks. */
TEST(PtpStatusNoClocksTest, GetBestClockReturnsNull) {
  const PtpStatus EMPTY{};
  EXPECT_EQ(EMPTY.getBestClock(), nullptr);
}

/** @test All enumerated clocks are valid. */
TEST_F(PtpStatusTest, AllClocksValid) {
  for (std::size_t i = 0; i < status_.clockCount; ++i) {
    EXPECT_TRUE(status_.clocks[i].isValid()) << "Clock " << i << " should be valid";
  }
}

/** @test All enumerated clocks have device names starting with "ptp". */
TEST_F(PtpStatusTest, AllClocksHavePtpPrefix) {
  for (std::size_t i = 0; i < status_.clockCount; ++i) {
    EXPECT_EQ(std::strncmp(status_.clocks[i].device.data(), "ptp", 3), 0)
        << "Clock " << i << " device name should start with 'ptp'";
  }
}

/** @test findByDevice finds enumerated clocks. */
TEST_F(PtpStatusTest, FindByDeviceFindsClocks) {
  for (std::size_t i = 0; i < status_.clockCount; ++i) {
    const PtpClock* FOUND = status_.findByDevice(status_.clocks[i].device.data());
    EXPECT_NE(FOUND, nullptr) << "Should find clock " << status_.clocks[i].device.data();
    if (FOUND != nullptr) {
      EXPECT_EQ(FOUND->index, status_.clocks[i].index);
    }
  }
}

/** @test findByIndex finds enumerated clocks. */
TEST_F(PtpStatusTest, FindByIndexFindsClocks) {
  for (std::size_t i = 0; i < status_.clockCount; ++i) {
    const PtpClock* FOUND = status_.findByIndex(status_.clocks[i].index);
    EXPECT_NE(FOUND, nullptr) << "Should find clock with index " << status_.clocks[i].index;
    if (FOUND != nullptr) {
      EXPECT_EQ(std::strcmp(FOUND->device.data(), status_.clocks[i].device.data()), 0);
    }
  }
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test RT score is in valid range. */
TEST_F(PtpStatusTest, RtScoreInRange) {
  const int SCORE = status_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test RT score is 0 when PTP not supported. */
TEST(PtpStatusScoreTest, ZeroWhenNotSupported) {
  PtpStatus status;
  status.ptpSupported = false;
  EXPECT_EQ(status.rtScore(), 0);
}

/** @test RT score is low when no hardware clocks. */
TEST(PtpStatusScoreTest, LowWhenNoClocks) {
  PtpStatus status;
  status.ptpSupported = true;
  status.clockCount = 0;
  EXPECT_LE(status.rtScore(), 20);
}

/* ----------------------------- API Function Tests ----------------------------- */

/** @test isPtpSupported returns consistent result. */
TEST(PtpSupportedTest, ConsistentResult) {
  const bool S1 = isPtpSupported();
  const bool S2 = isPtpSupported();
  EXPECT_EQ(S1, S2);
}

/** @test isPtpSupported consistent with getPtpStatus. */
TEST_F(PtpStatusTest, SupportedConsistent) { EXPECT_EQ(isPtpSupported(), status_.ptpSupported); }

/** @test getPtpClockCaps returns empty for null. */
TEST(PtpClockCapsQueryTest, ReturnsEmptyForNull) {
  const PtpClockCaps CAPS = getPtpClockCaps(nullptr);
  EXPECT_EQ(CAPS.maxAdjPpb, 0);
  EXPECT_EQ(CAPS.nAlarm, 0);
}

/** @test getPtpClockCaps returns empty for invalid device. */
TEST(PtpClockCapsQueryTest, ReturnsEmptyForInvalid) {
  const PtpClockCaps CAPS = getPtpClockCaps("/dev/definitely_not_a_ptp_device");
  EXPECT_EQ(CAPS.maxAdjPpb, 0);
}

/** @test getPhcIndexForInterface returns -1 for null. */
TEST(PhcIndexTest, ReturnsNegativeForNull) { EXPECT_EQ(getPhcIndexForInterface(nullptr), -1); }

/** @test getPhcIndexForInterface returns -1 for unknown interface. */
TEST(PhcIndexTest, ReturnsNegativeForUnknown) {
  EXPECT_EQ(getPhcIndexForInterface("definitely_not_an_interface"), -1);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(PtpStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains "PTP". */
TEST_F(PtpStatusTest, ToStringContainsPtp) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("PTP"), std::string::npos);
}

/** @test toString mentions hardware clocks count. */
TEST_F(PtpStatusTest, ToStringMentionsClockCount) {
  if (status_.ptpSupported) {
    const std::string OUTPUT = status_.toString();
    EXPECT_NE(OUTPUT.find("clocks"), std::string::npos);
  }
}

/** @test toJson produces valid JSON structure. */
TEST_F(PtpStatusTest, ToJsonValidStructure) {
  const std::string JSON = status_.toJson();
  EXPECT_FALSE(JSON.empty());
  EXPECT_EQ(JSON.front(), '{');
  EXPECT_EQ(JSON.back(), '}');
}

/** @test toJson contains expected fields. */
TEST_F(PtpStatusTest, ToJsonContainsFields) {
  const std::string JSON = status_.toJson();
  EXPECT_NE(JSON.find("\"ptpSupported\""), std::string::npos);
  EXPECT_NE(JSON.find("\"clockCount\""), std::string::npos);
  EXPECT_NE(JSON.find("\"clocks\""), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getPtpStatus returns consistent results. */
TEST(PtpStatusDeterminismTest, ConsistentResults) {
  const PtpStatus S1 = getPtpStatus();
  const PtpStatus S2 = getPtpStatus();

  EXPECT_EQ(S1.ptpSupported, S2.ptpSupported);
  EXPECT_EQ(S1.clockCount, S2.clockCount);
  EXPECT_EQ(S1.hasHardwareClock, S2.hasHardwareClock);

  for (std::size_t i = 0; i < S1.clockCount; ++i) {
    EXPECT_EQ(S1.clocks[i].index, S2.clocks[i].index);
    EXPECT_EQ(std::strcmp(S1.clocks[i].device.data(), S2.clocks[i].device.data()), 0);
  }
}