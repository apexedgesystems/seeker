/**
 * @file CpuFreq_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuFrequencySummary.
 *
 * Notes:
 *  - Tests verify invariants, not specific frequency values.
 *  - Systems without cpufreq driver will have empty results (valid).
 */

#include "src/cpu/inc/CpuFreq.hpp"

#include <gtest/gtest.h>

#include <algorithm> // std::sort
#include <cstring>   // std::strlen
#include <set>
#include <string>

using seeker::cpu::CoreFrequency;
using seeker::cpu::CpuFrequencySummary;
using seeker::cpu::getCpuFrequencySummary;
using seeker::cpu::GOVERNOR_STRING_SIZE;

class CpuFreqTest : public ::testing::Test {
protected:
  CpuFrequencySummary summary_{};

  void SetUp() override { summary_ = getCpuFrequencySummary(); }
};

/* ----------------------------- Basic Invariants ----------------------------- */

/** @test Empty result is valid (system may not have cpufreq driver). */
TEST_F(CpuFreqTest, EmptyResultIsValid) {
  // Just verify no crash; empty is acceptable
  if (summary_.cores.empty()) {
    GTEST_LOG_(INFO) << "No cpufreq data available (driver may not be loaded)";
  }
}

/** @test All CPU IDs are non-negative. */
TEST_F(CpuFreqTest, CpuIdsNonNegative) {
  for (const CoreFrequency& CORE : summary_.cores) {
    EXPECT_GE(CORE.cpuId, 0) << "Invalid CPU ID";
  }
}

/** @test CPU IDs are unique. */
TEST_F(CpuFreqTest, CpuIdsUnique) {
  std::set<int> seen;
  for (const CoreFrequency& CORE : summary_.cores) {
    const auto [IT, INSERTED] = seen.insert(CORE.cpuId);
    EXPECT_TRUE(INSERTED) << "Duplicate CPU ID: " << CORE.cpuId;
  }
}

/* ----------------------------- Frequency Invariants ----------------------------- */

/** @test Frequency values are non-negative. */
TEST_F(CpuFreqTest, FrequenciesNonNegative) {
  for (const CoreFrequency& CORE : summary_.cores) {
    EXPECT_GE(CORE.minKHz, 0) << "cpu" << CORE.cpuId << " minKHz negative";
    EXPECT_GE(CORE.maxKHz, 0) << "cpu" << CORE.cpuId << " maxKHz negative";
    EXPECT_GE(CORE.curKHz, 0) << "cpu" << CORE.cpuId << " curKHz negative";
  }
}

/** @test Min frequency does not exceed max (when both valid). */
TEST_F(CpuFreqTest, MinDoesNotExceedMax) {
  for (const CoreFrequency& CORE : summary_.cores) {
    if (CORE.minKHz > 0 && CORE.maxKHz > 0) {
      EXPECT_LE(CORE.minKHz, CORE.maxKHz) << "cpu" << CORE.cpuId << " min > max";
    }
  }
}

/** @test Current frequency is within reasonable range (when valid). */
TEST_F(CpuFreqTest, CurrentWithinReasonableRange) {
  for (const CoreFrequency& CORE : summary_.cores) {
    if (CORE.curKHz > 0 && CORE.minKHz > 0) {
      // Current should be >= min (may be slightly below due to timing)
      EXPECT_GE(CORE.curKHz, CORE.minKHz * 9 / 10)
          << "cpu" << CORE.cpuId << " curKHz significantly below minKHz";
    }
    if (CORE.curKHz > 0 && CORE.maxKHz > 0) {
      // Current can exceed max during turbo, but not by huge amount
      EXPECT_LE(CORE.curKHz, CORE.maxKHz * 2) << "cpu" << CORE.cpuId << " curKHz unreasonably high";
    }
  }
}

/* ----------------------------- Governor String Tests ----------------------------- */

/** @test Governor strings are null-terminated. */
TEST_F(CpuFreqTest, GovernorStringsNullTerminated) {
  for (const CoreFrequency& CORE : summary_.cores) {
    bool foundNull = false;
    for (std::size_t i = 0; i < GOVERNOR_STRING_SIZE; ++i) {
      if (CORE.governor[i] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "cpu" << CORE.cpuId << " governor not null-terminated";
  }
}

/** @test Governor strings are within bounds. */
TEST_F(CpuFreqTest, GovernorStringsWithinBounds) {
  for (const CoreFrequency& CORE : summary_.cores) {
    const std::size_t LEN = std::strlen(CORE.governor.data());
    EXPECT_LT(LEN, GOVERNOR_STRING_SIZE) << "cpu" << CORE.cpuId << " governor string too long";
  }
}

/** @test Known governors are recognized (informational). */
TEST_F(CpuFreqTest, KnownGovernors) {
  const std::set<std::string> KNOWN = {
      "performance", "powersave", "ondemand", "conservative",
      "schedutil",   "userspace", "" // empty is valid if not available
  };

  for (const CoreFrequency& CORE : summary_.cores) {
    const std::string GOV(CORE.governor.data());
    if (KNOWN.find(GOV) == KNOWN.end()) {
      GTEST_LOG_(INFO) << "cpu" << CORE.cpuId << " has unknown governor: " << GOV;
    }
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test CoreFrequency::toString produces valid output. */
TEST_F(CpuFreqTest, CoreToStringValid) {
  for (const CoreFrequency& CORE : summary_.cores) {
    const std::string OUTPUT = CORE.toString();
    EXPECT_FALSE(OUTPUT.empty());
    EXPECT_NE(OUTPUT.find("cpu"), std::string::npos);
    EXPECT_NE(OUTPUT.find("kHz"), std::string::npos);
  }
}

/** @test CpuFrequencySummary::toString produces valid output. */
TEST_F(CpuFreqTest, SummaryToStringValid) {
  const std::string OUTPUT = summary_.toString();
  EXPECT_FALSE(OUTPUT.empty());

  if (!summary_.cores.empty()) {
    // Should contain at least one CPU reference
    EXPECT_NE(OUTPUT.find("cpu"), std::string::npos);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default CoreFrequency has sane defaults. */
TEST(CpuFreqDefaultTest, DefaultCoreFrequency) {
  const CoreFrequency DEFAULT{};

  EXPECT_EQ(DEFAULT.cpuId, -1);
  EXPECT_EQ(DEFAULT.minKHz, 0);
  EXPECT_EQ(DEFAULT.maxKHz, 0);
  EXPECT_EQ(DEFAULT.curKHz, 0);
  EXPECT_FALSE(DEFAULT.turboAvailable);
  EXPECT_EQ(DEFAULT.governor[0], '\0');
}

/** @test Default CpuFrequencySummary is empty. */
TEST(CpuFreqDefaultTest, DefaultSummaryEmpty) {
  const CpuFrequencySummary DEFAULT{};
  EXPECT_TRUE(DEFAULT.cores.empty());
}