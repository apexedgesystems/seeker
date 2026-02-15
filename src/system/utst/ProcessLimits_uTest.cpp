/**
 * @file ProcessLimits_uTest.cpp
 * @brief Unit tests for seeker::system::ProcessLimits.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Actual limit values vary by system configuration and user privileges.
 */

#include "src/system/inc/ProcessLimits.hpp"

#include <gtest/gtest.h>

#include <sys/resource.h> // RLIMIT_* constants

#include <string>

using seeker::system::formatLimit;
using seeker::system::getProcessLimits;
using seeker::system::getRlimit;
using seeker::system::ProcessLimits;
using seeker::system::RLIMIT_UNLIMITED_VALUE;
using seeker::system::RlimitValue;

class ProcessLimitsTest : public ::testing::Test {
protected:
  ProcessLimits limits_{};

  void SetUp() override { limits_ = getProcessLimits(); }
};

/* ----------------------------- RlimitValue Tests ----------------------------- */

/** @test RlimitValue::canIncreaseTo for unlimited. */
TEST(RlimitValueTest, CanIncreaseToUnlimited) {
  RlimitValue val{};
  val.soft = 100;
  val.hard = RLIMIT_UNLIMITED_VALUE;
  val.unlimited = false;

  EXPECT_TRUE(val.canIncreaseTo(1000000));
  EXPECT_TRUE(val.canIncreaseTo(RLIMIT_UNLIMITED_VALUE));
}

/** @test RlimitValue::canIncreaseTo for limited. */
TEST(RlimitValueTest, CanIncreaseToLimited) {
  RlimitValue val{};
  val.soft = 100;
  val.hard = 1000;
  val.unlimited = false;

  EXPECT_TRUE(val.canIncreaseTo(500));
  EXPECT_TRUE(val.canIncreaseTo(1000));
  EXPECT_FALSE(val.canIncreaseTo(1001));
}

/** @test RlimitValue::hasAtLeast for unlimited. */
TEST(RlimitValueTest, HasAtLeastUnlimited) {
  RlimitValue val{};
  val.unlimited = true;
  val.soft = RLIMIT_UNLIMITED_VALUE;

  EXPECT_TRUE(val.hasAtLeast(0));
  EXPECT_TRUE(val.hasAtLeast(1000000000ULL));
}

/** @test RlimitValue::hasAtLeast for limited. */
TEST(RlimitValueTest, HasAtLeastLimited) {
  RlimitValue val{};
  val.soft = 1000;
  val.hard = 2000;
  val.unlimited = false;

  EXPECT_TRUE(val.hasAtLeast(500));
  EXPECT_TRUE(val.hasAtLeast(1000));
  EXPECT_FALSE(val.hasAtLeast(1001));
}

/* ----------------------------- Individual Limit Tests ----------------------------- */

/** @test RTPRIO limit is queryable. */
TEST_F(ProcessLimitsTest, RtprioQueryable) {
  // Just verify it doesn't crash and has valid structure
  EXPECT_TRUE(limits_.rtprio.unlimited || limits_.rtprio.soft <= limits_.rtprio.hard ||
              limits_.rtprio.hard == RLIMIT_UNLIMITED_VALUE);
}

/** @test RTTIME limit is queryable. */
TEST_F(ProcessLimitsTest, RttimeQueryable) {
  EXPECT_TRUE(limits_.rttime.unlimited || limits_.rttime.soft <= limits_.rttime.hard ||
              limits_.rttime.hard == RLIMIT_UNLIMITED_VALUE);
}

/** @test MEMLOCK limit is queryable. */
TEST_F(ProcessLimitsTest, MemlockQueryable) {
  EXPECT_TRUE(limits_.memlock.unlimited || limits_.memlock.soft <= limits_.memlock.hard ||
              limits_.memlock.hard == RLIMIT_UNLIMITED_VALUE);
}

/** @test NOFILE limit is positive. */
TEST_F(ProcessLimitsTest, NofilePositive) {
  // All processes must be able to open at least stdin/stdout/stderr
  EXPECT_TRUE(limits_.nofile.unlimited || limits_.nofile.soft >= 3);
}

