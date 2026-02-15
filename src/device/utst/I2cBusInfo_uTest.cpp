/**
 * @file I2cBusInfo_uTest.cpp
 * @brief Unit tests for seeker::device::I2cBusInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - I2C bus availability varies by hardware configuration.
 *  - Device scanning tests are conservative to avoid hardware disruption.
 *  - Permission-dependent tests may require elevated privileges.
 */

#include "src/device/inc/I2cBusInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::device::getAllI2cBuses;
using seeker::device::getI2cBusInfo;
using seeker::device::getI2cBusInfoByName;
using seeker::device::getI2cFunctionality;
using seeker::device::I2C_ADDR_MAX;
using seeker::device::I2C_ADDR_MIN;
using seeker::device::I2cBusInfo;
using seeker::device::I2cBusList;
using seeker::device::I2cDevice;
using seeker::device::I2cDeviceList;
using seeker::device::I2cFunctionality;
using seeker::device::MAX_I2C_BUSES;
using seeker::device::MAX_I2C_DEVICES;
using seeker::device::parseI2cBusNumber;
using seeker::device::probeI2cAddress;
using seeker::device::scanI2cBus;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default I2cFunctionality has no capabilities. */
TEST(I2cFunctionalityDefaultTest, DefaultNoCapabilities) {
  const I2cFunctionality DEFAULT{};

  EXPECT_FALSE(DEFAULT.i2c);
  EXPECT_FALSE(DEFAULT.tenBitAddr);
  EXPECT_FALSE(DEFAULT.smbusQuick);
  EXPECT_FALSE(DEFAULT.smbusByte);
  EXPECT_FALSE(DEFAULT.smbusWord);
  EXPECT_FALSE(DEFAULT.smbusBlock);
  EXPECT_FALSE(DEFAULT.smbusPec);
  EXPECT_FALSE(DEFAULT.hasBasicI2c());
  EXPECT_FALSE(DEFAULT.hasSmbus());
}

/** @test Default I2cDevice is invalid. */
TEST(I2cDeviceDefaultTest, DefaultInvalid) {
  const I2cDevice DEFAULT{};

  EXPECT_EQ(DEFAULT.address, 0U);
  EXPECT_FALSE(DEFAULT.responsive);
  EXPECT_FALSE(DEFAULT.isValid());
}

/** @test Default I2cDeviceList is empty. */
TEST(I2cDeviceListDefaultTest, DefaultEmpty) {
  const I2cDeviceList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_FALSE(DEFAULT.hasAddress(0x50));
}

/** @test Default I2cBusInfo is empty. */
TEST(I2cBusInfoDefaultTest, DefaultEmpty) {
  const I2cBusInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.busNumber, 0U);
  EXPECT_FALSE(DEFAULT.exists);
  EXPECT_FALSE(DEFAULT.accessible);
  EXPECT_FALSE(DEFAULT.isUsable());
}

/** @test Default I2cBusList is empty. */
TEST(I2cBusListDefaultTest, DefaultEmpty) {
  const I2cBusList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_EQ(DEFAULT.find("i2c-0"), nullptr);
  EXPECT_EQ(DEFAULT.findByNumber(0), nullptr);
}

/* ----------------------------- I2cFunctionality Methods ----------------------------- */

/** @test I2cFunctionality hasBasicI2c checks i2c flag. */
TEST(I2cFunctionalityMethodsTest, HasBasicI2c) {
  I2cFunctionality func{};

  EXPECT_FALSE(func.hasBasicI2c());

  func.i2c = true;
  EXPECT_TRUE(func.hasBasicI2c());
}

/** @test I2cFunctionality hasSmbus checks SMBus flags. */
TEST(I2cFunctionalityMethodsTest, HasSmbus) {
  I2cFunctionality func{};

  EXPECT_FALSE(func.hasSmbus());

  func.smbusQuick = true;
  EXPECT_TRUE(func.hasSmbus());

  func = I2cFunctionality{};
  func.smbusByte = true;
  EXPECT_TRUE(func.hasSmbus());

  func = I2cFunctionality{};
  func.smbusWord = true;
  EXPECT_TRUE(func.hasSmbus());

  func = I2cFunctionality{};
  func.smbusBlock = true;
  EXPECT_TRUE(func.hasSmbus());
}

/* ----------------------------- I2cDevice Methods ----------------------------- */

/** @test I2cDevice isValid checks address range and responsive. */
TEST(I2cDeviceMethodsTest, IsValidChecks) {
  I2cDevice dev{};

  // Default is invalid
  EXPECT_FALSE(dev.isValid());

  // Valid address but not responsive
  dev.address = 0x50;
  dev.responsive = false;
  EXPECT_FALSE(dev.isValid());

  // Valid and responsive
  dev.responsive = true;
  EXPECT_TRUE(dev.isValid());

  // Address below range
  dev.address = 0x02;
  EXPECT_FALSE(dev.isValid());

  // Address above range
  dev.address = 0x78;
  EXPECT_FALSE(dev.isValid());
}

