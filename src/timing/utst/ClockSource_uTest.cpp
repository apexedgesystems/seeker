/**
 * @file ClockSource_uTest.cpp
 * @brief Unit tests for seeker::timing::ClockSource.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have clocksources and timer resolution.
 *  - Specific clocksource (TSC vs HPET) depends on kernel and hardware.
 */

#include "src/timing/inc/ClockSource.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <time.h>

using seeker::timing::ClockResolution;
using seeker::timing::ClockSource;
using seeker::timing::CLOCKSOURCE_NAME_SIZE;
using seeker::timing::getClockResolutionNs;
using seeker::timing::getClockSource;
using seeker::timing::MAX_CLOCKSOURCES;

class ClockSourceTest : public ::testing::Test {
protected:
  ClockSource cs_{};

  void SetUp() override { cs_ = getClockSource(); }
};

/* ----------------------------- Clocksource Tests ----------------------------- */

/** @test Current clocksource is non-empty. */
TEST_F(ClockSourceTest, CurrentClockSourceNonEmpty) {
  EXPECT_NE(cs_.current[0], '\0') << "Current clocksource should be set";
  EXPECT_GT(std::strlen(cs_.current.data()), 0U);
}

/** @test Current clocksource is null-terminated within bounds. */
TEST_F(ClockSourceTest, CurrentClockSourceNullTerminated) {
  bool foundNull = false;
  for (std::size_t i = 0; i < CLOCKSOURCE_NAME_SIZE; ++i) {
    if (cs_.current[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull) << "Current clocksource must be null-terminated";
}

/** @test At least one clocksource is available. */
TEST_F(ClockSourceTest, AtLeastOneClockSourceAvailable) {
  EXPECT_GT(cs_.availableCount, 0U) << "At least one clocksource should be available";
}

/** @test Available clocksource count within bounds. */
TEST_F(ClockSourceTest, AvailableCountWithinBounds) {
  EXPECT_LE(cs_.availableCount, MAX_CLOCKSOURCES);
}

/** @test Current clocksource is in available list. */
TEST_F(ClockSourceTest, CurrentInAvailableList) {
  if (cs_.current[0] == '\0') {
    GTEST_SKIP() << "No current clocksource detected";
  }

  EXPECT_TRUE(cs_.hasClockSource(cs_.current.data()))
      << "Current clocksource should be in available list";
}

/** @test Available clocksources are null-terminated. */
TEST_F(ClockSourceTest, AvailableClockSourcesNullTerminated) {
  for (std::size_t i = 0; i < cs_.availableCount; ++i) {
    bool foundNull = false;
    for (std::size_t j = 0; j < CLOCKSOURCE_NAME_SIZE; ++j) {
      if (cs_.available[i][j] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Available clocksource " << i << " must be null-terminated";
  }
}

/** @test hasClockSource returns false for null. */
TEST_F(ClockSourceTest, HasClockSourceRejectsNull) { EXPECT_FALSE(cs_.hasClockSource(nullptr)); }

/** @test hasClockSource returns false for unknown name. */
TEST_F(ClockSourceTest, HasClockSourceRejectsUnknown) {
  EXPECT_FALSE(cs_.hasClockSource("definitely_not_a_clocksource_xyz123"));
}

/* ----------------------------- Timer Resolution Tests ----------------------------- */

/** @test CLOCK_MONOTONIC is available. */
TEST_F(ClockSourceTest, MonotonicAvailable) {
  EXPECT_TRUE(cs_.monotonic.available) << "CLOCK_MONOTONIC should be available on Linux";
}

/** @test CLOCK_MONOTONIC has positive resolution. */
TEST_F(ClockSourceTest, MonotonicResolutionPositive) {
  if (!cs_.monotonic.available) {
    GTEST_SKIP() << "CLOCK_MONOTONIC not available";
  }
  EXPECT_GT(cs_.monotonic.resolutionNs, 0) << "Resolution should be positive";
}

/** @test CLOCK_MONOTONIC resolution is reasonable (1ns to 10ms). */
TEST_F(ClockSourceTest, MonotonicResolutionReasonable) {
  if (!cs_.monotonic.available) {
    GTEST_SKIP() << "CLOCK_MONOTONIC not available";
  }
  // Expected range: 1ns (high-res) to 10ms (very coarse)
  EXPECT_GE(cs_.monotonic.resolutionNs, 1);
  EXPECT_LE(cs_.monotonic.resolutionNs, 10'000'000);
}

/** @test CLOCK_MONOTONIC_RAW is available. */
TEST_F(ClockSourceTest, MonotonicRawAvailable) {
  EXPECT_TRUE(cs_.monotonicRaw.available) << "CLOCK_MONOTONIC_RAW should be available on Linux";
}

/** @test CLOCK_REALTIME is available. */
TEST_F(ClockSourceTest, RealtimeAvailable) {
  EXPECT_TRUE(cs_.realtime.available) << "CLOCK_REALTIME should be available";
}

/** @test Coarse clocks have lower or equal resolution. */
TEST_F(ClockSourceTest, CoarseClocksCoarserResolution) {
  if (cs_.monotonic.available && cs_.monotonicCoarse.available) {
    // Coarse resolution should be >= fine resolution (larger value = coarser)
    EXPECT_GE(cs_.monotonicCoarse.resolutionNs, cs_.monotonic.resolutionNs)
        << "Coarse clock should have coarser resolution";
  }

  if (cs_.realtime.available && cs_.realtimeCoarse.available) {
    EXPECT_GE(cs_.realtimeCoarse.resolutionNs, cs_.realtime.resolutionNs)
        << "Coarse realtime should have coarser resolution";
  }
}

/** @test CLOCK_BOOTTIME is available on modern kernels. */
TEST_F(ClockSourceTest, BoottimeAvailable) {
  // CLOCK_BOOTTIME available since Linux 2.6.39
  EXPECT_TRUE(cs_.boottime.available) << "CLOCK_BOOTTIME should be available";
}

/* ----------------------------- ClockResolution Helper Tests ----------------------------- */

/** @test isHighRes returns true for <= 1us. */
TEST(ClockResolutionTest, IsHighResThreshold) {
  ClockResolution res;
  res.available = true;

  res.resolutionNs = 1;
  EXPECT_TRUE(res.isHighRes());

  res.resolutionNs = 1000;
  EXPECT_TRUE(res.isHighRes());

  res.resolutionNs = 1001;
  EXPECT_FALSE(res.isHighRes());
}

/** @test isHighRes returns false when unavailable. */
TEST(ClockResolutionTest, IsHighResRequiresAvailable) {
  ClockResolution res;
  res.available = false;
  res.resolutionNs = 1;
  EXPECT_FALSE(res.isHighRes());
}

/** @test isCoarse returns true for > 1ms. */
TEST(ClockResolutionTest, IsCoarseThreshold) {
  ClockResolution res;
  res.available = true;

  res.resolutionNs = 1'000'000;
  EXPECT_FALSE(res.isCoarse());

  res.resolutionNs = 1'000'001;
  EXPECT_TRUE(res.isCoarse());
}

/* ----------------------------- Helper Method Tests ----------------------------- */

/** @test isTsc returns true for TSC clocksource. */
TEST(ClockSourceHelperTest, IsTscDetection) {
  ClockSource cs;
  std::strcpy(cs.current.data(), "tsc");
  EXPECT_TRUE(cs.isTsc());

  std::strcpy(cs.current.data(), "hpet");
  EXPECT_FALSE(cs.isTsc());
}

/** @test isHpet returns true for HPET clocksource. */
TEST(ClockSourceHelperTest, IsHpetDetection) {
  ClockSource cs;
  std::strcpy(cs.current.data(), "hpet");
  EXPECT_TRUE(cs.isHpet());

  std::strcpy(cs.current.data(), "tsc");
  EXPECT_FALSE(cs.isHpet());
}

/** @test isAcpiPm returns true for acpi_pm clocksource. */
TEST(ClockSourceHelperTest, IsAcpiPmDetection) {
  ClockSource cs;
  std::strcpy(cs.current.data(), "acpi_pm");
  EXPECT_TRUE(cs.isAcpiPm());

  std::strcpy(cs.current.data(), "tsc");
  EXPECT_FALSE(cs.isAcpiPm());
}

/** @test hasHighResTimers reflects MONOTONIC resolution. */
TEST_F(ClockSourceTest, HasHighResTimersConsistent) {
  EXPECT_EQ(cs_.hasHighResTimers(), cs_.monotonic.isHighRes());
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test RT score is in valid range. */
TEST_F(ClockSourceTest, RtScoreInRange) {
  const int SCORE = cs_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test TSC with high-res timers gets high score. */
TEST(ClockSourceScoreTest, TscHighResHighScore) {
  ClockSource cs;
  std::strcpy(cs.current.data(), "tsc");
  cs.monotonic.available = true;
  cs.monotonic.resolutionNs = 1;
  cs.monotonicRaw.available = true;
  cs.monotonicRaw.resolutionNs = 1;

  EXPECT_GE(cs.rtScore(), 90);
}

/** @test HPET gets lower score than TSC. */
TEST(ClockSourceScoreTest, HpetLowerScoreThanTsc) {
  ClockSource tsc;
  std::strcpy(tsc.current.data(), "tsc");
  tsc.monotonic.available = true;
  tsc.monotonic.resolutionNs = 1000; // 1us - avoids both capping at 100

  ClockSource hpet;
  std::strcpy(hpet.current.data(), "hpet");
  hpet.monotonic.available = true;
  hpet.monotonic.resolutionNs = 1000;

  EXPECT_GT(tsc.rtScore(), hpet.rtScore());
}

/** @test Unknown clocksource gets base score. */
TEST(ClockSourceScoreTest, UnknownClockSourceBaseScore) {
  ClockSource cs;
  std::strcpy(cs.current.data(), "unknown_source");
  cs.monotonic.available = false;

  const int SCORE = cs.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 60); // Should be relatively low
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(ClockSourceTest, ToStringNonEmpty) {
  const std::string OUTPUT = cs_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains clocksource info. */
TEST_F(ClockSourceTest, ToStringContainsCurrent) {
  const std::string OUTPUT = cs_.toString();
  EXPECT_NE(OUTPUT.find("Current:"), std::string::npos);
}

/** @test toString contains resolution info. */
TEST_F(ClockSourceTest, ToStringContainsResolution) {
  const std::string OUTPUT = cs_.toString();
  EXPECT_NE(OUTPUT.find("MONOTONIC"), std::string::npos);
}

/** @test toString contains RT score. */
TEST_F(ClockSourceTest, ToStringContainsRtScore) {
  const std::string OUTPUT = cs_.toString();
  EXPECT_NE(OUTPUT.find("RT Score:"), std::string::npos);
}

/* ----------------------------- getClockResolutionNs Tests ----------------------------- */

/** @test getClockResolutionNs returns positive for CLOCK_MONOTONIC. */
TEST(GetClockResolutionTest, MonotonicPositive) {
  const std::int64_t RES = getClockResolutionNs(CLOCK_MONOTONIC);
  EXPECT_GT(RES, 0);
}

/** @test getClockResolutionNs returns 0 for invalid clock. */
TEST(GetClockResolutionTest, InvalidClockReturnsZero) {
  // -1 is not a valid clock ID
  const std::int64_t RES = getClockResolutionNs(-1);
  EXPECT_EQ(RES, 0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getClockSource returns consistent results. */
TEST(ClockSourceDeterminismTest, ConsistentResults) {
  const ClockSource CS1 = getClockSource();
  const ClockSource CS2 = getClockSource();

  EXPECT_STREQ(CS1.current.data(), CS2.current.data());
  EXPECT_EQ(CS1.availableCount, CS2.availableCount);
  EXPECT_EQ(CS1.monotonic.resolutionNs, CS2.monotonic.resolutionNs);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default ClockSource is zeroed. */
TEST(ClockSourceDefaultTest, DefaultZeroed) {
  const ClockSource DEFAULT{};

  EXPECT_EQ(DEFAULT.current[0], '\0');
  EXPECT_EQ(DEFAULT.availableCount, 0U);
  EXPECT_FALSE(DEFAULT.monotonic.available);
  EXPECT_EQ(DEFAULT.monotonic.resolutionNs, 0);
}

/** @test Default ClockResolution is zeroed. */
TEST(ClockResolutionDefaultTest, DefaultZeroed) {
  const ClockResolution DEFAULT{};

  EXPECT_EQ(DEFAULT.resolutionNs, 0);
  EXPECT_FALSE(DEFAULT.available);
  EXPECT_FALSE(DEFAULT.isHighRes());
  EXPECT_FALSE(DEFAULT.isCoarse());
}