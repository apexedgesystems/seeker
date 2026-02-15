/**
 * @file Affinity_uTest.cpp
 * @brief Unit tests for seeker::cpu affinity API.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Affinity changes are scoped to the test thread only.
 */

#include "src/cpu/inc/Affinity.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::cpu::AffinityStatus;
using seeker::cpu::CpuSet;
using seeker::cpu::getConfiguredCpuCount;
using seeker::cpu::getCurrentThreadAffinity;
using seeker::cpu::MAX_CPUS;
using seeker::cpu::setCurrentThreadAffinity;
using seeker::cpu::toString;

/* ----------------------------- CpuSet Tests ----------------------------- */

class CpuSetTest : public ::testing::Test {
protected:
  CpuSet set_{};
};

/** @test Default-constructed CpuSet is empty. */
TEST_F(CpuSetTest, DefaultIsEmpty) {
  EXPECT_TRUE(set_.empty());
  EXPECT_EQ(set_.count(), 0U);
}

/** @test Set and test individual CPUs. */
TEST_F(CpuSetTest, SetAndTest) {
  set_.set(0);
  set_.set(5);
  set_.set(63);

  EXPECT_TRUE(set_.test(0));
  EXPECT_TRUE(set_.test(5));
  EXPECT_TRUE(set_.test(63));
  EXPECT_FALSE(set_.test(1));
  EXPECT_FALSE(set_.test(64));
  EXPECT_EQ(set_.count(), 3U);
  EXPECT_FALSE(set_.empty());
}

/** @test Clear removes CPUs from set. */
TEST_F(CpuSetTest, ClearRemovesCpu) {
  set_.set(10);
  set_.set(20);
  EXPECT_EQ(set_.count(), 2U);

  set_.clear(10);
  EXPECT_FALSE(set_.test(10));
  EXPECT_TRUE(set_.test(20));
  EXPECT_EQ(set_.count(), 1U);
}

/** @test Reset clears all CPUs. */
TEST_F(CpuSetTest, ResetClearsAll) {
  set_.set(0);
  set_.set(100);
  set_.set(500);
  EXPECT_EQ(set_.count(), 3U);

  set_.reset();
  EXPECT_TRUE(set_.empty());
  EXPECT_EQ(set_.count(), 0U);
}

/** @test Out-of-range operations are safe (no crash, no effect). */
TEST_F(CpuSetTest, OutOfRangeSafe) {
  set_.set(MAX_CPUS);
  set_.set(MAX_CPUS + 100);
  EXPECT_TRUE(set_.empty());

  EXPECT_FALSE(set_.test(MAX_CPUS));
  EXPECT_FALSE(set_.test(MAX_CPUS + 100));

  set_.clear(MAX_CPUS);
  EXPECT_TRUE(set_.empty());
}

/** @test toString produces valid format. */
TEST_F(CpuSetTest, ToStringFormat) {
  EXPECT_EQ(set_.toString(), "{}");

  set_.set(0);
  EXPECT_EQ(set_.toString(), "{0}");

  set_.set(2);
  set_.set(5);
  EXPECT_EQ(set_.toString(), "{0,2,5}");
}

/* ----------------------------- Status Tests ----------------------------- */

/** @test AffinityStatus toString returns non-null strings. */
TEST(AffinityStatusTest, ToStringReturnsValidStrings) {
  EXPECT_STREQ(toString(AffinityStatus::OK), "OK");
  EXPECT_STREQ(toString(AffinityStatus::INVALID_ARGUMENT), "INVALID_ARGUMENT");
  EXPECT_STREQ(toString(AffinityStatus::SYSCALL_FAILED), "SYSCALL_FAILED");
}

/* ----------------------------- System Query Tests ----------------------------- */

