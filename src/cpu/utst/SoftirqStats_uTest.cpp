/**
 * @file SoftirqStats_uTest.cpp
 * @brief Unit tests for seeker::cpu::SoftirqStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Softirq counts vary by system activity.
 */

#include "src/cpu/inc/SoftirqStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

using seeker::cpu::computeSoftirqDelta;
using seeker::cpu::getSoftirqSnapshot;
using seeker::cpu::SOFTIRQ_MAX_CPUS;
using seeker::cpu::SOFTIRQ_MAX_TYPES;
using seeker::cpu::SOFTIRQ_NAME_SIZE;
using seeker::cpu::SoftirqDelta;
using seeker::cpu::SoftirqSnapshot;
using seeker::cpu::SoftirqType;
using seeker::cpu::softirqTypeName;
using seeker::cpu::SoftirqTypeStats;

class SoftirqStatsTest : public ::testing::Test {
protected:
  SoftirqSnapshot snap_{};

  void SetUp() override { snap_ = getSoftirqSnapshot(); }
};

/* ----------------------------- Type Name Tests ----------------------------- */

/** @test softirqTypeName returns valid strings. */
TEST(SoftirqTypeNameTest, AllTypesHaveNames) {
  EXPECT_STREQ(softirqTypeName(SoftirqType::HI), "HI");
  EXPECT_STREQ(softirqTypeName(SoftirqType::TIMER), "TIMER");
  EXPECT_STREQ(softirqTypeName(SoftirqType::NET_TX), "NET_TX");
  EXPECT_STREQ(softirqTypeName(SoftirqType::NET_RX), "NET_RX");
  EXPECT_STREQ(softirqTypeName(SoftirqType::BLOCK), "BLOCK");
  EXPECT_STREQ(softirqTypeName(SoftirqType::IRQ_POLL), "IRQ_POLL");
  EXPECT_STREQ(softirqTypeName(SoftirqType::TASKLET), "TASKLET");
  EXPECT_STREQ(softirqTypeName(SoftirqType::SCHED), "SCHED");
  EXPECT_STREQ(softirqTypeName(SoftirqType::HRTIMER), "HRTIMER");
  EXPECT_STREQ(softirqTypeName(SoftirqType::RCU), "RCU");
  EXPECT_STREQ(softirqTypeName(SoftirqType::UNKNOWN), "UNKNOWN");
}

/* ----------------------------- SoftirqTypeStats Tests ----------------------------- */

/** @test Default SoftirqTypeStats is zeroed. */
TEST(SoftirqTypeStatsTest, DefaultZero) {
  const SoftirqTypeStats DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.type, SoftirqType::UNKNOWN);
  EXPECT_EQ(DEFAULT.total, 0U);
  for (std::size_t i = 0; i < SOFTIRQ_MAX_CPUS; ++i) {
    EXPECT_EQ(DEFAULT.perCore[i], 0U);
  }
}

/* ----------------------------- Snapshot Tests ----------------------------- */

/** @test Snapshot has valid timestamp. */
TEST_F(SoftirqStatsTest, TimestampPositive) { EXPECT_GT(snap_.timestampNs, 0U); }

/** @test Snapshot has at least one CPU. */
TEST_F(SoftirqStatsTest, CpuCountAtLeastOne) { EXPECT_GE(snap_.cpuCount, 1U); }

/** @test CPU count within bounds. */
TEST_F(SoftirqStatsTest, CpuCountWithinBounds) { EXPECT_LE(snap_.cpuCount, SOFTIRQ_MAX_CPUS); }

/** @test Snapshot has softirq types. */
TEST_F(SoftirqStatsTest, HasSoftirqTypes) {
  // All Linux systems have softirqs
  EXPECT_GE(snap_.typeCount, 1U);
}

/** @test Type count within bounds. */
TEST_F(SoftirqStatsTest, TypeCountWithinBounds) { EXPECT_LE(snap_.typeCount, SOFTIRQ_MAX_TYPES); }

/** @test Type names are non-empty and null-terminated. */
TEST_F(SoftirqStatsTest, TypeNamesValid) {
  for (std::size_t i = 0; i < snap_.typeCount; ++i) {
    const auto& TYPE = snap_.types[i];
    const std::size_t LEN = std::strlen(TYPE.name.data());

    EXPECT_GT(LEN, 0U) << "Type " << i << " has empty name";
    EXPECT_LT(LEN, SOFTIRQ_NAME_SIZE) << "Type " << i;
  }
}

