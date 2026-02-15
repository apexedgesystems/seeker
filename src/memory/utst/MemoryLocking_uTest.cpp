/**
 * @file MemoryLocking_uTest.cpp
 * @brief Unit tests for seeker::memory::MemoryLocking.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Actual limits depend on system configuration and user privileges.
 */

#include "src/memory/inc/MemoryLocking.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::memory::getMemoryLockingStatus;
using seeker::memory::getMlockallStatus;
using seeker::memory::hasCapIpcLock;
using seeker::memory::MemoryLockingStatus;
using seeker::memory::MLOCK_UNLIMITED;
using seeker::memory::MlockallStatus;

class MemoryLockingTest : public ::testing::Test {
protected:
  MemoryLockingStatus status_{};

  void SetUp() override { status_ = getMemoryLockingStatus(); }
};

/* ----------------------------- MemoryLockingStatus Helper Tests ----------------------------- */

/** @test Default MemoryLockingStatus is zeroed. */
TEST(MemoryLockingStatusDefaultTest, DefaultZero) {
  const MemoryLockingStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.softLimitBytes, 0U);
  EXPECT_EQ(DEFAULT.hardLimitBytes, 0U);
  EXPECT_EQ(DEFAULT.currentLockedBytes, 0U);
  EXPECT_FALSE(DEFAULT.hasCapIpcLock);
  EXPECT_FALSE(DEFAULT.isRoot);
}

/** @test isUnlimited() with CAP_IPC_LOCK. */
TEST(MemoryLockingStatusHelperTest, IsUnlimitedWithCap) {
  MemoryLockingStatus status{};
  status.hasCapIpcLock = true;
  status.softLimitBytes = 0;

  EXPECT_TRUE(status.isUnlimited());
}

/** @test isUnlimited() as root. */
TEST(MemoryLockingStatusHelperTest, IsUnlimitedAsRoot) {
  MemoryLockingStatus status{};
  status.isRoot = true;
  status.softLimitBytes = 0;

  EXPECT_TRUE(status.isUnlimited());
}

/** @test isUnlimited() with unlimited soft limit. */
TEST(MemoryLockingStatusHelperTest, IsUnlimitedWithUnlimitedLimit) {
  MemoryLockingStatus status{};
  status.softLimitBytes = MLOCK_UNLIMITED;

  EXPECT_TRUE(status.isUnlimited());
}

/** @test isUnlimited() with finite limit and no privileges. */
TEST(MemoryLockingStatusHelperTest, NotUnlimitedNormal) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 65536;
  status.hasCapIpcLock = false;
  status.isRoot = false;

  EXPECT_FALSE(status.isUnlimited());
}

/** @test canLock() with unlimited status. */
TEST(MemoryLockingStatusHelperTest, CanLockUnlimited) {
  MemoryLockingStatus status{};
  status.hasCapIpcLock = true;

  EXPECT_TRUE(status.canLock(1ULL * 1024 * 1024 * 1024));   // 1 GiB
  EXPECT_TRUE(status.canLock(100ULL * 1024 * 1024 * 1024)); // 100 GiB
}

/** @test canLock() within soft limit. */
TEST(MemoryLockingStatusHelperTest, CanLockWithinLimit) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 1024 * 1024;    // 1 MiB
  status.currentLockedBytes = 256 * 1024; // 256 KiB already locked

  EXPECT_TRUE(status.canLock(512 * 1024));     // 512 KiB more
  EXPECT_TRUE(status.canLock(768 * 1024 - 1)); // Just under remaining
  EXPECT_FALSE(status.canLock(1024 * 1024));   // Would exceed
}

/** @test canLock() exceeds soft limit. */
TEST(MemoryLockingStatusHelperTest, CanLockExceedsLimit) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 65536;
  status.currentLockedBytes = 0;

  EXPECT_FALSE(status.canLock(65536 + 1));
  EXPECT_FALSE(status.canLock(1024 * 1024));
}

