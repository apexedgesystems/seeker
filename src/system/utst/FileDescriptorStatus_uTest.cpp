/**
 * @file FileDescriptorStatus_uTest.cpp
 * @brief Unit tests for seeker::system::FileDescriptorStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact counts.
 *  - All Linux processes have at least stdin/stdout/stderr (3 FDs).
 */

#include "src/system/inc/FileDescriptorStatus.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <set>
#include <string>

using seeker::system::FD_PATH_SIZE;
using seeker::system::FdType;
using seeker::system::FdTypeCount;
using seeker::system::FileDescriptorStatus;
using seeker::system::getFdHardLimit;
using seeker::system::getFdSoftLimit;
using seeker::system::getFileDescriptorStatus;
using seeker::system::getOpenFdCount;
using seeker::system::getProcessFdStatus;
using seeker::system::getSystemFdStatus;
using seeker::system::MAX_FD_TYPES;
using seeker::system::ProcessFdStatus;
using seeker::system::SystemFdStatus;
using seeker::system::toString;

class FileDescriptorStatusTest : public ::testing::Test {
protected:
  FileDescriptorStatus status_{};

  void SetUp() override { status_ = getFileDescriptorStatus(); }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default FdType is UNKNOWN. */
TEST(FdTypeDefaultTest, DefaultIsUnknown) {
  const FdType TYPE{};
  EXPECT_EQ(TYPE, FdType::UNKNOWN);
}

/** @test Default FdTypeCount is zeroed. */
TEST(FdTypeCountDefaultTest, DefaultIsZeroed) {
  const FdTypeCount DEFAULT{};
  EXPECT_EQ(DEFAULT.type, FdType::UNKNOWN);
  EXPECT_EQ(DEFAULT.count, 0U);
}

/** @test Default ProcessFdStatus is zeroed. */
TEST(ProcessFdStatusDefaultTest, DefaultIsZeroed) {
  const ProcessFdStatus DEFAULT{};
  EXPECT_EQ(DEFAULT.openCount, 0U);
  EXPECT_EQ(DEFAULT.softLimit, 0U);
  EXPECT_EQ(DEFAULT.hardLimit, 0U);
  EXPECT_EQ(DEFAULT.typeCount, 0U);
  EXPECT_EQ(DEFAULT.highestFd, 0U);
}

/** @test Default SystemFdStatus is zeroed. */
TEST(SystemFdStatusDefaultTest, DefaultIsZeroed) {
  const SystemFdStatus DEFAULT{};
  EXPECT_EQ(DEFAULT.allocated, 0U);
  EXPECT_EQ(DEFAULT.free, 0U);
  EXPECT_EQ(DEFAULT.maximum, 0U);
  EXPECT_EQ(DEFAULT.nrOpen, 0U);
  EXPECT_EQ(DEFAULT.inodeMax, 0U);
}

/** @test Default FileDescriptorStatus is zeroed. */
TEST(FileDescriptorStatusDefaultTest, DefaultIsZeroed) {
  const FileDescriptorStatus DEFAULT{};
  EXPECT_EQ(DEFAULT.process.openCount, 0U);
  EXPECT_EQ(DEFAULT.system.allocated, 0U);
}

/* ----------------------------- FdType Method Tests ----------------------------- */

/** @test toString covers all FdType values. */
TEST(FdTypeTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(FdType::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(FdType::REGULAR), "file");
  EXPECT_STREQ(toString(FdType::DIRECTORY), "directory");
  EXPECT_STREQ(toString(FdType::PIPE), "pipe");
  EXPECT_STREQ(toString(FdType::SOCKET), "socket");
  EXPECT_STREQ(toString(FdType::DEVICE), "device");
  EXPECT_STREQ(toString(FdType::EVENTFD), "eventfd");
  EXPECT_STREQ(toString(FdType::TIMERFD), "timerfd");
  EXPECT_STREQ(toString(FdType::SIGNALFD), "signalfd");
  EXPECT_STREQ(toString(FdType::EPOLL), "epoll");
  EXPECT_STREQ(toString(FdType::INOTIFY), "inotify");
  EXPECT_STREQ(toString(FdType::ANON_INODE), "anon_inode");
}

/** @test toString handles invalid FdType values. */
TEST(FdTypeTest, ToStringHandlesInvalid) {
  const auto INVALID = static_cast<FdType>(255);
  const char* RESULT = toString(INVALID);
  EXPECT_NE(RESULT, nullptr);
  EXPECT_GT(std::strlen(RESULT), 0U);
}

/** @test All FdType enum values are distinct. */
TEST(FdTypeTest, AllValuesDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(FdType::UNKNOWN));
  values.insert(static_cast<std::uint8_t>(FdType::REGULAR));
  values.insert(static_cast<std::uint8_t>(FdType::DIRECTORY));
  values.insert(static_cast<std::uint8_t>(FdType::PIPE));
  values.insert(static_cast<std::uint8_t>(FdType::SOCKET));
  values.insert(static_cast<std::uint8_t>(FdType::DEVICE));
  values.insert(static_cast<std::uint8_t>(FdType::EVENTFD));
  values.insert(static_cast<std::uint8_t>(FdType::TIMERFD));
  values.insert(static_cast<std::uint8_t>(FdType::SIGNALFD));
  values.insert(static_cast<std::uint8_t>(FdType::EPOLL));
  values.insert(static_cast<std::uint8_t>(FdType::INOTIFY));
  values.insert(static_cast<std::uint8_t>(FdType::ANON_INODE));
  EXPECT_EQ(values.size(), 12U);
}

