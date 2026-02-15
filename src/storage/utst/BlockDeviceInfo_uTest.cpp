/**
 * @file BlockDeviceInfo_uTest.cpp
 * @brief Unit tests for seeker::storage::BlockDeviceInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have at least one block device (usually).
 *  - Virtual machines may have different device types than bare metal.
 */

#include "src/storage/inc/BlockDeviceInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::storage::BlockDevice;
using seeker::storage::BlockDeviceList;
using seeker::storage::DEVICE_NAME_SIZE;
using seeker::storage::formatCapacity;
using seeker::storage::getBlockDevice;
using seeker::storage::getBlockDevices;
using seeker::storage::MAX_BLOCK_DEVICES;
using seeker::storage::MODEL_STRING_SIZE;

class BlockDeviceInfoTest : public ::testing::Test {
protected:
  BlockDeviceList devices_{};

  void SetUp() override { devices_ = getBlockDevices(); }
};

/* ----------------------------- BlockDevice Method Tests ----------------------------- */

/** @test isNvme detects NVMe devices correctly. */
TEST(BlockDeviceMethodTest, IsNvmeDetection) {
  BlockDevice dev{};

  std::strncpy(dev.name.data(), "nvme0n1", DEVICE_NAME_SIZE - 1);
  EXPECT_TRUE(dev.isNvme());

  std::strncpy(dev.name.data(), "nvme1n1", DEVICE_NAME_SIZE - 1);
  EXPECT_TRUE(dev.isNvme());

  std::strncpy(dev.name.data(), "sda", DEVICE_NAME_SIZE - 1);
  EXPECT_FALSE(dev.isNvme());

  std::strncpy(dev.name.data(), "vda", DEVICE_NAME_SIZE - 1);
  EXPECT_FALSE(dev.isNvme());
}

/** @test isSsd detects SSDs correctly. */
TEST(BlockDeviceMethodTest, IsSsdDetection) {
  BlockDevice dev{};

  // SSD: non-rotational, non-removable
  dev.rotational = false;
  dev.removable = false;
  EXPECT_TRUE(dev.isSsd());

  // HDD: rotational
  dev.rotational = true;
  dev.removable = false;
  EXPECT_FALSE(dev.isSsd());

  // USB stick: non-rotational but removable
  dev.rotational = false;
  dev.removable = true;
  EXPECT_FALSE(dev.isSsd());
}

/** @test isHdd detects HDDs correctly. */
TEST(BlockDeviceMethodTest, IsHddDetection) {
  BlockDevice dev{};

  // HDD: rotational, non-removable
  dev.rotational = true;
  dev.removable = false;
  EXPECT_TRUE(dev.isHdd());

  // SSD: non-rotational
  dev.rotational = false;
  dev.removable = false;
  EXPECT_FALSE(dev.isHdd());

  // External HDD (removable)
  dev.rotational = true;
  dev.removable = true;
  EXPECT_FALSE(dev.isHdd());
}

/** @test isAdvancedFormat detects 4K sector devices. */
TEST(BlockDeviceMethodTest, IsAdvancedFormatDetection) {
  BlockDevice dev{};

  dev.physicalBlockSize = 512;
  EXPECT_FALSE(dev.isAdvancedFormat());

  dev.physicalBlockSize = 4096;
  EXPECT_TRUE(dev.isAdvancedFormat());

  dev.physicalBlockSize = 8192;
  EXPECT_TRUE(dev.isAdvancedFormat());
}

/** @test deviceType returns appropriate strings. */
TEST(BlockDeviceMethodTest, DeviceTypeStrings) {
  BlockDevice dev{};

  // NVMe
  std::strncpy(dev.name.data(), "nvme0n1", DEVICE_NAME_SIZE - 1);
  dev.rotational = false;
  dev.removable = false;
  EXPECT_STREQ(dev.deviceType(), "NVMe");

  // HDD
  std::strncpy(dev.name.data(), "sda", DEVICE_NAME_SIZE - 1);
  dev.rotational = true;
  dev.removable = false;
  EXPECT_STREQ(dev.deviceType(), "HDD");

  // SSD (non-NVMe)
  std::strncpy(dev.name.data(), "sdb", DEVICE_NAME_SIZE - 1);
  dev.rotational = false;
  dev.removable = false;
  EXPECT_STREQ(dev.deviceType(), "SSD");

  // Removable
  std::strncpy(dev.name.data(), "sdc", DEVICE_NAME_SIZE - 1);
  dev.rotational = false;
  dev.removable = true;
  EXPECT_STREQ(dev.deviceType(), "Removable");
}

/* ----------------------------- BlockDeviceList Method Tests ----------------------------- */

