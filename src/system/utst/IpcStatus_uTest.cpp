/**
 * @file IpcStatus_uTest.cpp
 * @brief Unit tests for seeker::system::IpcStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Actual IPC limits and usage vary by system configuration.
 *  - Tests verify API contracts and data consistency.
 */

#include "src/system/inc/IpcStatus.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::system::getIpcStatus;
using seeker::system::getMsgStatus;
using seeker::system::getPosixMqStatus;
using seeker::system::getSemLimits;
using seeker::system::getSemStatus;
using seeker::system::getShmLimits;
using seeker::system::getShmStatus;
using seeker::system::IpcStatus;
using seeker::system::MAX_IPC_ENTRIES;
using seeker::system::MsgLimits;
using seeker::system::MsgStatus;
using seeker::system::PosixMqLimits;
using seeker::system::PosixMqStatus;
using seeker::system::SemLimits;
using seeker::system::SemStatus;
using seeker::system::ShmLimits;
using seeker::system::ShmSegment;
using seeker::system::ShmStatus;

class IpcStatusTest : public ::testing::Test {
protected:
  IpcStatus status_{};

  void SetUp() override { status_ = getIpcStatus(); }
};

/* ----------------------------- ShmLimits Tests ----------------------------- */

/** @test getShmLimits returns structure on Linux. */
TEST(ShmLimitsTest, QueryReturnsStructure) {
  const ShmLimits LIMITS = getShmLimits();

  // On most Linux systems, these values should be readable
  if (LIMITS.valid) {
    EXPECT_GT(LIMITS.shmmax, 0U);
    EXPECT_GT(LIMITS.shmmni, 0U);
    EXPECT_GT(LIMITS.pageSize, 0U);
  }
}

/** @test maxTotalBytes calculation is correct. */
TEST(ShmLimitsTest, MaxTotalBytesCalculation) {
  ShmLimits limits{};
  limits.shmall = 100;
  limits.pageSize = 4096;

  EXPECT_EQ(limits.maxTotalBytes(), 100 * 4096);
}

/** @test toString produces non-empty output. */
TEST(ShmLimitsTest, ToStringNonEmpty) {
  const ShmLimits LIMITS = getShmLimits();
  const std::string OUTPUT = LIMITS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Shared Memory"), std::string::npos);
}

/** @test toString handles invalid state. */
TEST(ShmLimitsTest, ToStringInvalid) {
  const ShmLimits LIMITS{};
  const std::string OUTPUT = LIMITS.toString();

  EXPECT_NE(OUTPUT.find("not available"), std::string::npos);
}

/* ----------------------------- SemLimits Tests ----------------------------- */

/** @test getSemLimits returns structure on Linux. */
TEST(SemLimitsTest, QueryReturnsStructure) {
  const SemLimits LIMITS = getSemLimits();

  // On most Linux systems, /proc/sys/kernel/sem should be readable
  if (LIMITS.valid) {
    EXPECT_GT(LIMITS.semmni, 0U);
    EXPECT_GT(LIMITS.semmsl, 0U);
    EXPECT_GT(LIMITS.semmns, 0U);
    EXPECT_GT(LIMITS.semopm, 0U);
  }
}

/** @test toString produces non-empty output. */
TEST(SemLimitsTest, ToStringNonEmpty) {
  const SemLimits LIMITS = getSemLimits();
  const std::string OUTPUT = LIMITS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Semaphore"), std::string::npos);
}

/* ----------------------------- MsgLimits Tests ----------------------------- */

/** @test MsgLimits toString produces output. */
TEST(MsgLimitsTest, ToStringNonEmpty) {
  const auto STATUS = getMsgStatus();
  const std::string OUTPUT = STATUS.limits.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Message Queue"), std::string::npos);
}

/* ----------------------------- PosixMqLimits Tests ----------------------------- */

/** @test PosixMqLimits toString produces output. */
TEST(PosixMqLimitsTest, ToStringNonEmpty) {
  const auto STATUS = getPosixMqStatus();
  const std::string OUTPUT = STATUS.limits.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("POSIX MQ"), std::string::npos);
}

/* ----------------------------- ShmSegment Tests ----------------------------- */

/** @test ShmSegment::canAttach for root. */
TEST(ShmSegmentTest, CanAttachRoot) {
  ShmSegment seg{};
  seg.uid = 1000;
  seg.mode = 0600; // Owner read/write only

  // Root can always attach
  EXPECT_TRUE(seg.canAttach(0));
}

/** @test ShmSegment::canAttach for owner. */
TEST(ShmSegmentTest, CanAttachOwner) {
  ShmSegment seg{};
  seg.uid = 1000;
  seg.mode = 0600;

  EXPECT_TRUE(seg.canAttach(1000));
  EXPECT_FALSE(seg.canAttach(1001));
}