/* ----------------------------- ProcessFdStatus Method Tests ----------------------------- */

/** @test available() calculates correctly. */
TEST(ProcessFdStatusMethodTest, AvailableCalculation) {
  ProcessFdStatus status{};
  status.softLimit = 1000;
  status.openCount = 100;
  EXPECT_EQ(status.available(), 900U);
}

/** @test available() returns zero when at limit. */
TEST(ProcessFdStatusMethodTest, AvailableWhenAtLimit) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 100;
  EXPECT_EQ(status.available(), 0U);
}

/** @test available() returns zero when over limit. */
TEST(ProcessFdStatusMethodTest, AvailableWhenOverLimit) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 150;
  EXPECT_EQ(status.available(), 0U);
}

/** @test utilizationPercent() calculates correctly. */
TEST(ProcessFdStatusMethodTest, UtilizationPercent) {
  ProcessFdStatus status{};
  status.softLimit = 1000;
  status.openCount = 500;
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 50.0);
}

/** @test utilizationPercent() returns zero when limit is zero. */
TEST(ProcessFdStatusMethodTest, UtilizationPercentZeroLimit) {
  ProcessFdStatus status{};
  status.softLimit = 0;
  status.openCount = 100;
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 0.0);
}

/** @test isCritical() returns true above 90%. */
TEST(ProcessFdStatusMethodTest, IsCriticalAbove90) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 91;
  EXPECT_TRUE(status.isCritical());
}

/** @test isCritical() returns false at 90%. */
TEST(ProcessFdStatusMethodTest, NotCriticalAt90) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 90;
  EXPECT_FALSE(status.isCritical());
}

/** @test isElevated() returns true above 75%. */
TEST(ProcessFdStatusMethodTest, IsElevatedAbove75) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 76;
  EXPECT_TRUE(status.isElevated());
}

/** @test isElevated() returns false at 75%. */
TEST(ProcessFdStatusMethodTest, NotElevatedAt75) {
  ProcessFdStatus status{};
  status.softLimit = 100;
  status.openCount = 75;
  EXPECT_FALSE(status.isElevated());
}

