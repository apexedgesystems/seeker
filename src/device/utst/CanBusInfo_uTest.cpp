/**
 * @file CanBusInfo_uTest.cpp
 * @brief Unit tests for SocketCAN interface enumeration and status.
 *
 * Tests cover:
 *  - Enum conversions (CanInterfaceType, CanBusState)
 *  - Struct default construction and method behavior
 *  - Error handling for invalid/missing interfaces
 *  - API function behavior with platform-agnostic assertions
 *  - toString output consistency
 *  - Determinism for RT-safe functions
 */

#include "src/device/inc/CanBusInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

using seeker::device::CAN_MAX_BITRATE_CLASSIC;
using seeker::device::CAN_MAX_BITRATE_FD;
using seeker::device::CAN_NAME_SIZE;
using seeker::device::CAN_PATH_SIZE;
using seeker::device::CanBitTiming;
using seeker::device::CanBusState;
using seeker::device::CanCtrlMode;
using seeker::device::CanErrorCounters;
using seeker::device::canInterfaceExists;
using seeker::device::CanInterfaceInfo;
using seeker::device::CanInterfaceList;
using seeker::device::CanInterfaceStats;
using seeker::device::CanInterfaceType;
using seeker::device::getAllCanInterfaces;
using seeker::device::getCanBitTiming;
using seeker::device::getCanBusState;
using seeker::device::getCanErrorCounters;
using seeker::device::getCanInterfaceInfo;
using seeker::device::isCanInterface;
using seeker::device::MAX_CAN_INTERFACES;
using seeker::device::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default CanInterfaceType is UNKNOWN */
TEST(CanInterfaceTypeTest, DefaultIsUnknown) {
  CanInterfaceType type{};
  EXPECT_EQ(type, CanInterfaceType::UNKNOWN);
}

/** @test Default CanBusState is UNKNOWN */
TEST(CanBusStateTest, DefaultIsUnknown) {
  CanBusState state{};
  EXPECT_EQ(state, CanBusState::UNKNOWN);
}

/** @test Default CanCtrlMode has all flags false */
TEST(CanCtrlModeTest, DefaultConstruction) {
  CanCtrlMode mode{};
  EXPECT_FALSE(mode.loopback);
  EXPECT_FALSE(mode.listenOnly);
  EXPECT_FALSE(mode.tripleSampling);
  EXPECT_FALSE(mode.oneShot);
  EXPECT_FALSE(mode.berr);
  EXPECT_FALSE(mode.fd);
  EXPECT_FALSE(mode.presumeAck);
  EXPECT_FALSE(mode.fdNonIso);
  EXPECT_FALSE(mode.ccLen8Dlc);
}

/** @test Default CanBitTiming has all fields zero */
TEST(CanBitTimingTest, DefaultConstruction) {
  CanBitTiming timing{};
  EXPECT_EQ(timing.bitrate, 0U);
  EXPECT_EQ(timing.samplePoint, 0U);
  EXPECT_EQ(timing.tq, 0U);
  EXPECT_EQ(timing.propSeg, 0U);
  EXPECT_EQ(timing.phaseSeg1, 0U);
  EXPECT_EQ(timing.phaseSeg2, 0U);
  EXPECT_EQ(timing.sjw, 0U);
  EXPECT_EQ(timing.brp, 0U);
}

/** @test Default CanErrorCounters has all fields zero */
TEST(CanErrorCountersTest, DefaultConstruction) {
  CanErrorCounters errors{};
  EXPECT_EQ(errors.txErrors, 0U);
  EXPECT_EQ(errors.rxErrors, 0U);
  EXPECT_EQ(errors.busErrors, 0U);
  EXPECT_EQ(errors.errorWarning, 0U);
  EXPECT_EQ(errors.errorPassive, 0U);
  EXPECT_EQ(errors.busOff, 0U);
  EXPECT_EQ(errors.arbitrationLost, 0U);
  EXPECT_EQ(errors.restarts, 0U);
}

/** @test Default CanInterfaceStats has all fields zero */
TEST(CanInterfaceStatsTest, DefaultConstruction) {
  CanInterfaceStats stats{};
  EXPECT_EQ(stats.txFrames, 0U);
  EXPECT_EQ(stats.rxFrames, 0U);
  EXPECT_EQ(stats.txBytes, 0U);
  EXPECT_EQ(stats.rxBytes, 0U);
  EXPECT_EQ(stats.txDropped, 0U);
  EXPECT_EQ(stats.rxDropped, 0U);
  EXPECT_EQ(stats.txErrors, 0U);
  EXPECT_EQ(stats.rxErrors, 0U);
}

