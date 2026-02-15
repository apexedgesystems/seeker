/**
 * @file CapabilityStatus_uTest.cpp
 * @brief Unit tests for seeker::system::CapabilityStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Actual capabilities vary by user privileges and binary caps.
 */

#include "src/system/inc/CapabilityStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::CAP_IPC_LOCK_BIT;
using seeker::system::CAP_NET_ADMIN_BIT;
using seeker::system::CAP_NET_RAW_BIT;
using seeker::system::CAP_SYS_ADMIN_BIT;
using seeker::system::CAP_SYS_NICE_BIT;
using seeker::system::CAP_SYS_RAWIO_BIT;
using seeker::system::CAP_SYS_RESOURCE_BIT;
using seeker::system::capabilityName;
using seeker::system::CapabilityStatus;
using seeker::system::getCapabilityStatus;
using seeker::system::hasCapability;
using seeker::system::isRunningAsRoot;

class CapabilityStatusTest : public ::testing::Test {
protected:
  CapabilityStatus status_{};

  void SetUp() override { status_ = getCapabilityStatus(); }
};

/* ----------------------------- Basic Query Tests ----------------------------- */

/** @test getCapabilityStatus doesn't crash. */
TEST_F(CapabilityStatusTest, QueryDoesNotCrash) {
  // Just verify we got here without crashing
  SUCCEED();
}

/** @test isRoot is consistent with geteuid check. */
TEST_F(CapabilityStatusTest, IsRootConsistent) {
  const bool EXPECTED_ROOT = isRunningAsRoot();
  EXPECT_EQ(status_.isRoot, EXPECTED_ROOT);
}

/* ----------------------------- Capability Consistency Tests ----------------------------- */

/** @test If root, all RT capabilities should be set. */
TEST_F(CapabilityStatusTest, RootHasAllRtCaps) {
  if (status_.isRoot) {
    EXPECT_TRUE(status_.sysNice);
    EXPECT_TRUE(status_.ipcLock);
    EXPECT_TRUE(status_.sysRawio);
    EXPECT_TRUE(status_.sysResource);
    EXPECT_TRUE(status_.sysAdmin);
  }
}

/** @test hasCapability is consistent with struct fields for CAP_SYS_NICE. */
TEST_F(CapabilityStatusTest, HasCapabilitySysNiceConsistent) {
  const bool HAS_CAP = status_.hasCapability(CAP_SYS_NICE_BIT) || status_.isRoot;
  EXPECT_EQ(status_.sysNice, HAS_CAP);
}

/** @test hasCapability is consistent with struct fields for CAP_IPC_LOCK. */
TEST_F(CapabilityStatusTest, HasCapabilityIpcLockConsistent) {
  const bool HAS_CAP = status_.hasCapability(CAP_IPC_LOCK_BIT) || status_.isRoot;
  EXPECT_EQ(status_.ipcLock, HAS_CAP);
}

/** @test hasCapability is consistent with struct fields for CAP_SYS_ADMIN. */
TEST_F(CapabilityStatusTest, HasCapabilitySysAdminConsistent) {
  const bool HAS_CAP = status_.hasCapability(CAP_SYS_ADMIN_BIT) || status_.isRoot;
  EXPECT_EQ(status_.sysAdmin, HAS_CAP);
}

/* ----------------------------- Convenience Method Tests ----------------------------- */

/** @test canUseRtScheduling is consistent with sysNice and isRoot. */
TEST_F(CapabilityStatusTest, CanUseRtSchedulingConsistent) {
  const bool EXPECTED = status_.isRoot || status_.sysNice;
  EXPECT_EQ(status_.canUseRtScheduling(), EXPECTED);
}

/** @test canLockMemory is consistent with ipcLock and isRoot. */
TEST_F(CapabilityStatusTest, CanLockMemoryConsistent) {
  const bool EXPECTED = status_.isRoot || status_.ipcLock;
  EXPECT_EQ(status_.canLockMemory(), EXPECTED);
}

/** @test isPrivileged is consistent with sysAdmin and isRoot. */
TEST_F(CapabilityStatusTest, IsPrivilegedConsistent) {
  const bool EXPECTED = status_.isRoot || status_.sysAdmin;
  EXPECT_EQ(status_.isPrivileged(), EXPECTED);
}

/* ----------------------------- Raw Mask Tests ----------------------------- */

/** @test Effective mask contains sysNice bit when sysNice is true (non-root). */
TEST_F(CapabilityStatusTest, EffectiveMaskContainsSysNice) {
  if (!status_.isRoot && status_.sysNice) {
    const bool BIT_SET = (status_.effective & (1ULL << CAP_SYS_NICE_BIT)) != 0;
    EXPECT_TRUE(BIT_SET);
  }
}

