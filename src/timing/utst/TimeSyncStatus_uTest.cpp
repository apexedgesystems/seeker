/**
 * @file TimeSyncStatus_uTest.cpp
 * @brief Unit tests for seeker::timing::TimeSyncStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Sync daemon detection depends on what's installed/running.
 *  - Kernel time status via adjtimex should work on all Linux systems.
 */

#include "src/timing/inc/TimeSyncStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::timing::getKernelTimeStatus;
using seeker::timing::getTimeSyncStatus;
using seeker::timing::isSyncDaemonRunning;
using seeker::timing::KernelTimeStatus;
using seeker::timing::MAX_PTP_DEVICES;
using seeker::timing::PtpDevice;
using seeker::timing::TimeSyncStatus;

class TimeSyncStatusTest : public ::testing::Test {
protected:
  TimeSyncStatus status_{};

  void SetUp() override { status_ = getTimeSyncStatus(); }
};

/* ----------------------------- Sync Daemon Tests ----------------------------- */

/** @test hasAnySyncDaemon returns true if any daemon detected. */
TEST_F(TimeSyncStatusTest, HasAnySyncDaemonConsistent) {
  const bool ANY = status_.chronyDetected || status_.ntpdDetected ||
                   status_.systemdTimesyncDetected || status_.ptpLinuxDetected;
  EXPECT_EQ(status_.hasAnySyncDaemon(), ANY);
}

/** @test primarySyncMethod returns valid string. */
TEST_F(TimeSyncStatusTest, PrimarySyncMethodValid) {
  const char* METHOD = status_.primarySyncMethod();
  EXPECT_NE(METHOD, nullptr);
  EXPECT_GT(std::strlen(METHOD), 0U);

  // Should be one of the known values
  const bool IS_KNOWN = std::strcmp(METHOD, "ptp") == 0 || std::strcmp(METHOD, "chrony") == 0 ||
                        std::strcmp(METHOD, "ntpd") == 0 ||
                        std::strcmp(METHOD, "systemd-timesyncd") == 0 ||
                        std::strcmp(METHOD, "none") == 0;
  EXPECT_TRUE(IS_KNOWN) << "Unknown sync method: " << METHOD;
}

/** @test primarySyncMethod returns "none" when no daemon detected. */
TEST(TimeSyncMethodTest, NoneWhenNoSync) {
  TimeSyncStatus status;
  // All detection flags false by default
  EXPECT_STREQ(status.primarySyncMethod(), "none");
}

/** @test primarySyncMethod priorities are correct. */
TEST(TimeSyncMethodTest, PriorityOrder) {
  TimeSyncStatus status;

  // PTP with hardware takes priority
  status.ptpLinuxDetected = true;
  status.ptpDeviceCount = 1;
  status.chronyDetected = true;
  EXPECT_STREQ(status.primarySyncMethod(), "ptp");

  // chrony over ntpd
  status.ptpLinuxDetected = false;
  status.ptpDeviceCount = 0;
  status.ntpdDetected = true;
  EXPECT_STREQ(status.primarySyncMethod(), "chrony");

  // ntpd over systemd-timesyncd
  status.chronyDetected = false;
  status.systemdTimesyncDetected = true;
  EXPECT_STREQ(status.primarySyncMethod(), "ntpd");

  // systemd-timesyncd last
  status.ntpdDetected = false;
  EXPECT_STREQ(status.primarySyncMethod(), "systemd-timesyncd");
}

/* ----------------------------- PTP Device Tests ----------------------------- */

/** @test PTP device count within bounds. */
TEST_F(TimeSyncStatusTest, PtpDeviceCountWithinBounds) {
  EXPECT_LE(status_.ptpDeviceCount, MAX_PTP_DEVICES);
}

/** @test hasPtpHardware consistent with count. */
TEST_F(TimeSyncStatusTest, HasPtpHardwareConsistent) {
  EXPECT_EQ(status_.hasPtpHardware(), status_.ptpDeviceCount > 0);
}

/** @test PTP devices are valid when count > 0. */
TEST_F(TimeSyncStatusTest, PtpDevicesValidWhenPresent) {
  for (std::size_t i = 0; i < status_.ptpDeviceCount; ++i) {
    EXPECT_TRUE(status_.ptpDevices[i].isValid()) << "PTP device " << i << " should be valid";
    EXPECT_GT(std::strlen(status_.ptpDevices[i].name.data()), 0U)
        << "PTP device " << i << " name should be non-empty";
  }
}

/** @test PTP device names start with "ptp". */
TEST_F(TimeSyncStatusTest, PtpDeviceNamesValid) {
  for (std::size_t i = 0; i < status_.ptpDeviceCount; ++i) {
    const char* NAME = status_.ptpDevices[i].name.data();
    EXPECT_EQ(std::strncmp(NAME, "ptp", 3), 0)
        << "PTP device name should start with 'ptp': " << NAME;
  }
}

/* ----------------------------- PtpDevice Tests ----------------------------- */

/** @test Default PtpDevice is not valid. */
TEST(PtpDeviceTest, DefaultNotValid) {
  const PtpDevice DEV{};
  EXPECT_FALSE(DEV.isValid());
}