/* ----------------------------- I2cDeviceList Methods ----------------------------- */

/** @test I2cDeviceList hasAddress finds devices. */
TEST(I2cDeviceListMethodsTest, HasAddressFinds) {
  I2cDeviceList list{};

  list.devices[0].address = 0x50;
  list.devices[0].responsive = true;
  list.devices[1].address = 0x51;
  list.devices[1].responsive = true;
  list.count = 2;

  EXPECT_TRUE(list.hasAddress(0x50));
  EXPECT_TRUE(list.hasAddress(0x51));
  EXPECT_FALSE(list.hasAddress(0x52));
}

/** @test I2cDeviceList addressList formats correctly. */
TEST(I2cDeviceListMethodsTest, AddressListFormats) {
  I2cDeviceList list{};

  // Empty list
  EXPECT_EQ(list.addressList(), "none");

  // Single device
  list.devices[0].address = 0x50;
  list.devices[0].responsive = true;
  list.count = 1;

  const std::string SINGLE = list.addressList();
  EXPECT_NE(SINGLE.find("0x50"), std::string::npos);

  // Multiple devices
  list.devices[1].address = 0x68;
  list.devices[1].responsive = true;
  list.count = 2;

  const std::string MULTI = list.addressList();
  EXPECT_NE(MULTI.find("0x50"), std::string::npos);
  EXPECT_NE(MULTI.find("0x68"), std::string::npos);
}

/* ----------------------------- I2cBusInfo Methods ----------------------------- */

/** @test I2cBusInfo isUsable checks all requirements. */
TEST(I2cBusInfoMethodsTest, IsUsableChecks) {
  I2cBusInfo info{};

  // Default is not usable
  EXPECT_FALSE(info.isUsable());

  // Exists but not accessible
  info.exists = true;
  EXPECT_FALSE(info.isUsable());

  // Accessible but no functionality
  info.accessible = true;
  EXPECT_FALSE(info.isUsable());

  // Has I2C functionality
  info.functionality.i2c = true;
  EXPECT_TRUE(info.isUsable());

  // Or has SMBus functionality
  info.functionality.i2c = false;
  info.functionality.smbusByte = true;
  EXPECT_TRUE(info.isUsable());
}

/** @test I2cBusInfo supports10BitAddr checks functionality. */
TEST(I2cBusInfoMethodsTest, Supports10BitAddr) {
  I2cBusInfo info{};

  EXPECT_FALSE(info.supports10BitAddr());

  info.functionality.tenBitAddr = true;
  EXPECT_TRUE(info.supports10BitAddr());
}

/** @test I2cBusInfo supportsSmbus checks functionality. */
TEST(I2cBusInfoMethodsTest, SupportsSmbus) {
  I2cBusInfo info{};

  EXPECT_FALSE(info.supportsSmbus());

  info.functionality.smbusQuick = true;
  EXPECT_TRUE(info.supportsSmbus());
}

/* ----------------------------- I2cBusList Methods ----------------------------- */

/** @test I2cBusList findByNumber locates buses. */
TEST(I2cBusListMethodsTest, FindByNumber) {
  I2cBusList list{};

  std::strcpy(list.buses[0].name.data(), "i2c-1");
  list.buses[0].busNumber = 1;
  std::strcpy(list.buses[1].name.data(), "i2c-2");
  list.buses[1].busNumber = 2;
  list.count = 2;

  EXPECT_NE(list.findByNumber(1), nullptr);
  EXPECT_NE(list.findByNumber(2), nullptr);
  EXPECT_EQ(list.findByNumber(3), nullptr);
}

/** @test I2cBusList find locates buses by name. */
TEST(I2cBusListMethodsTest, FindByName) {
  I2cBusList list{};

  std::strcpy(list.buses[0].name.data(), "i2c-1");
  list.count = 1;

  EXPECT_NE(list.find("i2c-1"), nullptr);
  EXPECT_EQ(list.find("i2c-2"), nullptr);
  EXPECT_EQ(list.find(nullptr), nullptr);
}

/** @test I2cBusList countAccessible counts correctly. */
TEST(I2cBusListMethodsTest, CountAccessible) {
  I2cBusList list{};

  list.buses[0].accessible = true;
  list.buses[1].accessible = true;
  list.buses[2].accessible = false;
  list.count = 3;

  EXPECT_EQ(list.countAccessible(), 2U);
}

/* ----------------------------- parseI2cBusNumber Tests ----------------------------- */