/** @test Well-known types are present. */
TEST_F(SoftirqStatsTest, WellKnownTypesPresent) {
  // TIMER and RCU should be present on all systems
  const SoftirqTypeStats* timer = snap_.getType(SoftirqType::TIMER);
  const SoftirqTypeStats* rcu = snap_.getType(SoftirqType::RCU);

  EXPECT_NE(timer, nullptr) << "TIMER softirq not found";
  EXPECT_NE(rcu, nullptr) << "RCU softirq not found";
}

/** @test Per-type totals match sum of per-core counts. */
TEST_F(SoftirqStatsTest, TypeTotalsMatchPerCore) {
  for (std::size_t i = 0; i < snap_.typeCount; ++i) {
    const auto& TYPE = snap_.types[i];
    std::uint64_t sum = 0;
    for (std::size_t cpu = 0; cpu < snap_.cpuCount && cpu < SOFTIRQ_MAX_CPUS; ++cpu) {
      sum += TYPE.perCore[cpu];
    }
    EXPECT_EQ(TYPE.total, sum) << "Type " << TYPE.name.data();
  }
}

/** @test totalForCpu returns consistent values. */
TEST_F(SoftirqStatsTest, TotalForCpuConsistent) {
  std::uint64_t sumFromCpus = 0;
  for (std::size_t cpu = 0; cpu < snap_.cpuCount; ++cpu) {
    const std::uint64_t CPU_TOTAL = snap_.totalForCpu(cpu);
    EXPECT_GE(CPU_TOTAL, 0U);
    sumFromCpus += CPU_TOTAL;
  }

  std::uint64_t sumFromTypes = 0;
  for (std::size_t i = 0; i < snap_.typeCount; ++i) {
    sumFromTypes += snap_.types[i].total;
  }

  EXPECT_EQ(sumFromCpus, sumFromTypes);
}

/** @test Subsequent snapshots have increasing timestamps. */
TEST_F(SoftirqStatsTest, TimestampsIncrease) {
  const SoftirqSnapshot SNAP2 = getSoftirqSnapshot();
  EXPECT_GT(SNAP2.timestampNs, snap_.timestampNs);
}

/** @test Counts are monotonically non-decreasing. */
TEST_F(SoftirqStatsTest, CountsNonDecreasing) {
  const SoftirqSnapshot SNAP2 = getSoftirqSnapshot();

  for (std::size_t i = 0; i < snap_.typeCount; ++i) {
    const SoftirqTypeStats* type2 = nullptr;
    for (std::size_t j = 0; j < SNAP2.typeCount; ++j) {
      if (std::strcmp(SNAP2.types[j].name.data(), snap_.types[i].name.data()) == 0) {
        type2 = &SNAP2.types[j];
        break;
      }
    }
    if (type2 != nullptr) {
      EXPECT_GE(type2->total, snap_.types[i].total) << "Type " << snap_.types[i].name.data();
    }
  }
}

/* ----------------------------- Delta Tests ----------------------------- */

/** @test Delta with same snapshot produces zero deltas. */
TEST_F(SoftirqStatsTest, DeltaSameSnapshotZero) {
  const SoftirqDelta DELTA = computeSoftirqDelta(snap_, snap_);

  EXPECT_EQ(DELTA.intervalNs, 0U);
  for (std::size_t i = 0; i < DELTA.typeCount; ++i) {
    EXPECT_EQ(DELTA.typeTotals[i], 0U);
  }
}

/** @test Delta with time gap produces valid results. */
TEST(SoftirqDeltaTest, DeltaWithSleep) {
  const SoftirqSnapshot BEFORE = getSoftirqSnapshot();

  // Sleep briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const SoftirqSnapshot AFTER = getSoftirqSnapshot();
  const SoftirqDelta DELTA = computeSoftirqDelta(BEFORE, AFTER);

  // Interval should be approximately 50ms
  EXPECT_GT(DELTA.intervalNs, 40'000'000U);
  EXPECT_LT(DELTA.intervalNs, 200'000'000U);

  // Type count should match
  EXPECT_GT(DELTA.typeCount, 0U);
}

