/**
 * @file CpuUtilization_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuUtilization.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Delta tests use short sleep to generate measurable time difference.
 */

#include "src/cpu/inc/CpuUtilization.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using seeker::cpu::computeUtilizationDelta;
using seeker::cpu::CpuTimeCounters;
using seeker::cpu::CpuUtilizationDelta;
using seeker::cpu::CpuUtilizationPercent;
using seeker::cpu::CpuUtilizationSnapshot;
using seeker::cpu::getCpuUtilizationSnapshot;
using seeker::cpu::MAX_CPUS;

class CpuUtilizationTest : public ::testing::Test {
protected:
  CpuUtilizationSnapshot snap_{};

  void SetUp() override { snap_ = getCpuUtilizationSnapshot(); }
};

/* ----------------------------- CpuTimeCounters Tests ----------------------------- */

/** @test Default CpuTimeCounters is zeroed. */
TEST(CpuTimeCountersTest, DefaultZero) {
  const CpuTimeCounters DEFAULT{};

  EXPECT_EQ(DEFAULT.user, 0U);
  EXPECT_EQ(DEFAULT.nice, 0U);
  EXPECT_EQ(DEFAULT.system, 0U);
  EXPECT_EQ(DEFAULT.idle, 0U);
  EXPECT_EQ(DEFAULT.iowait, 0U);
  EXPECT_EQ(DEFAULT.irq, 0U);
  EXPECT_EQ(DEFAULT.softirq, 0U);
  EXPECT_EQ(DEFAULT.steal, 0U);
  EXPECT_EQ(DEFAULT.guest, 0U);
  EXPECT_EQ(DEFAULT.guestNice, 0U);
}

/** @test CpuTimeCounters total() sums all fields. */
TEST(CpuTimeCountersTest, TotalSumsAllFields) {
  CpuTimeCounters c{};
  c.user = 100;
  c.nice = 10;
  c.system = 50;
  c.idle = 500;
  c.iowait = 20;
  c.irq = 5;
  c.softirq = 3;
  c.steal = 2;
  c.guest = 1;
  c.guestNice = 1;

  EXPECT_EQ(c.total(), 692U);
}

/** @test CpuTimeCounters active() excludes idle and iowait. */
TEST(CpuTimeCountersTest, ActiveExcludesIdleAndIowait) {
  CpuTimeCounters c{};
  c.user = 100;
  c.system = 50;
  c.idle = 500;
  c.iowait = 20;

  const std::uint64_t TOTAL = c.total();
  const std::uint64_t ACTIVE = c.active();

  EXPECT_EQ(ACTIVE, TOTAL - 500 - 20);
}

/* ----------------------------- Snapshot Tests ----------------------------- */

/** @test Snapshot has valid timestamp. */
TEST_F(CpuUtilizationTest, TimestampPositive) { EXPECT_GT(snap_.timestampNs, 0U); }

/** @test Snapshot has at least one core. */
TEST_F(CpuUtilizationTest, CoreCountAtLeastOne) { EXPECT_GE(snap_.coreCount, 1U); }

/** @test Core count does not exceed MAX_CPUS. */
TEST_F(CpuUtilizationTest, CoreCountWithinBounds) { EXPECT_LE(snap_.coreCount, MAX_CPUS); }

/** @test Aggregate counters are non-zero (system has been running). */
TEST_F(CpuUtilizationTest, AggregateNonZero) { EXPECT_GT(snap_.aggregate.total(), 0U); }

/** @test Aggregate idle is typically the largest component. */
TEST_F(CpuUtilizationTest, AggregateIdlePositive) {
  // Idle should be positive on any system
  EXPECT_GT(snap_.aggregate.idle, 0U);
}

/** @test Per-core counters are populated. */
TEST_F(CpuUtilizationTest, PerCoreCountersPopulated) {
  for (std::size_t i = 0; i < snap_.coreCount; ++i) {
    // Each core should have accumulated some time
    EXPECT_GT(snap_.perCore[i].total(), 0U) << "Core " << i << " has zero total time";
  }
}

/** @test Subsequent snapshots have increasing timestamps. */
TEST_F(CpuUtilizationTest, TimestampsIncrease) {
  const CpuUtilizationSnapshot SNAP2 = getCpuUtilizationSnapshot();
  EXPECT_GT(SNAP2.timestampNs, snap_.timestampNs);
}

/** @test Counters are monotonically non-decreasing. */
TEST_F(CpuUtilizationTest, CountersNonDecreasing) {
  const CpuUtilizationSnapshot SNAP2 = getCpuUtilizationSnapshot();

  EXPECT_GE(SNAP2.aggregate.total(), snap_.aggregate.total());
  EXPECT_GE(SNAP2.aggregate.user, snap_.aggregate.user);
  EXPECT_GE(SNAP2.aggregate.system, snap_.aggregate.system);
  EXPECT_GE(SNAP2.aggregate.idle, snap_.aggregate.idle);
}

/* ----------------------------- Delta Tests ----------------------------- */

/** @test Delta with same snapshot produces zero percentages. */
TEST_F(CpuUtilizationTest, DeltaSameSnapshotZero) {
  const CpuUtilizationDelta DELTA = computeUtilizationDelta(snap_, snap_);

  EXPECT_EQ(DELTA.intervalNs, 0U);
  EXPECT_EQ(DELTA.aggregate.user, 0.0);
  EXPECT_EQ(DELTA.aggregate.system, 0.0);
  EXPECT_EQ(DELTA.aggregate.idle, 0.0);
}