/** @test countByType() finds matching type. */
TEST(ProcessFdStatusMethodTest, CountByTypeFound) {
  ProcessFdStatus status{};
  status.byType[0].type = FdType::SOCKET;
  status.byType[0].count = 5;
  status.byType[1].type = FdType::PIPE;
  status.byType[1].count = 3;
  status.typeCount = 2;

  EXPECT_EQ(status.countByType(FdType::SOCKET), 5U);
  EXPECT_EQ(status.countByType(FdType::PIPE), 3U);
}

/** @test countByType() returns zero when not found. */
TEST(ProcessFdStatusMethodTest, CountByTypeNotFound) {
  ProcessFdStatus status{};
  status.byType[0].type = FdType::SOCKET;
  status.byType[0].count = 5;
  status.typeCount = 1;

  EXPECT_EQ(status.countByType(FdType::PIPE), 0U);
  EXPECT_EQ(status.countByType(FdType::REGULAR), 0U);
}

/* ----------------------------- SystemFdStatus Method Tests ----------------------------- */

/** @test available() calculates correctly. */
TEST(SystemFdStatusMethodTest, AvailableCalculation) {
  SystemFdStatus status{};
  status.maximum = 100000;
  status.allocated = 5000;
  EXPECT_EQ(status.available(), 95000U);
}

/** @test available() returns zero when full. */
TEST(SystemFdStatusMethodTest, AvailableWhenFull) {
  SystemFdStatus status{};
  status.maximum = 100;
  status.allocated = 100;
  EXPECT_EQ(status.available(), 0U);
}

/** @test utilizationPercent() calculates correctly. */
TEST(SystemFdStatusMethodTest, UtilizationPercent) {
  SystemFdStatus status{};
  status.maximum = 100000;
  status.allocated = 10000;
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 10.0);
}

/** @test utilizationPercent() returns zero when max is zero. */
TEST(SystemFdStatusMethodTest, UtilizationPercentZeroMax) {
  SystemFdStatus status{};
  status.maximum = 0;
  status.allocated = 100;
  EXPECT_DOUBLE_EQ(status.utilizationPercent(), 0.0);
}

/** @test isCritical() returns true above 90%. */
TEST(SystemFdStatusMethodTest, IsCriticalAbove90) {
  SystemFdStatus status{};
  status.maximum = 100;
  status.allocated = 91;
  EXPECT_TRUE(status.isCritical());
}

/* ----------------------------- FileDescriptorStatus Method Tests ----------------------------- */

/** @test anyCritical() detects process critical. */
TEST(FileDescriptorStatusMethodTest, AnyCriticalProcess) {
  FileDescriptorStatus status{};
  status.process.softLimit = 100;
  status.process.openCount = 95;
  status.system.maximum = 100000;
  status.system.allocated = 1000;
  EXPECT_TRUE(status.anyCritical());
}

/** @test anyCritical() detects system critical. */
TEST(FileDescriptorStatusMethodTest, AnyCriticalSystem) {
  FileDescriptorStatus status{};
  status.process.softLimit = 1000;
  status.process.openCount = 10;
  status.system.maximum = 100;
  status.system.allocated = 95;
  EXPECT_TRUE(status.anyCritical());
}

