/**
 * @file RtcStatus_uTest.cpp
 * @brief Unit tests for seeker::timing::RtcStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - RTC availability varies by system (embedded vs containers).
 *  - Time values depend on system clock; test relationships, not absolutes.
 */

#include "src/timing/inc/RtcStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <ctime>
#include <string>

using seeker::timing::getRtcAlarm;
using seeker::timing::getRtcStatus;
using seeker::timing::getRtcTime;
using seeker::timing::isRtcSupported;
using seeker::timing::RTC_MAX_DEVICES;
using seeker::timing::RtcAlarm;
using seeker::timing::RtcCapabilities;
using seeker::timing::RtcDevice;
using seeker::timing::RtcStatus;
using seeker::timing::RtcTime;

class RtcStatusTest : public ::testing::Test {
protected:
  RtcStatus status_{};

  void SetUp() override { status_ = getRtcStatus(); }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default RtcCapabilities is zeroed. */
TEST(RtcCapabilitiesDefaultTest, DefaultZeroed) {
  const RtcCapabilities DEFAULT{};

  EXPECT_FALSE(DEFAULT.hasAlarm);
  EXPECT_FALSE(DEFAULT.hasPeriodicIrq);
  EXPECT_FALSE(DEFAULT.hasUpdateIrq);
  EXPECT_FALSE(DEFAULT.hasWakeAlarm);
  EXPECT_FALSE(DEFAULT.hasBattery);
  EXPECT_EQ(DEFAULT.irqFreqMin, 0);
  EXPECT_EQ(DEFAULT.irqFreqMax, 0);
}

/** @test Default RtcTime is zeroed. */
TEST(RtcTimeDefaultTest, DefaultZeroed) {
  const RtcTime DEFAULT{};

  EXPECT_EQ(DEFAULT.year, 0);
  EXPECT_EQ(DEFAULT.month, 0);
  EXPECT_EQ(DEFAULT.day, 0);
  EXPECT_EQ(DEFAULT.hour, 0);
  EXPECT_EQ(DEFAULT.minute, 0);
  EXPECT_EQ(DEFAULT.second, 0);
  EXPECT_EQ(DEFAULT.epochSeconds, 0);
  EXPECT_EQ(DEFAULT.driftSeconds, 0);
  EXPECT_FALSE(DEFAULT.querySucceeded);
}

/** @test Default RtcAlarm is zeroed. */
TEST(RtcAlarmDefaultTest, DefaultZeroed) {
  const RtcAlarm DEFAULT{};

  EXPECT_FALSE(DEFAULT.enabled);
  EXPECT_FALSE(DEFAULT.pending);
  EXPECT_EQ(DEFAULT.alarmEpoch, 0);
  EXPECT_EQ(DEFAULT.secondsUntil, 0);
  EXPECT_FALSE(DEFAULT.querySucceeded);
}

/** @test Default RtcDevice is zeroed. */
TEST(RtcDeviceDefaultTest, DefaultZeroed) {
  const RtcDevice DEFAULT{};

  EXPECT_EQ(DEFAULT.device[0], '\0');
  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.index, -1);
  EXPECT_FALSE(DEFAULT.isSystemRtc);
}

/** @test Default RtcStatus is zeroed. */
TEST(RtcStatusDefaultTest, DefaultZeroed) {
  const RtcStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.deviceCount, 0U);
  EXPECT_FALSE(DEFAULT.rtcSupported);
  EXPECT_FALSE(DEFAULT.hasHardwareRtc);
  EXPECT_FALSE(DEFAULT.hasWakeCapable);
  EXPECT_EQ(DEFAULT.systemRtcIndex, -1);
}

/* ----------------------------- RtcCapabilities Method Tests ----------------------------- */

/** @test canWakeFromSuspend requires hasWakeAlarm. */
TEST(RtcCapabilitiesTest, CanWakeFromSuspend) {
  RtcCapabilities caps{};
  EXPECT_FALSE(caps.canWakeFromSuspend());

  caps.hasWakeAlarm = true;
  EXPECT_TRUE(caps.canWakeFromSuspend());
}

