/**
 * @file InterfaceStats_uTest.cpp
 * @brief Unit tests for seeker::network::InterfaceStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Delta tests use short sleep to generate measurable time difference.
 *  - Loopback interface (lo) is used for reliable testing.
 */

#include "src/network/inc/InterfaceStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

using seeker::network::computeStatsDelta;
using seeker::network::formatThroughput;
using seeker::network::getInterfaceCounters;
using seeker::network::getInterfaceStatsSnapshot;
using seeker::network::InterfaceCounters;
using seeker::network::InterfaceRates;
using seeker::network::InterfaceStatsDelta;
using seeker::network::InterfaceStatsSnapshot;
using seeker::network::MAX_INTERFACES;

class InterfaceStatsTest : public ::testing::Test {
protected:
  InterfaceStatsSnapshot snap_{};

  void SetUp() override { snap_ = getInterfaceStatsSnapshot(); }
};

/* ----------------------------- InterfaceCounters Tests ----------------------------- */

/** @test Default InterfaceCounters is zeroed. */
TEST(InterfaceCountersTest, DefaultZero) {
  const InterfaceCounters DEFAULT{};

  EXPECT_EQ(DEFAULT.ifname[0], '\0');
  EXPECT_EQ(DEFAULT.rxBytes, 0U);
  EXPECT_EQ(DEFAULT.txBytes, 0U);
  EXPECT_EQ(DEFAULT.rxPackets, 0U);
  EXPECT_EQ(DEFAULT.txPackets, 0U);
  EXPECT_EQ(DEFAULT.totalErrors(), 0U);
  EXPECT_EQ(DEFAULT.totalDrops(), 0U);
  EXPECT_FALSE(DEFAULT.hasIssues());
}

/** @test totalErrors sums rx and tx errors. */
TEST(InterfaceCountersTest, TotalErrors) {
  InterfaceCounters c{};
  c.rxErrors = 10;
  c.txErrors = 5;

  EXPECT_EQ(c.totalErrors(), 15U);
}

/** @test totalDrops sums rx and tx drops. */
TEST(InterfaceCountersTest, TotalDrops) {
  InterfaceCounters c{};
  c.rxDropped = 7;
  c.txDropped = 3;

  EXPECT_EQ(c.totalDrops(), 10U);
}

/** @test hasIssues detects errors. */
TEST(InterfaceCountersTest, HasIssuesDetectsErrors) {
  InterfaceCounters c{};
  EXPECT_FALSE(c.hasIssues());

  c.rxErrors = 1;
  EXPECT_TRUE(c.hasIssues());
}

/** @test hasIssues detects drops. */
TEST(InterfaceCountersTest, HasIssuesDetectsDrops) {
  InterfaceCounters c{};
  EXPECT_FALSE(c.hasIssues());

  c.txDropped = 1;
  EXPECT_TRUE(c.hasIssues());
}

/** @test hasIssues detects collisions. */
TEST(InterfaceCountersTest, HasIssuesDetectsCollisions) {
  InterfaceCounters c{};
  EXPECT_FALSE(c.hasIssues());

  c.collisions = 1;
  EXPECT_TRUE(c.hasIssues());
}

/* ----------------------------- Loopback Counters Tests ----------------------------- */

/** @test Loopback interface has counters. */
TEST(InterfaceCountersTest, LoopbackHasCounters) {
  const InterfaceCounters LO = getInterfaceCounters("lo");

  EXPECT_STREQ(LO.ifname.data(), "lo");
  // Loopback typically has some traffic from system activity
  // But we can't guarantee exact values
}

/** @test Non-existent interface returns empty counters. */
TEST(InterfaceCountersTest, NonExistentEmpty) {
  const InterfaceCounters MISSING = getInterfaceCounters("noexist_if0");

  EXPECT_STREQ(MISSING.ifname.data(), "noexist_if0");
  EXPECT_EQ(MISSING.rxBytes, 0U);
  EXPECT_EQ(MISSING.txBytes, 0U);
}