/** @test anyCritical() returns false when neither critical. */
TEST(FileDescriptorStatusMethodTest, NeitherCritical) {
  FileDescriptorStatus status{};
  status.process.softLimit = 1000;
  status.process.openCount = 10;
  status.system.maximum = 100000;
  status.system.allocated = 1000;
  EXPECT_FALSE(status.anyCritical());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getFdSoftLimit() returns positive value. */
TEST(FdStatusApiTest, GetFdSoftLimitReturnsPositive) {
  const std::uint64_t LIMIT = getFdSoftLimit();
  EXPECT_GT(LIMIT, 0U);
  EXPECT_GE(LIMIT, 64U);
}

/** @test getFdHardLimit() returns positive value. */
TEST(FdStatusApiTest, GetFdHardLimitReturnsPositive) {
  const std::uint64_t LIMIT = getFdHardLimit();
  EXPECT_GT(LIMIT, 0U);
}

/** @test Hard limit >= soft limit. */
TEST(FdStatusApiTest, HardLimitGeqSoftLimit) {
  const std::uint64_t SOFT = getFdSoftLimit();
  const std::uint64_t HARD = getFdHardLimit();
  EXPECT_GE(HARD, SOFT);
}

/** @test getOpenFdCount() returns at least 3 (stdin/stdout/stderr). */
TEST(FdStatusApiTest, GetOpenFdCountReturnsAtLeastThree) {
  const std::uint32_t COUNT = getOpenFdCount();
  EXPECT_GE(COUNT, 3U);
}

/** @test getProcessFdStatus() returns valid struct. */
TEST(FdStatusApiTest, GetProcessFdStatusReturnsValid) {
  const ProcessFdStatus STATUS = getProcessFdStatus();
  EXPECT_GE(STATUS.openCount, 3U);
  EXPECT_GT(STATUS.softLimit, 0U);
  EXPECT_GT(STATUS.hardLimit, 0U);
  EXPECT_GE(STATUS.hardLimit, STATUS.softLimit);
  EXPECT_LE(STATUS.openCount, STATUS.softLimit);
  EXPECT_GE(STATUS.highestFd, 2U);
}

/** @test getProcessFdStatus() has type breakdown. */
TEST(FdStatusApiTest, GetProcessFdStatusHasTypeInfo) {
  const ProcessFdStatus STATUS = getProcessFdStatus();
  EXPECT_GT(STATUS.typeCount, 0U);
  EXPECT_LE(STATUS.typeCount, MAX_FD_TYPES);

  std::uint32_t typeTotal = 0;
  for (std::size_t i = 0; i < STATUS.typeCount; ++i) {
    typeTotal += STATUS.byType[i].count;
  }
  // Type total should be close to open count (some FDs may be uncategorized)
  EXPECT_GE(typeTotal + 5, STATUS.openCount);
}

/** @test getSystemFdStatus() returns valid struct. */
TEST(FdStatusApiTest, GetSystemFdStatusReturnsValid) {
  const SystemFdStatus STATUS = getSystemFdStatus();
  EXPECT_GT(STATUS.allocated, 0U);
  EXPECT_GT(STATUS.maximum, 0U);
  EXPECT_LE(STATUS.allocated, STATUS.maximum);
  EXPECT_GT(STATUS.nrOpen, 0U);
}

/** @test getFileDescriptorStatus() returns valid struct. */
TEST_F(FileDescriptorStatusTest, ReturnsValidStruct) {
  EXPECT_GE(status_.process.openCount, 3U);
  EXPECT_GT(status_.process.softLimit, 0U);
  EXPECT_GT(status_.system.allocated, 0U);
  EXPECT_GT(status_.system.maximum, 0U);
}

/** @test getOpenFdCount() matches getProcessFdStatus().openCount. */
TEST(FdStatusApiTest, OpenFdCountMatchesProcessStatus) {
  const std::uint32_t QUICK_COUNT = getOpenFdCount();
  const ProcessFdStatus STATUS = getProcessFdStatus();

  // Allow small variance due to test harness
  const std::int32_t DIFF =
      static_cast<std::int32_t>(QUICK_COUNT) - static_cast<std::int32_t>(STATUS.openCount);
  EXPECT_LE(std::abs(DIFF), 5);
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test FD_PATH_SIZE is reasonable. */
TEST(FdStatusConstantsTest, FdPathSizeReasonable) {
  EXPECT_GE(FD_PATH_SIZE, 128U);
  EXPECT_LE(FD_PATH_SIZE, 1024U);
}

/** @test MAX_FD_TYPES is reasonable. */
TEST(FdStatusConstantsTest, MaxFdTypesReasonable) {
  EXPECT_GE(MAX_FD_TYPES, 8U);
  EXPECT_LE(MAX_FD_TYPES, 64U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test FdType toString returns non-null for all values. */
TEST(FdStatusToStringTest, FdTypeNotNull) {
  for (int i = 0; i < 16; ++i) {
    const char* RESULT = toString(static_cast<FdType>(i));
    EXPECT_NE(RESULT, nullptr);
  }
}

/** @test FdTypeCount::toString produces output. */
TEST(FdStatusToStringTest, FdTypeCountProducesOutput) {
  FdTypeCount count{};
  count.type = FdType::SOCKET;
  count.count = 5;
  const std::string OUTPUT = count.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("socket"), std::string::npos);
  EXPECT_NE(OUTPUT.find("5"), std::string::npos);
}

/** @test ProcessFdStatus::toString produces output. */
TEST(FdStatusToStringTest, ProcessFdStatusProducesOutput) {
  ProcessFdStatus status{};
  status.openCount = 100;
  status.softLimit = 1024;
  status.hardLimit = 65536;
  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("100"), std::string::npos);
  EXPECT_NE(OUTPUT.find("1024"), std::string::npos);
}

/** @test ProcessFdStatus::toString shows warning when critical. */
TEST(FdStatusToStringTest, ProcessFdStatusWarningWhenCritical) {
  ProcessFdStatus status{};
  status.openCount = 95;
  status.softLimit = 100;
  status.hardLimit = 100;
  const std::string OUTPUT = status.toString();
  EXPECT_NE(OUTPUT.find("WARNING"), std::string::npos);
}

/** @test SystemFdStatus::toString produces output. */
TEST(FdStatusToStringTest, SystemFdStatusProducesOutput) {
  SystemFdStatus status{};
  status.allocated = 5000;
  status.maximum = 100000;
  status.nrOpen = 1048576;
  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("5000"), std::string::npos);
  EXPECT_NE(OUTPUT.find("100000"), std::string::npos);
}

/** @test FileDescriptorStatus::toString produces output. */
TEST_F(FileDescriptorStatusTest, ToStringProducesOutput) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Process"), std::string::npos);
  EXPECT_NE(OUTPUT.find("System"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getFdSoftLimit() returns consistent results. */
TEST(FdStatusDeterminismTest, GetFdSoftLimitDeterministic) {
  const std::uint64_t FIRST = getFdSoftLimit();
  const std::uint64_t SECOND = getFdSoftLimit();
  EXPECT_EQ(FIRST, SECOND);
}

/** @test getFdHardLimit() returns consistent results. */
TEST(FdStatusDeterminismTest, GetFdHardLimitDeterministic) {
  const std::uint64_t FIRST = getFdHardLimit();
  const std::uint64_t SECOND = getFdHardLimit();
  EXPECT_EQ(FIRST, SECOND);
}

/** @test getSystemFdStatus() returns consistent structure. */
TEST(FdStatusDeterminismTest, GetSystemFdStatusDeterministic) {
  const SystemFdStatus FIRST = getSystemFdStatus();
  const SystemFdStatus SECOND = getSystemFdStatus();

  // Static limits should be identical
  EXPECT_EQ(FIRST.maximum, SECOND.maximum);
  EXPECT_EQ(FIRST.nrOpen, SECOND.nrOpen);

  // Dynamic values may vary slightly
  const std::int64_t DIFF =
      static_cast<std::int64_t>(FIRST.allocated) - static_cast<std::int64_t>(SECOND.allocated);
  EXPECT_LE(std::abs(DIFF), 100);
}

/** @test toString returns same pointer for same enum value. */
TEST(FdStatusDeterminismTest, ToStringEnumDeterministic) {
  const char* FIRST = toString(FdType::SOCKET);
  const char* SECOND = toString(FdType::SOCKET);
  EXPECT_EQ(FIRST, SECOND);
}