/* ----------------------------- RtcTime Method Tests ----------------------------- */

/** @test Default RtcTime is not valid. */
TEST(RtcTimeTest, DefaultNotValid) {
  const RtcTime DEFAULT{};
  EXPECT_FALSE(DEFAULT.isValid());
}

/** @test RtcTime with querySucceeded=false is not valid. */
TEST(RtcTimeTest, NotValidIfQueryFailed) {
  RtcTime rtcTime{};
  rtcTime.year = 2024;
  rtcTime.month = 6;
  rtcTime.day = 15;
  rtcTime.hour = 12;
  rtcTime.minute = 0;
  rtcTime.second = 0;
  rtcTime.querySucceeded = false;

  EXPECT_FALSE(rtcTime.isValid());
}

/** @test RtcTime with valid values is valid. */
TEST(RtcTimeTest, ValidWithGoodValues) {
  RtcTime rtcTime{};
  rtcTime.year = 2024;
  rtcTime.month = 6;
  rtcTime.day = 15;
  rtcTime.hour = 12;
  rtcTime.minute = 30;
  rtcTime.second = 45;
  rtcTime.querySucceeded = true;

  EXPECT_TRUE(rtcTime.isValid());
}

/** @test RtcTime with out-of-range month is not valid. */
TEST(RtcTimeTest, InvalidMonth) {
  RtcTime rtcTime{};
  rtcTime.year = 2024;
  rtcTime.month = 13; // Invalid
  rtcTime.day = 15;
  rtcTime.hour = 12;
  rtcTime.querySucceeded = true;

  EXPECT_FALSE(rtcTime.isValid());
}

/** @test RtcTime with out-of-range year is not valid. */
TEST(RtcTimeTest, InvalidYear) {
  RtcTime rtcTime{};
  rtcTime.year = 1960; // Before Unix epoch, invalid for RTC
  rtcTime.month = 6;
  rtcTime.day = 15;
  rtcTime.querySucceeded = true;

  EXPECT_FALSE(rtcTime.isValid());
}

/** @test isDriftAcceptable with zero drift returns true. */
TEST(RtcTimeTest, ZeroDriftAcceptable) {
  RtcTime rtcTime{};
  rtcTime.driftSeconds = 0;
  rtcTime.querySucceeded = true;

  EXPECT_TRUE(rtcTime.isDriftAcceptable());
}

/** @test isDriftAcceptable with small drift returns true. */
TEST(RtcTimeTest, SmallDriftAcceptable) {
  RtcTime rtcTime{};
  rtcTime.driftSeconds = 3;
  rtcTime.querySucceeded = true;

  EXPECT_TRUE(rtcTime.isDriftAcceptable());
}

/** @test isDriftAcceptable with large drift returns false. */
TEST(RtcTimeTest, LargeDriftNotAcceptable) {
  RtcTime rtcTime{};
  rtcTime.driftSeconds = 60; // 1 minute
  rtcTime.querySucceeded = true;

  EXPECT_FALSE(rtcTime.isDriftAcceptable());
}

/** @test isDriftAcceptable with negative drift uses absolute value. */
TEST(RtcTimeTest, NegativeDriftAbsolute) {
  RtcTime rtcTime{};
  rtcTime.driftSeconds = -3;
  rtcTime.querySucceeded = true;

  EXPECT_TRUE(rtcTime.isDriftAcceptable());
  EXPECT_EQ(rtcTime.absDrift(), 3);
}

/** @test absDrift returns absolute value. */
TEST(RtcTimeTest, AbsDrift) {
  RtcTime rtcTime{};

  rtcTime.driftSeconds = 10;
  EXPECT_EQ(rtcTime.absDrift(), 10);

  rtcTime.driftSeconds = -10;
  EXPECT_EQ(rtcTime.absDrift(), 10);

  rtcTime.driftSeconds = 0;
  EXPECT_EQ(rtcTime.absDrift(), 0);
}

