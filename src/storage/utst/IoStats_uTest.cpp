/**
 * @file IoStats_uTest.cpp
 * @brief Unit tests for seeker::storage::IoStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Delta tests use synthetic data to verify computation correctness.
 *  - Real device tests require at least one block device.
 */

#include "src/storage/inc/IoStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using seeker::storage::computeIoStatsDelta;
using seeker::storage::getIoStatsSnapshot;
using seeker::storage::IoCounters;
using seeker::storage::IOSTAT_DEVICE_NAME_SIZE;
using seeker::storage::IoStatsDelta;
using seeker::storage::IoStatsSnapshot;

/* ----------------------------- IoCounters Tests ----------------------------- */

/** @test readBytes converts sectors to bytes correctly. */
TEST(IoCountersTest, ReadBytesConversion) {
  IoCounters counters{};
  counters.readSectors = 100;

  EXPECT_EQ(counters.readBytes(), 100U * 512U);
}

/** @test writeBytes converts sectors to bytes correctly. */
TEST(IoCountersTest, WriteBytesConversion) {
  IoCounters counters{};
  counters.writeSectors = 200;

  EXPECT_EQ(counters.writeBytes(), 200U * 512U);
}

/** @test totalOps sums read and write ops. */
TEST(IoCountersTest, TotalOpsSum) {
  IoCounters counters{};
  counters.readOps = 100;
  counters.writeOps = 50;

  EXPECT_EQ(counters.totalOps(), 150U);
}

/** @test totalBytes sums read and write bytes. */
TEST(IoCountersTest, TotalBytesSum) {
  IoCounters counters{};
  counters.readSectors = 100;
  counters.writeSectors = 200;

  EXPECT_EQ(counters.totalBytes(), 300U * 512U);
}

/** @test Default IoCounters is zeroed. */
TEST(IoCountersTest, DefaultZeroed) {
  const IoCounters DEFAULT{};

  EXPECT_EQ(DEFAULT.readOps, 0U);
  EXPECT_EQ(DEFAULT.writeOps, 0U);
  EXPECT_EQ(DEFAULT.readSectors, 0U);
  EXPECT_EQ(DEFAULT.writeSectors, 0U);
  EXPECT_EQ(DEFAULT.ioTimeMs, 0U);
  EXPECT_EQ(DEFAULT.totalOps(), 0U);
  EXPECT_EQ(DEFAULT.totalBytes(), 0U);
}

/* ----------------------------- IoStatsDelta Method Tests ----------------------------- */

/** @test isIdle returns true when device is inactive. */
TEST(IoStatsDeltaTest, IsIdleDetection) {
  IoStatsDelta delta{};

  delta.totalIops = 0.0;
  delta.utilizationPct = 0.0;
  EXPECT_TRUE(delta.isIdle());

  delta.totalIops = 100.0;
  delta.utilizationPct = 50.0;
  EXPECT_FALSE(delta.isIdle());
}

/** @test isHighUtilization returns true above 80%. */
TEST(IoStatsDeltaTest, IsHighUtilizationDetection) {
  IoStatsDelta delta{};

  delta.utilizationPct = 50.0;
  EXPECT_FALSE(delta.isHighUtilization());

  delta.utilizationPct = 80.0;
  EXPECT_FALSE(delta.isHighUtilization());

  delta.utilizationPct = 81.0;
  EXPECT_TRUE(delta.isHighUtilization());

  delta.utilizationPct = 100.0;
  EXPECT_TRUE(delta.isHighUtilization());
}

/** @test Default IoStatsDelta is zeroed. */
TEST(IoStatsDeltaTest, DefaultZeroed) {
  const IoStatsDelta DEFAULT{};

  EXPECT_EQ(DEFAULT.intervalSec, 0.0);
  EXPECT_EQ(DEFAULT.readIops, 0.0);
  EXPECT_EQ(DEFAULT.writeIops, 0.0);
  EXPECT_EQ(DEFAULT.totalIops, 0.0);
  EXPECT_EQ(DEFAULT.utilizationPct, 0.0);
  EXPECT_TRUE(DEFAULT.isIdle());
}

/* ----------------------------- Synthetic Delta Computation Tests ----------------------------- */