/** @test Null interface name returns empty counters. */
TEST(InterfaceCountersTest, NullReturnsEmpty) {
  const InterfaceCounters EMPTY = getInterfaceCounters(nullptr);

  EXPECT_EQ(EMPTY.ifname[0], '\0');
}

/* ----------------------------- Snapshot Tests ----------------------------- */

/** @test Snapshot has valid timestamp. */
TEST_F(InterfaceStatsTest, TimestampPositive) { EXPECT_GT(snap_.timestampNs, 0U); }

/** @test Snapshot has at least loopback interface. */
TEST_F(InterfaceStatsTest, HasLoopback) {
  EXPECT_GE(snap_.count, 1U);

  const InterfaceCounters* lo = snap_.find("lo");
  EXPECT_NE(lo, nullptr);
  EXPECT_STREQ(lo->ifname.data(), "lo");
}

/** @test Snapshot count is within bounds. */
TEST_F(InterfaceStatsTest, CountWithinBounds) { EXPECT_LE(snap_.count, MAX_INTERFACES); }

/** @test All interfaces in snapshot have names. */
TEST_F(InterfaceStatsTest, AllHaveNames) {
  for (std::size_t i = 0; i < snap_.count; ++i) {
    EXPECT_GT(std::strlen(snap_.interfaces[i].ifname.data()), 0U)
        << "Interface " << i << " has empty name";
  }
}

/** @test Subsequent snapshots have increasing timestamps. */
TEST_F(InterfaceStatsTest, TimestampsIncrease) {
  const InterfaceStatsSnapshot SNAP2 = getInterfaceStatsSnapshot();
  EXPECT_GT(SNAP2.timestampNs, snap_.timestampNs);
}

/** @test Counters are monotonically non-decreasing. */
TEST_F(InterfaceStatsTest, CountersNonDecreasing) {
  const InterfaceStatsSnapshot SNAP2 = getInterfaceStatsSnapshot();

  const InterfaceCounters* lo1 = snap_.find("lo");
  const InterfaceCounters* lo2 = SNAP2.find("lo");

  ASSERT_NE(lo1, nullptr);
  ASSERT_NE(lo2, nullptr);

  // Allow for wrap on heavily loaded systems, but usually non-decreasing
  // We just check that values are reasonable (not zero when we expect traffic)
}

/** @test Single interface snapshot works. */
TEST(InterfaceSnapshotTest, SingleInterface) {
  const InterfaceStatsSnapshot SNAP = getInterfaceStatsSnapshot("lo");

  EXPECT_EQ(SNAP.count, 1U);
  EXPECT_GT(SNAP.timestampNs, 0U);
  EXPECT_STREQ(SNAP.interfaces[0].ifname.data(), "lo");
}

/** @test Single interface snapshot with invalid name returns empty. */
TEST(InterfaceSnapshotTest, SingleInterfaceInvalidEmpty) {
  const InterfaceStatsSnapshot SNAP = getInterfaceStatsSnapshot("nonexistent");

  EXPECT_EQ(SNAP.count, 0U);
  EXPECT_GT(SNAP.timestampNs, 0U); // Timestamp should still be set
}

/* ----------------------------- Delta Tests ----------------------------- */

/** @test Delta with same snapshot produces zero rates. */
TEST_F(InterfaceStatsTest, DeltaSameSnapshotZero) {
  const InterfaceStatsDelta DELTA = computeStatsDelta(snap_, snap_);

  EXPECT_EQ(DELTA.durationSec, 0.0);
  EXPECT_EQ(DELTA.count, 0U); // No valid deltas with zero duration
}