/* ----------------------------- RtcAlarm Method Tests ----------------------------- */

/** @test isFutureAlarm requires enabled and positive secondsUntil. */
TEST(RtcAlarmTest, IsFutureAlarm) {
  RtcAlarm alarm{};
  EXPECT_FALSE(alarm.isFutureAlarm());

  alarm.enabled = true;
  alarm.secondsUntil = -100; // Past
  EXPECT_FALSE(alarm.isFutureAlarm());

  alarm.secondsUntil = 100; // Future
  EXPECT_TRUE(alarm.isFutureAlarm());

  alarm.enabled = false;
  EXPECT_FALSE(alarm.isFutureAlarm());
}

/* ----------------------------- RtcDevice Method Tests ----------------------------- */

/** @test Default RtcDevice is not valid. */
TEST(RtcDeviceTest, DefaultNotValid) {
  const RtcDevice DEFAULT{};
  EXPECT_FALSE(DEFAULT.isValid());
}

/** @test RtcDevice with device and index is valid. */
TEST(RtcDeviceTest, WithDeviceAndIndexIsValid) {
  RtcDevice device{};
  std::strcpy(device.device.data(), "rtc0");
  device.index = 0;
  EXPECT_TRUE(device.isValid());
}

/** @test healthString returns "invalid" for invalid device. */
TEST(RtcDeviceTest, HealthStringInvalid) {
  const RtcDevice DEFAULT{};
  EXPECT_STREQ(DEFAULT.healthString(), "invalid");
}

/** @test healthString returns "healthy" for good device. */
TEST(RtcDeviceTest, HealthStringHealthy) {
  RtcDevice device{};
  std::strcpy(device.device.data(), "rtc0");
  device.index = 0;
  device.time.querySucceeded = true;
  device.time.year = 2024;
  device.time.month = 6;
  device.time.day = 15;
  device.time.hour = 12;
  device.time.minute = 0;
  device.time.second = 0;
  device.time.driftSeconds = 0;

  EXPECT_STREQ(device.healthString(), "healthy");
}

/** @test healthString returns "drifted" for high drift. */
TEST(RtcDeviceTest, HealthStringDrifted) {
  RtcDevice device{};
  std::strcpy(device.device.data(), "rtc0");
  device.index = 0;
  device.time.querySucceeded = true;
  device.time.year = 2024;
  device.time.month = 6;
  device.time.day = 15;
  device.time.driftSeconds = 3600; // 1 hour drift

  EXPECT_STREQ(device.healthString(), "drifted");
}

/* ----------------------------- RtcStatus Method Tests ----------------------------- */

/** @test Device count within bounds. */
TEST_F(RtcStatusTest, DeviceCountWithinBounds) { EXPECT_LE(status_.deviceCount, RTC_MAX_DEVICES); }

/** @test hasHardwareRtc consistent with deviceCount. */
TEST_F(RtcStatusTest, HasHardwareRtcConsistent) {
  EXPECT_EQ(status_.hasHardwareRtc, status_.deviceCount > 0);
}

/** @test findByName returns nullptr for unknown device. */
TEST_F(RtcStatusTest, FindByNameUnknown) {
  EXPECT_EQ(status_.findByName("definitely_not_an_rtc"), nullptr);
}

/** @test findByName returns nullptr for null. */
TEST_F(RtcStatusTest, FindByNameNull) { EXPECT_EQ(status_.findByName(nullptr), nullptr); }

/** @test findByIndex returns nullptr for invalid index. */
TEST_F(RtcStatusTest, FindByIndexInvalid) {
  EXPECT_EQ(status_.findByIndex(-1), nullptr);
  EXPECT_EQ(status_.findByIndex(9999), nullptr);
}

/** @test All enumerated devices are valid. */
TEST_F(RtcStatusTest, AllDevicesValid) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    EXPECT_TRUE(status_.devices[i].isValid()) << "Device " << i << " should be valid";
  }
}

