/**
 * @file GpioInfo_uTest.cpp
 * @brief Unit tests for GPIO chip enumeration and line information.
 *
 * Tests cover:
 *  - Enum conversions (GpioDirection, GpioDrive, GpioBias, GpioEdge)
 *  - Struct default construction and method behavior
 *  - Error handling for invalid/missing chips
 *  - API function behavior with platform-agnostic assertions
 *  - toString output consistency
 *  - Determinism for RT-safe functions
 */

#include "src/device/inc/GpioInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

using seeker::device::findGpioLine;
using seeker::device::getAllGpioChips;
using seeker::device::getGpioChipInfo;
using seeker::device::getGpioChipInfoByName;
using seeker::device::getGpioLineInfo;
using seeker::device::getGpioLines;
using seeker::device::GPIO_LABEL_SIZE;
using seeker::device::GPIO_NAME_SIZE;
using seeker::device::GPIO_PATH_SIZE;
using seeker::device::GpioBias;
using seeker::device::gpioChipExists;
using seeker::device::GpioChipInfo;
using seeker::device::GpioChipList;
using seeker::device::GpioDirection;
using seeker::device::GpioDrive;
using seeker::device::GpioEdge;
using seeker::device::GpioLineFlags;
using seeker::device::GpioLineInfo;
using seeker::device::GpioLineList;
using seeker::device::MAX_GPIO_CHIPS;
using seeker::device::MAX_GPIO_LINES_DETAILED;
using seeker::device::parseGpioChipNumber;
using seeker::device::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default GpioDirection is UNKNOWN */
TEST(GpioDirectionTest, DefaultIsUnknown) {
  GpioDirection dir{};
  EXPECT_EQ(dir, GpioDirection::UNKNOWN);
}

/** @test Default GpioDrive is UNKNOWN */
TEST(GpioDriveTest, DefaultIsUnknown) {
  GpioDrive drive{};
  EXPECT_EQ(drive, GpioDrive::UNKNOWN);
}

/** @test Default GpioBias is UNKNOWN */
TEST(GpioBiasTest, DefaultIsUnknown) {
  GpioBias bias{};
  EXPECT_EQ(bias, GpioBias::UNKNOWN);
}

/** @test Default GpioEdge is NONE */
TEST(GpioEdgeTest, DefaultIsNone) {
  GpioEdge edge{};
  EXPECT_EQ(edge, GpioEdge::NONE);
}

/** @test Default construction sets all flags to default values */
TEST(GpioLineFlagsTest, DefaultConstruction) {
  GpioLineFlags flags{};
  EXPECT_FALSE(flags.used);
  EXPECT_FALSE(flags.activeLow);
  EXPECT_EQ(flags.direction, GpioDirection::UNKNOWN);
  EXPECT_EQ(flags.drive, GpioDrive::UNKNOWN);
  EXPECT_EQ(flags.bias, GpioBias::UNKNOWN);
  EXPECT_EQ(flags.edge, GpioEdge::NONE);
}

/** @test Default construction initializes all fields to empty or zero */
TEST(GpioLineInfoTest, DefaultConstruction) {
  GpioLineInfo info{};
  EXPECT_EQ(info.offset, 0U);
  EXPECT_EQ(info.name[0], '\0');
  EXPECT_EQ(info.consumer[0], '\0');
  EXPECT_FALSE(info.flags.used);
}

/** @test Default construction initializes all fields to empty or invalid */
TEST(GpioChipInfoTest, DefaultConstruction) {
  GpioChipInfo info{};
  EXPECT_EQ(info.name[0], '\0');
  EXPECT_EQ(info.label[0], '\0');
  EXPECT_EQ(info.path[0], '\0');
  EXPECT_EQ(info.numLines, 0U);
  EXPECT_EQ(info.linesUsed, 0U);
  EXPECT_EQ(info.chipNumber, -1);
  EXPECT_FALSE(info.exists);
  EXPECT_FALSE(info.accessible);
}

/** @test Default construction creates empty list */
TEST(GpioChipListTest, DefaultConstruction) {
  GpioChipList list{};
  EXPECT_EQ(list.count, 0U);
  EXPECT_TRUE(list.empty());
}

/** @test Default construction creates empty list with invalid chip number */
TEST(GpioLineListTest, DefaultConstruction) {
  GpioLineList list{};
  EXPECT_EQ(list.count, 0U);
  EXPECT_EQ(list.chipNumber, -1);
  EXPECT_TRUE(list.empty());
}