/** @test getConfiguredCpuCount returns reasonable value. */
TEST(AffinitySystemTest, ConfiguredCpuCountReasonable) {
  const std::size_t COUNT = getConfiguredCpuCount();

  EXPECT_GE(COUNT, 1U);
  EXPECT_LE(COUNT, MAX_CPUS);
}

/** @test getCurrentThreadAffinity returns non-empty set. */
TEST(AffinitySystemTest, CurrentAffinityNonEmpty) {
  const CpuSet AFFINITY = getCurrentThreadAffinity();

  EXPECT_FALSE(AFFINITY.empty());
  EXPECT_GE(AFFINITY.count(), 1U);
}

/** @test Current affinity CPUs are within configured count. */
TEST(AffinitySystemTest, AffinityWithinConfiguredRange) {
  const std::size_t CPU_COUNT = getConfiguredCpuCount();
  const CpuSet AFFINITY = getCurrentThreadAffinity();

  for (std::size_t i = CPU_COUNT; i < MAX_CPUS; ++i) {
    EXPECT_FALSE(AFFINITY.test(i)) << "CPU " << i << " set but only " << CPU_COUNT << " configured";
  }
}

/* ----------------------------- Set Affinity Tests ----------------------------- */

/** @test Setting empty affinity returns INVALID_ARGUMENT. */
TEST(AffinitySetTest, EmptySetReturnsInvalidArgument) {
  const CpuSet EMPTY{};
  const AffinityStatus STATUS = setCurrentThreadAffinity(EMPTY);

  EXPECT_EQ(STATUS, AffinityStatus::INVALID_ARGUMENT);
}

/** @test Round-trip: set affinity then get should match. */
TEST(AffinitySetTest, RoundTripSingleCpu) {
  const CpuSet ORIGINAL = getCurrentThreadAffinity();
  if (ORIGINAL.count() < 2) {
    GTEST_SKIP() << "Need at least 2 CPUs for round-trip test";
  }

  // Find first CPU in original set
  std::size_t firstCpu = 0;
  for (std::size_t i = 0; i < MAX_CPUS; ++i) {
    if (ORIGINAL.test(i)) {
      firstCpu = i;
      break;
    }
  }

  // Set affinity to single CPU
  CpuSet singleCpu{};
  singleCpu.set(firstCpu);

  const AffinityStatus SET_STATUS = setCurrentThreadAffinity(singleCpu);
  ASSERT_EQ(SET_STATUS, AffinityStatus::OK);

  // Verify
  const CpuSet CURRENT = getCurrentThreadAffinity();
  EXPECT_EQ(CURRENT.count(), 1U);
  EXPECT_TRUE(CURRENT.test(firstCpu));

  // Restore original affinity
  const AffinityStatus RESTORE_STATUS = setCurrentThreadAffinity(ORIGINAL);
  EXPECT_EQ(RESTORE_STATUS, AffinityStatus::OK);
}

/** @test Setting multiple CPUs works. */
TEST(AffinitySetTest, SetMultipleCpus) {
  const CpuSet ORIGINAL = getCurrentThreadAffinity();
  if (ORIGINAL.count() < 2) {
    GTEST_SKIP() << "Need at least 2 CPUs for multi-CPU test";
  }

  // Build set of first two CPUs from original
  CpuSet twoCpus{};
  std::size_t found = 0;
  for (std::size_t i = 0; i < MAX_CPUS && found < 2; ++i) {
    if (ORIGINAL.test(i)) {
      twoCpus.set(i);
      ++found;
    }
  }

  const AffinityStatus SET_STATUS = setCurrentThreadAffinity(twoCpus);
  ASSERT_EQ(SET_STATUS, AffinityStatus::OK);

  const CpuSet CURRENT = getCurrentThreadAffinity();
  EXPECT_EQ(CURRENT.count(), 2U);

  // Restore
  const AffinityStatus RESTORE_STATUS = setCurrentThreadAffinity(ORIGINAL);
  EXPECT_EQ(RESTORE_STATUS, AffinityStatus::OK);
}