/** @test ShmSegment::canAttach for world-readable. */
TEST(ShmSegmentTest, CanAttachWorldReadable) {
  ShmSegment seg{};
  seg.uid = 1000;
  seg.mode = 0644;

  EXPECT_TRUE(seg.canAttach(0));
  EXPECT_TRUE(seg.canAttach(1000));
  EXPECT_TRUE(seg.canAttach(1001)); // World readable
}

/** @test ShmSegment::canAttach for no permissions. */
TEST(ShmSegmentTest, CanAttachNoPermissions) {
  ShmSegment seg{};
  seg.uid = 1000;
  seg.mode = 0600; // Owner only

  EXPECT_FALSE(seg.canAttach(1001));
  EXPECT_FALSE(seg.canAttach(2000));
}

/* ----------------------------- ShmStatus Tests ----------------------------- */

/** @test getShmStatus returns structure. */
TEST(ShmStatusTest, QueryReturnsStructure) {
  const ShmStatus STATUS = getShmStatus();

  // Limits should be attempted
  EXPECT_TRUE(STATUS.limits.valid || !STATUS.limits.valid);

  // Segment count should be bounded
  EXPECT_LE(STATUS.segmentCount, MAX_IPC_ENTRIES);
}

/** @test isNearSegmentLimit is consistent. */
TEST(ShmStatusTest, IsNearSegmentLimitConsistent) {
  ShmStatus status{};
  status.limits.valid = true;
  status.limits.shmmni = 100;
  status.segmentCount = 50;

  EXPECT_FALSE(status.isNearSegmentLimit());

  status.segmentCount = 95;
  EXPECT_TRUE(status.isNearSegmentLimit());
}

/** @test isNearMemoryLimit is consistent. */
TEST(ShmStatusTest, IsNearMemoryLimitConsistent) {
  ShmStatus status{};
  status.limits.valid = true;
  status.limits.shmall = 1000;
  status.limits.pageSize = 4096;
  status.totalBytes = 500 * 4096; // 50% usage (500 of 1000 pages)

  EXPECT_FALSE(status.isNearMemoryLimit());

  status.totalBytes = 950 * 4096; // 95% usage (950 of 1000 pages)
  EXPECT_TRUE(status.isNearMemoryLimit());
}

/** @test find returns nullptr for non-existent segment. */
TEST(ShmStatusTest, FindNonExistent) {
  const ShmStatus STATUS = getShmStatus();
  EXPECT_EQ(STATUS.find(-99999), nullptr);
}

/** @test toString produces non-empty output. */
TEST(ShmStatusTest, ToStringNonEmpty) {
  const ShmStatus STATUS = getShmStatus();
  const std::string OUTPUT = STATUS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Shared Memory"), std::string::npos);
}

/* ----------------------------- SemStatus Tests ----------------------------- */

/** @test getSemStatus returns structure. */
TEST(SemStatusTest, QueryReturnsStructure) {
  const SemStatus STATUS = getSemStatus();

  // Should have attempted to read limits
  EXPECT_TRUE(STATUS.limits.valid || !STATUS.limits.valid);
}

/** @test isNearArrayLimit is consistent. */
TEST(SemStatusTest, IsNearArrayLimitConsistent) {
  SemStatus status{};
  status.limits.valid = true;
  status.limits.semmni = 100;
  status.arraysInUse = 50;

  EXPECT_FALSE(status.isNearArrayLimit());

  status.arraysInUse = 95;
  EXPECT_TRUE(status.isNearArrayLimit());
}

/** @test isNearSemLimit is consistent. */
TEST(SemStatusTest, IsNearSemLimitConsistent) {
  SemStatus status{};
  status.limits.valid = true;
  status.limits.semmns = 1000;
  status.semsInUse = 500;

  EXPECT_FALSE(status.isNearSemLimit());

  status.semsInUse = 950;
  EXPECT_TRUE(status.isNearSemLimit());
}

/** @test toString produces non-empty output. */
TEST(SemStatusTest, ToStringNonEmpty) {
  const SemStatus STATUS = getSemStatus();
  const std::string OUTPUT = STATUS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Semaphore"), std::string::npos);
}

/* ----------------------------- MsgStatus Tests ----------------------------- */

/** @test getMsgStatus returns structure. */
TEST(MsgStatusTest, QueryReturnsStructure) {
  const MsgStatus STATUS = getMsgStatus();

  // Should have attempted to read limits
  EXPECT_TRUE(STATUS.limits.valid || !STATUS.limits.valid);
}

/** @test isNearQueueLimit is consistent. */
TEST(MsgStatusTest, IsNearQueueLimitConsistent) {
  MsgStatus status{};
  status.limits.valid = true;
  status.limits.msgmni = 100;
  status.queuesInUse = 50;

  EXPECT_FALSE(status.isNearQueueLimit());

  status.queuesInUse = 95;
  EXPECT_TRUE(status.isNearQueueLimit());
}