/** @test availableBytes() with unlimited status. */
TEST(MemoryLockingStatusHelperTest, AvailableBytesUnlimited) {
  MemoryLockingStatus status{};
  status.hasCapIpcLock = true;

  EXPECT_EQ(status.availableBytes(), MLOCK_UNLIMITED);
}

/** @test availableBytes() with finite limit. */
TEST(MemoryLockingStatusHelperTest, AvailableBytesFinite) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 1024 * 1024;
  status.currentLockedBytes = 256 * 1024;

  EXPECT_EQ(status.availableBytes(), 768 * 1024);
}

/** @test availableBytes() when fully used. */
TEST(MemoryLockingStatusHelperTest, AvailableBytesNone) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 65536;
  status.currentLockedBytes = 65536;

  EXPECT_EQ(status.availableBytes(), 0U);
}

/** @test availableBytes() when over limit (shouldn't happen normally). */
TEST(MemoryLockingStatusHelperTest, AvailableBytesOverLimit) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 65536;
  status.currentLockedBytes = 100000; // Over limit

  EXPECT_EQ(status.availableBytes(), 0U);
}

/** @test toString() produces valid output. */
TEST(MemoryLockingStatusHelperTest, ToStringValid) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 65536;
  status.hardLimitBytes = 65536;
  status.currentLockedBytes = 4096;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Memory Locking:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Soft limit:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Hard limit:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Current locked:"), std::string::npos);
}

/** @test toString() shows unlimited correctly. */
TEST(MemoryLockingStatusHelperTest, ToStringUnlimited) {
  MemoryLockingStatus status{};
  status.softLimitBytes = MLOCK_UNLIMITED;
  status.hardLimitBytes = MLOCK_UNLIMITED;

  const std::string OUTPUT = status.toString();
  EXPECT_NE(OUTPUT.find("unlimited"), std::string::npos);
}

/* ----------------------------- MlockallStatus Tests ----------------------------- */

/** @test Default MlockallStatus is false. */
TEST(MlockallStatusDefaultTest, DefaultFalse) {
  const MlockallStatus DEFAULT{};

  EXPECT_FALSE(DEFAULT.canLockCurrent);
  EXPECT_FALSE(DEFAULT.canLockFuture);
  EXPECT_FALSE(DEFAULT.isCurrentlyLocked);
}

/** @test MlockallStatus toString() produces output. */
TEST(MlockallStatusHelperTest, ToStringValid) {
  MlockallStatus status{};
  status.canLockCurrent = true;
  status.canLockFuture = false;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("mlockall()"), std::string::npos);
  EXPECT_NE(OUTPUT.find("MCL_CURRENT"), std::string::npos);
  EXPECT_NE(OUTPUT.find("MCL_FUTURE"), std::string::npos);
}

/* ----------------------------- Live System Tests ----------------------------- */

/** @test softLimitBytes is valid. */
TEST_F(MemoryLockingTest, SoftLimitValid) {
  // Soft limit should be either unlimited or a reasonable value
  if (status_.softLimitBytes != MLOCK_UNLIMITED) {
    // If not unlimited, should be at least 0 and not absurdly large
    EXPECT_GE(status_.softLimitBytes, 0U);
    // Most systems default to 64KB or more for regular users
  }
}

/** @test hardLimitBytes >= softLimitBytes. */
TEST_F(MemoryLockingTest, HardLimitGeSoftLimit) {
  // Hard limit should be >= soft limit (unless both unlimited)
  if (status_.softLimitBytes != MLOCK_UNLIMITED && status_.hardLimitBytes != MLOCK_UNLIMITED) {
    EXPECT_GE(status_.hardLimitBytes, status_.softLimitBytes);
  }
}

/** @test currentLockedBytes is reasonable. */
TEST_F(MemoryLockingTest, CurrentLockedReasonable) {
  // Current locked should be within soft limit (unless privileged)
  if (!status_.isUnlimited() && status_.softLimitBytes != MLOCK_UNLIMITED) {
    EXPECT_LE(status_.currentLockedBytes, status_.softLimitBytes);
  }
}

