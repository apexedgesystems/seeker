/**
 * @file SpiBusInfo_uTest.cpp
 * @brief Unit tests for seeker::device::SpiBusInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - SPI device availability varies by hardware configuration.
 *  - Most systems will not have SPI devices unless embedded/dev board.
 *  - Permission-dependent tests may require elevated privileges.
 */

#include "src/device/inc/SpiBusInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::device::getAllSpiDevices;
using seeker::device::getSpiConfig;
using seeker::device::getSpiDeviceInfo;
using seeker::device::getSpiDeviceInfoByName;
using seeker::device::MAX_SPI_DEVICES;
using seeker::device::MAX_SPI_SPEED_HZ;
using seeker::device::parseSpiDeviceName;
using seeker::device::SpiConfig;
using seeker::device::spiDeviceExists;
using seeker::device::SpiDeviceInfo;
using seeker::device::SpiDeviceList;
using seeker::device::SpiMode;
using seeker::device::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SpiConfig is mode 0 with 8-bit word. */
TEST(SpiConfigDefaultTest, DefaultValues) {
  const SpiConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.mode, SpiMode::MODE_0);
  EXPECT_EQ(DEFAULT.bitsPerWord, 8U);
  EXPECT_EQ(DEFAULT.maxSpeedHz, 0U);
  EXPECT_FALSE(DEFAULT.lsbFirst);
  EXPECT_FALSE(DEFAULT.csHigh);
  EXPECT_FALSE(DEFAULT.threeWire);
  EXPECT_FALSE(DEFAULT.loopback);
  EXPECT_FALSE(DEFAULT.noCs);
  EXPECT_FALSE(DEFAULT.ready);
}

/** @test Default SpiDeviceInfo is empty. */
TEST(SpiDeviceInfoDefaultTest, DefaultEmpty) {
  const SpiDeviceInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.busNumber, 0U);
  EXPECT_EQ(DEFAULT.chipSelect, 0U);
  EXPECT_FALSE(DEFAULT.exists);
  EXPECT_FALSE(DEFAULT.accessible);
  EXPECT_FALSE(DEFAULT.isUsable());
}

/** @test Default SpiDeviceList is empty. */
TEST(SpiDeviceListDefaultTest, DefaultEmpty) {
  const SpiDeviceList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_EQ(DEFAULT.find("spidev0.0"), nullptr);
  EXPECT_EQ(DEFAULT.findByBusCs(0, 0), nullptr);
}

/* ----------------------------- SpiMode Tests ----------------------------- */

/** @test SpiMode toString returns expected strings. */
TEST(SpiModeTest, ToStringValues) {
  EXPECT_STREQ(toString(SpiMode::MODE_0), "mode0");
  EXPECT_STREQ(toString(SpiMode::MODE_1), "mode1");
  EXPECT_STREQ(toString(SpiMode::MODE_2), "mode2");
  EXPECT_STREQ(toString(SpiMode::MODE_3), "mode3");
}

/** @test SpiMode values match expected bit patterns. */
TEST(SpiModeTest, ValuePatterns) {
  EXPECT_EQ(static_cast<std::uint8_t>(SpiMode::MODE_0), 0U);
  EXPECT_EQ(static_cast<std::uint8_t>(SpiMode::MODE_1), 1U);
  EXPECT_EQ(static_cast<std::uint8_t>(SpiMode::MODE_2), 2U);
  EXPECT_EQ(static_cast<std::uint8_t>(SpiMode::MODE_3), 3U);
}

/* ----------------------------- SpiConfig Method Tests ----------------------------- */

/** @test SpiConfig isValid checks bits per word. */
TEST(SpiConfigMethodsTest, IsValidChecks) {
  SpiConfig cfg{};

  // Default (8-bit) is valid
  EXPECT_TRUE(cfg.isValid());

  // 1-bit is valid (rare but possible)
  cfg.bitsPerWord = 1;
  EXPECT_TRUE(cfg.isValid());

  // 32-bit is valid
  cfg.bitsPerWord = 32;
  EXPECT_TRUE(cfg.isValid());

  // 0-bit is invalid
  cfg.bitsPerWord = 0;
  EXPECT_FALSE(cfg.isValid());

  // 33-bit is invalid
  cfg.bitsPerWord = 33;
  EXPECT_FALSE(cfg.isValid());
}

/** @test SpiConfig cpol extracts clock polarity from mode. */
TEST(SpiConfigMethodsTest, CpolExtraction) {
  SpiConfig cfg{};

  cfg.mode = SpiMode::MODE_0;
  EXPECT_FALSE(cfg.cpol());

  cfg.mode = SpiMode::MODE_1;
  EXPECT_FALSE(cfg.cpol());

  cfg.mode = SpiMode::MODE_2;
  EXPECT_TRUE(cfg.cpol());

  cfg.mode = SpiMode::MODE_3;
  EXPECT_TRUE(cfg.cpol());
}