/* ----------------------------- GpioDirection Method Tests ----------------------------- */

/** @test toString covers all GpioDirection values */
TEST(GpioDirectionTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(GpioDirection::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(GpioDirection::INPUT), "input");
  EXPECT_STREQ(toString(GpioDirection::OUTPUT), "output");
}

/** @test toString handles invalid GpioDirection value */
TEST(GpioDirectionTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<GpioDirection>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All GpioDirection enum values are distinct */
TEST(GpioDirectionTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(GpioDirection::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(GpioDirection::INPUT));
  values.insert(static_cast<std::uint8_t>(GpioDirection::OUTPUT));
  EXPECT_EQ(values.size(), 3U);
}

/* ----------------------------- GpioDrive Method Tests ----------------------------- */

/** @test toString covers all GpioDrive values */
TEST(GpioDriveTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(GpioDrive::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(GpioDrive::PUSH_PULL), "push-pull");
  EXPECT_STREQ(toString(GpioDrive::OPEN_DRAIN), "open-drain");
  EXPECT_STREQ(toString(GpioDrive::OPEN_SOURCE), "open-source");
}

/** @test toString handles invalid GpioDrive value */
TEST(GpioDriveTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<GpioDrive>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All GpioDrive enum values are distinct */
TEST(GpioDriveTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(GpioDrive::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(GpioDrive::PUSH_PULL));
  values.insert(static_cast<std::uint8_t>(GpioDrive::OPEN_DRAIN));
  values.insert(static_cast<std::uint8_t>(GpioDrive::OPEN_SOURCE));
  EXPECT_EQ(values.size(), 4U);
}

/* ----------------------------- GpioBias Method Tests ----------------------------- */

/** @test toString covers all GpioBias values */
TEST(GpioBiasTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(GpioBias::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(GpioBias::DISABLED), "disabled");
  EXPECT_STREQ(toString(GpioBias::PULL_UP), "pull-up");
  EXPECT_STREQ(toString(GpioBias::PULL_DOWN), "pull-down");
}

/** @test toString handles invalid GpioBias value */
TEST(GpioBiasTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<GpioBias>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All GpioBias enum values are distinct */
TEST(GpioBiasTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(GpioBias::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(GpioBias::DISABLED));
  values.insert(static_cast<std::uint8_t>(GpioBias::PULL_UP));
  values.insert(static_cast<std::uint8_t>(GpioBias::PULL_DOWN));
  EXPECT_EQ(values.size(), 4U);
}

/* ----------------------------- GpioEdge Method Tests ----------------------------- */

/** @test toString covers all GpioEdge values */
TEST(GpioEdgeTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(GpioEdge::NONE), "none");
  EXPECT_STREQ(toString(GpioEdge::RISING), "rising");
  EXPECT_STREQ(toString(GpioEdge::FALLING), "falling");
  EXPECT_STREQ(toString(GpioEdge::BOTH), "both");
}

/** @test toString handles invalid GpioEdge value */
TEST(GpioEdgeTest, ToStringHandlesInvalidValue) {
  const auto INVALID = static_cast<GpioEdge>(255);
  const char* result = toString(INVALID);
  EXPECT_NE(result, nullptr);
  EXPECT_GT(std::strlen(result), 0U);
}

/** @test All GpioEdge enum values are distinct */
TEST(GpioEdgeTest, AllEnumValuesAreDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(GpioEdge::NONE));
  values.insert(static_cast<std::uint8_t>(GpioEdge::RISING));
  values.insert(static_cast<std::uint8_t>(GpioEdge::FALLING));
  values.insert(static_cast<std::uint8_t>(GpioEdge::BOTH));
  EXPECT_EQ(values.size(), 4U);
}

/* ----------------------------- GpioLineFlags Method Tests ----------------------------- */

/** @test hasSpecialConfig returns false for default flags */
TEST(GpioLineFlagsTest, HasSpecialConfigDefaultFalse) {
  GpioLineFlags flags{};
  EXPECT_FALSE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig detects active-low configuration */
TEST(GpioLineFlagsTest, HasSpecialConfigDetectsActiveLow) {
  GpioLineFlags flags{};
  flags.activeLow = true;
  EXPECT_TRUE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig detects open-drain configuration */
TEST(GpioLineFlagsTest, HasSpecialConfigDetectsOpenDrain) {
  GpioLineFlags flags{};
  flags.drive = GpioDrive::OPEN_DRAIN;
  EXPECT_TRUE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig detects pull-up configuration */
TEST(GpioLineFlagsTest, HasSpecialConfigDetectsPullUp) {
  GpioLineFlags flags{};
  flags.bias = GpioBias::PULL_UP;
  EXPECT_TRUE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig detects edge detection configuration */
TEST(GpioLineFlagsTest, HasSpecialConfigDetectsEdge) {
  GpioLineFlags flags{};
  flags.edge = GpioEdge::RISING;
  EXPECT_TRUE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig ignores push-pull drive mode */
TEST(GpioLineFlagsTest, HasSpecialConfigIgnoresPushPull) {
  GpioLineFlags flags{};
  flags.drive = GpioDrive::PUSH_PULL;
  EXPECT_FALSE(flags.hasSpecialConfig());
}

/** @test hasSpecialConfig ignores disabled bias mode */
TEST(GpioLineFlagsTest, HasSpecialConfigIgnoresDisabledBias) {
  GpioLineFlags flags{};
  flags.bias = GpioBias::DISABLED;
  EXPECT_FALSE(flags.hasSpecialConfig());
}

/** @test toString produces non-empty output */
TEST(GpioLineFlagsTest, ToStringProducesOutput) {
  GpioLineFlags flags{};
  std::string result = flags.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes direction in output */
TEST(GpioLineFlagsTest, ToStringIncludesDirection) {
  GpioLineFlags flags{};
  flags.direction = GpioDirection::INPUT;
  std::string result = flags.toString();
  EXPECT_NE(result.find("input"), std::string::npos);
}

/** @test toString includes used flag in output */
TEST(GpioLineFlagsTest, ToStringIncludesUsedFlag) {
  GpioLineFlags flags{};
  flags.used = true;
  std::string result = flags.toString();
  EXPECT_NE(result.find("used"), std::string::npos);
}

/* ----------------------------- GpioLineInfo Method Tests ----------------------------- */

/** @test hasName returns false when name is empty */
TEST(GpioLineInfoTest, HasNameFalseWhenEmpty) {
  GpioLineInfo info{};
  EXPECT_FALSE(info.hasName());
}

/** @test hasName returns true when name is set */
TEST(GpioLineInfoTest, HasNameTrueWhenSet) {
  GpioLineInfo info{};
  std::strncpy(info.name.data(), "GPIO_LED", GPIO_NAME_SIZE - 1);
  EXPECT_TRUE(info.hasName());
}

/** @test isUsed returns false for default flags */
TEST(GpioLineInfoTest, IsUsedFalseWhenDefault) {
  GpioLineInfo info{};
  EXPECT_FALSE(info.isUsed());
}

/** @test isUsed returns true when used flag is set */
TEST(GpioLineInfoTest, IsUsedTrueWhenFlagSet) {
  GpioLineInfo info{};
  info.flags.used = true;
  EXPECT_TRUE(info.isUsed());
}

/** @test toString produces non-empty output */
TEST(GpioLineInfoTest, ToStringProducesOutput) {
  GpioLineInfo info{};
  std::string result = info.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes offset in output */
TEST(GpioLineInfoTest, ToStringIncludesOffset) {
  GpioLineInfo info{};
  info.offset = 17;
  std::string result = info.toString();
  EXPECT_NE(result.find("17"), std::string::npos);
}

/** @test toString includes name in output */
TEST(GpioLineInfoTest, ToStringIncludesName) {
  GpioLineInfo info{};
  std::strncpy(info.name.data(), "SPI_CLK", GPIO_NAME_SIZE - 1);
  std::string result = info.toString();
  EXPECT_NE(result.find("SPI_CLK"), std::string::npos);
}

/** @test toString shows unnamed for empty name */
TEST(GpioLineInfoTest, ToStringShowsUnnamedForEmptyName) {
  GpioLineInfo info{};
  std::string result = info.toString();
  EXPECT_NE(result.find("unnamed"), std::string::npos);
}

/* ----------------------------- GpioChipInfo Method Tests ----------------------------- */

/** @test isUsable returns false for default chip info */
TEST(GpioChipInfoTest, IsUsableFalseWhenDefault) {
  GpioChipInfo info{};
  EXPECT_FALSE(info.isUsable());
}

/** @test isUsable requires both exists and accessible flags */
TEST(GpioChipInfoTest, IsUsableRequiresExistsAndAccessible) {
  GpioChipInfo info{};
  info.exists = true;
  EXPECT_FALSE(info.isUsable());

  info.accessible = true;
  EXPECT_TRUE(info.isUsable());
}

/** @test isUsable returns false when not accessible */
TEST(GpioChipInfoTest, IsUsableFalseWhenNotAccessible) {
  GpioChipInfo info{};
  info.exists = true;
  info.accessible = false;
  EXPECT_FALSE(info.isUsable());
}

/** @test toString produces non-empty output */
TEST(GpioChipInfoTest, ToStringProducesOutput) {
  GpioChipInfo info{};
  std::string result = info.toString();
  EXPECT_FALSE(result.empty());
}

/** @test toString includes chip name in output */
TEST(GpioChipInfoTest, ToStringIncludesName) {
  GpioChipInfo info{};
  std::strncpy(info.name.data(), "gpiochip0", GPIO_NAME_SIZE - 1);
  std::string result = info.toString();
  EXPECT_NE(result.find("gpiochip0"), std::string::npos);
}

/** @test toString includes line count in output */
TEST(GpioChipInfoTest, ToStringIncludesLineCount) {
  GpioChipInfo info{};
  info.numLines = 54;
  std::string result = info.toString();
  EXPECT_NE(result.find("54"), std::string::npos);
}

/* ----------------------------- GpioChipList Method Tests ----------------------------- */

/** @test empty returns true when count is zero */
TEST(GpioChipListTest, EmptyWhenCountZero) {
  GpioChipList list{};
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.count, 0U);
}

/** @test empty returns false when count is nonzero */
TEST(GpioChipListTest, NotEmptyWhenCountNonzero) {
  GpioChipList list{};
  list.count = 1;
  EXPECT_FALSE(list.empty());
}

/** @test find returns null for empty list */
TEST(GpioChipListTest, FindReturnsNullForEmptyList) {
  GpioChipList list{};
  EXPECT_EQ(list.find("gpiochip0"), nullptr);
}

/** @test find returns null for null name */
TEST(GpioChipListTest, FindReturnsNullForNullName) {
  GpioChipList list{};
  EXPECT_EQ(list.find(nullptr), nullptr);
}

/** @test find locates chip by name */
TEST(GpioChipListTest, FindLocatesChip) {
  GpioChipList list{};
  std::strncpy(list.chips[0].name.data(), "gpiochip0", GPIO_NAME_SIZE - 1);
  list.chips[0].exists = true;
  std::strncpy(list.chips[1].name.data(), "gpiochip1", GPIO_NAME_SIZE - 1);
  list.chips[1].exists = true;
  list.count = 2;

  const auto* found = list.find("gpiochip1");
  ASSERT_NE(found, nullptr);
  EXPECT_STREQ(found->name.data(), "gpiochip1");
}

/** @test find handles device path format */
TEST(GpioChipListTest, FindHandlesDevPath) {
  GpioChipList list{};
  std::strncpy(list.chips[0].name.data(), "gpiochip0", GPIO_NAME_SIZE - 1);
  list.count = 1;

  const auto* found = list.find("/dev/gpiochip0");
  ASSERT_NE(found, nullptr);
  EXPECT_STREQ(found->name.data(), "gpiochip0");
}

/** @test findByNumber returns null when list is empty */
TEST(GpioChipListTest, FindByNumberReturnsNullWhenEmpty) {
  GpioChipList list{};
  EXPECT_EQ(list.findByNumber(0), nullptr);
}

/** @test findByNumber locates chip by number */
TEST(GpioChipListTest, FindByNumberLocatesChip) {
  GpioChipList list{};
  list.chips[0].chipNumber = 0;
  list.chips[1].chipNumber = 4;
  list.count = 2;

  const auto* found = list.findByNumber(4);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->chipNumber, 4);
}

/** @test totalLines returns zero for empty list */
TEST(GpioChipListTest, TotalLinesZeroWhenEmpty) {
  GpioChipList list{};
  EXPECT_EQ(list.totalLines(), 0U);
}

/** @test totalLines sums line counts correctly */
TEST(GpioChipListTest, TotalLinesSumsCorrectly) {
  GpioChipList list{};
  list.chips[0].numLines = 54;
  list.chips[1].numLines = 32;
  list.count = 2;
  EXPECT_EQ(list.totalLines(), 86U);
}

/** @test totalUsed returns zero for empty list */
TEST(GpioChipListTest, TotalUsedZeroWhenEmpty) {
  GpioChipList list{};
  EXPECT_EQ(list.totalUsed(), 0U);
}

/** @test totalUsed sums used counts correctly */
TEST(GpioChipListTest, TotalUsedSumsCorrectly) {
  GpioChipList list{};
  list.chips[0].linesUsed = 5;
  list.chips[1].linesUsed = 10;
  list.count = 2;
  EXPECT_EQ(list.totalUsed(), 15U);
}

/** @test toString produces non-empty output */
TEST(GpioChipListTest, ToStringProducesOutput) {
  GpioChipList list{};
  std::string result = list.toString();
  EXPECT_FALSE(result.empty());
}

/* ----------------------------- GpioLineList Method Tests ----------------------------- */

/** @test empty returns true when count is zero */
TEST(GpioLineListTest, EmptyWhenCountZero) {
  GpioLineList list{};
  EXPECT_TRUE(list.empty());
}

/** @test findByOffset returns null when list is empty */
TEST(GpioLineListTest, FindByOffsetReturnsNullWhenEmpty) {
  GpioLineList list{};
  EXPECT_EQ(list.findByOffset(0), nullptr);
}

/** @test findByOffset locates line by offset */
TEST(GpioLineListTest, FindByOffsetLocatesLine) {
  GpioLineList list{};
  list.lines[0].offset = 5;
  list.lines[1].offset = 17;
  list.count = 2;

  const auto* found = list.findByOffset(17);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->offset, 17U);
}

/** @test findByName returns null when list is empty */
TEST(GpioLineListTest, FindByNameReturnsNullWhenEmpty) {
  GpioLineList list{};
  EXPECT_EQ(list.findByName("GPIO_LED"), nullptr);
}

/** @test findByName returns null for null input */
TEST(GpioLineListTest, FindByNameReturnsNullForNull) {
  GpioLineList list{};
  EXPECT_EQ(list.findByName(nullptr), nullptr);
}

/** @test findByName locates line by name */
TEST(GpioLineListTest, FindByNameLocatesLine) {
  GpioLineList list{};
  std::strncpy(list.lines[0].name.data(), "SPI_CLK", GPIO_NAME_SIZE - 1);
  std::strncpy(list.lines[1].name.data(), "GPIO_LED", GPIO_NAME_SIZE - 1);
  list.count = 2;

  const auto* found = list.findByName("GPIO_LED");
  ASSERT_NE(found, nullptr);
  EXPECT_STREQ(found->name.data(), "GPIO_LED");
}

/** @test countUsed returns zero when list is empty */
TEST(GpioLineListTest, CountUsedReturnsZeroWhenEmpty) {
  GpioLineList list{};
  EXPECT_EQ(list.countUsed(), 0U);
}

/** @test countUsed counts used lines correctly */
TEST(GpioLineListTest, CountUsedCountsCorrectly) {
  GpioLineList list{};
  list.lines[0].flags.used = true;
  list.lines[1].flags.used = false;
  list.lines[2].flags.used = true;
  list.count = 3;
  EXPECT_EQ(list.countUsed(), 2U);
}

/** @test countInputs returns zero when list is empty */
TEST(GpioLineListTest, CountInputsReturnsZeroWhenEmpty) {
  GpioLineList list{};
  EXPECT_EQ(list.countInputs(), 0U);
}

/** @test countInputs counts input lines correctly */
TEST(GpioLineListTest, CountInputsCountsCorrectly) {
  GpioLineList list{};
  list.lines[0].flags.direction = GpioDirection::INPUT;
  list.lines[1].flags.direction = GpioDirection::OUTPUT;
  list.lines[2].flags.direction = GpioDirection::INPUT;
  list.count = 3;
  EXPECT_EQ(list.countInputs(), 2U);
}

/** @test countOutputs counts output lines correctly */
TEST(GpioLineListTest, CountOutputsCountsCorrectly) {
  GpioLineList list{};
  list.lines[0].flags.direction = GpioDirection::OUTPUT;
  list.lines[1].flags.direction = GpioDirection::OUTPUT;
  list.lines[2].flags.direction = GpioDirection::INPUT;
  list.count = 3;
  EXPECT_EQ(list.countOutputs(), 2U);
}

/** @test toString produces non-empty output */
TEST(GpioLineListTest, ToStringProducesOutput) {
  GpioLineList list{};
  std::string result = list.toString();
  EXPECT_FALSE(result.empty());
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test getGpioChipInfo returns default for negative chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioChipInfoNegativeChipReturnsDefault) {
  GpioChipInfo info = getGpioChipInfo(-1);
  EXPECT_FALSE(info.exists);
  EXPECT_EQ(info.chipNumber, -1);
}

/** @test getGpioChipInfo returns default for invalid chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioChipInfoInvalidChipReturnsDefault) {
  GpioChipInfo info = getGpioChipInfo(999);
  EXPECT_FALSE(info.exists);
}

/** @test getGpioChipInfoByName returns default for null name */
TEST(GpioInfoErrorHandlingTest, GetGpioChipInfoByNameNullReturnsDefault) {
  GpioChipInfo info = getGpioChipInfoByName(nullptr);
  EXPECT_FALSE(info.exists);
}

/** @test getGpioChipInfoByName returns default for empty name */
TEST(GpioInfoErrorHandlingTest, GetGpioChipInfoByNameEmptyReturnsDefault) {
  GpioChipInfo info = getGpioChipInfoByName("");
  EXPECT_FALSE(info.exists);
}

/** @test getGpioChipInfoByName returns default for invalid name */
TEST(GpioInfoErrorHandlingTest, GetGpioChipInfoByNameInvalidReturnsDefault) {
  GpioChipInfo info = getGpioChipInfoByName("notachip");
  EXPECT_FALSE(info.exists);
}

/** @test getGpioLineInfo returns default for negative chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioLineInfoNegativeChipReturnsDefault) {
  GpioLineInfo info = getGpioLineInfo(-1, 0);
  EXPECT_EQ(info.offset, 0U);
}

/** @test getGpioLineInfo returns default for invalid chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioLineInfoInvalidChipReturnsDefault) {
  GpioLineInfo info = getGpioLineInfo(999, 0);
  EXPECT_EQ(info.name[0], '\0');
}

/** @test getGpioLines returns empty list for negative chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioLinesNegativeChipReturnsEmpty) {
  GpioLineList list = getGpioLines(-1);
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.chipNumber, -1);
}

/** @test getGpioLines returns empty list for invalid chip number */
TEST(GpioInfoErrorHandlingTest, GetGpioLinesInvalidChipReturnsEmpty) {
  GpioLineList list = getGpioLines(999);
  EXPECT_TRUE(list.empty());
}

/** @test gpioChipExists returns false for negative chip number */
TEST(GpioInfoErrorHandlingTest, GpioChipExistsNegativeReturnsFalse) {
  EXPECT_FALSE(gpioChipExists(-1));
}

/** @test gpioChipExists returns false for large chip number */
TEST(GpioInfoErrorHandlingTest, GpioChipExistsLargeReturnsFalse) {
  EXPECT_FALSE(gpioChipExists(999));
}

/** @test parseGpioChipNumber returns false for null input */
TEST(GpioInfoErrorHandlingTest, ParseGpioChipNumberNullReturnsFalse) {
  std::int32_t chipNum = -1;
  EXPECT_FALSE(parseGpioChipNumber(nullptr, chipNum));
}

/** @test parseGpioChipNumber returns false for empty string */
TEST(GpioInfoErrorHandlingTest, ParseGpioChipNumberEmptyReturnsFalse) {
  std::int32_t chipNum = -1;
  EXPECT_FALSE(parseGpioChipNumber("", chipNum));
}

/** @test parseGpioChipNumber returns false for invalid format */
TEST(GpioInfoErrorHandlingTest, ParseGpioChipNumberInvalidReturnsFalse) {
  std::int32_t chipNum = -1;
  EXPECT_FALSE(parseGpioChipNumber("notachip", chipNum));
}

/** @test findGpioLine returns false for negative chip number */
TEST(GpioInfoErrorHandlingTest, FindGpioLineNegativeReturnsFalse) {
  std::int32_t chipNum = -1;
  std::uint32_t offset = 0;
  EXPECT_FALSE(findGpioLine(-1, chipNum, offset));
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getAllGpioChips returns valid list */
TEST(GpioInfoApiTest, GetAllGpioChipsReturnsValidList) {
  GpioChipList list = getAllGpioChips();
  EXPECT_LE(list.count, MAX_GPIO_CHIPS);
  EXPECT_EQ(list.empty(), list.count == 0);
}

/** @test getAllGpioChips list is internally consistent */
TEST(GpioInfoApiTest, GetAllGpioChipsListConsistent) {
  GpioChipList list = getAllGpioChips();
  std::size_t counted = 0;
  for (std::size_t i = 0; i < list.count; ++i) {
    if (list.chips[i].name[0] != '\0') {
      ++counted;
    }
  }
  EXPECT_EQ(counted, list.count);
}

/** @test parseGpioChipNumber parses basic chip name */
TEST(GpioInfoApiTest, ParseGpioChipNumberBasicName) {
  std::int32_t chipNum = -1;
  EXPECT_TRUE(parseGpioChipNumber("gpiochip0", chipNum));
  EXPECT_EQ(chipNum, 0);
}

/** @test parseGpioChipNumber parses chip name with device path */
TEST(GpioInfoApiTest, ParseGpioChipNumberWithDevPath) {
  std::int32_t chipNum = -1;
  EXPECT_TRUE(parseGpioChipNumber("/dev/gpiochip4", chipNum));
  EXPECT_EQ(chipNum, 4);
}

/** @test parseGpioChipNumber parses multi-digit chip number */
TEST(GpioInfoApiTest, ParseGpioChipNumberMultiDigit) {
  std::int32_t chipNum = -1;
  EXPECT_TRUE(parseGpioChipNumber("gpiochip123", chipNum));
  EXPECT_EQ(chipNum, 123);
}

/** @test parseGpioChipNumber rejects name without number */
TEST(GpioInfoApiTest, ParseGpioChipNumberNoNumber) {
  std::int32_t chipNum = 99;
  EXPECT_FALSE(parseGpioChipNumber("gpiochip", chipNum));
}

/** @test parseGpioChipNumber rejects name with trailing characters */
TEST(GpioInfoApiTest, ParseGpioChipNumberTrailingChars) {
  std::int32_t chipNum = 99;
  EXPECT_FALSE(parseGpioChipNumber("gpiochip0abc", chipNum));
}

/** @test Found chips are queryable via getGpioChipInfo */
TEST(GpioInfoApiTest, FoundChipsAreQueryable) {
  GpioChipList list = getAllGpioChips();
  for (std::size_t i = 0; i < list.count && i < 3; ++i) {
    std::int32_t chipNum = list.chips[i].chipNumber;
    GpioChipInfo info = getGpioChipInfo(chipNum);
    EXPECT_TRUE(info.exists) << "Chip " << chipNum << " should exist";
    EXPECT_EQ(info.chipNumber, chipNum);
  }
}

/** @test totalLines is greater than or equal to totalUsed */
TEST(GpioInfoApiTest, ChipListCountMethods) {
  GpioChipList list = getAllGpioChips();
  EXPECT_GE(list.totalLines(), list.totalUsed());
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test GPIO_NAME_SIZE is within reasonable bounds */
TEST(GpioInfoConstantsTest, NameSizeIsReasonable) {
  EXPECT_GE(GPIO_NAME_SIZE, 32U);
  EXPECT_LE(GPIO_NAME_SIZE, 128U);
}

/** @test GPIO_LABEL_SIZE is within reasonable bounds */
TEST(GpioInfoConstantsTest, LabelSizeIsReasonable) {
  EXPECT_GE(GPIO_LABEL_SIZE, 32U);
  EXPECT_LE(GPIO_LABEL_SIZE, 128U);
}

/** @test GPIO_PATH_SIZE is within reasonable bounds */
TEST(GpioInfoConstantsTest, PathSizeIsReasonable) {
  EXPECT_GE(GPIO_PATH_SIZE, 64U);
  EXPECT_LE(GPIO_PATH_SIZE, 256U);
}

/** @test MAX_GPIO_CHIPS is within reasonable bounds */
TEST(GpioInfoConstantsTest, MaxChipsIsReasonable) {
  EXPECT_GE(MAX_GPIO_CHIPS, 8U);
  EXPECT_LE(MAX_GPIO_CHIPS, 128U);
}

/** @test MAX_GPIO_LINES_DETAILED is within reasonable bounds */
TEST(GpioInfoConstantsTest, MaxLinesDetailedIsReasonable) {
  EXPECT_GE(MAX_GPIO_LINES_DETAILED, 64U);
  EXPECT_LE(MAX_GPIO_LINES_DETAILED, 256U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test All enum toString functions return non-null for any input */
TEST(GpioInfoToStringTest, AllEnumToStringsReturnNonNull) {
  for (int i = 0; i < 16; ++i) {
    EXPECT_NE(toString(static_cast<GpioDirection>(i)), nullptr);
    EXPECT_NE(toString(static_cast<GpioDrive>(i)), nullptr);
    EXPECT_NE(toString(static_cast<GpioBias>(i)), nullptr);
    EXPECT_NE(toString(static_cast<GpioEdge>(i)), nullptr);
  }
}

/** @test GpioLineFlags toString describes configuration */
TEST(GpioInfoToStringTest, LineFlagsToStringDescribesConfig) {
  GpioLineFlags flags{};
  flags.direction = GpioDirection::OUTPUT;
  flags.drive = GpioDrive::OPEN_DRAIN;
  flags.used = true;
  std::string result = flags.toString();
  EXPECT_NE(result.find("output"), std::string::npos);
  EXPECT_NE(result.find("open-drain"), std::string::npos);
  EXPECT_NE(result.find("used"), std::string::npos);
}

/** @test GpioLineInfo toString includes complete information */
TEST(GpioInfoToStringTest, LineInfoToStringComplete) {
  GpioLineInfo info{};
  info.offset = 7;
  std::strncpy(info.name.data(), "GPIO_TEST", GPIO_NAME_SIZE - 1);
  std::strncpy(info.consumer.data(), "test_driver", GPIO_LABEL_SIZE - 1);
  info.flags.direction = GpioDirection::INPUT;
  std::string result = info.toString();
  EXPECT_NE(result.find("7"), std::string::npos);
  EXPECT_NE(result.find("GPIO_TEST"), std::string::npos);
  EXPECT_NE(result.find("test_driver"), std::string::npos);
}

/** @test GpioChipList toString handles empty list */
TEST(GpioInfoToStringTest, ChipListToStringEmpty) {
  GpioChipList list{};
  std::string result = list.toString();
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("0"), std::string::npos);
}

/** @test GpioChipList toString includes chip details */
TEST(GpioInfoToStringTest, ChipListToStringWithChips) {
  GpioChipList list{};
  std::strncpy(list.chips[0].name.data(), "gpiochip0", GPIO_NAME_SIZE - 1);
  list.chips[0].numLines = 54;
  list.count = 1;
  std::string result = list.toString();
  EXPECT_NE(result.find("gpiochip0"), std::string::npos);
  EXPECT_NE(result.find("54"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getGpioChipInfo returns consistent results */
TEST(GpioInfoDeterminismTest, GetGpioChipInfoDeterministic) {
  constexpr std::int32_t CHIP_NUM = 0;
  GpioChipInfo first = getGpioChipInfo(CHIP_NUM);
  GpioChipInfo second = getGpioChipInfo(CHIP_NUM);
  EXPECT_EQ(first.exists, second.exists);
  EXPECT_EQ(first.chipNumber, second.chipNumber);
  EXPECT_STREQ(first.name.data(), second.name.data());
}

/** @test getGpioLineInfo returns consistent results */
TEST(GpioInfoDeterminismTest, GetGpioLineInfoDeterministic) {
  constexpr std::int32_t CHIP_NUM = 0;
  constexpr std::uint32_t LINE = 0;
  GpioLineInfo first = getGpioLineInfo(CHIP_NUM, LINE);
  GpioLineInfo second = getGpioLineInfo(CHIP_NUM, LINE);
  EXPECT_EQ(first.offset, second.offset);
  EXPECT_STREQ(first.name.data(), second.name.data());
}

/** @test gpioChipExists returns consistent results */
TEST(GpioInfoDeterminismTest, GpioChipExistsDeterministic) {
  constexpr std::int32_t CHIP_NUM = 0;
  bool first = gpioChipExists(CHIP_NUM);
  bool second = gpioChipExists(CHIP_NUM);
  EXPECT_EQ(first, second);
}

/** @test parseGpioChipNumber returns consistent results */
TEST(GpioInfoDeterminismTest, ParseGpioChipNumberDeterministic) {
  std::int32_t first = -1;
  std::int32_t second = -1;
  (void)parseGpioChipNumber("gpiochip5", first);
  (void)parseGpioChipNumber("gpiochip5", second);
  EXPECT_EQ(first, second);
}

/** @test Enum toString functions return consistent results */
TEST(GpioInfoDeterminismTest, ToStringEnumDeterministic) {
  for (int i = 0; i < 4; ++i) {
    const char* first = toString(static_cast<GpioDirection>(i));
    const char* second = toString(static_cast<GpioDirection>(i));
    EXPECT_EQ(first, second);
  }
}

/** @test Struct toString methods return consistent results */
TEST(GpioInfoDeterminismTest, ToStringStructDeterministic) {
  GpioLineFlags flags{};
  flags.direction = GpioDirection::OUTPUT;
  std::string first = flags.toString();
  std::string second = flags.toString();
  EXPECT_EQ(first, second);

  GpioChipInfo chip{};
  chip.numLines = 32;
  first = chip.toString();
  second = chip.toString();
  EXPECT_EQ(first, second);
}