/** @test Delta with time gap produces valid percentages. */
TEST(CpuUtilizationDeltaTest, DeltaWithSleepValid) {
  const CpuUtilizationSnapshot BEFORE = getCpuUtilizationSnapshot();

  // Sleep briefly to accumulate some idle time
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const CpuUtilizationSnapshot AFTER = getCpuUtilizationSnapshot();
  const CpuUtilizationDelta DELTA = computeUtilizationDelta(BEFORE, AFTER);

  // Interval should be approximately 50ms
  EXPECT_GT(DELTA.intervalNs, 40'000'000U);  // > 40ms
  EXPECT_LT(DELTA.intervalNs, 200'000'000U); // < 200ms

  // Percentages should sum to approximately 100% for aggregate
  const double SUM = DELTA.aggregate.user + DELTA.aggregate.nice + DELTA.aggregate.system +
                     DELTA.aggregate.idle + DELTA.aggregate.iowait + DELTA.aggregate.irq +
                     DELTA.aggregate.softirq + DELTA.aggregate.steal + DELTA.aggregate.guest +
                     DELTA.aggregate.guestNice;
  EXPECT_NEAR(SUM, 100.0, 1.0);

  // Core count should match
  EXPECT_EQ(DELTA.coreCount, std::min(BEFORE.coreCount, AFTER.coreCount));
}

/** @test Percentages are in valid range (0-100). */
TEST(CpuUtilizationDeltaTest, PercentagesInRange) {
  const CpuUtilizationSnapshot BEFORE = getCpuUtilizationSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const CpuUtilizationSnapshot AFTER = getCpuUtilizationSnapshot();
  const CpuUtilizationDelta DELTA = computeUtilizationDelta(BEFORE, AFTER);

  // Check aggregate
  EXPECT_GE(DELTA.aggregate.user, 0.0);
  EXPECT_LE(DELTA.aggregate.user, 100.0);
  EXPECT_GE(DELTA.aggregate.idle, 0.0);
  EXPECT_LE(DELTA.aggregate.idle, 100.0);
  EXPECT_GE(DELTA.aggregate.active(), 0.0);
  EXPECT_LE(DELTA.aggregate.active(), 100.0);

  // Check per-core
  for (std::size_t i = 0; i < DELTA.coreCount; ++i) {
    EXPECT_GE(DELTA.perCore[i].user, 0.0) << "Core " << i;
    EXPECT_LE(DELTA.perCore[i].user, 100.0) << "Core " << i;
    EXPECT_GE(DELTA.perCore[i].idle, 0.0) << "Core " << i;
    EXPECT_LE(DELTA.perCore[i].idle, 100.0) << "Core " << i;
  }
}

/* ----------------------------- CpuUtilizationPercent Tests ----------------------------- */

/** @test Default CpuUtilizationPercent is zeroed. */
TEST(CpuUtilizationPercentTest, DefaultZero) {
  const CpuUtilizationPercent DEFAULT{};

  EXPECT_EQ(DEFAULT.user, 0.0);
  EXPECT_EQ(DEFAULT.system, 0.0);
  EXPECT_EQ(DEFAULT.idle, 0.0);
  EXPECT_EQ(DEFAULT.active(), 0.0);
}

/** @test active() sums non-idle components. */
TEST(CpuUtilizationPercentTest, ActiveSumsCorrectly) {
  CpuUtilizationPercent pct{};
  pct.user = 10.0;
  pct.nice = 2.0;
  pct.system = 5.0;
  pct.idle = 80.0;
  pct.iowait = 3.0;
  pct.irq = 0.5;
  pct.softirq = 0.3;

  // active = user + nice + system + irq + softirq + steal + guest + guestNice
  const double EXPECTED = 10.0 + 2.0 + 5.0 + 0.5 + 0.3;
  EXPECT_NEAR(pct.active(), EXPECTED, 0.001);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Snapshot toString produces non-empty output. */
TEST_F(CpuUtilizationTest, SnapshotToStringNonEmpty) {
  const std::string OUTPUT = snap_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test Snapshot toString contains expected sections. */
TEST_F(CpuUtilizationTest, SnapshotToStringContainsSections) {
  const std::string OUTPUT = snap_.toString();

  EXPECT_NE(OUTPUT.find("Timestamp:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Aggregate:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Cores:"), std::string::npos);
}

/** @test Delta toString produces non-empty output. */
TEST(CpuUtilizationDeltaTest, DeltaToStringNonEmpty) {
  const CpuUtilizationSnapshot BEFORE = getCpuUtilizationSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const CpuUtilizationSnapshot AFTER = getCpuUtilizationSnapshot();
  const CpuUtilizationDelta DELTA = computeUtilizationDelta(BEFORE, AFTER);

  const std::string OUTPUT = DELTA.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Interval:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Aggregate:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default CpuUtilizationSnapshot is zeroed. */
TEST(CpuUtilizationDefaultTest, SnapshotDefaultZero) {
  const CpuUtilizationSnapshot DEFAULT{};

  EXPECT_EQ(DEFAULT.coreCount, 0U);
  EXPECT_EQ(DEFAULT.timestampNs, 0U);
  EXPECT_EQ(DEFAULT.aggregate.total(), 0U);
}

/** @test Default CpuUtilizationDelta is zeroed. */
TEST(CpuUtilizationDefaultTest, DeltaDefaultZero) {
  const CpuUtilizationDelta DEFAULT{};

  EXPECT_EQ(DEFAULT.coreCount, 0U);
  EXPECT_EQ(DEFAULT.intervalNs, 0U);
  EXPECT_EQ(DEFAULT.aggregate.active(), 0.0);
}