/** @test SpiConfig cpha extracts clock phase from mode. */
TEST(SpiConfigMethodsTest, CphaExtraction) {
  SpiConfig cfg{};

  cfg.mode = SpiMode::MODE_0;
  EXPECT_FALSE(cfg.cpha());

  cfg.mode = SpiMode::MODE_1;
  EXPECT_TRUE(cfg.cpha());

  cfg.mode = SpiMode::MODE_2;
  EXPECT_FALSE(cfg.cpha());

  cfg.mode = SpiMode::MODE_3;
  EXPECT_TRUE(cfg.cpha());
}

/** @test SpiConfig speedMHz converts correctly. */
TEST(SpiConfigMethodsTest, SpeedConversion) {
  SpiConfig cfg{};

  cfg.maxSpeedHz = 0;
  EXPECT_DOUBLE_EQ(cfg.speedMHz(), 0.0);

  cfg.maxSpeedHz = 1000000;
  EXPECT_DOUBLE_EQ(cfg.speedMHz(), 1.0);

  cfg.maxSpeedHz = 10000000;
  EXPECT_DOUBLE_EQ(cfg.speedMHz(), 10.0);

  cfg.maxSpeedHz = 500000;
  EXPECT_DOUBLE_EQ(cfg.speedMHz(), 0.5);
}

/* ----------------------------- SpiDeviceInfo Method Tests ----------------------------- */

/** @test SpiDeviceInfo isUsable checks all requirements. */
TEST(SpiDeviceInfoMethodsTest, IsUsableChecks) {
  SpiDeviceInfo info{};

  // Default is not usable
  EXPECT_FALSE(info.isUsable());

  // Exists but not accessible
  info.exists = true;
  EXPECT_FALSE(info.isUsable());

  // Accessible but config invalid
  info.accessible = true;
  info.config.bitsPerWord = 0;
  EXPECT_FALSE(info.isUsable());

  // All conditions met
  info.config.bitsPerWord = 8;
  EXPECT_TRUE(info.isUsable());
}

/* ----------------------------- SpiDeviceList Method Tests ----------------------------- */

/** @test SpiDeviceList find locates devices by name. */
TEST(SpiDeviceListMethodsTest, FindByName) {
  SpiDeviceList list{};

  std::strcpy(list.devices[0].name.data(), "spidev0.0");
  list.devices[0].busNumber = 0;
  list.devices[0].chipSelect = 0;
  std::strcpy(list.devices[1].name.data(), "spidev1.1");
  list.devices[1].busNumber = 1;
  list.devices[1].chipSelect = 1;
  list.count = 2;

  EXPECT_NE(list.find("spidev0.0"), nullptr);
  EXPECT_NE(list.find("spidev1.1"), nullptr);
  EXPECT_EQ(list.find("spidev2.0"), nullptr);
  EXPECT_EQ(list.find(nullptr), nullptr);
}

/** @test SpiDeviceList findByBusCs locates devices. */
TEST(SpiDeviceListMethodsTest, FindByBusCs) {
  SpiDeviceList list{};

  list.devices[0].busNumber = 0;
  list.devices[0].chipSelect = 0;
  list.devices[1].busNumber = 0;
  list.devices[1].chipSelect = 1;
  list.devices[2].busNumber = 1;
  list.devices[2].chipSelect = 0;
  list.count = 3;

  EXPECT_NE(list.findByBusCs(0, 0), nullptr);
  EXPECT_NE(list.findByBusCs(0, 1), nullptr);
  EXPECT_NE(list.findByBusCs(1, 0), nullptr);
  EXPECT_EQ(list.findByBusCs(1, 1), nullptr);
  EXPECT_EQ(list.findByBusCs(2, 0), nullptr);
}

/** @test SpiDeviceList countAccessible counts correctly. */
TEST(SpiDeviceListMethodsTest, CountAccessible) {
  SpiDeviceList list{};

  list.devices[0].accessible = true;
  list.devices[1].accessible = true;
  list.devices[2].accessible = false;
  list.count = 3;

  EXPECT_EQ(list.countAccessible(), 2U);
}

/** @test SpiDeviceList countUniqueBuses counts correctly. */
TEST(SpiDeviceListMethodsTest, CountUniqueBuses) {
  SpiDeviceList list{};

  // Same bus, different chip selects
  list.devices[0].busNumber = 0;
  list.devices[0].chipSelect = 0;
  list.devices[1].busNumber = 0;
  list.devices[1].chipSelect = 1;
  list.devices[2].busNumber = 1;
  list.devices[2].chipSelect = 0;
  list.count = 3;

  EXPECT_EQ(list.countUniqueBuses(), 2U);
}