/** @test NPROC limit is positive. */
TEST_F(ProcessLimitsTest, NprocPositive) {
  // At least this process is running
  EXPECT_TRUE(limits_.nproc.unlimited || limits_.nproc.soft >= 1);
}

/** @test STACK limit is reasonable. */
TEST_F(ProcessLimitsTest, StackReasonable) {
  // Stack should be at least 8KB (typical minimum)
  EXPECT_TRUE(limits_.stack.unlimited || limits_.stack.soft >= 8192);
}

/* ----------------------------- Convenience Method Tests ----------------------------- */

/** @test rtprioMax returns valid range. */
TEST_F(ProcessLimitsTest, RtprioMaxRange) {
  const int MAX = limits_.rtprioMax();
  EXPECT_GE(MAX, 0);
  EXPECT_LE(MAX, 99);
}

/** @test rtprioMax is consistent with soft limit. */
TEST_F(ProcessLimitsTest, RtprioMaxConsistent) {
  if (limits_.rtprio.unlimited) {
    EXPECT_EQ(limits_.rtprioMax(), 99);
  } else if (limits_.rtprio.soft == 0) {
    EXPECT_EQ(limits_.rtprioMax(), 0);
  } else {
    EXPECT_GE(limits_.rtprioMax(), 1);
    EXPECT_LE(static_cast<std::uint64_t>(limits_.rtprioMax()), limits_.rtprio.soft);
  }
}

/** @test canUseRtScheduling is consistent with rtprioMax. */
TEST_F(ProcessLimitsTest, CanUseRtSchedulingConsistent) {
  EXPECT_EQ(limits_.canUseRtScheduling(), limits_.rtprioMax() > 0);
}

/** @test canUseRtPriority validates priority range. */
TEST_F(ProcessLimitsTest, CanUseRtPriorityRange) {
  // Invalid priorities should always fail
  EXPECT_FALSE(limits_.canUseRtPriority(0));
  EXPECT_FALSE(limits_.canUseRtPriority(-1));
  EXPECT_FALSE(limits_.canUseRtPriority(100));

  // Priority 1 should work if RT is allowed at all
  if (limits_.canUseRtScheduling()) {
    EXPECT_TRUE(limits_.canUseRtPriority(1));
  }
}

/** @test hasUnlimitedMemlock is consistent with limit. */
TEST_F(ProcessLimitsTest, HasUnlimitedMemlockConsistent) {
  if (limits_.memlock.unlimited || limits_.memlock.soft == RLIMIT_UNLIMITED_VALUE) {
    EXPECT_TRUE(limits_.hasUnlimitedMemlock());
  } else {
    EXPECT_FALSE(limits_.hasUnlimitedMemlock());
  }
}

/** @test canLockMemory for unlimited. */
TEST_F(ProcessLimitsTest, CanLockMemoryUnlimited) {
  if (limits_.hasUnlimitedMemlock()) {
    EXPECT_TRUE(limits_.canLockMemory(1));
    EXPECT_TRUE(limits_.canLockMemory(1024ULL * 1024 * 1024)); // 1 GiB
  }
}