/** @test PtpDevice with name is valid. */
TEST(PtpDeviceTest, WithNameIsValid) {
  PtpDevice dev;
  std::strcpy(dev.name.data(), "ptp0");
  EXPECT_TRUE(dev.isValid());
}

/* ----------------------------- KernelTimeStatus Tests ----------------------------- */

/** @test Kernel time query succeeds. */
TEST_F(TimeSyncStatusTest, KernelQuerySucceeds) {
  // adjtimex should work on all Linux systems
  EXPECT_TRUE(status_.kernel.querySucceeded) << "adjtimex query should succeed on Linux";
}

/** @test getKernelTimeStatus returns consistent results. */
TEST(KernelTimeStatusTest, QuerySucceeds) {
  const KernelTimeStatus STATUS = getKernelTimeStatus();
  EXPECT_TRUE(STATUS.querySucceeded);
}

/** @test Kernel time status values in reasonable range. */
TEST_F(TimeSyncStatusTest, KernelValuesReasonable) {
  if (!status_.kernel.querySucceeded) {
    GTEST_SKIP() << "Kernel time query failed";
  }

  // Offset should be within +/- 1 hour (3,600,000,000 us) even on unsync'd systems
  const std::int64_t MAX_OFFSET_US = 3'600'000'000LL;
  EXPECT_LE(status_.kernel.offsetUs, MAX_OFFSET_US);
  EXPECT_GE(status_.kernel.offsetUs, -MAX_OFFSET_US);

  // Error estimates should be non-negative
  EXPECT_GE(status_.kernel.maxErrorUs, 0);
  EXPECT_GE(status_.kernel.estErrorUs, 0);
}

/** @test isWellSynced requires querySucceeded. */
TEST(KernelTimeStatusWellSyncedTest, RequiresQuery) {
  KernelTimeStatus status;
  status.querySucceeded = false;
  status.synced = true;
  status.offsetUs = 0;
  EXPECT_FALSE(status.isWellSynced());
}

/** @test isWellSynced requires synced flag. */
TEST(KernelTimeStatusWellSyncedTest, RequiresSynced) {
  KernelTimeStatus status;
  status.querySucceeded = true;
  status.synced = false;
  status.offsetUs = 0;
  EXPECT_FALSE(status.isWellSynced());
}

/** @test isWellSynced with good values returns true. */
TEST(KernelTimeStatusWellSyncedTest, GoodValuesPass) {
  KernelTimeStatus status;
  status.querySucceeded = true;
  status.synced = true;
  status.offsetUs = 50;    // 50us offset
  status.estErrorUs = 500; // 500us error
  EXPECT_TRUE(status.isWellSynced());
}

/** @test isWellSynced with high offset returns false. */
TEST(KernelTimeStatusWellSyncedTest, HighOffsetFails) {
  KernelTimeStatus status;
  status.querySucceeded = true;
  status.synced = true;
  status.offsetUs = 5000; // 5ms offset (> 1ms threshold)
  status.estErrorUs = 500;
  EXPECT_FALSE(status.isWellSynced());
}

/** @test qualityString returns valid string. */
TEST_F(TimeSyncStatusTest, QualityStringValid) {
  const char* QUALITY = status_.kernel.qualityString();
  EXPECT_NE(QUALITY, nullptr);
  EXPECT_GT(std::strlen(QUALITY), 0U);

  // Should be one of the known values
  const bool IS_KNOWN =
      std::strcmp(QUALITY, "excellent") == 0 || std::strcmp(QUALITY, "good") == 0 ||
      std::strcmp(QUALITY, "fair") == 0 || std::strcmp(QUALITY, "poor") == 0 ||
      std::strcmp(QUALITY, "unsynchronized") == 0 || std::strcmp(QUALITY, "unknown") == 0;
  EXPECT_TRUE(IS_KNOWN) << "Unknown quality string: " << QUALITY;
}

