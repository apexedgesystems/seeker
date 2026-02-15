/**
 * @file ContainerLimits_uTest.cpp
 * @brief Unit tests for seeker::system::ContainerLimits.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Container detection depends on runtime environment.
 *  - cgroup limits depend on system configuration and container runtime.
 */

#include "src/system/inc/ContainerLimits.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::CgroupVersion;
using seeker::system::ContainerLimits;
using seeker::system::detectCgroupVersion;
using seeker::system::getContainerLimits;
using seeker::system::isRunningInContainer;
using seeker::system::LIMIT_UNLIMITED;
using seeker::system::toString;

class ContainerLimitsTest : public ::testing::Test {
protected:
  ContainerLimits limits_{};

  void SetUp() override { limits_ = getContainerLimits(); }
};

/* ----------------------------- Basic Query Tests ----------------------------- */

/** @test getContainerLimits doesn't crash. */
TEST_F(ContainerLimitsTest, QueryDoesNotCrash) {
  // Just verify we got here without crashing
  SUCCEED();
}

/** @test isRunningInContainer is consistent with limits.detected. */
TEST_F(ContainerLimitsTest, DetectedConsistent) {
  const bool QUICK_CHECK = isRunningInContainer();
  EXPECT_EQ(limits_.detected, QUICK_CHECK);
}

/* ----------------------------- cgroup Version Tests ----------------------------- */

/** @test cgroup version is detected. */
TEST_F(ContainerLimitsTest, CgroupVersionDetected) {
  // On any Linux system, we should detect some cgroup version
  // (unless running on very old kernel or in unusual environment)
  const CgroupVersion VER = detectCgroupVersion();
  // Don't require specific version, just note what we got
  GTEST_LOG_(INFO) << "Detected cgroup version: " << toString(VER);
}

/** @test cgroup version is consistent. */
TEST_F(ContainerLimitsTest, CgroupVersionConsistent) {
  const CgroupVersion VER1 = detectCgroupVersion();
  const CgroupVersion VER2 = detectCgroupVersion();
  EXPECT_EQ(VER1, VER2);
  EXPECT_EQ(VER1, limits_.cgroupVersion);
}

/* ----------------------------- CPU Limit Tests ----------------------------- */

/** @test hasCpuLimit is consistent with cpuQuotaUs. */
TEST_F(ContainerLimitsTest, HasCpuLimitConsistent) {
  const bool EXPECTED = (limits_.cpuQuotaUs != LIMIT_UNLIMITED && limits_.cpuQuotaUs > 0);
  EXPECT_EQ(limits_.hasCpuLimit(), EXPECTED);
}

/** @test cpuQuotaPercent returns sensible value when limited. */
TEST_F(ContainerLimitsTest, CpuQuotaPercentSensible) {
  if (limits_.hasCpuLimit()) {
    const double PERCENT = limits_.cpuQuotaPercent();
    // Should be positive
    EXPECT_GT(PERCENT, 0.0);
    // Should be reasonable (< 100 CPUs worth)
    EXPECT_LT(PERCENT, 10000.0);
  } else {
    EXPECT_EQ(limits_.cpuQuotaPercent(), 0.0);
  }
}

/** @test cpuPeriodUs is reasonable when set. */
TEST_F(ContainerLimitsTest, CpuPeriodReasonable) {
  if (limits_.cpuPeriodUs > 0 && limits_.cpuPeriodUs != LIMIT_UNLIMITED) {
    // Typical values: 100000 us (100ms) or 1000 us (1ms)
    EXPECT_GE(limits_.cpuPeriodUs, 1000);     // At least 1ms
    EXPECT_LE(limits_.cpuPeriodUs, 10000000); // At most 10s
  }
}

/* ----------------------------- Memory Limit Tests ----------------------------- */

/** @test hasMemoryLimit is consistent with memMaxBytes. */
TEST_F(ContainerLimitsTest, HasMemoryLimitConsistent) {
  const bool EXPECTED = (limits_.memMaxBytes != LIMIT_UNLIMITED && limits_.memMaxBytes > 0);
  EXPECT_EQ(limits_.hasMemoryLimit(), EXPECTED);
}

/** @test memCurrentBytes <= memMaxBytes when both are set. */
TEST_F(ContainerLimitsTest, MemCurrentNotExceedsMax) {
  if (limits_.hasMemoryLimit() && limits_.memCurrentBytes != LIMIT_UNLIMITED) {
    EXPECT_LE(limits_.memCurrentBytes, limits_.memMaxBytes);
  }
}

/* ----------------------------- PID Limit Tests ----------------------------- */

/** @test hasPidLimit is consistent with pidsMax. */
TEST_F(ContainerLimitsTest, HasPidLimitConsistent) {
  const bool EXPECTED = (limits_.pidsMax != LIMIT_UNLIMITED && limits_.pidsMax > 0);
  EXPECT_EQ(limits_.hasPidLimit(), EXPECTED);
}

/** @test pidsCurrent >= 1 when set (at least this process). */
TEST_F(ContainerLimitsTest, PidsCurrentPositive) {
  if (limits_.pidsCurrent != LIMIT_UNLIMITED) {
    EXPECT_GE(limits_.pidsCurrent, 1);
  }
}

/** @test pidsCurrent <= pidsMax when both are set. */
TEST_F(ContainerLimitsTest, PidsCurrentNotExceedsMax) {
  if (limits_.hasPidLimit() && limits_.pidsCurrent != LIMIT_UNLIMITED) {
    EXPECT_LE(limits_.pidsCurrent, limits_.pidsMax);
  }
}

