/**
 * @file KernelInfo_uTest.cpp
 * @brief Unit tests for seeker::system::KernelInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have a kernel; specific features may vary.
 */

#include "src/system/inc/KernelInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::getKernelInfo;
using seeker::system::KernelInfo;
using seeker::system::PreemptModel;
using seeker::system::toString;

class KernelInfoTest : public ::testing::Test {
protected:
  KernelInfo info_{};

  void SetUp() override { info_ = getKernelInfo(); }
};

/* ----------------------------- Kernel Release Tests ----------------------------- */

/** @test Kernel release string is non-empty. */
TEST_F(KernelInfoTest, ReleaseNonEmpty) { EXPECT_GT(std::strlen(info_.release.data()), 0U); }

/** @test Kernel release contains expected format (major.minor). */
TEST_F(KernelInfoTest, ReleaseContainsDot) {
  const std::string RELEASE{info_.release.data()};
  // All Linux kernel releases contain at least one '.'
  EXPECT_NE(RELEASE.find('.'), std::string::npos) << "Release: " << RELEASE;
}

/** @test Kernel release starts with digit. */
TEST_F(KernelInfoTest, ReleaseStartsWithDigit) {
  EXPECT_GE(info_.release[0], '0');
  EXPECT_LE(info_.release[0], '9');
}

/* ----------------------------- Kernel Version Tests ----------------------------- */

/** @test Kernel version string is non-empty. */
TEST_F(KernelInfoTest, VersionNonEmpty) { EXPECT_GT(std::strlen(info_.version.data()), 0U); }

/** @test Kernel version contains "Linux". */
TEST_F(KernelInfoTest, VersionContainsLinux) {
  const std::string VERSION{info_.version.data()};
  EXPECT_NE(VERSION.find("Linux"), std::string::npos) << "Version: " << VERSION;
}

/* ----------------------------- Preemption Model Tests ----------------------------- */

/** @test Preemption model is set to a valid value. */
TEST_F(KernelInfoTest, PreemptModelValid) {
  EXPECT_NE(info_.preempt, PreemptModel::UNKNOWN)
      << "Preemption model should be detected on any Linux kernel";
}

/** @test Preemption model string is non-empty. */
TEST_F(KernelInfoTest, PreemptStrNonEmpty) { EXPECT_GT(std::strlen(info_.preemptModelStr()), 0U); }

/** @test RT-PREEMPT flag is consistent with preemption model. */
TEST_F(KernelInfoTest, RtPreemptConsistent) {
  if (info_.rtPreemptPatched) {
    EXPECT_EQ(info_.preempt, PreemptModel::PREEMPT_RT);
  }
  if (info_.preempt == PreemptModel::PREEMPT_RT) {
    EXPECT_TRUE(info_.rtPreemptPatched);
  }
}

/** @test isRtKernel returns true for PREEMPT or PREEMPT_RT. */
TEST_F(KernelInfoTest, IsRtKernelLogic) {
  const bool EXPECTED =
      (info_.preempt == PreemptModel::PREEMPT || info_.preempt == PreemptModel::PREEMPT_RT);
  EXPECT_EQ(info_.isRtKernel(), EXPECTED);
}

/** @test isPreemptRt returns true only for PREEMPT_RT. */
TEST_F(KernelInfoTest, IsPreemptRtLogic) {
  const bool EXPECTED = (info_.preempt == PreemptModel::PREEMPT_RT || info_.rtPreemptPatched);
  EXPECT_EQ(info_.isPreemptRt(), EXPECTED);
}

/* ----------------------------- Cmdline Flags Tests ----------------------------- */

/** @test hasRtCmdlineFlags is consistent with individual flags. */
TEST_F(KernelInfoTest, HasRtCmdlineFlagsConsistent) {
  const bool EXPECTED = info_.nohzFull || info_.isolCpus || info_.rcuNocbs;
  EXPECT_EQ(info_.hasRtCmdlineFlags(), EXPECTED);
}

/** @test Individual cmdline flags are booleans. */
TEST_F(KernelInfoTest, CmdlineFlagsAreBool) {
  // Just verify they compile and have valid boolean values
  EXPECT_TRUE(info_.nohzFull || !info_.nohzFull);
  EXPECT_TRUE(info_.isolCpus || !info_.isolCpus);
  EXPECT_TRUE(info_.rcuNocbs || !info_.rcuNocbs);
  EXPECT_TRUE(info_.skewTick || !info_.skewTick);
  EXPECT_TRUE(info_.tscReliable || !info_.tscReliable);
  EXPECT_TRUE(info_.cstateLimit || !info_.cstateLimit);
  EXPECT_TRUE(info_.idlePoll || !info_.idlePoll);
}