/** @test qualityString classifications are correct. */
TEST(KernelTimeQualityTest, Classifications) {
  KernelTimeStatus status;

  // Unknown when query failed
  status.querySucceeded = false;
  EXPECT_STREQ(status.qualityString(), "unknown");

  // Unsynchronized
  status.querySucceeded = true;
  status.synced = false;
  EXPECT_STREQ(status.qualityString(), "unsynchronized");

  // Excellent
  status.synced = true;
  status.offsetUs = 50;
  status.estErrorUs = 500;
  EXPECT_STREQ(status.qualityString(), "excellent");

  // Good
  status.offsetUs = 500;
  status.estErrorUs = 5000;
  EXPECT_STREQ(status.qualityString(), "good");

  // Fair
  status.offsetUs = 5000;
  status.estErrorUs = 50000;
  EXPECT_STREQ(status.qualityString(), "fair");

  // Poor
  status.offsetUs = 50000;
  status.estErrorUs = 500000;
  EXPECT_STREQ(status.qualityString(), "poor");
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test RT score is in valid range. */
TEST_F(TimeSyncStatusTest, RtScoreInRange) {
  const int SCORE = status_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test Optimal config gets high score. */
TEST(TimeSyncScoreTest, OptimalConfigHighScore) {
  TimeSyncStatus status;
  status.ptpLinuxDetected = true;
  status.ptpDeviceCount = 1;
  status.kernel.querySucceeded = true;
  status.kernel.synced = true;
  status.kernel.offsetUs = 50;

  EXPECT_GE(status.rtScore(), 90);
}

/** @test No sync gets low score. */
TEST(TimeSyncScoreTest, NoSyncLowScore) {
  TimeSyncStatus status;
  // All defaults (no sync)
  status.kernel.querySucceeded = true;
  status.kernel.synced = false;

  EXPECT_LE(status.rtScore(), 20);
}

/* ----------------------------- isSyncDaemonRunning Tests ----------------------------- */

/** @test isSyncDaemonRunning returns false for null. */
TEST(SyncDaemonRunningTest, RejectsNull) { EXPECT_FALSE(isSyncDaemonRunning(nullptr)); }

/** @test isSyncDaemonRunning returns false for unknown daemon. */
TEST(SyncDaemonRunningTest, RejectsUnknown) {
  EXPECT_FALSE(isSyncDaemonRunning("definitely_not_a_daemon_xyz"));
}

/** @test isSyncDaemonRunning consistent with status. */
TEST_F(TimeSyncStatusTest, SyncDaemonRunningConsistent) {
  EXPECT_EQ(isSyncDaemonRunning("chrony"), status_.chronyDetected);
  EXPECT_EQ(isSyncDaemonRunning("ntpd"), status_.ntpdDetected);
  EXPECT_EQ(isSyncDaemonRunning("systemd-timesyncd"), status_.systemdTimesyncDetected);

  // ptp4l and linuxptp should both work
  if (status_.ptpLinuxDetected) {
    EXPECT_TRUE(isSyncDaemonRunning("ptp4l") || isSyncDaemonRunning("linuxptp"));
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(TimeSyncStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains daemon info. */
TEST_F(TimeSyncStatusTest, ToStringContainsDaemons) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("Sync Daemons"), std::string::npos);
}

/** @test toString contains PTP info. */
TEST_F(TimeSyncStatusTest, ToStringContainsPtp) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("PTP Hardware"), std::string::npos);
}

/** @test toString contains kernel status. */
TEST_F(TimeSyncStatusTest, ToStringContainsKernel) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("Kernel Time Status"), std::string::npos);
}

/** @test toString contains RT score. */
TEST_F(TimeSyncStatusTest, ToStringContainsRtScore) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("RT Score:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default TimeSyncStatus is zeroed. */
TEST(TimeSyncStatusDefaultTest, DefaultZeroed) {
  const TimeSyncStatus DEFAULT{};

  EXPECT_FALSE(DEFAULT.chronyDetected);
  EXPECT_FALSE(DEFAULT.ntpdDetected);
  EXPECT_FALSE(DEFAULT.systemdTimesyncDetected);
  EXPECT_FALSE(DEFAULT.ptpLinuxDetected);
  EXPECT_EQ(DEFAULT.ptpDeviceCount, 0U);
  EXPECT_FALSE(DEFAULT.kernel.querySucceeded);
}

/** @test Default KernelTimeStatus is zeroed. */
TEST(KernelTimeStatusDefaultTest, DefaultZeroed) {
  const KernelTimeStatus DEFAULT{};

  EXPECT_FALSE(DEFAULT.synced);
  EXPECT_FALSE(DEFAULT.querySucceeded);
  EXPECT_EQ(DEFAULT.offsetUs, 0);
  EXPECT_EQ(DEFAULT.freqPpb, 0);
}

/** @test Default PtpDevice is zeroed. */
TEST(PtpDeviceDefaultTest, DefaultZeroed) {
  const PtpDevice DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.clock[0], '\0');
  EXPECT_EQ(DEFAULT.maxAdjPpb, 0);
  EXPECT_EQ(DEFAULT.ppsAvailable, -1);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getTimeSyncStatus returns consistent results. */
TEST(TimeSyncDeterminismTest, ConsistentResults) {
  const TimeSyncStatus S1 = getTimeSyncStatus();
  const TimeSyncStatus S2 = getTimeSyncStatus();

  EXPECT_EQ(S1.chronyDetected, S2.chronyDetected);
  EXPECT_EQ(S1.ntpdDetected, S2.ntpdDetected);
  EXPECT_EQ(S1.ptpDeviceCount, S2.ptpDeviceCount);
  EXPECT_EQ(S1.kernel.synced, S2.kernel.synced);
}

/** @test getKernelTimeStatus returns consistent results. */
TEST(KernelTimeDeterminismTest, ConsistentResults) {
  const KernelTimeStatus S1 = getKernelTimeStatus();
  const KernelTimeStatus S2 = getKernelTimeStatus();

  EXPECT_EQ(S1.querySucceeded, S2.querySucceeded);
  EXPECT_EQ(S1.synced, S2.synced);
  // Offset may drift slightly between calls, so don't compare exactly
}