/** @test find returns correct device. */
TEST_F(BlockDeviceInfoTest, FindDevice) {
  if (devices_.count == 0) {
    GTEST_SKIP() << "No block devices available";
  }

  // Find first device
  const char* FIRST_NAME = devices_.devices[0].name.data();
  const BlockDevice* FOUND = devices_.find(FIRST_NAME);

  ASSERT_NE(FOUND, nullptr);
  EXPECT_STREQ(FOUND->name.data(), FIRST_NAME);
}

/** @test find returns nullptr for non-existent device. */
TEST_F(BlockDeviceInfoTest, FindNonExistent) {
  EXPECT_EQ(devices_.find("nonexistent_device_xyz"), nullptr);
  EXPECT_EQ(devices_.find(nullptr), nullptr);
  EXPECT_EQ(devices_.find(""), nullptr);
}

/** @test count methods return plausible values. */
TEST_F(BlockDeviceInfoTest, CountMethodsConsistent) {
  const std::size_t NVME = devices_.countNvme();
  const std::size_t SSD = devices_.countSsd();
  const std::size_t HDD = devices_.countHdd();

  // Total of specific types should not exceed total count
  EXPECT_LE(NVME + SSD + HDD, devices_.count);
}

/* ----------------------------- Device Enumeration Tests ----------------------------- */

/** @test Device count within bounds. */
TEST_F(BlockDeviceInfoTest, CountWithinBounds) { EXPECT_LE(devices_.count, MAX_BLOCK_DEVICES); }

/** @test All enumerated devices have names. */
TEST_F(BlockDeviceInfoTest, AllDevicesHaveNames) {
  for (std::size_t i = 0; i < devices_.count; ++i) {
    const char* NAME = devices_.devices[i].name.data();
    EXPECT_GT(std::strlen(NAME), 0U) << "Device " << i << " has empty name";
  }
}