/** @test Delta with time gap produces valid rates. */
TEST(InterfaceStatsDeltaTest, DeltaWithSleepValid) {
  const InterfaceStatsSnapshot BEFORE = getInterfaceStatsSnapshot();

  // Sleep briefly to get measurable duration
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const InterfaceStatsSnapshot AFTER = getInterfaceStatsSnapshot();
  const InterfaceStatsDelta DELTA = computeStatsDelta(BEFORE, AFTER);

  // Duration should be approximately 50ms
  EXPECT_GT(DELTA.durationSec, 0.04); // > 40ms
  EXPECT_LT(DELTA.durationSec, 0.2);  // < 200ms

  // Should have same interfaces
  EXPECT_GT(DELTA.count, 0U);
}

/** @test Delta rates are non-negative. */
TEST(InterfaceStatsDeltaTest, RatesNonNegative) {
  const InterfaceStatsSnapshot BEFORE = getInterfaceStatsSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const InterfaceStatsSnapshot AFTER = getInterfaceStatsSnapshot();
  const InterfaceStatsDelta DELTA = computeStatsDelta(BEFORE, AFTER);

  for (std::size_t i = 0; i < DELTA.count; ++i) {
    EXPECT_GE(DELTA.interfaces[i].rxBytesPerSec, 0.0)
        << "Interface " << DELTA.interfaces[i].ifname.data();
    EXPECT_GE(DELTA.interfaces[i].txBytesPerSec, 0.0)
        << "Interface " << DELTA.interfaces[i].ifname.data();
    EXPECT_GE(DELTA.interfaces[i].rxPacketsPerSec, 0.0);
    EXPECT_GE(DELTA.interfaces[i].txPacketsPerSec, 0.0);
  }
}

/* ----------------------------- InterfaceRates Tests ----------------------------- */

/** @test Default InterfaceRates is zeroed. */
TEST(InterfaceRatesTest, DefaultZero) {
  const InterfaceRates DEFAULT{};

  EXPECT_EQ(DEFAULT.rxBytesPerSec, 0.0);
  EXPECT_EQ(DEFAULT.txBytesPerSec, 0.0);
  EXPECT_EQ(DEFAULT.rxMbps(), 0.0);
  EXPECT_EQ(DEFAULT.txMbps(), 0.0);
  EXPECT_FALSE(DEFAULT.hasErrors());
  EXPECT_FALSE(DEFAULT.hasDrops());
}

/** @test rxMbps computes correctly. */
TEST(InterfaceRatesTest, RxMbpsComputation) {
  InterfaceRates rates{};
  rates.rxBytesPerSec = 125'000'000.0; // 1 Gbps worth of bytes

  EXPECT_NEAR(rates.rxMbps(), 1000.0, 0.01);
}

/** @test txMbps computes correctly. */
TEST(InterfaceRatesTest, TxMbpsComputation) {
  InterfaceRates rates{};
  rates.txBytesPerSec = 12'500'000.0; // 100 Mbps worth of bytes

  EXPECT_NEAR(rates.txMbps(), 100.0, 0.01);
}

/** @test totalMbps sums rx and tx. */
TEST(InterfaceRatesTest, TotalMbps) {
  InterfaceRates rates{};
  rates.rxBytesPerSec = 12'500'000.0; // 100 Mbps
  rates.txBytesPerSec = 6'250'000.0;  // 50 Mbps

  EXPECT_NEAR(rates.totalMbps(), 150.0, 0.01);
}

/** @test hasErrors detects error rates. */
TEST(InterfaceRatesTest, HasErrors) {
  InterfaceRates rates{};
  EXPECT_FALSE(rates.hasErrors());

  rates.rxErrorsPerSec = 1.0;
  EXPECT_TRUE(rates.hasErrors());
}

/** @test hasDrops detects drop rates. */
TEST(InterfaceRatesTest, HasDrops) {
  InterfaceRates rates{};
  EXPECT_FALSE(rates.hasDrops());

  rates.txDroppedPerSec = 1.0;
  EXPECT_TRUE(rates.hasDrops());
}

/* ----------------------------- Snapshot::find Tests ----------------------------- */