/** @test All enumerated devices have names starting with "rtc". */
TEST_F(RtcStatusTest, AllDevicesHaveRtcPrefix) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    EXPECT_EQ(std::strncmp(status_.devices[i].device.data(), "rtc", 3), 0)
        << "Device " << i << " name should start with 'rtc'";
  }
}

/** @test findByName finds enumerated devices. */
TEST_F(RtcStatusTest, FindByNameFindsDevices) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const RtcDevice* FOUND = status_.findByName(status_.devices[i].device.data());
    EXPECT_NE(FOUND, nullptr) << "Should find device " << status_.devices[i].device.data();
    if (FOUND != nullptr) {
      EXPECT_EQ(FOUND->index, status_.devices[i].index);
    }
  }
}

/** @test findByIndex finds enumerated devices. */
TEST_F(RtcStatusTest, FindByIndexFindsDevices) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const RtcDevice* FOUND = status_.findByIndex(status_.devices[i].index);
    EXPECT_NE(FOUND, nullptr) << "Should find device with index " << status_.devices[i].index;
    if (FOUND != nullptr) {
      EXPECT_EQ(std::strcmp(FOUND->device.data(), status_.devices[i].device.data()), 0);
    }
  }
}

/** @test getSystemRtc returns nullptr when no devices. */
TEST(RtcStatusNullTest, GetSystemRtcReturnsNull) {
  const RtcStatus EMPTY{};
  EXPECT_EQ(EMPTY.getSystemRtc(), nullptr);
}

/** @test maxDriftSeconds returns 0 when no devices. */
TEST(RtcStatusNullTest, MaxDriftZeroWhenEmpty) {
  const RtcStatus EMPTY{};
  EXPECT_EQ(EMPTY.maxDriftSeconds(), 0);
}

/** @test allDriftAcceptable returns true when no devices. */
TEST(RtcStatusNullTest, AllDriftAcceptableWhenEmpty) {
  const RtcStatus EMPTY{};
  EXPECT_TRUE(EMPTY.allDriftAcceptable());
}

/** @test If system RTC exists, it should be rtc0 or have hctosys. */
TEST_F(RtcStatusTest, SystemRtcIsRtc0OrHctosys) {
  const RtcDevice* SYS = status_.getSystemRtc();
  if (SYS != nullptr) {
    const bool IS_RTC0 = (SYS->index == 0);
    const bool IS_HCTOSYS = (SYS->hctosys[0] == '1');
    EXPECT_TRUE(IS_RTC0 || IS_HCTOSYS);
  }
}

/* ----------------------------- Drift Tests ----------------------------- */

/** @test maxDriftSeconds returns maximum drift. */
TEST(RtcStatusDriftTest, MaxDriftReturnsMax) {
  RtcStatus status;
  status.deviceCount = 2;

  std::strcpy(status.devices[0].device.data(), "rtc0");
  status.devices[0].index = 0;
  status.devices[0].time.querySucceeded = true;
  status.devices[0].time.driftSeconds = 3;

  std::strcpy(status.devices[1].device.data(), "rtc1");
  status.devices[1].index = 1;
  status.devices[1].time.querySucceeded = true;
  status.devices[1].time.driftSeconds = -10;

  EXPECT_EQ(status.maxDriftSeconds(), 10); // abs(-10)
}

/** @test allDriftAcceptable returns false if any drift too high. */
TEST(RtcStatusDriftTest, AllDriftAcceptableFalseIfHigh) {
  RtcStatus status;
  status.deviceCount = 2;

  std::strcpy(status.devices[0].device.data(), "rtc0");
  status.devices[0].index = 0;
  status.devices[0].time.querySucceeded = true;
  status.devices[0].time.driftSeconds = 2; // OK

  std::strcpy(status.devices[1].device.data(), "rtc1");
  status.devices[1].index = 1;
  status.devices[1].time.querySucceeded = true;
  status.devices[1].time.driftSeconds = 100; // Too high

  EXPECT_FALSE(status.allDriftAcceptable());
}