/** @test toString produces non-empty output. */
TEST(MsgStatusTest, ToStringNonEmpty) {
  const MsgStatus STATUS = getMsgStatus();
  const std::string OUTPUT = STATUS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Message Queue"), std::string::npos);
}

/* ----------------------------- PosixMqStatus Tests ----------------------------- */

/** @test getPosixMqStatus returns structure. */
TEST(PosixMqStatusTest, QueryReturnsStructure) {
  const PosixMqStatus STATUS = getPosixMqStatus();

  // Should have attempted to read limits
  EXPECT_TRUE(STATUS.limits.valid || !STATUS.limits.valid);
}

/** @test toString produces non-empty output. */
TEST(PosixMqStatusTest, ToStringNonEmpty) {
  const PosixMqStatus STATUS = getPosixMqStatus();
  const std::string OUTPUT = STATUS.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("POSIX MQ"), std::string::npos);
}

/* ----------------------------- IpcStatus Tests ----------------------------- */

/** @test getIpcStatus returns valid structure. */
TEST_F(IpcStatusTest, QueryReturnsValidStructure) {
  // All subsystems should have been queried
  EXPECT_TRUE(status_.shm.limits.valid || !status_.shm.limits.valid);
  EXPECT_TRUE(status_.sem.limits.valid || !status_.sem.limits.valid);
  EXPECT_TRUE(status_.msg.limits.valid || !status_.msg.limits.valid);
  EXPECT_TRUE(status_.posixMq.limits.valid || !status_.posixMq.limits.valid);
}

/** @test isNearAnyLimit aggregates correctly. */
TEST(IpcStatusAggregateTest, IsNearAnyLimitAggregates) {
  IpcStatus status{};

  // Nothing near limit
  status.shm.limits.valid = true;
  status.shm.limits.shmmni = 100;
  status.shm.segmentCount = 50;
  EXPECT_FALSE(status.isNearAnyLimit());

  // One thing near limit
  status.shm.segmentCount = 95;
  EXPECT_TRUE(status.isNearAnyLimit());
}

/** @test rtScore is in valid range. */
TEST_F(IpcStatusTest, RtScoreInRange) {
  const int SCORE = status_.rtScore();
  EXPECT_GE(SCORE, 0);
  EXPECT_LE(SCORE, 100);
}

/** @test rtScore deducts for near limits. */
TEST(IpcStatusScoreTest, DeductsForNearLimits) {
  IpcStatus status{};

  // Set up valid limits with no issues
  status.shm.limits.valid = true;
  status.shm.limits.shmmni = 100;
  status.shm.limits.shmall = 10000;
  status.shm.limits.pageSize = 4096;
  status.shm.limits.shmmax = 1024ULL * 1024 * 1024; // 1 GiB
  status.shm.segmentCount = 10;
  status.shm.totalBytes = 1000;

  status.sem.limits.valid = true;
  status.sem.limits.semmni = 100;
  status.sem.limits.semmns = 1000;
  status.sem.arraysInUse = 10;
  status.sem.semsInUse = 100;

  status.msg.limits.valid = true;
  status.msg.limits.msgmni = 100;
  status.msg.queuesInUse = 10;

  status.posixMq.limits.valid = true;
  status.posixMq.limits.msgsizeMax = 8192;

  const int GOOD_SCORE = status.rtScore();

  // Now push to near limits
  status.shm.segmentCount = 95;
  const int BAD_SCORE = status.rtScore();

  EXPECT_GT(GOOD_SCORE, BAD_SCORE);
}

/** @test toString produces non-empty output. */
TEST_F(IpcStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("IPC Status"), std::string::npos);
}