/** @test Default CanInterfaceInfo has empty strings and UNKNOWN state */
TEST(CanInterfaceInfoTest, DefaultConstruction) {
  CanInterfaceInfo info{};
  EXPECT_EQ(info.name[0], '\0');
  EXPECT_EQ(info.sysfsPath[0], '\0');
  EXPECT_EQ(info.driver[0], '\0');
  EXPECT_EQ(info.type, CanInterfaceType::UNKNOWN);
  EXPECT_EQ(info.state, CanBusState::UNKNOWN);
  EXPECT_EQ(info.clockFreq, 0U);
  EXPECT_EQ(info.txqLen, 0U);
  EXPECT_EQ(info.ifindex, -1);
  EXPECT_FALSE(info.exists);
  EXPECT_FALSE(info.isUp);
  EXPECT_FALSE(info.isRunning);
}

/** @test Default CanInterfaceList is empty */
TEST(CanInterfaceListTest, DefaultConstruction) {
  CanInterfaceList list{};
  EXPECT_EQ(list.count, 0U);
  EXPECT_TRUE(list.empty());
}

/* ----------------------------- CanInterfaceType Method Tests ----------------------------- */

/** @test toString covers all CanInterfaceType values */
TEST(CanInterfaceTypeTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(CanInterfaceType::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(CanInterfaceType::PHYSICAL), "physical");
  EXPECT_STREQ(toString(CanInterfaceType::VIRTUAL), "virtual");
  EXPECT_STREQ(toString(CanInterfaceType::SLCAN), "slcan");
  EXPECT_STREQ(toString(CanInterfaceType::SOCKETCAND), "socketcand");
  EXPECT_STREQ(toString(CanInterfaceType::PEAK), "peak");
  EXPECT_STREQ(toString(CanInterfaceType::KVASER), "kvaser");
  EXPECT_STREQ(toString(CanInterfaceType::VECTOR), "vector");
}

/** @test toString handles invalid CanInterfaceType value */
TEST(CanInterfaceTypeTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<CanInterfaceType>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All CanInterfaceType enum values are distinct */
TEST(CanInterfaceTypeTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::PHYSICAL));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::VIRTUAL));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::SLCAN));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::SOCKETCAND));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::PEAK));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::KVASER));
  values.insert(static_cast<std::uint8_t>(CanInterfaceType::VECTOR));
  EXPECT_EQ(values.size(), 8U);
}

/* ----------------------------- CanBusState Method Tests ----------------------------- */

/** @test toString covers all CanBusState values */
TEST(CanBusStateTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(CanBusState::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(CanBusState::ERROR_ACTIVE), "error-active");
  EXPECT_STREQ(toString(CanBusState::ERROR_WARNING), "error-warning");
  EXPECT_STREQ(toString(CanBusState::ERROR_PASSIVE), "error-passive");
  EXPECT_STREQ(toString(CanBusState::BUS_OFF), "bus-off");
  EXPECT_STREQ(toString(CanBusState::STOPPED), "stopped");
}