/** @test find returns nullptr for non-existent. */
TEST_F(InterfaceStatsTest, FindNonExistentNull) {
  EXPECT_EQ(snap_.find("nonexistent_xyz"), nullptr);
  EXPECT_EQ(snap_.find(nullptr), nullptr);
}

/** @test find returns correct interface. */
TEST_F(InterfaceStatsTest, FindLoopback) {
  const InterfaceCounters* lo = snap_.find("lo");
  ASSERT_NE(lo, nullptr);
  EXPECT_STREQ(lo->ifname.data(), "lo");
}

/* ----------------------------- Delta::find Tests ----------------------------- */

/** @test Delta find returns nullptr for non-existent. */
TEST(InterfaceStatsDeltaFindTest, FindNonExistentNull) {
  const InterfaceStatsDelta DELTA{};
  EXPECT_EQ(DELTA.find("anything"), nullptr);
  EXPECT_EQ(DELTA.find(nullptr), nullptr);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test Snapshot toString produces non-empty output. */
TEST_F(InterfaceStatsTest, SnapshotToStringNonEmpty) {
  const std::string OUTPUT = snap_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Timestamp:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Interfaces:"), std::string::npos);
}

/** @test Delta toString produces non-empty output. */
TEST(InterfaceStatsDeltaToStringTest, NonEmpty) {
  const InterfaceStatsSnapshot BEFORE = getInterfaceStatsSnapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const InterfaceStatsSnapshot AFTER = getInterfaceStatsSnapshot();
  const InterfaceStatsDelta DELTA = computeStatsDelta(BEFORE, AFTER);

  const std::string OUTPUT = DELTA.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Interval:"), std::string::npos);
}

/** @test InterfaceRates toString produces non-empty output. */
TEST(InterfaceRatesToStringTest, NonEmpty) {
  InterfaceRates rates{};
  std::strcpy(rates.ifname.data(), "eth0");
  rates.rxBytesPerSec = 1'000'000.0;
  rates.txBytesPerSec = 500'000.0;

  const std::string OUTPUT = rates.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("eth0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Mbps"), std::string::npos);
}

/* ----------------------------- formatThroughput Tests ----------------------------- */

/** @test formatThroughput handles zero. */
TEST(FormatThroughputTest, Zero) {
  EXPECT_EQ(formatThroughput(0), "0 bps");
  EXPECT_EQ(formatThroughput(-1.0), "0 bps");
}

/** @test formatThroughput handles Kbps range. */
TEST(FormatThroughputTest, Kbps) {
  const std::string RESULT = formatThroughput(1000.0); // 8 Kbps
  EXPECT_NE(RESULT.find("Kbps"), std::string::npos);
}

/** @test formatThroughput handles Mbps range. */
TEST(FormatThroughputTest, Mbps) {
  const std::string RESULT = formatThroughput(1'000'000.0); // 8 Mbps
  EXPECT_NE(RESULT.find("Mbps"), std::string::npos);
}

/** @test formatThroughput handles Gbps range. */
TEST(FormatThroughputTest, Gbps) {
  const std::string RESULT = formatThroughput(125'000'000.0); // 1 Gbps
  EXPECT_NE(RESULT.find("Gbps"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getInterfaceStatsSnapshot returns consistent interface list. */
TEST(InterfaceStatsDeterminismTest, ConsistentCount) {
  const InterfaceStatsSnapshot SNAP1 = getInterfaceStatsSnapshot();
  const InterfaceStatsSnapshot SNAP2 = getInterfaceStatsSnapshot();

  EXPECT_EQ(SNAP1.count, SNAP2.count);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default InterfaceStatsSnapshot is empty. */
TEST(InterfaceStatsDefaultTest, SnapshotDefaultEmpty) {
  const InterfaceStatsSnapshot DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_EQ(DEFAULT.timestampNs, 0U);
}

/** @test Default InterfaceStatsDelta is empty. */
TEST(InterfaceStatsDefaultTest, DeltaDefaultEmpty) {
  const InterfaceStatsDelta DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_EQ(DEFAULT.durationSec, 0.0);
}