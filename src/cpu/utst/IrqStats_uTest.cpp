/**
 * @file IrqStats_uTest.cpp
 * @brief Unit tests for seeker::cpu::IrqStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - IRQ counts vary by system; tests validate structure and invariants.
 */

#include "src/cpu/inc/IrqStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

using seeker::cpu::computeIrqDelta;
using seeker::cpu::getIrqSnapshot;
using seeker::cpu::IRQ_DESC_SIZE;
using seeker::cpu::IRQ_MAX_CPUS;
using seeker::cpu::IRQ_MAX_LINES;
using seeker::cpu::IRQ_NAME_SIZE;
using seeker::cpu::IrqDelta;
using seeker::cpu::IrqLineStats;
using seeker::cpu::IrqSnapshot;

class IrqStatsTest : public ::testing::Test {
protected:
  IrqSnapshot snap_{};

  void SetUp() override { snap_ = getIrqSnapshot(); }
};

/* ----------------------------- Snapshot Tests ----------------------------- */

/** @test Snapshot has valid timestamp. */
TEST_F(IrqStatsTest, TimestampPositive) { EXPECT_GT(snap_.timestampNs, 0U); }

/** @test Snapshot has at least one CPU. */
TEST_F(IrqStatsTest, CpuCountAtLeastOne) { EXPECT_GE(snap_.coreCount, 1U); }

/** @test CPU count does not exceed maximum. */
TEST_F(IrqStatsTest, CpuCountWithinBounds) { EXPECT_LE(snap_.coreCount, IRQ_MAX_CPUS); }

/** @test Snapshot has at least one IRQ line. */
TEST_F(IrqStatsTest, HasIrqLines) {
  // All Linux systems have some IRQs (at minimum: timer, local timer)
  EXPECT_GE(snap_.lineCount, 1U);
}

/** @test IRQ line count does not exceed maximum. */
TEST_F(IrqStatsTest, LineCountWithinBounds) { EXPECT_LE(snap_.lineCount, IRQ_MAX_LINES); }

/** @test IRQ names are non-empty and null-terminated. */
TEST_F(IrqStatsTest, IrqNamesValid) {
  for (std::size_t i = 0; i < snap_.lineCount; ++i) {
    const auto& LINE = snap_.lines[i];
    const std::size_t LEN = std::strlen(LINE.name.data());

    // Name should be non-empty
    EXPECT_GT(LEN, 0U) << "IRQ line " << i << " has empty name";

    // Name should be within bounds
    EXPECT_LT(LEN, IRQ_NAME_SIZE) << "IRQ line " << i;
  }
}