/** @test toString handles invalid CanBusState value */
TEST(CanBusStateTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<CanBusState>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All CanBusState enum values are distinct */
TEST(CanBusStateTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(CanBusState::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(CanBusState::ERROR_ACTIVE));
  values.insert(static_cast<std::uint8_t>(CanBusState::ERROR_WARNING));
  values.insert(static_cast<std::uint8_t>(CanBusState::ERROR_PASSIVE));
  values.insert(static_cast<std::uint8_t>(CanBusState::BUS_OFF));
  values.insert(static_cast<std::uint8_t>(CanBusState::STOPPED));
  EXPECT_EQ(values.size(), 6U);
}

/* ----------------------------- CanCtrlMode Method Tests ----------------------------- */

/** @test hasSpecialModes returns false for default CanCtrlMode */
TEST(CanCtrlModeTest, HasSpecialModesDefaultFalse) {
  CanCtrlMode mode{};
  EXPECT_FALSE(mode.hasSpecialModes());
}

/** @test hasSpecialModes detects loopback mode */
TEST(CanCtrlModeTest, HasSpecialModesDetectsLoopback) {
  CanCtrlMode mode{};
  mode.loopback = true;
  EXPECT_TRUE(mode.hasSpecialModes());
}

/** @test hasSpecialModes detects listen-only mode */
TEST(CanCtrlModeTest, HasSpecialModesDetectsListenOnly) {
  CanCtrlMode mode{};
  mode.listenOnly = true;
  EXPECT_TRUE(mode.hasSpecialModes());
}

/** @test hasSpecialModes detects FD mode */
TEST(CanCtrlModeTest, HasSpecialModesDetectsFd) {
  CanCtrlMode mode{};
  mode.fd = true;
  EXPECT_TRUE(mode.hasSpecialModes());
}

/** @test hasSpecialModes detects one-shot mode */
TEST(CanCtrlModeTest, HasSpecialModesDetectsOneShot) {
  CanCtrlMode mode{};
  mode.oneShot = true;
  EXPECT_TRUE(mode.hasSpecialModes());
}

/** @test hasSpecialModes detects triple-sampling mode */
TEST(CanCtrlModeTest, HasSpecialModesDetectsTripleSampling) {
  CanCtrlMode mode{};
  mode.tripleSampling = true;
  EXPECT_TRUE(mode.hasSpecialModes());
}

/** @test toString produces non-empty output for CanCtrlMode */
TEST(CanCtrlModeTest, ToStringProducesOutput) {
  CanCtrlMode mode{};
  std::string result = mode.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes 'fd' when FD mode enabled */
TEST(CanCtrlModeTest, ToStringIncludesFdWhenEnabled) {
  CanCtrlMode mode{};
  mode.fd = true;
  std::string result = mode.toString();
  EXPECT_NE(result.find("fd"), std::string::npos);
}

/** @test toString includes 'loopback' when loopback enabled */
TEST(CanCtrlModeTest, ToStringIncludesLoopbackWhenEnabled) {
  CanCtrlMode mode{};
  mode.loopback = true;
  std::string result = mode.toString();
  EXPECT_NE(result.find("loopback"), std::string::npos);
}

/* ----------------------------- CanBitTiming Method Tests ----------------------------- */

/** @test isConfigured returns false when bitrate is zero */
TEST(CanBitTimingTest, IsConfiguredFalseWhenZeroBitrate) {
  CanBitTiming timing{};
  EXPECT_FALSE(timing.isConfigured());
}

/** @test isConfigured returns true when bitrate is set */
TEST(CanBitTimingTest, IsConfiguredTrueWhenBitrateSet) {
  CanBitTiming timing{};
  timing.bitrate = 500000;
  EXPECT_TRUE(timing.isConfigured());
}

/** @test samplePointPercent returns zero when sample point is zero */
TEST(CanBitTimingTest, SamplePointPercentZeroWhenZero) {
  CanBitTiming timing{};
  EXPECT_DOUBLE_EQ(timing.samplePointPercent(), 0.0);
}

/** @test samplePointPercent calculates correctly */
TEST(CanBitTimingTest, SamplePointPercentCalculation) {
  CanBitTiming timing{};
  timing.samplePoint = 875;
  EXPECT_DOUBLE_EQ(timing.samplePointPercent(), 87.5);
}

/** @test samplePointPercent handles typical value */
TEST(CanBitTimingTest, SamplePointPercentTypicalValue) {
  CanBitTiming timing{};
  timing.samplePoint = 800;
  EXPECT_DOUBLE_EQ(timing.samplePointPercent(), 80.0);
}

/** @test toString produces non-empty output for CanBitTiming */
TEST(CanBitTimingTest, ToStringProducesOutput) {
  CanBitTiming timing{};
  std::string result = timing.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes bitrate value */
TEST(CanBitTimingTest, ToStringIncludesBitrate) {
  CanBitTiming timing{};
  timing.bitrate = 500000;
  std::string result = timing.toString();
  EXPECT_NE(result.find("500"), std::string::npos);
}

/* ----------------------------- CanErrorCounters Method Tests ----------------------------- */

/** @test hasErrors returns false when all counters are zero */
TEST(CanErrorCountersTest, HasErrorsFalseWhenZero) {
  CanErrorCounters errors{};
  EXPECT_FALSE(errors.hasErrors());
}

/** @test hasErrors returns true with TX errors */
TEST(CanErrorCountersTest, HasErrorsTrueWithTxErrors) {
  CanErrorCounters errors{};
  errors.txErrors = 1;
  EXPECT_TRUE(errors.hasErrors());
}

/** @test hasErrors returns true with RX errors */
TEST(CanErrorCountersTest, HasErrorsTrueWithRxErrors) {
  CanErrorCounters errors{};
  errors.rxErrors = 5;
  EXPECT_TRUE(errors.hasErrors());
}

/** @test hasErrors returns true with bus-off errors */
TEST(CanErrorCountersTest, HasErrorsTrueWithBusOff) {
  CanErrorCounters errors{};
  errors.busOff = 1;
  EXPECT_TRUE(errors.hasErrors());
}

/** @test totalErrors sums all error counters correctly */
TEST(CanErrorCountersTest, TotalErrorsSumsCorrectly) {
  CanErrorCounters errors{};
  errors.txErrors = 10;
  errors.rxErrors = 20;
  errors.busErrors = 5;
  errors.arbitrationLost = 3;
  EXPECT_EQ(errors.totalErrors(), 38U);
}

/** @test totalErrors returns zero when empty */
TEST(CanErrorCountersTest, TotalErrorsZeroWhenEmpty) {
  CanErrorCounters errors{};
  EXPECT_EQ(errors.totalErrors(), 0U);
}

/** @test toString produces non-empty output for CanErrorCounters */
TEST(CanErrorCountersTest, ToStringProducesOutput) {
  CanErrorCounters errors{};
  std::string result = errors.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes TX and RX error counts */
TEST(CanErrorCountersTest, ToStringIncludesTxRxErrors) {
  CanErrorCounters errors{};
  errors.txErrors = 5;
  errors.rxErrors = 3;
  std::string result = errors.toString();
  EXPECT_NE(result.find("5"), std::string::npos);
  EXPECT_NE(result.find("3"), std::string::npos);
}

/* ----------------------------- CanInterfaceStats Method Tests ----------------------------- */

/** @test toString produces non-empty output for CanInterfaceStats */
TEST(CanInterfaceStatsTest, ToStringProducesOutput) {
  CanInterfaceStats stats{};
  std::string result = stats.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes TX and RX frame counts */
TEST(CanInterfaceStatsTest, ToStringIncludesTxRxFrames) {
  CanInterfaceStats stats{};
  stats.txFrames = 100;
  stats.rxFrames = 200;
  std::string result = stats.toString();
  EXPECT_NE(result.find("100"), std::string::npos);
  EXPECT_NE(result.find("200"), std::string::npos);
}

/* ----------------------------- CanInterfaceInfo Method Tests ----------------------------- */

/** @test isUsable returns false for default CanInterfaceInfo */
TEST(CanInterfaceInfoTest, IsUsableFalseWhenDefault) {
  CanInterfaceInfo info{};
  EXPECT_FALSE(info.isUsable());
}

/** @test isUsable requires exists, isUp, and isRunning flags */
TEST(CanInterfaceInfoTest, IsUsableRequiresExistsUpRunning) {
  CanInterfaceInfo info{};
  info.exists = true;
  EXPECT_FALSE(info.isUsable());

  info.isUp = true;
  EXPECT_FALSE(info.isUsable());

  info.isRunning = true;
  EXPECT_TRUE(info.isUsable());
}

/** @test isUsable returns false when bus state is BUS_OFF */
TEST(CanInterfaceInfoTest, IsUsableFalseWhenBusOff) {
  CanInterfaceInfo info{};
  info.exists = true;
  info.isUp = true;
  info.isRunning = true;
  info.state = CanBusState::BUS_OFF;
  EXPECT_FALSE(info.isUsable());
}

/** @test isFd returns false for default CanInterfaceInfo */
TEST(CanInterfaceInfoTest, IsFdFalseWhenDefault) {
  CanInterfaceInfo info{};
  EXPECT_FALSE(info.isFd());
}

/** @test isFd returns true when ctrlMode FD flag is enabled */
TEST(CanInterfaceInfoTest, IsFdTrueWhenCtrlModeFdEnabled) {
  CanInterfaceInfo info{};
  info.ctrlMode.fd = true;
  EXPECT_TRUE(info.isFd());
}

/** @test hasErrors returns false for default CanInterfaceInfo */
TEST(CanInterfaceInfoTest, HasErrorsFalseWhenDefault) {
  CanInterfaceInfo info{};
  EXPECT_FALSE(info.hasErrors());
}

/** @test hasErrors returns true when error counters have errors */
TEST(CanInterfaceInfoTest, HasErrorsTrueWhenErrorCountersHaveErrors) {
  CanInterfaceInfo info{};
  info.errors.txErrors = 5;
  EXPECT_TRUE(info.hasErrors());
}

/** @test toString produces non-empty output for CanInterfaceInfo */
TEST(CanInterfaceInfoTest, ToStringProducesOutput) {
  CanInterfaceInfo info{};
  std::string result = info.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes interface name */
TEST(CanInterfaceInfoTest, ToStringIncludesName) {
  CanInterfaceInfo info{};
  std::strncpy(info.name.data(), "can0", CAN_NAME_SIZE - 1);
  std::string result = info.toString();
  EXPECT_NE(result.find("can0"), std::string::npos);
}

/* ----------------------------- CanInterfaceList Method Tests ----------------------------- */

/** @test empty returns true when count is zero */
TEST(CanInterfaceListTest, EmptyWhenCountZero) {
  CanInterfaceList list{};
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.count, 0U);
}

/** @test empty returns false when count is non-zero */
TEST(CanInterfaceListTest, NotEmptyWhenCountNonzero) {
  CanInterfaceList list{};
  list.count = 1;
  EXPECT_FALSE(list.empty());
}

/** @test find returns null for empty list */
TEST(CanInterfaceListTest, FindReturnsNullForEmptyList) {
  CanInterfaceList list{};
  EXPECT_EQ(list.find("can0"), nullptr);
}

/** @test find returns null for null name */
TEST(CanInterfaceListTest, FindReturnsNullForNullName) {
  CanInterfaceList list{};
  EXPECT_EQ(list.find(nullptr), nullptr);
}

/** @test find locates interface by name */
TEST(CanInterfaceListTest, FindLocatesInterface) {
  CanInterfaceList list{};
  std::strncpy(list.interfaces[0].name.data(), "can0", CAN_NAME_SIZE - 1);
  list.interfaces[0].exists = true;
  std::strncpy(list.interfaces[1].name.data(), "can1", CAN_NAME_SIZE - 1);
  list.interfaces[1].exists = true;
  list.count = 2;

  const auto* found = list.find("can1");
  ASSERT_NE(found, nullptr);
  EXPECT_STREQ(found->name.data(), "can1");
}

/** @test find returns null when interface not found */
TEST(CanInterfaceListTest, FindReturnsNullWhenNotFound) {
  CanInterfaceList list{};
  std::strncpy(list.interfaces[0].name.data(), "can0", CAN_NAME_SIZE - 1);
  list.count = 1;
  EXPECT_EQ(list.find("can99"), nullptr);
}

/** @test countUp returns zero when empty */
TEST(CanInterfaceListTest, CountUpReturnsZeroWhenEmpty) {
  CanInterfaceList list{};
  EXPECT_EQ(list.countUp(), 0U);
}

/** @test countUp counts interfaces with isUp flag correctly */
TEST(CanInterfaceListTest, CountUpCountsCorrectly) {
  CanInterfaceList list{};
  list.interfaces[0].isUp = true;
  list.interfaces[1].isUp = false;
  list.interfaces[2].isUp = true;
  list.count = 3;
  EXPECT_EQ(list.countUp(), 2U);
}

/** @test countPhysical returns zero when empty */
TEST(CanInterfaceListTest, CountPhysicalReturnsZeroWhenEmpty) {
  CanInterfaceList list{};
  EXPECT_EQ(list.countPhysical(), 0U);
}

/** @test countPhysical counts physical interfaces correctly */
TEST(CanInterfaceListTest, CountPhysicalCountsCorrectly) {
  CanInterfaceList list{};
  list.interfaces[0].type = CanInterfaceType::PHYSICAL;
  list.interfaces[1].type = CanInterfaceType::VIRTUAL;
  list.interfaces[2].type = CanInterfaceType::PHYSICAL;
  list.count = 3;
  EXPECT_EQ(list.countPhysical(), 2U);
}

/** @test countWithErrors returns zero when empty */
TEST(CanInterfaceListTest, CountWithErrorsReturnsZeroWhenEmpty) {
  CanInterfaceList list{};
  EXPECT_EQ(list.countWithErrors(), 0U);
}

/** @test countWithErrors counts interfaces with errors correctly */
TEST(CanInterfaceListTest, CountWithErrorsCountsCorrectly) {
  CanInterfaceList list{};
  list.interfaces[0].errors.txErrors = 5;
  list.interfaces[1].errors.rxErrors = 0;
  list.interfaces[2].errors.busOff = 1;
  list.count = 3;
  EXPECT_EQ(list.countWithErrors(), 2U);
}

/** @test toString produces non-empty output for CanInterfaceList */
TEST(CanInterfaceListTest, ToStringProducesOutput) {
  CanInterfaceList list{};
  std::string result = list.toString();
  EXPECT_FALSE(result.empty());
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test getCanInterfaceInfo returns default for null name */
TEST(CanBusInfoErrorHandlingTest, GetCanInterfaceInfoNullNameReturnsDefault) {
  CanInterfaceInfo info = getCanInterfaceInfo(nullptr);
  EXPECT_FALSE(info.exists);
  EXPECT_EQ(info.name[0], '\0');
}

/** @test getCanInterfaceInfo returns default for empty name */
TEST(CanBusInfoErrorHandlingTest, GetCanInterfaceInfoEmptyNameReturnsDefault) {
  CanInterfaceInfo info = getCanInterfaceInfo("");
  EXPECT_FALSE(info.exists);
}

/** @test getCanInterfaceInfo returns default for invalid name */
TEST(CanBusInfoErrorHandlingTest, GetCanInterfaceInfoInvalidNameReturnsDefault) {
  CanInterfaceInfo info = getCanInterfaceInfo("nonexistent_interface_12345");
  EXPECT_FALSE(info.exists);
}

/** @test getCanBitTiming returns default for null name */
TEST(CanBusInfoErrorHandlingTest, GetCanBitTimingNullNameReturnsDefault) {
  CanBitTiming timing = getCanBitTiming(nullptr);
  EXPECT_EQ(timing.bitrate, 0U);
  EXPECT_FALSE(timing.isConfigured());
}

/** @test getCanBitTiming returns default for empty name */
TEST(CanBusInfoErrorHandlingTest, GetCanBitTimingEmptyNameReturnsDefault) {
  CanBitTiming timing = getCanBitTiming("");
  EXPECT_FALSE(timing.isConfigured());
}

/** @test getCanErrorCounters returns default for null name */
TEST(CanBusInfoErrorHandlingTest, GetCanErrorCountersNullNameReturnsDefault) {
  CanErrorCounters errors = getCanErrorCounters(nullptr);
  EXPECT_EQ(errors.txErrors, 0U);
  EXPECT_EQ(errors.rxErrors, 0U);
  EXPECT_FALSE(errors.hasErrors());
}

/** @test getCanErrorCounters returns default for empty name */
TEST(CanBusInfoErrorHandlingTest, GetCanErrorCountersEmptyNameReturnsDefault) {
  CanErrorCounters errors = getCanErrorCounters("");
  EXPECT_FALSE(errors.hasErrors());
}

/** @test getCanBusState returns UNKNOWN for null name */
TEST(CanBusInfoErrorHandlingTest, GetCanBusStateNullNameReturnsUnknown) {
  CanBusState state = getCanBusState(nullptr);
  EXPECT_EQ(state, CanBusState::UNKNOWN);
}

/** @test getCanBusState returns UNKNOWN for empty name */
TEST(CanBusInfoErrorHandlingTest, GetCanBusStateEmptyNameReturnsUnknown) {
  CanBusState state = getCanBusState("");
  EXPECT_EQ(state, CanBusState::UNKNOWN);
}

/** @test isCanInterface returns false for null name */
TEST(CanBusInfoErrorHandlingTest, IsCanInterfaceNullNameReturnsFalse) {
  EXPECT_FALSE(isCanInterface(nullptr));
}

/** @test isCanInterface returns false for empty name */
TEST(CanBusInfoErrorHandlingTest, IsCanInterfaceEmptyNameReturnsFalse) {
  EXPECT_FALSE(isCanInterface(""));
}

/** @test canInterfaceExists returns false for null name */
TEST(CanBusInfoErrorHandlingTest, CanInterfaceExistsNullNameReturnsFalse) {
  EXPECT_FALSE(canInterfaceExists(nullptr));
}

/** @test canInterfaceExists returns false for empty name */
TEST(CanBusInfoErrorHandlingTest, CanInterfaceExistsEmptyNameReturnsFalse) {
  EXPECT_FALSE(canInterfaceExists(""));
}

/** @test canInterfaceExists returns false for non-existent interface */
TEST(CanBusInfoErrorHandlingTest, CanInterfaceExistsNonexistentReturnsFalse) {
  EXPECT_FALSE(canInterfaceExists("nonexistent_can_99"));
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getAllCanInterfaces returns valid list */
TEST(CanBusInfoApiTest, GetAllCanInterfacesReturnsValidList) {
  CanInterfaceList list = getAllCanInterfaces();
  EXPECT_LE(list.count, MAX_CAN_INTERFACES);
  EXPECT_EQ(list.empty(), list.count == 0);
}

/** @test getAllCanInterfaces list count matches actual interface count */
TEST(CanBusInfoApiTest, GetAllCanInterfacesListCountMatchesReality) {
  CanInterfaceList list = getAllCanInterfaces();
  std::size_t counted = 0;
  for (std::size_t i = 0; i < list.count; ++i) {
    if (list.interfaces[i].name[0] != '\0') {
      ++counted;
    }
  }
  EXPECT_EQ(counted, list.count);
}

/** @test getCanInterfaceInfo handles long interface name */
TEST(CanBusInfoApiTest, GetCanInterfaceInfoHandlesLongName) {
  std::string longName(CAN_NAME_SIZE + 100, 'x');
  CanInterfaceInfo info = getCanInterfaceInfo(longName.c_str());
  EXPECT_FALSE(info.exists);
}

/** @test isCanInterface returns false for non-CAN interfaces */
TEST(CanBusInfoApiTest, IsCanInterfaceNonCanReturnsFalse) {
  EXPECT_FALSE(isCanInterface("lo"));
  EXPECT_FALSE(isCanInterface("eth0"));
}

/** @test Found interfaces are queryable via getCanInterfaceInfo */
TEST(CanBusInfoApiTest, FoundInterfacesAreQueryable) {
  CanInterfaceList list = getAllCanInterfaces();
  for (std::size_t i = 0; i < list.count && i < 3; ++i) {
    const char* name = list.interfaces[i].name.data();
    CanInterfaceInfo info = getCanInterfaceInfo(name);
    EXPECT_TRUE(info.exists) << "Interface " << name << " should exist";
    EXPECT_STREQ(info.name.data(), name);
  }
}

/** @test Interface count methods return values within valid range */
TEST(CanBusInfoApiTest, InterfaceCountMethods) {
  CanInterfaceList list = getAllCanInterfaces();
  EXPECT_LE(list.countUp(), list.count);
  EXPECT_LE(list.countPhysical(), list.count);
  EXPECT_LE(list.countWithErrors(), list.count);
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test CAN_NAME_SIZE is within reasonable range */
TEST(CanBusInfoConstantsTest, NameSizeIsReasonable) {
  EXPECT_GE(CAN_NAME_SIZE, 16U);
  EXPECT_LE(CAN_NAME_SIZE, 64U);
}

/** @test CAN_PATH_SIZE is within reasonable range */
TEST(CanBusInfoConstantsTest, PathSizeIsReasonable) {
  EXPECT_GE(CAN_PATH_SIZE, 64U);
  EXPECT_LE(CAN_PATH_SIZE, 256U);
}

/** @test MAX_CAN_INTERFACES is within reasonable range */
TEST(CanBusInfoConstantsTest, MaxInterfacesIsReasonable) {
  EXPECT_GE(MAX_CAN_INTERFACES, 8U);
  EXPECT_LE(MAX_CAN_INTERFACES, 128U);
}

/** @test CAN_MAX_BITRATE_CLASSIC is 1 Mbps */
TEST(CanBusInfoConstantsTest, ClassicBitrateIsOneMbps) {
  EXPECT_EQ(CAN_MAX_BITRATE_CLASSIC, 1000000U);
}

/** @test CAN_MAX_BITRATE_FD is 8 Mbps */
TEST(CanBusInfoConstantsTest, FdBitrateIsEightMbps) { EXPECT_EQ(CAN_MAX_BITRATE_FD, 8000000U); }

/* ----------------------------- toString Tests ----------------------------- */

/** @test All enum toString functions return non-null */
TEST(CanBusInfoToStringTest, AllEnumToStringsReturnNonNull) {
  for (int i = 0; i < 16; ++i) {
    EXPECT_NE(toString(static_cast<CanInterfaceType>(i)), nullptr);
    EXPECT_NE(toString(static_cast<CanBusState>(i)), nullptr);
  }
}

/** @test CanCtrlMode toString describes all enabled flags */
TEST(CanBusInfoToStringTest, CtrlModeToStringDescribesFlags) {
  CanCtrlMode mode{};
  mode.loopback = true;
  mode.fd = true;
  mode.listenOnly = true;
  std::string result = mode.toString();
  EXPECT_NE(result.find("loopback"), std::string::npos);
  EXPECT_NE(result.find("fd"), std::string::npos);
  EXPECT_NE(result.find("listen"), std::string::npos);
}

/** @test CanBitTiming toString shows zero state */
TEST(CanBusInfoToStringTest, BitTimingToStringShowsZeroState) {
  CanBitTiming timing{};
  std::string result = timing.toString();
  EXPECT_FALSE(result.empty());
}

/** @test CanBitTiming toString shows configured state */
TEST(CanBusInfoToStringTest, BitTimingToStringShowsConfigured) {
  CanBitTiming timing{};
  timing.bitrate = 500000;
  timing.samplePoint = 875;
  std::string result = timing.toString();
  EXPECT_NE(result.find("500"), std::string::npos);
}

/** @test CanErrorCounters toString shows error counts */
TEST(CanBusInfoToStringTest, ErrorCountersToStringShowsCounts) {
  CanErrorCounters errors{};
  errors.txErrors = 10;
  errors.rxErrors = 20;
  errors.busOff = 1;
  std::string result = errors.toString();
  EXPECT_NE(result.find("10"), std::string::npos);
  EXPECT_NE(result.find("20"), std::string::npos);
}

/** @test CanInterfaceList toString produces output for empty list */
TEST(CanBusInfoToStringTest, InterfaceListToStringEmpty) {
  CanInterfaceList list{};
  std::string result = list.toString();
  EXPECT_FALSE(result.empty());
}

/** @test CanInterfaceList toString includes interface names */
TEST(CanBusInfoToStringTest, InterfaceListToStringWithInterfaces) {
  CanInterfaceList list{};
  std::strncpy(list.interfaces[0].name.data(), "can0", CAN_NAME_SIZE - 1);
  list.interfaces[0].exists = true;
  list.count = 1;
  std::string result = list.toString();
  EXPECT_NE(result.find("can0"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getCanInterfaceInfo returns consistent results */
TEST(CanBusInfoDeterminismTest, GetCanInterfaceInfoDeterministic) {
  const char* NAME = "can0";
  CanInterfaceInfo first = getCanInterfaceInfo(NAME);
  CanInterfaceInfo second = getCanInterfaceInfo(NAME);
  EXPECT_EQ(first.exists, second.exists);
  EXPECT_STREQ(first.name.data(), second.name.data());
  EXPECT_EQ(first.type, second.type);
}

/** @test getCanBitTiming returns consistent results */
TEST(CanBusInfoDeterminismTest, GetCanBitTimingDeterministic) {
  const char* NAME = "can0";
  CanBitTiming first = getCanBitTiming(NAME);
  CanBitTiming second = getCanBitTiming(NAME);
  EXPECT_EQ(first.bitrate, second.bitrate);
  EXPECT_EQ(first.samplePoint, second.samplePoint);
}

/** @test getCanBusState returns consistent results */
TEST(CanBusInfoDeterminismTest, GetCanBusStateDeterministic) {
  const char* NAME = "can0";
  CanBusState first = getCanBusState(NAME);
  CanBusState second = getCanBusState(NAME);
  EXPECT_EQ(first, second);
}

/** @test isCanInterface returns consistent results */
TEST(CanBusInfoDeterminismTest, IsCanInterfaceDeterministic) {
  const char* NAME = "can0";
  bool first = isCanInterface(NAME);
  bool second = isCanInterface(NAME);
  EXPECT_EQ(first, second);
}

/** @test canInterfaceExists returns consistent results */
TEST(CanBusInfoDeterminismTest, CanInterfaceExistsDeterministic) {
  const char* NAME = "can0";
  bool first = canInterfaceExists(NAME);
  bool second = canInterfaceExists(NAME);
  EXPECT_EQ(first, second);
}

/** @test Enum toString functions return consistent pointers */
TEST(CanBusInfoDeterminismTest, ToStringEnumDeterministic) {
  for (int i = 0; i < 8; ++i) {
    const char* first = toString(static_cast<CanInterfaceType>(i));
    const char* second = toString(static_cast<CanInterfaceType>(i));
    EXPECT_EQ(first, second);
  }
}

/** @test Struct toString methods return consistent results */
TEST(CanBusInfoDeterminismTest, ToStringStructDeterministic) {
  CanCtrlMode mode{};
  mode.fd = true;
  std::string first = mode.toString();
  std::string second = mode.toString();
  EXPECT_EQ(first, second);

  CanBitTiming timing{};
  timing.bitrate = 500000;
  first = timing.toString();
  second = timing.toString();
  EXPECT_EQ(first, second);

  CanErrorCounters errors{};
  errors.txErrors = 5;
  first = errors.toString();
  second = errors.toString();
  EXPECT_EQ(first, second);
}