/** @test canLockMemory for limited. */
TEST_F(ProcessLimitsTest, CanLockMemoryLimited) {
  if (!limits_.hasUnlimitedMemlock()) {
    // Should be able to lock less than limit
    if (limits_.memlock.soft > 0) {
      EXPECT_TRUE(limits_.canLockMemory(limits_.memlock.soft - 1));
    }
    // Should not be able to lock more than limit
    EXPECT_FALSE(limits_.canLockMemory(limits_.memlock.soft + 1));
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(ProcessLimitsTest, ToStringNonEmpty) {
  const std::string OUTPUT = limits_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(ProcessLimitsTest, ToStringContainsSections) {
  const std::string OUTPUT = limits_.toString();
  EXPECT_NE(OUTPUT.find("Process Limits"), std::string::npos);
  EXPECT_NE(OUTPUT.find("RT Scheduling"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Memory"), std::string::npos);
  EXPECT_NE(OUTPUT.find("RTPRIO"), std::string::npos);
  EXPECT_NE(OUTPUT.find("MEMLOCK"), std::string::npos);
}

/** @test toRtSummary produces non-empty output. */
TEST_F(ProcessLimitsTest, ToRtSummaryNonEmpty) {
  const std::string OUTPUT = limits_.toRtSummary();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("RT"), std::string::npos);
}

/* ----------------------------- formatLimit Tests ----------------------------- */

/** @test formatLimit handles unlimited. */
TEST(FormatLimitTest, Unlimited) {
  EXPECT_EQ(formatLimit(RLIMIT_UNLIMITED_VALUE, false), "unlimited");
  EXPECT_EQ(formatLimit(RLIMIT_UNLIMITED_VALUE, true), "unlimited");
}

/** @test formatLimit handles zero. */
TEST(FormatLimitTest, Zero) {
  EXPECT_EQ(formatLimit(0, false), "0");
  EXPECT_EQ(formatLimit(0, true), "0 B");
}

/** @test formatLimit handles bytes. */
TEST(FormatLimitTest, Bytes) {
  EXPECT_EQ(formatLimit(1024, true), "1.0 KiB");
  EXPECT_EQ(formatLimit(1024 * 1024, true), "1.0 MiB");
  EXPECT_EQ(formatLimit(1024ULL * 1024 * 1024, true), "1.0 GiB");
}

/** @test formatLimit handles counts. */
TEST(FormatLimitTest, Counts) {
  EXPECT_EQ(formatLimit(1024, false), "1024");
  EXPECT_EQ(formatLimit(99, false), "99");
}

/* ----------------------------- getRlimit Tests ----------------------------- */

/** @test getRlimit returns valid structure. */
TEST(GetRlimitTest, ReturnsValidStructure) {
  const RlimitValue VAL = getRlimit(RLIMIT_NOFILE);

  // Soft should not exceed hard (unless both unlimited)
  if (!VAL.unlimited && VAL.hard != RLIMIT_UNLIMITED_VALUE) {
    EXPECT_LE(VAL.soft, VAL.hard);
  }
}

/** @test getRlimit handles invalid resource gracefully. */
TEST(GetRlimitTest, InvalidResourceReturnsZero) {
  // Invalid resource number should return zeroed struct
  const RlimitValue VAL = getRlimit(-1);
  EXPECT_EQ(VAL.soft, 0U);
  EXPECT_EQ(VAL.hard, 0U);
  EXPECT_FALSE(VAL.unlimited);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getProcessLimits returns consistent results. */
TEST(ProcessLimitsDeterminismTest, ConsistentResults) {
  const ProcessLimits L1 = getProcessLimits();
  const ProcessLimits L2 = getProcessLimits();

  // All static limits should be identical
  EXPECT_EQ(L1.rtprio.soft, L2.rtprio.soft);
  EXPECT_EQ(L1.rtprio.hard, L2.rtprio.hard);
  EXPECT_EQ(L1.memlock.soft, L2.memlock.soft);
  EXPECT_EQ(L1.nofile.soft, L2.nofile.soft);
  EXPECT_EQ(L1.nproc.soft, L2.nproc.soft);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default ProcessLimits is zeroed. */
TEST(ProcessLimitsDefaultTest, DefaultZeroed) {
  const ProcessLimits DEFAULT{};

  EXPECT_EQ(DEFAULT.rtprio.soft, 0U);
  EXPECT_EQ(DEFAULT.rtprio.hard, 0U);
  EXPECT_FALSE(DEFAULT.rtprio.unlimited);
  EXPECT_EQ(DEFAULT.memlock.soft, 0U);
  EXPECT_EQ(DEFAULT.nofile.soft, 0U);
}

/** @test Default RlimitValue is zeroed. */
TEST(RlimitValueDefaultTest, DefaultZeroed) {
  const RlimitValue DEFAULT{};

  EXPECT_EQ(DEFAULT.soft, 0U);
  EXPECT_EQ(DEFAULT.hard, 0U);
  EXPECT_FALSE(DEFAULT.unlimited);
}