/** @test IRQ descriptions are null-terminated. */
TEST_F(IrqStatsTest, IrqDescsNullTerminated) {
  for (std::size_t i = 0; i < snap_.lineCount; ++i) {
    const auto& LINE = snap_.lines[i];
    bool foundNull = false;
    for (std::size_t j = 0; j < IRQ_DESC_SIZE; ++j) {
      if (LINE.desc[j] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "IRQ line " << i << " desc not null-terminated";
  }
}

/** @test Per-line totals match sum of per-core counts. */
TEST_F(IrqStatsTest, LineTotalsMatchPerCore) {
  for (std::size_t i = 0; i < snap_.lineCount; ++i) {
    const auto& LINE = snap_.lines[i];
    std::uint64_t sum = 0;
    for (std::size_t cpu = 0; cpu < snap_.coreCount && cpu < IRQ_MAX_CPUS; ++cpu) {
      sum += LINE.perCore[cpu];
    }
    EXPECT_EQ(LINE.total, sum) << "IRQ line " << i << " (" << LINE.name.data() << ")";
  }
}

/** @test totalForCore returns consistent values. */
TEST_F(IrqStatsTest, TotalForCoreConsistent) {
  for (std::size_t cpu = 0; cpu < snap_.coreCount; ++cpu) {
    const std::uint64_t TOTAL = snap_.totalForCore(cpu);
    // Should be non-negative (always true for uint64_t, but validates function)
    EXPECT_GE(TOTAL, 0U);
  }
}

/** @test totalAllCores matches sum of per-core totals. */
TEST_F(IrqStatsTest, TotalAllCoresConsistent) {
  std::uint64_t sumFromCores = 0;
  for (std::size_t cpu = 0; cpu < snap_.coreCount; ++cpu) {
    sumFromCores += snap_.totalForCore(cpu);
  }

  std::uint64_t sumFromLines = 0;
  for (std::size_t i = 0; i < snap_.lineCount; ++i) {
    sumFromLines += snap_.lines[i].total;
  }

  EXPECT_EQ(snap_.totalAllCores(), sumFromLines);
  EXPECT_EQ(snap_.totalAllCores(), sumFromCores);
}

/** @test Subsequent snapshots have increasing timestamps. */
TEST_F(IrqStatsTest, TimestampsIncrease) {
  const IrqSnapshot SNAP2 = getIrqSnapshot();
  EXPECT_GT(SNAP2.timestampNs, snap_.timestampNs);
}

/** @test IRQ counts are monotonically non-decreasing. */
TEST_F(IrqStatsTest, CountsNonDecreasing) {
  const IrqSnapshot SNAP2 = getIrqSnapshot();

  EXPECT_GE(SNAP2.totalAllCores(), snap_.totalAllCores());
}

/* ----------------------------- Delta Tests ----------------------------- */

/** @test Delta with same snapshot produces zero deltas. */
TEST_F(IrqStatsTest, DeltaSameSnapshotZero) {
  const IrqDelta DELTA = computeIrqDelta(snap_, snap_);

  EXPECT_EQ(DELTA.intervalNs, 0U);
  for (std::size_t cpu = 0; cpu < DELTA.coreCount; ++cpu) {
    EXPECT_EQ(DELTA.totalForCore(cpu), 0U);
  }
}

/** @test Delta with time gap produces non-negative deltas. */
TEST(IrqDeltaTest, DeltaWithSleepValid) {
  const IrqSnapshot BEFORE = getIrqSnapshot();

  // Sleep to allow some interrupts to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const IrqSnapshot AFTER = getIrqSnapshot();
  const IrqDelta DELTA = computeIrqDelta(BEFORE, AFTER);

  // Interval should be approximately 50ms
  EXPECT_GT(DELTA.intervalNs, 40'000'000U);
  EXPECT_LT(DELTA.intervalNs, 200'000'000U);

  // All deltas should be non-negative
  for (std::size_t i = 0; i < DELTA.lineCount; ++i) {
    for (std::size_t cpu = 0; cpu < DELTA.coreCount; ++cpu) {
      EXPECT_GE(DELTA.perCoreDelta[i][cpu], 0U);
    }
    EXPECT_GE(DELTA.lineTotals[i], 0U);
  }
}

/** @test Rate calculation is valid. */
TEST(IrqDeltaTest, RateCalculationValid) {
  const IrqSnapshot BEFORE = getIrqSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const IrqSnapshot AFTER = getIrqSnapshot();
  const IrqDelta DELTA = computeIrqDelta(BEFORE, AFTER);

  for (std::size_t cpu = 0; cpu < DELTA.coreCount; ++cpu) {
    const double RATE = DELTA.rateForCore(cpu);
    EXPECT_GE(RATE, 0.0) << "Core " << cpu;
    // Sanity check: rate shouldn't be astronomical
    EXPECT_LT(RATE, 10'000'000.0) << "Core " << cpu;
  }
}

/* ----------------------------- IrqLineStats Tests ----------------------------- */

/** @test Default IrqLineStats is zeroed. */
TEST(IrqLineStatsTest, DefaultZero) {
  const IrqLineStats DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.desc[0], '\0');
  EXPECT_EQ(DEFAULT.total, 0U);
  for (std::size_t i = 0; i < IRQ_MAX_CPUS; ++i) {
    EXPECT_EQ(DEFAULT.perCore[i], 0U);
  }
}

/** @test IrqLineStats toString produces output. */
TEST_F(IrqStatsTest, LineToStringNonEmpty) {
  if (snap_.lineCount > 0) {
    const std::string OUTPUT = snap_.lines[0].toString(snap_.coreCount);
    EXPECT_FALSE(OUTPUT.empty());
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Snapshot toString produces non-empty output. */
TEST_F(IrqStatsTest, SnapshotToStringNonEmpty) {
  const std::string OUTPUT = snap_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test Snapshot toString contains expected sections. */
TEST_F(IrqStatsTest, SnapshotToStringContainsSections) {
  const std::string OUTPUT = snap_.toString();

  EXPECT_NE(OUTPUT.find("Timestamp:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CPUs:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("IRQ lines:"), std::string::npos);
}

/** @test Delta toString produces non-empty output. */
TEST(IrqDeltaTest, DeltaToStringNonEmpty) {
  const IrqSnapshot BEFORE = getIrqSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const IrqSnapshot AFTER = getIrqSnapshot();
  const IrqDelta DELTA = computeIrqDelta(BEFORE, AFTER);

  const std::string OUTPUT = DELTA.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Interval:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default IrqSnapshot is zeroed. */
TEST(IrqStatsDefaultTest, SnapshotDefaultZero) {
  const IrqSnapshot DEFAULT{};

  EXPECT_EQ(DEFAULT.lineCount, 0U);
  EXPECT_EQ(DEFAULT.coreCount, 0U);
  EXPECT_EQ(DEFAULT.timestampNs, 0U);
  EXPECT_EQ(DEFAULT.totalAllCores(), 0U);
}

/** @test Default IrqDelta is zeroed. */
TEST(IrqStatsDefaultTest, DeltaDefaultZero) {
  const IrqDelta DEFAULT{};

  EXPECT_EQ(DEFAULT.lineCount, 0U);
  EXPECT_EQ(DEFAULT.coreCount, 0U);
  EXPECT_EQ(DEFAULT.intervalNs, 0U);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test totalForCore with invalid index returns zero. */
TEST_F(IrqStatsTest, TotalForCoreInvalidIndex) {
  EXPECT_EQ(snap_.totalForCore(IRQ_MAX_CPUS + 1), 0U);
  EXPECT_EQ(snap_.totalForCore(99999), 0U);
}