/** @test Effective mask contains ipcLock bit when ipcLock is true (non-root). */
TEST_F(CapabilityStatusTest, EffectiveMaskContainsIpcLock) {
  if (!status_.isRoot && status_.ipcLock) {
    const bool BIT_SET = (status_.effective & (1ULL << CAP_IPC_LOCK_BIT)) != 0;
    EXPECT_TRUE(BIT_SET);
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(CapabilityStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(CapabilityStatusTest, ToStringContainsSections) {
  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("Capability Status"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CAP_SYS_NICE"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CAP_IPC_LOCK"), std::string::npos);
  EXPECT_NE(OUTPUT.find("root"), std::string::npos);
}

/** @test toRtSummary produces non-empty output. */
TEST_F(CapabilityStatusTest, ToRtSummaryNonEmpty) {
  const std::string OUTPUT = status_.toRtSummary();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("RT"), std::string::npos);
}

/** @test toRtSummary contains recommendation for unprivileged. */
TEST_F(CapabilityStatusTest, ToRtSummaryRecommendation) {
  if (!status_.canUseRtScheduling() || !status_.canLockMemory()) {
    const std::string OUTPUT = status_.toRtSummary();
    EXPECT_NE(OUTPUT.find("Recommendation"), std::string::npos);
  }
}

/* ----------------------------- hasCapability Tests ----------------------------- */

/** @test hasCapability handles invalid bit positions. */
TEST(HasCapabilityTest, InvalidBitPositions) {
  // Negative bit should return false (unless root)
  if (!isRunningAsRoot()) {
    EXPECT_FALSE(hasCapability(-1));
    EXPECT_FALSE(hasCapability(64)); // Beyond valid range
    EXPECT_FALSE(hasCapability(100));
  }
}

/** @test hasCapability returns consistent results. */
TEST(HasCapabilityTest, ConsistentResults) {
  const bool HAS_NICE1 = hasCapability(CAP_SYS_NICE_BIT);
  const bool HAS_NICE2 = hasCapability(CAP_SYS_NICE_BIT);
  EXPECT_EQ(HAS_NICE1, HAS_NICE2);
}

/* ----------------------------- capabilityName Tests ----------------------------- */

/** @test capabilityName returns known capability names. */
TEST(CapabilityNameTest, KnownCapabilities) {
  EXPECT_STREQ(capabilityName(CAP_SYS_NICE_BIT), "CAP_SYS_NICE");
  EXPECT_STREQ(capabilityName(CAP_IPC_LOCK_BIT), "CAP_IPC_LOCK");
  EXPECT_STREQ(capabilityName(CAP_SYS_ADMIN_BIT), "CAP_SYS_ADMIN");
  EXPECT_STREQ(capabilityName(CAP_NET_ADMIN_BIT), "CAP_NET_ADMIN");
  EXPECT_STREQ(capabilityName(CAP_NET_RAW_BIT), "CAP_NET_RAW");
  EXPECT_STREQ(capabilityName(CAP_SYS_RAWIO_BIT), "CAP_SYS_RAWIO");
  EXPECT_STREQ(capabilityName(CAP_SYS_RESOURCE_BIT), "CAP_SYS_RESOURCE");
}

/** @test capabilityName returns CAP_UNKNOWN for invalid bits. */
TEST(CapabilityNameTest, UnknownCapabilities) {
  EXPECT_STREQ(capabilityName(-1), "CAP_UNKNOWN");
  EXPECT_STREQ(capabilityName(64), "CAP_UNKNOWN");
  EXPECT_STREQ(capabilityName(100), "CAP_UNKNOWN");
}

/** @test capabilityName returns non-null for all valid bits. */
TEST(CapabilityNameTest, AllValidBitsNonNull) {
  for (int i = 0; i <= 40; ++i) {
    const char* NAME = capabilityName(i);
    EXPECT_NE(NAME, nullptr);
    EXPECT_GT(std::strlen(NAME), 0U);
  }
}

/* ----------------------------- isRunningAsRoot Tests ----------------------------- */

/** @test isRunningAsRoot returns consistent results. */
TEST(IsRunningAsRootTest, ConsistentResults) {
  const bool ROOT1 = isRunningAsRoot();
  const bool ROOT2 = isRunningAsRoot();
  EXPECT_EQ(ROOT1, ROOT2);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default CapabilityStatus is zeroed. */
TEST(CapabilityStatusDefaultTest, DefaultZeroed) {
  const CapabilityStatus DEFAULT{};

  EXPECT_FALSE(DEFAULT.sysNice);
  EXPECT_FALSE(DEFAULT.ipcLock);
  EXPECT_FALSE(DEFAULT.sysRawio);
  EXPECT_FALSE(DEFAULT.sysResource);
  EXPECT_FALSE(DEFAULT.sysAdmin);
  EXPECT_FALSE(DEFAULT.isRoot);
  EXPECT_EQ(DEFAULT.effective, 0U);
  EXPECT_EQ(DEFAULT.permitted, 0U);
  EXPECT_EQ(DEFAULT.inheritable, 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getCapabilityStatus returns consistent results. */
TEST(CapabilityStatusDeterminismTest, ConsistentResults) {
  const CapabilityStatus S1 = getCapabilityStatus();
  const CapabilityStatus S2 = getCapabilityStatus();

  // All values should be identical (capabilities don't change mid-process normally)
  EXPECT_EQ(S1.isRoot, S2.isRoot);
  EXPECT_EQ(S1.sysNice, S2.sysNice);
  EXPECT_EQ(S1.ipcLock, S2.ipcLock);
  EXPECT_EQ(S1.sysAdmin, S2.sysAdmin);
  EXPECT_EQ(S1.effective, S2.effective);
  EXPECT_EQ(S1.permitted, S2.permitted);
}