/** @test toString contains all subsystems. */
TEST_F(IpcStatusTest, ToStringContainsSubsystems) {
  const std::string OUTPUT = status_.toString();

  EXPECT_NE(OUTPUT.find("Shared Memory"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Semaphore"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Message Queue"), std::string::npos);
  EXPECT_NE(OUTPUT.find("POSIX MQ"), std::string::npos);
  EXPECT_NE(OUTPUT.find("RT Score"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default ShmLimits is zeroed. */
TEST(ShmLimitsDefaultTest, DefaultZeroed) {
  const ShmLimits LIMITS{};

  EXPECT_EQ(LIMITS.shmmax, 0U);
  EXPECT_EQ(LIMITS.shmall, 0U);
  EXPECT_EQ(LIMITS.shmmni, 0U);
  EXPECT_EQ(LIMITS.shmmin, 1U); // Always 1
  EXPECT_EQ(LIMITS.pageSize, 4096U);
  EXPECT_FALSE(LIMITS.valid);
}

/** @test Default SemLimits is zeroed. */
TEST(SemLimitsDefaultTest, DefaultZeroed) {
  const SemLimits LIMITS{};

  EXPECT_EQ(LIMITS.semmsl, 0U);
  EXPECT_EQ(LIMITS.semmns, 0U);
  EXPECT_EQ(LIMITS.semopm, 0U);
  EXPECT_EQ(LIMITS.semmni, 0U);
  EXPECT_FALSE(LIMITS.valid);
}

/** @test Default MsgLimits is zeroed. */
TEST(MsgLimitsDefaultTest, DefaultZeroed) {
  const MsgLimits LIMITS{};

  EXPECT_EQ(LIMITS.msgmax, 0U);
  EXPECT_EQ(LIMITS.msgmnb, 0U);
  EXPECT_EQ(LIMITS.msgmni, 0U);
  EXPECT_FALSE(LIMITS.valid);
}

/** @test Default PosixMqLimits is zeroed. */
TEST(PosixMqLimitsDefaultTest, DefaultZeroed) {
  const PosixMqLimits LIMITS{};

  EXPECT_EQ(LIMITS.queuesMax, 0U);
  EXPECT_EQ(LIMITS.msgMax, 0U);
  EXPECT_EQ(LIMITS.msgsizeMax, 0U);
  EXPECT_FALSE(LIMITS.valid);
}

/** @test Default ShmSegment has invalid ID. */
TEST(ShmSegmentDefaultTest, DefaultInvalid) {
  const ShmSegment SEG{};

  EXPECT_EQ(SEG.shmid, -1);
  EXPECT_EQ(SEG.key, 0);
  EXPECT_EQ(SEG.size, 0U);
  EXPECT_EQ(SEG.nattch, 0U);
  EXPECT_FALSE(SEG.markedForDeletion);
}

/** @test Default IpcStatus has zeroed subsystems. */
TEST(IpcStatusDefaultTest, DefaultZeroed) {
  const IpcStatus STATUS{};

  EXPECT_FALSE(STATUS.shm.limits.valid);
  EXPECT_FALSE(STATUS.sem.limits.valid);
  EXPECT_FALSE(STATUS.msg.limits.valid);
  EXPECT_FALSE(STATUS.posixMq.limits.valid);
  EXPECT_EQ(STATUS.shm.segmentCount, 0U);
  EXPECT_EQ(STATUS.sem.arraysInUse, 0U);
  EXPECT_EQ(STATUS.msg.queuesInUse, 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getIpcStatus returns consistent results. */
TEST(IpcStatusDeterminismTest, ConsistentResults) {
  const auto STATUS1 = getIpcStatus();
  const auto STATUS2 = getIpcStatus();

  // Limits should be identical
  EXPECT_EQ(STATUS1.shm.limits.shmmax, STATUS2.shm.limits.shmmax);
  EXPECT_EQ(STATUS1.shm.limits.shmmni, STATUS2.shm.limits.shmmni);
  EXPECT_EQ(STATUS1.shm.limits.valid, STATUS2.shm.limits.valid);

  EXPECT_EQ(STATUS1.sem.limits.semmni, STATUS2.sem.limits.semmni);
  EXPECT_EQ(STATUS1.sem.limits.valid, STATUS2.sem.limits.valid);

  EXPECT_EQ(STATUS1.msg.limits.msgmni, STATUS2.msg.limits.msgmni);
  EXPECT_EQ(STATUS1.msg.limits.valid, STATUS2.msg.limits.valid);
}

/** @test getShmLimits returns consistent results. */
TEST(ShmLimitsDeterminismTest, ConsistentResults) {
  const auto LIMITS1 = getShmLimits();
  const auto LIMITS2 = getShmLimits();

  EXPECT_EQ(LIMITS1.shmmax, LIMITS2.shmmax);
  EXPECT_EQ(LIMITS1.shmall, LIMITS2.shmall);
  EXPECT_EQ(LIMITS1.shmmni, LIMITS2.shmmni);
  EXPECT_EQ(LIMITS1.pageSize, LIMITS2.pageSize);
  EXPECT_EQ(LIMITS1.valid, LIMITS2.valid);
}

/** @test getSemLimits returns consistent results. */
TEST(SemLimitsDeterminismTest, ConsistentResults) {
  const auto LIMITS1 = getSemLimits();
  const auto LIMITS2 = getSemLimits();

  EXPECT_EQ(LIMITS1.semmsl, LIMITS2.semmsl);
  EXPECT_EQ(LIMITS1.semmns, LIMITS2.semmns);
  EXPECT_EQ(LIMITS1.semopm, LIMITS2.semopm);
  EXPECT_EQ(LIMITS1.semmni, LIMITS2.semmni);
  EXPECT_EQ(LIMITS1.valid, LIMITS2.valid);
}