/** @test IOPS calculation is correct. */
TEST(IoStatsDeltaComputationTest, IopsCalculation) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  // 1 second interval
  before.timestampNs = 1000000000ULL; // 1 second
  after.timestampNs = 2000000000ULL;  // 2 seconds

  before.counters.readOps = 100;
  after.counters.readOps = 200; // 100 ops in 1 second = 100 IOPS

  before.counters.writeOps = 50;
  after.counters.writeOps = 150; // 100 ops in 1 second = 100 IOPS

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_NEAR(DELTA.intervalSec, 1.0, 0.001);
  EXPECT_NEAR(DELTA.readIops, 100.0, 0.1);
  EXPECT_NEAR(DELTA.writeIops, 100.0, 0.1);
  EXPECT_NEAR(DELTA.totalIops, 200.0, 0.1);
}

/** @test Throughput calculation is correct. */
TEST(IoStatsDeltaComputationTest, ThroughputCalculation) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  before.timestampNs = 1000000000ULL;
  after.timestampNs = 2000000000ULL;

  // 1000 sectors = 512000 bytes in 1 second = 512000 B/s
  before.counters.readSectors = 0;
  after.counters.readSectors = 1000;

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_NEAR(DELTA.readBytesPerSec, 512000.0, 1.0);
}

/** @test Average latency calculation is correct. */
TEST(IoStatsDeltaComputationTest, LatencyCalculation) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  before.timestampNs = 1000000000ULL;
  after.timestampNs = 2000000000ULL;

  // 100 ops taking 500ms total = 5ms average latency
  before.counters.readOps = 0;
  after.counters.readOps = 100;
  before.counters.readTimeMs = 0;
  after.counters.readTimeMs = 500;

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_NEAR(DELTA.avgReadLatencyMs, 5.0, 0.01);
}

/** @test Utilization calculation is correct. */
TEST(IoStatsDeltaComputationTest, UtilizationCalculation) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  // 1 second = 1000ms wall time
  before.timestampNs = 1000000000ULL;
  after.timestampNs = 2000000000ULL;

  // Device busy 500ms out of 1000ms = 50% utilization
  before.counters.ioTimeMs = 0;
  after.counters.ioTimeMs = 500;

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_NEAR(DELTA.utilizationPct, 50.0, 0.1);
}

/** @test Utilization is capped at 100%. */
TEST(IoStatsDeltaComputationTest, UtilizationCapped) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  before.timestampNs = 1000000000ULL;
  after.timestampNs = 2000000000ULL;

  // ioTimeMs > wall time (can happen with parallel I/O)
  before.counters.ioTimeMs = 0;
  after.counters.ioTimeMs = 2000; // 2000ms in 1000ms wall time

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_LE(DELTA.utilizationPct, 100.0);
}

/** @test Different devices return empty delta. */
TEST(IoStatsDeltaComputationTest, DifferentDevicesReturnsEmpty) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "sda", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "sdb", IOSTAT_DEVICE_NAME_SIZE - 1);

  before.timestampNs = 1000000000ULL;
  after.timestampNs = 2000000000ULL;

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_EQ(DELTA.intervalSec, 0.0);
}

/** @test Invalid timestamp order returns empty delta. */
TEST(IoStatsDeltaComputationTest, InvalidTimestampOrderReturnsEmpty) {
  IoStatsSnapshot before{};
  IoStatsSnapshot after{};

  std::strncpy(before.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  std::strncpy(after.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);

  // Before is later than after
  before.timestampNs = 2000000000ULL;
  after.timestampNs = 1000000000ULL;

  const IoStatsDelta DELTA = computeIoStatsDelta(before, after);

  EXPECT_EQ(DELTA.intervalSec, 0.0);
}

/* ----------------------------- Real Device Tests ----------------------------- */

class IoStatsRealDeviceTest : public ::testing::Test {
protected:
  IoStatsSnapshot snap_{};
  bool hasDevice_{false};

  void SetUp() override {
    static const char* DEVICES[] = {"nvme0n1", "sda", "vda", "xvda"};

    for (const char* dev : DEVICES) {
      snap_ = getIoStatsSnapshot(dev);
      if (snap_.timestampNs > 0) {
        hasDevice_ = true;
        break;
      }
    }
  }
};

/** @test Real device has valid timestamp. */
TEST_F(IoStatsRealDeviceTest, HasValidTimestamp) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  EXPECT_GT(snap_.timestampNs, 0U);
}