/* ----------------------------- API Function Tests ----------------------------- */

/** @test isRtcSupported returns consistent result. */
TEST(RtcSupportedTest, ConsistentResult) {
  const bool S1 = isRtcSupported();
  const bool S2 = isRtcSupported();
  EXPECT_EQ(S1, S2);
}

/** @test isRtcSupported consistent with getRtcStatus. */
TEST_F(RtcStatusTest, SupportedConsistent) { EXPECT_EQ(isRtcSupported(), status_.rtcSupported); }

/** @test getRtcTime returns empty for null. */
TEST(RtcTimeQueryTest, ReturnsEmptyForNull) {
  const RtcTime TIME = getRtcTime(nullptr);
  EXPECT_FALSE(TIME.querySucceeded);
}

/** @test getRtcTime returns empty for invalid device. */
TEST(RtcTimeQueryTest, ReturnsEmptyForInvalid) {
  const RtcTime TIME = getRtcTime("definitely_not_an_rtc");
  EXPECT_FALSE(TIME.querySucceeded);
}

/** @test getRtcAlarm returns empty for null. */
TEST(RtcAlarmQueryTest, ReturnsEmptyForNull) {
  const RtcAlarm ALARM = getRtcAlarm(nullptr);
  EXPECT_FALSE(ALARM.querySucceeded);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(RtcStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains "RTC". */
TEST_F(RtcStatusTest, ToStringContainsRtc) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("RTC"), std::string::npos);
}

/** @test toJson produces valid JSON structure. */
TEST_F(RtcStatusTest, ToJsonValidStructure) {
  const std::string JSON = status_.toJson();
  EXPECT_FALSE(JSON.empty());
  EXPECT_EQ(JSON.front(), '{');
  EXPECT_EQ(JSON.back(), '}');
}

/** @test toJson contains expected fields. */
TEST_F(RtcStatusTest, ToJsonContainsFields) {
  const std::string JSON = status_.toJson();
  EXPECT_NE(JSON.find("\"rtcSupported\""), std::string::npos);
  EXPECT_NE(JSON.find("\"deviceCount\""), std::string::npos);
  EXPECT_NE(JSON.find("\"devices\""), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getRtcStatus returns consistent results. */
TEST(RtcStatusDeterminismTest, ConsistentResults) {
  const RtcStatus S1 = getRtcStatus();
  const RtcStatus S2 = getRtcStatus();

  EXPECT_EQ(S1.rtcSupported, S2.rtcSupported);
  EXPECT_EQ(S1.deviceCount, S2.deviceCount);
  EXPECT_EQ(S1.hasHardwareRtc, S2.hasHardwareRtc);
  EXPECT_EQ(S1.hasWakeCapable, S2.hasWakeCapable);

  for (std::size_t i = 0; i < S1.deviceCount; ++i) {
    EXPECT_EQ(S1.devices[i].index, S2.devices[i].index);
    EXPECT_EQ(std::strcmp(S1.devices[i].device.data(), S2.devices[i].device.data()), 0);
    EXPECT_EQ(S1.devices[i].caps.hasWakeAlarm, S2.devices[i].caps.hasWakeAlarm);
  }
}

/** @test RTC time epoch values are reasonable. */
TEST_F(RtcStatusTest, TimeValuesReasonable) {
  // Get current system time
  const std::int64_t NOW = static_cast<std::int64_t>(std::time(nullptr));

  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    if (status_.devices[i].time.querySucceeded && status_.devices[i].time.isValid()) {
      // RTC time should be within 24 hours of system time (very generous)
      const std::int64_t DIFF = status_.devices[i].time.epochSeconds - NOW;
      const std::int64_t ABS_DIFF = (DIFF < 0) ? -DIFF : DIFF;
      EXPECT_LT(ABS_DIFF, 86400) << "Device " << i << " time should be within 24 hours of now";
    }
  }
}