/** @test parseI2cBusNumber handles various formats. */
TEST(ParseI2cBusNumberTest, HandlesFormats) {
  std::uint32_t busNum = 0;

  // Plain number
  EXPECT_TRUE(parseI2cBusNumber("1", busNum));
  EXPECT_EQ(busNum, 1U);

  // i2c- prefix
  EXPECT_TRUE(parseI2cBusNumber("i2c-2", busNum));
  EXPECT_EQ(busNum, 2U);

  // /dev/ prefix
  EXPECT_TRUE(parseI2cBusNumber("/dev/i2c-3", busNum));
  EXPECT_EQ(busNum, 3U);

  // Larger number
  EXPECT_TRUE(parseI2cBusNumber("i2c-10", busNum));
  EXPECT_EQ(busNum, 10U);
}

/** @test parseI2cBusNumber rejects invalid input. */
TEST(ParseI2cBusNumberTest, RejectsInvalid) {
  std::uint32_t busNum = 0;

  EXPECT_FALSE(parseI2cBusNumber(nullptr, busNum));
  EXPECT_FALSE(parseI2cBusNumber("", busNum));
  EXPECT_FALSE(parseI2cBusNumber("abc", busNum));
  EXPECT_FALSE(parseI2cBusNumber("i2c-abc", busNum));
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test getI2cBusInfo handles nonexistent bus. */
TEST(I2cBusInfoErrorTest, NonexistentBus) {
  // Use a very high bus number unlikely to exist
  const I2cBusInfo INFO = getI2cBusInfo(999);

  EXPECT_FALSE(INFO.exists);
  EXPECT_FALSE(INFO.accessible);
  EXPECT_FALSE(INFO.isUsable());
}

/** @test getI2cBusInfoByName handles null/empty. */
TEST(I2cBusInfoErrorTest, NullEmptyName) {
  const I2cBusInfo INFO1 = getI2cBusInfoByName(nullptr);
  EXPECT_EQ(INFO1.name[0], '\0');

  const I2cBusInfo INFO2 = getI2cBusInfoByName("");
  EXPECT_EQ(INFO2.name[0], '\0');
}

/** @test getI2cFunctionality handles nonexistent bus. */
TEST(I2cBusInfoErrorTest, FunctionalityNonexistent) {
  const I2cFunctionality FUNC = getI2cFunctionality(999);

  EXPECT_FALSE(FUNC.hasBasicI2c());
  EXPECT_FALSE(FUNC.hasSmbus());
}

/** @test probeI2cAddress handles nonexistent bus. */
TEST(I2cBusInfoErrorTest, ProbeNonexistent) {
  const bool FOUND = probeI2cAddress(999, 0x50);
  EXPECT_FALSE(FOUND);
}

/** @test probeI2cAddress rejects reserved addresses. */
TEST(I2cBusInfoErrorTest, ProbeReservedAddresses) {
  // Reserved addresses should return false even if bus exists
  EXPECT_FALSE(probeI2cAddress(0, 0x00));
  EXPECT_FALSE(probeI2cAddress(0, 0x01));
  EXPECT_FALSE(probeI2cAddress(0, 0x02));
  EXPECT_FALSE(probeI2cAddress(0, 0x78));
  EXPECT_FALSE(probeI2cAddress(0, 0x7F));
}

/** @test scanI2cBus handles nonexistent bus. */
TEST(I2cBusInfoErrorTest, ScanNonexistent) {
  const I2cDeviceList LIST = scanI2cBus(999);

  EXPECT_TRUE(LIST.empty());
}

/* ----------------------------- Enumeration Tests ----------------------------- */

/** @test getAllI2cBuses returns list within bounds. */
TEST(I2cBusListTest, ListWithinBounds) {
  const I2cBusList LIST = getAllI2cBuses();

  EXPECT_LE(LIST.count, MAX_I2C_BUSES);
}

/** @test All entries in list have non-empty names. */
TEST(I2cBusListTest, AllEntriesHaveNames) {
  const I2cBusList LIST = getAllI2cBuses();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(std::strlen(LIST.buses[i].name.data()), 0U) << "Entry " << i << " has empty name";
  }
}

/** @test All entries have consistent bus numbers and paths. */
TEST(I2cBusListTest, ConsistentNumbersAndPaths) {
  const I2cBusList LIST = getAllI2cBuses();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const I2cBusInfo& BUS = LIST.buses[i];

    // Name should contain bus number
    char expectedName[32];
    std::snprintf(expectedName, sizeof(expectedName), "i2c-%u", BUS.busNumber);
    EXPECT_STREQ(BUS.name.data(), expectedName) << "Bus " << i << " has inconsistent name";

    // Device path should be /dev/i2c-N
    char expectedPath[64];
    std::snprintf(expectedPath, sizeof(expectedPath), "/dev/i2c-%u", BUS.busNumber);
    EXPECT_STREQ(BUS.devicePath.data(), expectedPath)
        << "Bus " << i << " has inconsistent device path";
  }
}

