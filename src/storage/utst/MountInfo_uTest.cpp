/**
 * @file MountInfo_uTest.cpp
 * @brief Unit tests for seeker::storage::MountInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have at least root (/) mounted.
 *  - Pseudo-filesystems (proc, sys) should always be present.
 */

#include "src/storage/inc/MountInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::storage::FSTYPE_SIZE;
using seeker::storage::getMountForPath;
using seeker::storage::getMountTable;
using seeker::storage::MAX_MOUNTS;
using seeker::storage::MOUNT_OPTIONS_SIZE;
using seeker::storage::MountEntry;
using seeker::storage::MountTable;
using seeker::storage::PATH_SIZE;

class MountInfoTest : public ::testing::Test {
protected:
  MountTable table_{};

  void SetUp() override { table_ = getMountTable(); }
};

/* ----------------------------- MountEntry Option Tests ----------------------------- */

/** @test isReadOnly detects ro option correctly. */
TEST(MountEntryOptionTest, IsReadOnlyDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "ro,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.isReadOnly());

  std::strncpy(entry.options.data(), "rw,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.isReadOnly());

  // Make sure "ro" inside another option doesn't match
  std::strncpy(entry.options.data(), "rw,errors=remount-ro", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.isReadOnly());
}

/** @test hasNoAtime detects noatime option. */
TEST(MountEntryOptionTest, HasNoAtimeDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "rw,noatime,nodiratime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.hasNoAtime());

  std::strncpy(entry.options.data(), "rw,relatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.hasNoAtime());
}

/** @test hasNoDirAtime detects nodiratime option. */
TEST(MountEntryOptionTest, HasNoDirAtimeDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "rw,nodiratime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.hasNoDirAtime());

  std::strncpy(entry.options.data(), "rw,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.hasNoDirAtime());
}

/** @test hasRelAtime detects relatime option. */
TEST(MountEntryOptionTest, HasRelAtimeDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "rw,relatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.hasRelAtime());

  std::strncpy(entry.options.data(), "rw,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.hasRelAtime());
}

/** @test hasNoBarrier detects various no-barrier options. */
TEST(MountEntryOptionTest, HasNoBarrierDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "rw,nobarrier", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.hasNoBarrier());

  std::strncpy(entry.options.data(), "rw,barrier=0", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.hasNoBarrier());

  std::strncpy(entry.options.data(), "rw,barrier=1", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.hasNoBarrier());

  std::strncpy(entry.options.data(), "rw,relatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.hasNoBarrier());
}

/** @test isSync detects sync option. */
TEST(MountEntryOptionTest, IsSyncDetection) {
  MountEntry entry{};

  std::strncpy(entry.options.data(), "rw,sync", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_TRUE(entry.isSync());

  std::strncpy(entry.options.data(), "rw,async", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_FALSE(entry.isSync());
}

/* ----------------------------- MountEntry Type Tests ----------------------------- */

/** @test isBlockDevice detects /dev/ devices. */
TEST(MountEntryTypeTest, IsBlockDeviceDetection) {
  MountEntry entry{};

  std::strncpy(entry.device.data(), "/dev/sda1", PATH_SIZE - 1);
  EXPECT_TRUE(entry.isBlockDevice());

  std::strncpy(entry.device.data(), "/dev/nvme0n1p2", PATH_SIZE - 1);
  EXPECT_TRUE(entry.isBlockDevice());

  std::strncpy(entry.device.data(), "proc", PATH_SIZE - 1);
  EXPECT_FALSE(entry.isBlockDevice());

  std::strncpy(entry.device.data(), "tmpfs", PATH_SIZE - 1);
  EXPECT_FALSE(entry.isBlockDevice());
}

/** @test isNetworkFs detects network filesystems. */
TEST(MountEntryTypeTest, IsNetworkFsDetection) {
  MountEntry entry{};

  std::strncpy(entry.fsType.data(), "nfs", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isNetworkFs());

  std::strncpy(entry.fsType.data(), "nfs4", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isNetworkFs());

  std::strncpy(entry.fsType.data(), "cifs", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isNetworkFs());

  std::strncpy(entry.fsType.data(), "ext4", FSTYPE_SIZE - 1);
  EXPECT_FALSE(entry.isNetworkFs());

  std::strncpy(entry.fsType.data(), "xfs", FSTYPE_SIZE - 1);
  EXPECT_FALSE(entry.isNetworkFs());
}

/** @test isTmpFs detects tmpfs/ramfs. */
TEST(MountEntryTypeTest, IsTmpFsDetection) {
  MountEntry entry{};

  std::strncpy(entry.fsType.data(), "tmpfs", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isTmpFs());

  std::strncpy(entry.fsType.data(), "ramfs", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isTmpFs());

  std::strncpy(entry.fsType.data(), "devtmpfs", FSTYPE_SIZE - 1);
  EXPECT_TRUE(entry.isTmpFs());

  std::strncpy(entry.fsType.data(), "ext4", FSTYPE_SIZE - 1);
  EXPECT_FALSE(entry.isTmpFs());
}

/** @test ext4DataMode extracts data mode option. */
TEST(MountEntryTypeTest, Ext4DataModeExtraction) {
  MountEntry entry{};

  // ext4 with explicit data mode
  std::strncpy(entry.fsType.data(), "ext4", FSTYPE_SIZE - 1);
  std::strncpy(entry.options.data(), "rw,data=journal,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_STREQ(entry.ext4DataMode(), "journal");

  std::strncpy(entry.options.data(), "rw,data=writeback", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_STREQ(entry.ext4DataMode(), "writeback");

  // ext4 without explicit data mode -> default is "ordered"
  std::strncpy(entry.options.data(), "rw,noatime", MOUNT_OPTIONS_SIZE - 1);
  EXPECT_STREQ(entry.ext4DataMode(), "ordered");

  // Non-ext4 filesystem
  std::strncpy(entry.fsType.data(), "xfs", FSTYPE_SIZE - 1);
  EXPECT_STREQ(entry.ext4DataMode(), "");
}

/* ----------------------------- MountTable Search Tests ----------------------------- */

/** @test Root mount is always present. */
TEST_F(MountInfoTest, RootMountPresent) {
  const MountEntry* ROOT = table_.findByMountPoint("/");
  ASSERT_NE(ROOT, nullptr);
  EXPECT_STREQ(ROOT->mountPoint.data(), "/");
}

/** @test findByMountPoint finds exact matches. */
TEST_F(MountInfoTest, FindByMountPointExact) {
  // / should always exist
  EXPECT_NE(table_.findByMountPoint("/"), nullptr);

  // Non-existent mount point
  EXPECT_EQ(table_.findByMountPoint("/nonexistent_mount_xyz"), nullptr);
}

/** @test findForPath finds containing mount. */
TEST_F(MountInfoTest, FindForPathContaining) {
  // Any path should resolve to some mount (at least /)
  const MountEntry* MOUNT = table_.findForPath("/usr/bin/ls");
  ASSERT_NE(MOUNT, nullptr);

  // The mount point should be a prefix of the path
  const char* MP = MOUNT->mountPoint.data();
  EXPECT_EQ(std::strncmp("/usr/bin/ls", MP, std::strlen(MP)), 0);
}

/** @test findForPath returns root for paths not under any other mount. */
TEST_F(MountInfoTest, FindForPathFallsBackToRoot) {
  // A random deep path should at least match root
  const MountEntry* MOUNT = table_.findForPath("/some/random/deep/path");
  ASSERT_NE(MOUNT, nullptr);
  // Should be / or something more specific
  EXPECT_GT(std::strlen(MOUNT->mountPoint.data()), 0U);
}

/** @test findByDevice finds by device path. */
TEST_F(MountInfoTest, FindByDeviceWithPath) {
  // Find root's device
  const MountEntry* ROOT = table_.findByMountPoint("/");
  if (ROOT != nullptr && ROOT->isBlockDevice()) {
    const MountEntry* BY_DEV = table_.findByDevice(ROOT->device.data());
    EXPECT_NE(BY_DEV, nullptr);
  }
}

/* ----------------------------- MountTable Enumeration Tests ----------------------------- */

/** @test Mount count is within bounds. */
TEST_F(MountInfoTest, CountWithinBounds) {
  EXPECT_GT(table_.count, 0U); // At least root
  EXPECT_LE(table_.count, MAX_MOUNTS);
}

/** @test All mounts have mount points. */
TEST_F(MountInfoTest, AllMountsHaveMountPoints) {
  for (std::size_t i = 0; i < table_.count; ++i) {
    const char* MP = table_.mounts[i].mountPoint.data();
    EXPECT_GT(std::strlen(MP), 0U) << "Mount " << i << " has empty mount point";
  }
}

/** @test All mounts have filesystem types. */
TEST_F(MountInfoTest, AllMountsHaveFsTypes) {
  for (std::size_t i = 0; i < table_.count; ++i) {
    const char* FS = table_.mounts[i].fsType.data();
    EXPECT_GT(std::strlen(FS), 0U) << "Mount " << i << " has empty fstype";
  }
}

/** @test Mount points are null-terminated. */
TEST_F(MountInfoTest, MountPointsNullTerminated) {
  for (std::size_t i = 0; i < table_.count; ++i) {
    bool foundNull = false;
    for (std::size_t j = 0; j < PATH_SIZE; ++j) {
      if (table_.mounts[i].mountPoint[j] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Mount " << i << " mount point not null-terminated";
  }
}

/** @test countBlockDevices counts correctly. */
TEST_F(MountInfoTest, CountBlockDevicesConsistent) {
  std::size_t manual = 0;
  for (std::size_t i = 0; i < table_.count; ++i) {
    if (table_.mounts[i].isBlockDevice()) {
      ++manual;
    }
  }
  EXPECT_EQ(table_.countBlockDevices(), manual);
}

/* ----------------------------- getMountForPath Tests ----------------------------- */

/** @test getMountForPath returns root for /. */
TEST(GetMountForPathTest, RootPath) {
  const MountEntry ENTRY = getMountForPath("/");
  EXPECT_STREQ(ENTRY.mountPoint.data(), "/");
}

/** @test getMountForPath returns valid entry for /tmp. */
TEST(GetMountForPathTest, TmpPath) {
  const MountEntry ENTRY = getMountForPath("/tmp");
  EXPECT_GT(std::strlen(ENTRY.mountPoint.data()), 0U);
}

/** @test getMountForPath handles invalid input. */
TEST(GetMountForPathTest, InvalidInput) {
  const MountEntry ENTRY1 = getMountForPath(nullptr);
  EXPECT_EQ(ENTRY1.mountPoint[0], '\0');

  const MountEntry ENTRY2 = getMountForPath("");
  EXPECT_EQ(ENTRY2.mountPoint[0], '\0');
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test MountEntry toString produces non-empty output. */
TEST_F(MountInfoTest, EntryToStringNonEmpty) {
  if (table_.count > 0) {
    const std::string OUTPUT = table_.mounts[0].toString();
    EXPECT_FALSE(OUTPUT.empty());
  }
}

/** @test MountTable toString produces non-empty output. */
TEST_F(MountInfoTest, TableToStringNonEmpty) {
  const std::string OUTPUT = table_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Mount table"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default MountEntry is empty. */
TEST(MountEntryDefaultTest, DefaultEmpty) {
  const MountEntry DEFAULT{};

  EXPECT_EQ(DEFAULT.mountPoint[0], '\0');
  EXPECT_EQ(DEFAULT.device[0], '\0');
  EXPECT_EQ(DEFAULT.fsType[0], '\0');
  EXPECT_EQ(DEFAULT.options[0], '\0');
  EXPECT_FALSE(DEFAULT.isBlockDevice());
}

/** @test Default MountTable is empty. */
TEST(MountTableDefaultTest, DefaultEmpty) {
  const MountTable DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_EQ(DEFAULT.findByMountPoint("/"), nullptr);
  EXPECT_EQ(DEFAULT.countBlockDevices(), 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getMountTable returns consistent results. */
TEST(MountInfoDeterminismTest, ConsistentResults) {
  const MountTable TABLE1 = getMountTable();
  const MountTable TABLE2 = getMountTable();

  EXPECT_EQ(TABLE1.count, TABLE2.count);

  for (std::size_t i = 0; i < TABLE1.count; ++i) {
    EXPECT_STREQ(TABLE1.mounts[i].mountPoint.data(), TABLE2.mounts[i].mountPoint.data());
    EXPECT_STREQ(TABLE1.mounts[i].device.data(), TABLE2.mounts[i].device.data());
    EXPECT_STREQ(TABLE1.mounts[i].fsType.data(), TABLE2.mounts[i].fsType.data());
  }
}