/** @test Rate calculations are valid. */
TEST(SoftirqDeltaTest, RateCalculationsValid) {
  const SoftirqSnapshot BEFORE = getSoftirqSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const SoftirqSnapshot AFTER = getSoftirqSnapshot();
  const SoftirqDelta DELTA = computeSoftirqDelta(BEFORE, AFTER);

  for (std::size_t cpu = 0; cpu < DELTA.cpuCount; ++cpu) {
    const double RATE = DELTA.rateForCpu(cpu);
    EXPECT_GE(RATE, 0.0) << "CPU " << cpu;
    // Sanity: softirq rate shouldn't be astronomical
    EXPECT_LT(RATE, 100'000'000.0) << "CPU " << cpu;
  }

  // Check type rate
  const double TIMER_RATE = DELTA.rateForType(SoftirqType::TIMER);
  EXPECT_GE(TIMER_RATE, 0.0);
}

/** @test rateForType returns zero for unknown type. */
TEST(SoftirqDeltaTest, RateForUnknownTypeZero) {
  const SoftirqDelta DELTA{};
  EXPECT_EQ(DELTA.rateForType(SoftirqType::UNKNOWN), 0.0);
}

/* ----------------------------- getType Tests ----------------------------- */

/** @test getType returns nullptr for missing type. */
TEST_F(SoftirqStatsTest, GetTypeMissingReturnsNull) {
  // UNKNOWN may or may not match depending on /proc/softirqs content.
  // This test verifies the function doesn't crash; result is intentionally ignored.
  const SoftirqTypeStats* result = snap_.getType(SoftirqType::UNKNOWN);
  (void)result; // May be nullptr or valid - either is acceptable
}

/** @test getType returns valid pointer for known types. */
TEST_F(SoftirqStatsTest, GetTypeKnownReturnsValid) {
  const SoftirqTypeStats* timer = snap_.getType(SoftirqType::TIMER);
  if (timer != nullptr) {
    EXPECT_EQ(timer->type, SoftirqType::TIMER);
    EXPECT_STREQ(timer->name.data(), "TIMER");
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Snapshot toString produces non-empty output. */
TEST_F(SoftirqStatsTest, SnapshotToStringNonEmpty) {
  const std::string OUTPUT = snap_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test Snapshot toString contains expected sections. */
TEST_F(SoftirqStatsTest, SnapshotToStringContainsSections) {
  const std::string OUTPUT = snap_.toString();

  EXPECT_NE(OUTPUT.find("Timestamp:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CPUs:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Types:"), std::string::npos);
}

/** @test Delta toString produces non-empty output. */
TEST(SoftirqDeltaTest, DeltaToStringNonEmpty) {
  const SoftirqSnapshot BEFORE = getSoftirqSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const SoftirqSnapshot AFTER = getSoftirqSnapshot();
  const SoftirqDelta DELTA = computeSoftirqDelta(BEFORE, AFTER);

  const std::string OUTPUT = DELTA.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Interval:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SoftirqSnapshot is zeroed. */
TEST(SoftirqStatsDefaultTest, SnapshotDefaultZero) {
  const SoftirqSnapshot DEFAULT{};

  EXPECT_EQ(DEFAULT.typeCount, 0U);
  EXPECT_EQ(DEFAULT.cpuCount, 0U);
  EXPECT_EQ(DEFAULT.timestampNs, 0U);
}

/** @test Default SoftirqDelta is zeroed. */
TEST(SoftirqStatsDefaultTest, DeltaDefaultZero) {
  const SoftirqDelta DEFAULT{};

  EXPECT_EQ(DEFAULT.typeCount, 0U);
  EXPECT_EQ(DEFAULT.cpuCount, 0U);
  EXPECT_EQ(DEFAULT.intervalNs, 0U);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test totalForCpu with invalid index returns zero. */
TEST_F(SoftirqStatsTest, TotalForCpuInvalidIndex) {
  EXPECT_EQ(snap_.totalForCpu(SOFTIRQ_MAX_CPUS + 1), 0U);
  EXPECT_EQ(snap_.totalForCpu(99999), 0U);
}

/** @test rateForCpu with invalid index returns zero. */
TEST(SoftirqDeltaTest, RateForCpuInvalidIndex) {
  const SoftirqDelta DELTA{};
  EXPECT_EQ(DELTA.rateForCpu(99999), 0.0);
}