/** @test isRoot matches getuid() result. */
TEST_F(MemoryLockingTest, IsRootConsistent) {
  const bool ACTUAL_ROOT = (::getuid() == 0);
  EXPECT_EQ(status_.isRoot, ACTUAL_ROOT);
}

/** @test hasCapIpcLock matches hasCapIpcLock() function. */
TEST_F(MemoryLockingTest, CapIpcLockConsistent) {
  const bool CAP_CHECK = hasCapIpcLock();
  EXPECT_EQ(status_.hasCapIpcLock, CAP_CHECK);
}

/** @test getMemoryLockingStatus() is deterministic. */
TEST_F(MemoryLockingTest, Deterministic) {
  const MemoryLockingStatus STATUS2 = getMemoryLockingStatus();

  EXPECT_EQ(status_.softLimitBytes, STATUS2.softLimitBytes);
  EXPECT_EQ(status_.hardLimitBytes, STATUS2.hardLimitBytes);
  EXPECT_EQ(status_.hasCapIpcLock, STATUS2.hasCapIpcLock);
  EXPECT_EQ(status_.isRoot, STATUS2.isRoot);
  // currentLockedBytes may change slightly between calls
}

/** @test toString() produces non-empty output. */
TEST_F(MemoryLockingTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Memory Locking:"), std::string::npos);
}

/* ----------------------------- getMlockallStatus Tests ----------------------------- */

/** @test getMlockallStatus() returns valid structure. */
TEST(MlockallStatusLiveTest, ReturnsValidStruct) {
  const MlockallStatus STATUS = getMlockallStatus();

  // Just verify it doesn't crash and has consistent values
  // If unlimited, both should be true
  const MemoryLockingStatus LOCK_STATUS = getMemoryLockingStatus();
  if (LOCK_STATUS.isUnlimited()) {
    EXPECT_TRUE(STATUS.canLockCurrent);
    EXPECT_TRUE(STATUS.canLockFuture);
  }
}

/** @test getMlockallStatus() toString() works. */
TEST(MlockallStatusLiveTest, ToStringWorks) {
  const MlockallStatus STATUS = getMlockallStatus();
  const std::string OUTPUT = STATUS.toString();

  EXPECT_FALSE(OUTPUT.empty());
}

/* ----------------------------- hasCapIpcLock Tests ----------------------------- */

/** @test hasCapIpcLock() is deterministic. */
TEST(HasCapIpcLockTest, Deterministic) {
  const bool RESULT1 = hasCapIpcLock();
  const bool RESULT2 = hasCapIpcLock();

  EXPECT_EQ(RESULT1, RESULT2);
}

/** @test hasCapIpcLock() result is consistent with root status. */
TEST(HasCapIpcLockTest, ConsistentWithRoot) {
  // Note: Root typically has all capabilities, but this isn't guaranteed
  // in all container environments. This test just documents behavior.
  const bool IS_ROOT = (::getuid() == 0);
  const bool HAS_CAP = hasCapIpcLock();

  if (IS_ROOT) {
    // Root usually has CAP_IPC_LOCK, but may not in containers
    // This is informational only
    GTEST_LOG_(INFO) << "Running as root, CAP_IPC_LOCK: " << (HAS_CAP ? "yes" : "no");
  }
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test canLock(0) always succeeds. */
TEST(MemoryLockingEdgeTest, CanLockZero) {
  MemoryLockingStatus status{};
  status.softLimitBytes = 0; // Zero limit

  EXPECT_TRUE(status.canLock(0));
}

/** @test canLock with very large value when unlimited. */
TEST(MemoryLockingEdgeTest, CanLockLargeWhenUnlimited) {
  MemoryLockingStatus status{};
  status.softLimitBytes = MLOCK_UNLIMITED;

  EXPECT_TRUE(status.canLock(1ULL << 40)); // 1 TiB
}

/** @test MLOCK_UNLIMITED is max uint64_t. */
TEST(MemoryLockingConstantTest, UnlimitedValue) {
  EXPECT_EQ(MLOCK_UNLIMITED, static_cast<std::uint64_t>(-1));
  EXPECT_EQ(MLOCK_UNLIMITED, UINT64_MAX);
}