/* ----------------------------- Taint Status Tests ----------------------------- */

/** @test Taint flag is consistent with taint mask. */
TEST_F(KernelInfoTest, TaintConsistent) { EXPECT_EQ(info_.tainted, (info_.taintMask != 0)); }

/** @test Taint mask is non-negative. */
TEST_F(KernelInfoTest, TaintMaskNonNegative) { EXPECT_GE(info_.taintMask, 0); }

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(KernelInfoTest, ToStringNonEmpty) {
  const std::string OUTPUT = info_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains kernel info. */
TEST_F(KernelInfoTest, ToStringContainsKernelInfo) {
  const std::string OUTPUT = info_.toString();
  EXPECT_NE(OUTPUT.find("Kernel"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Release"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Preemption"), std::string::npos);
}

/** @test toString contains release string. */
TEST_F(KernelInfoTest, ToStringContainsRelease) {
  const std::string OUTPUT = info_.toString();
  EXPECT_NE(OUTPUT.find(info_.release.data()), std::string::npos);
}

/* ----------------------------- PreemptModel toString Tests ----------------------------- */

/** @test toString(PreemptModel) returns valid strings. */
TEST(PreemptModelToStringTest, AllValues) {
  EXPECT_STREQ(toString(PreemptModel::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(PreemptModel::NONE), "none");
  EXPECT_STREQ(toString(PreemptModel::VOLUNTARY), "voluntary");
  EXPECT_STREQ(toString(PreemptModel::PREEMPT), "preempt");
  EXPECT_STREQ(toString(PreemptModel::PREEMPT_RT), "preempt_rt");
}

/** @test toString returns non-null for all enum values. */
TEST(PreemptModelToStringTest, NeverNull) {
  for (int i = 0; i <= 4; ++i) {
    const char* STR = toString(static_cast<PreemptModel>(i));
    EXPECT_NE(STR, nullptr);
    EXPECT_GT(std::strlen(STR), 0U);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default KernelInfo is zeroed. */
TEST(KernelInfoDefaultTest, DefaultZeroed) {
  const KernelInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.release[0], '\0');
  EXPECT_EQ(DEFAULT.version[0], '\0');
  EXPECT_EQ(DEFAULT.preempt, PreemptModel::UNKNOWN);
  EXPECT_FALSE(DEFAULT.rtPreemptPatched);
  EXPECT_FALSE(DEFAULT.nohzFull);
  EXPECT_FALSE(DEFAULT.isolCpus);
  EXPECT_FALSE(DEFAULT.rcuNocbs);
  EXPECT_FALSE(DEFAULT.tainted);
  EXPECT_EQ(DEFAULT.taintMask, 0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getKernelInfo returns consistent results. */
TEST(KernelInfoDeterminismTest, ConsistentResults) {
  const KernelInfo INFO1 = getKernelInfo();
  const KernelInfo INFO2 = getKernelInfo();

  // Static values should be identical
  EXPECT_STREQ(INFO1.release.data(), INFO2.release.data());
  EXPECT_STREQ(INFO1.version.data(), INFO2.version.data());
  EXPECT_EQ(INFO1.preempt, INFO2.preempt);
  EXPECT_EQ(INFO1.rtPreemptPatched, INFO2.rtPreemptPatched);

  // Cmdline flags should be identical
  EXPECT_EQ(INFO1.nohzFull, INFO2.nohzFull);
  EXPECT_EQ(INFO1.isolCpus, INFO2.isolCpus);
  EXPECT_EQ(INFO1.rcuNocbs, INFO2.rcuNocbs);
}

/* ----------------------------- RT Detection Scenarios ----------------------------- */

/** @test Non-RT kernel detection. */
TEST_F(KernelInfoTest, NonRtKernelDetection) {
  if (info_.preempt == PreemptModel::NONE || info_.preempt == PreemptModel::VOLUNTARY) {
    EXPECT_FALSE(info_.isRtKernel());
    EXPECT_FALSE(info_.isPreemptRt());
  }
}

/** @test PREEMPT kernel detection. */
TEST_F(KernelInfoTest, PreemptKernelDetection) {
  if (info_.preempt == PreemptModel::PREEMPT && !info_.rtPreemptPatched) {
    EXPECT_TRUE(info_.isRtKernel());
    EXPECT_FALSE(info_.isPreemptRt());
  }
}

/** @test PREEMPT_RT kernel detection. */
TEST_F(KernelInfoTest, PreemptRtKernelDetection) {
  if (info_.preempt == PreemptModel::PREEMPT_RT) {
    EXPECT_TRUE(info_.isRtKernel());
    EXPECT_TRUE(info_.isPreemptRt());
    EXPECT_TRUE(info_.rtPreemptPatched);
  }
}