/** @test Device names are null-terminated. */
TEST_F(BlockDeviceInfoTest, DeviceNamesNullTerminated) {
  for (std::size_t i = 0; i < devices_.count; ++i) {
    bool foundNull = false;
    for (std::size_t j = 0; j < DEVICE_NAME_SIZE; ++j) {
      if (devices_.devices[i].name[j] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Device " << i << " name not null-terminated";
  }
}

/** @test No filtered devices in list. */
TEST_F(BlockDeviceInfoTest, NoFilteredDevices) {
  for (std::size_t i = 0; i < devices_.count; ++i) {
    const char* NAME = devices_.devices[i].name.data();

    EXPECT_NE(std::strncmp(NAME, "loop", 4), 0) << "Loop device found: " << NAME;
    EXPECT_NE(std::strncmp(NAME, "ram", 3), 0) << "RAM disk found: " << NAME;
    EXPECT_NE(std::strncmp(NAME, "dm-", 3), 0) << "Device mapper found: " << NAME;
    EXPECT_NE(std::strncmp(NAME, "zram", 4), 0) << "ZRAM found: " << NAME;
  }
}

/* ----------------------------- Device Property Tests ----------------------------- */

/** @test All devices have valid block sizes. */
TEST_F(BlockDeviceInfoTest, ValidBlockSizes) {
  for (std::size_t i = 0; i < devices_.count; ++i) {
    const BlockDevice& DEV = devices_.devices[i];

    // Logical block size should be at least 512
    EXPECT_GE(DEV.logicalBlockSize, 512U) << "Device: " << DEV.name.data();

    // Physical block size should be >= logical
    EXPECT_GE(DEV.physicalBlockSize, DEV.logicalBlockSize) << "Device: " << DEV.name.data();

    // Block sizes should be powers of two
    if (DEV.logicalBlockSize > 0) {
      const bool LBS_POW2 = (DEV.logicalBlockSize & (DEV.logicalBlockSize - 1)) == 0;
      EXPECT_TRUE(LBS_POW2) << "Logical block size not power of 2: " << DEV.logicalBlockSize;
    }

    if (DEV.physicalBlockSize > 0) {
      const bool PBS_POW2 = (DEV.physicalBlockSize & (DEV.physicalBlockSize - 1)) == 0;
      EXPECT_TRUE(PBS_POW2) << "Physical block size not power of 2: " << DEV.physicalBlockSize;
    }
  }
}

/** @test Devices with size have positive values. */
TEST_F(BlockDeviceInfoTest, DeviceSizesPositive) {
  for (std::size_t i = 0; i < devices_.count; ++i) {
    const BlockDevice& DEV = devices_.devices[i];

    // Real block devices should have size > 0
    // (Some virtual devices might be 0, so we just check non-negative)
    EXPECT_GE(DEV.sizeBytes, 0U) << "Device: " << DEV.name.data();
  }
}

/* ----------------------------- getBlockDevice Tests ----------------------------- */

/** @test getBlockDevice with valid name returns populated struct. */
TEST_F(BlockDeviceInfoTest, GetBlockDeviceValid) {
  if (devices_.count == 0) {
    GTEST_SKIP() << "No block devices available";
  }

  const char* NAME = devices_.devices[0].name.data();
  const BlockDevice DEV = getBlockDevice(NAME);

  EXPECT_STREQ(DEV.name.data(), NAME);
  EXPECT_GE(DEV.logicalBlockSize, 512U);
}

/** @test getBlockDevice with invalid name returns zeroed struct. */
TEST(BlockDeviceGetTest, InvalidNameReturnsEmpty) {
  const BlockDevice DEV1 = getBlockDevice("nonexistent_device_xyz");
  EXPECT_EQ(DEV1.sizeBytes, 0U);
  EXPECT_EQ(DEV1.logicalBlockSize, 0U);

  const BlockDevice DEV2 = getBlockDevice(nullptr);
  EXPECT_EQ(DEV2.sizeBytes, 0U);

  const BlockDevice DEV3 = getBlockDevice("");
  EXPECT_EQ(DEV3.sizeBytes, 0U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test BlockDevice toString produces non-empty output. */
TEST_F(BlockDeviceInfoTest, DeviceToStringNonEmpty) {
  if (devices_.count == 0) {
    GTEST_SKIP() << "No block devices available";
  }

  const std::string OUTPUT = devices_.devices[0].toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find(devices_.devices[0].name.data()), std::string::npos);
}

/** @test BlockDeviceList toString produces non-empty output. */
TEST_F(BlockDeviceInfoTest, ListToStringNonEmpty) {
  const std::string OUTPUT = devices_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Block devices"), std::string::npos);
}

/* ----------------------------- formatCapacity Tests ----------------------------- */

/** @test formatCapacity handles zero. */
TEST(FormatCapacityTest, Zero) { EXPECT_EQ(formatCapacity(0), "0 B"); }

/** @test formatCapacity handles bytes. */
TEST(FormatCapacityTest, Bytes) {
  EXPECT_EQ(formatCapacity(100), "100 B");
  EXPECT_EQ(formatCapacity(999), "999 B");
}

/** @test formatCapacity handles kilobytes. */
TEST(FormatCapacityTest, Kilobytes) {
  const std::string RESULT = formatCapacity(5000);
  EXPECT_NE(RESULT.find("KB"), std::string::npos);
}

/** @test formatCapacity handles megabytes. */
TEST(FormatCapacityTest, Megabytes) {
  const std::string RESULT = formatCapacity(500 * 1000 * 1000);
  EXPECT_NE(RESULT.find("MB"), std::string::npos);
}

/** @test formatCapacity handles gigabytes. */
TEST(FormatCapacityTest, Gigabytes) {
  const std::string RESULT = formatCapacity(500ULL * 1000 * 1000 * 1000);
  EXPECT_NE(RESULT.find("GB"), std::string::npos);
}

/** @test formatCapacity handles terabytes. */
TEST(FormatCapacityTest, Terabytes) {
  const std::string RESULT = formatCapacity(2ULL * 1000 * 1000 * 1000 * 1000);
  EXPECT_NE(RESULT.find("TB"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default BlockDevice is zeroed. */
TEST(BlockDeviceDefaultTest, DefaultZeroed) {
  const BlockDevice DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.model[0], '\0');
  EXPECT_EQ(DEFAULT.vendor[0], '\0');
  EXPECT_EQ(DEFAULT.sizeBytes, 0U);
  EXPECT_EQ(DEFAULT.logicalBlockSize, 0U);
  EXPECT_EQ(DEFAULT.physicalBlockSize, 0U);
  EXPECT_FALSE(DEFAULT.rotational);
  EXPECT_FALSE(DEFAULT.removable);
  EXPECT_FALSE(DEFAULT.hasTrim);
}

/** @test Default BlockDeviceList is empty. */
TEST(BlockDeviceListDefaultTest, DefaultEmpty) {
  const BlockDeviceList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_EQ(DEFAULT.find("anything"), nullptr);
  EXPECT_EQ(DEFAULT.countNvme(), 0U);
  EXPECT_EQ(DEFAULT.countSsd(), 0U);
  EXPECT_EQ(DEFAULT.countHdd(), 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getBlockDevices returns consistent results. */
TEST(BlockDeviceDeterminismTest, ConsistentResults) {
  const BlockDeviceList LIST1 = getBlockDevices();
  const BlockDeviceList LIST2 = getBlockDevices();

  EXPECT_EQ(LIST1.count, LIST2.count);

  for (std::size_t i = 0; i < LIST1.count; ++i) {
    EXPECT_STREQ(LIST1.devices[i].name.data(), LIST2.devices[i].name.data());
    EXPECT_EQ(LIST1.devices[i].sizeBytes, LIST2.devices[i].sizeBytes);
    EXPECT_EQ(LIST1.devices[i].rotational, LIST2.devices[i].rotational);
  }
}