/* ----------------------------- Cpuset Tests ----------------------------- */

/** @test hasCpusetLimit is consistent with cpusetCpus content. */
TEST_F(ContainerLimitsTest, HasCpusetLimitConsistent) {
  const bool EXPECTED = (limits_.cpusetCpus[0] != '\0');
  EXPECT_EQ(limits_.hasCpusetLimit(), EXPECTED);
}

/** @test cpusetCpus contains valid characters when set. */
TEST_F(ContainerLimitsTest, CpusetCpusValidFormat) {
  if (limits_.hasCpusetLimit()) {
    const char* ptr = limits_.cpusetCpus.data();
    while (*ptr != '\0') {
      // Valid characters: digits, dash, comma, newline
      const char C = *ptr;
      EXPECT_TRUE(C == '-' || C == ',' || C == '\n' || (C >= '0' && C <= '9'))
          << "Invalid character in cpuset: " << static_cast<int>(C);
      ++ptr;
    }
  }
}

/* ----------------------------- Container Detection Tests ----------------------------- */

/** @test If detected, runtime or containerId may be set. */
TEST_F(ContainerLimitsTest, ContainerInfoIfDetected) {
  if (limits_.detected) {
    // At least log what we found
    GTEST_LOG_(INFO) << "Container detected: runtime=" << limits_.runtime.data()
                     << " id=" << limits_.containerId.data();
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(ContainerLimitsTest, ToStringNonEmpty) {
  const std::string OUTPUT = limits_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(ContainerLimitsTest, ToStringContainsSections) {
  const std::string OUTPUT = limits_.toString();
  EXPECT_NE(OUTPUT.find("Container"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Detected"), std::string::npos);
  EXPECT_NE(OUTPUT.find("cgroup"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CPU"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Memory"), std::string::npos);
  EXPECT_NE(OUTPUT.find("PIDs"), std::string::npos);
}

/* ----------------------------- CgroupVersion toString Tests ----------------------------- */

/** @test toString(CgroupVersion) returns valid strings. */
TEST(CgroupVersionToStringTest, AllValues) {
  EXPECT_STREQ(toString(CgroupVersion::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(CgroupVersion::V1), "v1");
  EXPECT_STREQ(toString(CgroupVersion::V2), "v2");
  EXPECT_STREQ(toString(CgroupVersion::HYBRID), "hybrid");
}

/** @test toString returns non-null for all enum values. */
TEST(CgroupVersionToStringTest, NeverNull) {
  for (int i = 0; i <= 3; ++i) {
    const char* STR = toString(static_cast<CgroupVersion>(i));
    EXPECT_NE(STR, nullptr);
    EXPECT_GT(std::strlen(STR), 0U);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default ContainerLimits has expected values. */
TEST(ContainerLimitsDefaultTest, DefaultValues) {
  const ContainerLimits DEFAULT{};

  EXPECT_FALSE(DEFAULT.detected);
  EXPECT_EQ(DEFAULT.containerId[0], '\0');
  EXPECT_EQ(DEFAULT.runtime[0], '\0');
  EXPECT_EQ(DEFAULT.cgroupVersion, CgroupVersion::UNKNOWN);
  EXPECT_EQ(DEFAULT.cpuQuotaUs, LIMIT_UNLIMITED);
  EXPECT_EQ(DEFAULT.cpuPeriodUs, LIMIT_UNLIMITED);
  EXPECT_EQ(DEFAULT.cpusetCpus[0], '\0');
  EXPECT_EQ(DEFAULT.memMaxBytes, LIMIT_UNLIMITED);
  EXPECT_EQ(DEFAULT.pidsMax, LIMIT_UNLIMITED);
}

/** @test Default ContainerLimits reports no limits. */
TEST(ContainerLimitsDefaultTest, NoLimits) {
  const ContainerLimits DEFAULT{};

  EXPECT_FALSE(DEFAULT.hasCpuLimit());
  EXPECT_FALSE(DEFAULT.hasMemoryLimit());
  EXPECT_FALSE(DEFAULT.hasPidLimit());
  EXPECT_FALSE(DEFAULT.hasCpusetLimit());
  EXPECT_EQ(DEFAULT.cpuQuotaPercent(), 0.0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getContainerLimits returns consistent results. */
TEST(ContainerLimitsDeterminismTest, ConsistentResults) {
  const ContainerLimits L1 = getContainerLimits();
  const ContainerLimits L2 = getContainerLimits();

  // Static detection should be identical
  EXPECT_EQ(L1.detected, L2.detected);
  EXPECT_STREQ(L1.runtime.data(), L2.runtime.data());
  EXPECT_EQ(L1.cgroupVersion, L2.cgroupVersion);

  // Limits should be identical (cgroup config doesn't change mid-test)
  EXPECT_EQ(L1.cpuQuotaUs, L2.cpuQuotaUs);
  EXPECT_EQ(L1.cpuPeriodUs, L2.cpuPeriodUs);
  EXPECT_EQ(L1.memMaxBytes, L2.memMaxBytes);
  EXPECT_EQ(L1.pidsMax, L2.pidsMax);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test LIMIT_UNLIMITED sentinel has expected value. */
TEST(ContainerLimitsConstantsTest, UnlimitedSentinel) { EXPECT_EQ(LIMIT_UNLIMITED, -1); }