/* ----------------------------- parseSpiDeviceName Tests ----------------------------- */

/** @test parseSpiDeviceName handles various formats. */
TEST(ParseSpiDeviceNameTest, HandlesFormats) {
  std::uint32_t bus = 0;
  std::uint32_t cs = 0;

  // Basic format
  EXPECT_TRUE(parseSpiDeviceName("spidev0.0", bus, cs));
  EXPECT_EQ(bus, 0U);
  EXPECT_EQ(cs, 0U);

  // With /dev/ prefix
  EXPECT_TRUE(parseSpiDeviceName("/dev/spidev1.2", bus, cs));
  EXPECT_EQ(bus, 1U);
  EXPECT_EQ(cs, 2U);

  // Just numbers
  EXPECT_TRUE(parseSpiDeviceName("2.3", bus, cs));
  EXPECT_EQ(bus, 2U);
  EXPECT_EQ(cs, 3U);

  // Larger numbers
  EXPECT_TRUE(parseSpiDeviceName("spidev10.5", bus, cs));
  EXPECT_EQ(bus, 10U);
  EXPECT_EQ(cs, 5U);
}

/** @test parseSpiDeviceName rejects invalid input. */
TEST(ParseSpiDeviceNameTest, RejectsInvalid) {
  std::uint32_t bus = 0;
  std::uint32_t cs = 0;

  EXPECT_FALSE(parseSpiDeviceName(nullptr, bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("", bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("spidev", bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("spidev0", bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("abc", bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("spidev.0", bus, cs));
  EXPECT_FALSE(parseSpiDeviceName("spidev0.", bus, cs));
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test getSpiDeviceInfo handles nonexistent device. */
TEST(SpiDeviceInfoErrorTest, NonexistentDevice) {
  // Use high bus/cs numbers unlikely to exist
  const SpiDeviceInfo INFO = getSpiDeviceInfo(99, 99);

  EXPECT_FALSE(INFO.exists);
  EXPECT_FALSE(INFO.accessible);
  EXPECT_FALSE(INFO.isUsable());
}

/** @test getSpiDeviceInfoByName handles null/empty. */
TEST(SpiDeviceInfoErrorTest, NullEmptyName) {
  const SpiDeviceInfo INFO1 = getSpiDeviceInfoByName(nullptr);
  EXPECT_EQ(INFO1.name[0], '\0');

  const SpiDeviceInfo INFO2 = getSpiDeviceInfoByName("");
  EXPECT_EQ(INFO2.name[0], '\0');
}

/** @test getSpiDeviceInfoByName handles invalid format. */
TEST(SpiDeviceInfoErrorTest, InvalidNameFormat) {
  const SpiDeviceInfo INFO = getSpiDeviceInfoByName("not-a-spi-device");
  EXPECT_EQ(INFO.name[0], '\0');
}

/** @test getSpiConfig handles nonexistent device. */
TEST(SpiDeviceInfoErrorTest, ConfigNonexistent) {
  const SpiConfig CFG = getSpiConfig(99, 99);

  // Should return default config (may or may not be valid depending on impl)
  EXPECT_EQ(CFG.bitsPerWord, 8U);
}

/** @test spiDeviceExists returns false for nonexistent. */
TEST(SpiDeviceInfoErrorTest, ExistsNonexistent) { EXPECT_FALSE(spiDeviceExists(99, 99)); }

/* ----------------------------- Enumeration Tests ----------------------------- */

/** @test getAllSpiDevices returns list within bounds. */
TEST(SpiDeviceListTest, ListWithinBounds) {
  const SpiDeviceList LIST = getAllSpiDevices();

  EXPECT_LE(LIST.count, MAX_SPI_DEVICES);
}

/** @test All entries in list have non-empty names. */
TEST(SpiDeviceListTest, AllEntriesHaveNames) {
  const SpiDeviceList LIST = getAllSpiDevices();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(std::strlen(LIST.devices[i].name.data()), 0U) << "Entry " << i << " has empty name";
  }
}

/** @test All entries have consistent bus/cs and names. */
TEST(SpiDeviceListTest, ConsistentNamesAndNumbers) {
  const SpiDeviceList LIST = getAllSpiDevices();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SpiDeviceInfo& DEV = LIST.devices[i];

    // Name should match spidevX.Y format
    char expectedName[32];
    std::snprintf(expectedName, sizeof(expectedName), "spidev%u.%u", DEV.busNumber, DEV.chipSelect);
    EXPECT_STREQ(DEV.name.data(), expectedName) << "Device " << i << " has inconsistent name";

    // Device path should be /dev/spidevX.Y
    char expectedPath[64];
    std::snprintf(expectedPath, sizeof(expectedPath), "/dev/spidev%u.%u", DEV.busNumber,
                  DEV.chipSelect);
    EXPECT_STREQ(DEV.devicePath.data(), expectedPath)
        << "Device " << i << " has inconsistent device path";
  }
}

/** @test All existing entries have exists flag set. */
TEST(SpiDeviceListTest, ExistingEntriesHaveExistsFlag) {
  const SpiDeviceList LIST = getAllSpiDevices();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    // All enumerated devices should exist
    EXPECT_TRUE(LIST.devices[i].exists)
        << "Enumerated device " << LIST.devices[i].name.data() << " should exist";
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test SpiConfig toString includes mode and bits. */
TEST(SpiBusInfoToStringTest, ConfigIncludesModeAndBits) {
  SpiConfig cfg{};
  cfg.mode = SpiMode::MODE_0;
  cfg.bitsPerWord = 8;
  cfg.maxSpeedHz = 10000000;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("mode0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("8-bit"), std::string::npos);
  EXPECT_NE(OUTPUT.find("10.0 MHz"), std::string::npos);
}

/** @test SpiConfig toString includes flags. */
TEST(SpiBusInfoToStringTest, ConfigIncludesFlags) {
  SpiConfig cfg{};
  cfg.lsbFirst = true;
  cfg.csHigh = true;
  cfg.threeWire = true;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("LSB-first"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CS-high"), std::string::npos);
  EXPECT_NE(OUTPUT.find("3-wire"), std::string::npos);
}

/** @test SpiDeviceInfo toString shows not found. */
TEST(SpiBusInfoToStringTest, DeviceInfoNotFound) {
  SpiDeviceInfo info{};
  std::strcpy(info.name.data(), "spidev99.99");
  info.exists = false;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("not found"), std::string::npos);
}

/** @test SpiDeviceInfo toString shows no access. */
TEST(SpiBusInfoToStringTest, DeviceInfoNoAccess) {
  SpiDeviceInfo info{};
  std::strcpy(info.name.data(), "spidev0.0");
  info.exists = true;
  info.accessible = false;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("no access"), std::string::npos);
}

/** @test SpiDeviceInfo toString shows bus and cs. */
TEST(SpiBusInfoToStringTest, DeviceInfoShowsBusCs) {
  SpiDeviceInfo info{};
  std::strcpy(info.name.data(), "spidev0.1");
  info.busNumber = 0;
  info.chipSelect = 1;
  info.exists = true;
  info.accessible = true;
  info.config.bitsPerWord = 8;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("bus 0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("cs 1"), std::string::npos);
}

/** @test SpiDeviceList toString handles empty. */
TEST(SpiBusInfoToStringTest, DeviceListEmpty) {
  const SpiDeviceList EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_NE(OUTPUT.find("No SPI devices"), std::string::npos);
}

/** @test SpiDeviceList toString includes count. */
TEST(SpiBusInfoToStringTest, DeviceListIncludesCount) {
  SpiDeviceList list{};
  std::strcpy(list.devices[0].name.data(), "spidev0.0");
  list.devices[0].exists = true;
  list.devices[0].accessible = true;
  list.count = 1;

  const std::string OUTPUT = list.toString();
  EXPECT_NE(OUTPUT.find("1 found"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getAllSpiDevices returns consistent count. */
TEST(SpiBusInfoDeterminismTest, ConsistentCount) {
  const SpiDeviceList LIST1 = getAllSpiDevices();
  const SpiDeviceList LIST2 = getAllSpiDevices();

  EXPECT_EQ(LIST1.count, LIST2.count);
}

/** @test getSpiDeviceInfo returns consistent results. */
TEST(SpiBusInfoDeterminismTest, ConsistentInfo) {
  const SpiDeviceInfo INFO1 = getSpiDeviceInfo(0, 0);
  const SpiDeviceInfo INFO2 = getSpiDeviceInfo(0, 0);

  EXPECT_STREQ(INFO1.name.data(), INFO2.name.data());
  EXPECT_EQ(INFO1.exists, INFO2.exists);
  EXPECT_EQ(INFO1.accessible, INFO2.accessible);
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test MAX_SPI_SPEED_HZ is reasonable. */
TEST(SpiBusInfoConstantsTest, MaxSpeedReasonable) {
  // 100 MHz is typical max for SPI
  EXPECT_EQ(MAX_SPI_SPEED_HZ, 100000000U);
}

/** @test MAX_SPI_DEVICES is adequate. */
TEST(SpiBusInfoConstantsTest, MaxDevicesAdequate) {
  // Should handle typical embedded systems
  EXPECT_GE(MAX_SPI_DEVICES, 16U);
}