/** @test Real device has device name populated. */
TEST_F(IoStatsRealDeviceTest, HasDeviceName) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  EXPECT_GT(std::strlen(snap_.device.data()), 0U);
}

/** @test Real device counters are non-negative. */
TEST_F(IoStatsRealDeviceTest, CountersNonNegative) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  // All counters should be non-negative (they're unsigned)
  EXPECT_GE(snap_.counters.readOps, 0U);
  EXPECT_GE(snap_.counters.writeOps, 0U);
  EXPECT_GE(snap_.counters.ioTimeMs, 0U);
}

/** @test Real device delta computation produces valid results. */
TEST_F(IoStatsRealDeviceTest, DeltaComputationWorks) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const IoStatsSnapshot BEFORE = snap_;

  // Brief delay
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const IoStatsSnapshot AFTER = getIoStatsSnapshot(snap_.device.data());
  const IoStatsDelta DELTA = computeIoStatsDelta(BEFORE, AFTER);

  // Interval should be approximately 100ms
  EXPECT_GE(DELTA.intervalSec, 0.09);
  EXPECT_LE(DELTA.intervalSec, 0.5);

  // Utilization should be in valid range
  EXPECT_GE(DELTA.utilizationPct, 0.0);
  EXPECT_LE(DELTA.utilizationPct, 100.0);

  // IOPS should be non-negative
  EXPECT_GE(DELTA.readIops, 0.0);
  EXPECT_GE(DELTA.writeIops, 0.0);
}

/* ----------------------------- Invalid Input Tests ----------------------------- */

/** @test getIoStatsSnapshot handles null device. */
TEST(IoStatsInvalidInputTest, NullDevice) {
  const IoStatsSnapshot SNAP = getIoStatsSnapshot(nullptr);
  EXPECT_EQ(SNAP.timestampNs, 0U);
}

/** @test getIoStatsSnapshot handles empty device. */
TEST(IoStatsInvalidInputTest, EmptyDevice) {
  const IoStatsSnapshot SNAP = getIoStatsSnapshot("");
  EXPECT_EQ(SNAP.timestampNs, 0U);
}

/** @test getIoStatsSnapshot handles non-existent device. */
TEST(IoStatsInvalidInputTest, NonExistentDevice) {
  const IoStatsSnapshot SNAP = getIoStatsSnapshot("nonexistent_device_xyz");
  EXPECT_EQ(SNAP.timestampNs, 0U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test IoStatsSnapshot toString produces non-empty output. */
TEST_F(IoStatsRealDeviceTest, SnapshotToStringNonEmpty) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const std::string OUTPUT = snap_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find(snap_.device.data()), std::string::npos);
}

/** @test IoStatsDelta toString produces non-empty output. */
TEST(IoStatsDeltaToStringTest, DeltaToStringNonEmpty) {
  IoStatsDelta delta{};
  std::strncpy(delta.device.data(), "test", IOSTAT_DEVICE_NAME_SIZE - 1);
  delta.intervalSec = 1.0;
  delta.readIops = 100.0;
  delta.writeIops = 50.0;
  delta.utilizationPct = 75.0;

  const std::string OUTPUT = delta.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("test"), std::string::npos);
}

/** @test formatThroughput produces readable output. */
TEST(IoStatsDeltaFormatTest, FormatThroughput) {
  IoStatsDelta delta{};
  delta.readBytesPerSec = 100000000.0; // 100 MB/s
  delta.writeBytesPerSec = 50000000.0; // 50 MB/s

  const std::string OUTPUT = delta.formatThroughput();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("MB/s"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Consecutive snapshots have increasing timestamps. */
TEST_F(IoStatsRealDeviceTest, IncreasingTimestamps) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const IoStatsSnapshot SNAP2 = getIoStatsSnapshot(snap_.device.data());

  // Second snapshot should have >= timestamp (may be equal if very fast)
  EXPECT_GE(SNAP2.timestampNs, snap_.timestampNs);
}