/** @test All existing entries have exists flag set. */
TEST(I2cBusListTest, ExistingEntriesHaveExistsFlag) {
  const I2cBusList LIST = getAllI2cBuses();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    // All enumerated buses should exist (that's how they were found)
    // Note: they may not be accessible due to permissions
    EXPECT_TRUE(LIST.buses[i].exists)
        << "Enumerated bus " << LIST.buses[i].name.data() << " should exist";
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test I2cFunctionality toString includes capabilities. */
TEST(I2cBusInfoToStringTest, FunctionalityIncludesCapabilities) {
  I2cFunctionality func{};
  func.i2c = true;
  func.smbusQuick = true;

  const std::string OUTPUT = func.toString();
  EXPECT_NE(OUTPUT.find("I2C"), std::string::npos);
  EXPECT_NE(OUTPUT.find("SMBus-quick"), std::string::npos);
}

/** @test I2cFunctionality toString handles no capabilities. */
TEST(I2cBusInfoToStringTest, FunctionalityNoCapabilities) {
  const I2cFunctionality FUNC{};
  const std::string OUTPUT = FUNC.toString();

  EXPECT_NE(OUTPUT.find("none"), std::string::npos);
}

/** @test I2cDeviceList toString handles empty. */
TEST(I2cBusInfoToStringTest, DeviceListEmpty) {
  const I2cDeviceList LIST{};
  const std::string OUTPUT = LIST.toString();

  EXPECT_NE(OUTPUT.find("No devices"), std::string::npos);
}

/** @test I2cDeviceList toString includes device count. */
TEST(I2cBusInfoToStringTest, DeviceListIncludesCount) {
  I2cDeviceList list{};
  list.devices[0].address = 0x50;
  list.devices[0].responsive = true;
  list.count = 1;

  const std::string OUTPUT = list.toString();
  EXPECT_NE(OUTPUT.find("1 device"), std::string::npos);
}

/** @test I2cBusInfo toString includes name. */
TEST(I2cBusInfoToStringTest, BusInfoIncludesName) {
  I2cBusInfo info{};
  std::strcpy(info.name.data(), "i2c-1");
  info.exists = true;
  info.accessible = true;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("i2c-1"), std::string::npos);
}

/** @test I2cBusInfo toString shows not found. */
TEST(I2cBusInfoToStringTest, BusInfoNotFound) {
  I2cBusInfo info{};
  std::strcpy(info.name.data(), "i2c-99");
  info.exists = false;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("not found"), std::string::npos);
}

/** @test I2cBusInfo toString shows no access. */
TEST(I2cBusInfoToStringTest, BusInfoNoAccess) {
  I2cBusInfo info{};
  std::strcpy(info.name.data(), "i2c-1");
  info.exists = true;
  info.accessible = false;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("no access"), std::string::npos);
}

/** @test I2cBusList toString handles empty. */
TEST(I2cBusInfoToStringTest, BusListEmpty) {
  const I2cBusList EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_NE(OUTPUT.find("No I2C buses"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getAllI2cBuses returns consistent count. */
TEST(I2cBusInfoDeterminismTest, ConsistentCount) {
  const I2cBusList LIST1 = getAllI2cBuses();
  const I2cBusList LIST2 = getAllI2cBuses();

  EXPECT_EQ(LIST1.count, LIST2.count);
}

/** @test getI2cBusInfo returns consistent results. */
TEST(I2cBusInfoDeterminismTest, ConsistentInfo) {
  const I2cBusInfo INFO1 = getI2cBusInfo(0);
  const I2cBusInfo INFO2 = getI2cBusInfo(0);

  EXPECT_STREQ(INFO1.name.data(), INFO2.name.data());
  EXPECT_EQ(INFO1.exists, INFO2.exists);
  EXPECT_EQ(INFO1.accessible, INFO2.accessible);
}

/* ----------------------------- Address Range Tests ----------------------------- */

/** @test Address constants are valid. */
TEST(I2cAddressConstantsTest, ValidRange) {
  // 7-bit I2C address range for general use
  EXPECT_EQ(I2C_ADDR_MIN, 0x03U);
  EXPECT_EQ(I2C_ADDR_MAX, 0x77U);

  // Should have room for most devices
  EXPECT_GT(I2C_ADDR_MAX - I2C_ADDR_MIN, 100U);
}

/** @test MAX_I2C_DEVICES accommodates full address range. */
TEST(I2cAddressConstantsTest, MaxDevicesAdequate) {
  // Should accommodate all possible 7-bit addresses
  EXPECT_GE(MAX_I2C_DEVICES, 128U);
}