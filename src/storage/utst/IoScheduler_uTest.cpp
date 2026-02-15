/**
 * @file IoScheduler_uTest.cpp
 * @brief Unit tests for seeker::storage::IoScheduler.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Scheduler availability varies by kernel version and device type.
 *  - Tests with real devices require at least one block device.
 */

#include "src/storage/inc/IoScheduler.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::storage::getIoSchedulerConfig;
using seeker::storage::IoSchedulerConfig;
using seeker::storage::MAX_SCHEDULERS;
using seeker::storage::SCHED_DEVICE_NAME_SIZE;
using seeker::storage::SCHEDULER_NAME_SIZE;

/* ----------------------------- Scheduler Detection Tests ----------------------------- */

/** @test isNoneScheduler detects "none" scheduler. */
TEST(IoSchedulerMethodTest, IsNoneSchedulerDetection) {
  IoSchedulerConfig config{};

  std::strncpy(config.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  EXPECT_TRUE(config.isNoneScheduler());

  std::strncpy(config.current.data(), "mq-deadline", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isNoneScheduler());

  std::strncpy(config.current.data(), "bfq", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isNoneScheduler());
}

/** @test isMqDeadline detects mq-deadline scheduler. */
TEST(IoSchedulerMethodTest, IsMqDeadlineDetection) {
  IoSchedulerConfig config{};

  std::strncpy(config.current.data(), "mq-deadline", SCHEDULER_NAME_SIZE - 1);
  EXPECT_TRUE(config.isMqDeadline());

  std::strncpy(config.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isMqDeadline());

  std::strncpy(config.current.data(), "bfq", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isMqDeadline());
}

/** @test isRtFriendly detects RT-friendly schedulers. */
TEST(IoSchedulerMethodTest, IsRtFriendlyDetection) {
  IoSchedulerConfig config{};

  // RT-friendly schedulers
  std::strncpy(config.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  EXPECT_TRUE(config.isRtFriendly());

  std::strncpy(config.current.data(), "mq-deadline", SCHEDULER_NAME_SIZE - 1);
  EXPECT_TRUE(config.isRtFriendly());

  // Non-RT-friendly schedulers
  std::strncpy(config.current.data(), "bfq", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isRtFriendly());

  std::strncpy(config.current.data(), "kyber", SCHEDULER_NAME_SIZE - 1);
  EXPECT_FALSE(config.isRtFriendly());
}

/* ----------------------------- Read-Ahead Tests ----------------------------- */

/** @test isReadAheadLow detects low read-ahead values. */
TEST(IoSchedulerMethodTest, IsReadAheadLowDetection) {
  IoSchedulerConfig config{};

  config.readAheadKb = 0;
  EXPECT_TRUE(config.isReadAheadLow());

  config.readAheadKb = 128;
  EXPECT_TRUE(config.isReadAheadLow());

  config.readAheadKb = 256;
  EXPECT_FALSE(config.isReadAheadLow());

  config.readAheadKb = 1024;
  EXPECT_FALSE(config.isReadAheadLow());

  config.readAheadKb = -1; // Unavailable
  EXPECT_FALSE(config.isReadAheadLow());
}

/* ----------------------------- hasScheduler Tests ----------------------------- */

/** @test hasScheduler finds available schedulers. */
TEST(IoSchedulerMethodTest, HasSchedulerDetection) {
  IoSchedulerConfig config{};

  std::strncpy(config.available[0].data(), "mq-deadline", SCHEDULER_NAME_SIZE - 1);
  std::strncpy(config.available[1].data(), "none", SCHEDULER_NAME_SIZE - 1);
  std::strncpy(config.available[2].data(), "bfq", SCHEDULER_NAME_SIZE - 1);
  config.availableCount = 3;

  EXPECT_TRUE(config.hasScheduler("mq-deadline"));
  EXPECT_TRUE(config.hasScheduler("none"));
  EXPECT_TRUE(config.hasScheduler("bfq"));
  EXPECT_FALSE(config.hasScheduler("kyber"));
  EXPECT_FALSE(config.hasScheduler(nullptr));
}

/* ----------------------------- RT Score Tests ----------------------------- */

/** @test rtScore gives highest score to "none" scheduler. */
TEST(IoSchedulerMethodTest, RtScoreNoneHighest) {
  IoSchedulerConfig configNone{};
  std::strncpy(configNone.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  configNone.readAheadKb = 0;
  configNone.noMerges = 2;
  configNone.nrRequests = 32;

  IoSchedulerConfig configBfq{};
  std::strncpy(configBfq.current.data(), "bfq", SCHEDULER_NAME_SIZE - 1);
  configBfq.readAheadKb = 0;
  configBfq.noMerges = 2;
  configBfq.nrRequests = 32;

  // "none" should score higher than "bfq" with same other settings
  EXPECT_GT(configNone.rtScore(), configBfq.rtScore());
}

/** @test rtScore penalizes high read-ahead. */
TEST(IoSchedulerMethodTest, RtScorePenalizesHighReadAhead) {
  IoSchedulerConfig configLow{};
  std::strncpy(configLow.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  configLow.readAheadKb = 0;

  IoSchedulerConfig configHigh{};
  std::strncpy(configHigh.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  configHigh.readAheadKb = 1024;

  EXPECT_GT(configLow.rtScore(), configHigh.rtScore());
}

/** @test rtScore is in valid range. */
TEST(IoSchedulerMethodTest, RtScoreInValidRange) {
  IoSchedulerConfig config{};

  // Default/empty config
  const int SCORE_DEFAULT = config.rtScore();
  EXPECT_GE(SCORE_DEFAULT, 0);
  EXPECT_LE(SCORE_DEFAULT, 100);

  // Best possible config
  std::strncpy(config.current.data(), "none", SCHEDULER_NAME_SIZE - 1);
  config.readAheadKb = 0;
  config.noMerges = 2;
  config.nrRequests = 32;

  const int SCORE_BEST = config.rtScore();
  EXPECT_GE(SCORE_BEST, 0);
  EXPECT_LE(SCORE_BEST, 100);
  EXPECT_GT(SCORE_BEST, SCORE_DEFAULT);
}

/* ----------------------------- Queue Parameter Tests ----------------------------- */

/** @test Queue parameters have sensible defaults. */
TEST(IoSchedulerMethodTest, DefaultValues) {
  const IoSchedulerConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.nrRequests, -1);
  EXPECT_EQ(DEFAULT.readAheadKb, -1);
  EXPECT_EQ(DEFAULT.maxSectorsKb, -1);
  EXPECT_EQ(DEFAULT.rqAffinity, -1);
  EXPECT_EQ(DEFAULT.noMerges, -1);
  EXPECT_FALSE(DEFAULT.iostatsEnabled);
  EXPECT_FALSE(DEFAULT.addRandom);
  EXPECT_EQ(DEFAULT.availableCount, 0U);
}

/* ----------------------------- Real Device Tests ----------------------------- */

class IoSchedulerRealDeviceTest : public ::testing::Test {
protected:
  IoSchedulerConfig config_{};
  bool hasDevice_{false};

  void SetUp() override {
    // Try common device names
    static const char* DEVICES[] = {"nvme0n1", "sda", "vda", "xvda"};

    for (const char* dev : DEVICES) {
      config_ = getIoSchedulerConfig(dev);
      if (config_.current[0] != '\0') {
        hasDevice_ = true;
        break;
      }
    }
  }
};

/** @test Real device has a current scheduler. */
TEST_F(IoSchedulerRealDeviceTest, HasCurrentScheduler) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  EXPECT_GT(std::strlen(config_.current.data()), 0U);
}

/** @test Real device has at least one available scheduler. */
TEST_F(IoSchedulerRealDeviceTest, HasAvailableSchedulers) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  EXPECT_GE(config_.availableCount, 1U);
  EXPECT_LE(config_.availableCount, MAX_SCHEDULERS);
}

/** @test Current scheduler is in available list. */
TEST_F(IoSchedulerRealDeviceTest, CurrentInAvailable) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  EXPECT_TRUE(config_.hasScheduler(config_.current.data()));
}

/** @test Real device has valid queue parameters. */
TEST_F(IoSchedulerRealDeviceTest, ValidQueueParameters) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  // nr_requests should be positive
  EXPECT_GT(config_.nrRequests, 0);

  // read_ahead_kb should be non-negative
  EXPECT_GE(config_.readAheadKb, 0);
}

/** @test Scheduler names are null-terminated. */
TEST_F(IoSchedulerRealDeviceTest, SchedulerNamesNullTerminated) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  // Check current scheduler
  bool foundNull = false;
  for (std::size_t i = 0; i < SCHEDULER_NAME_SIZE; ++i) {
    if (config_.current[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);

  // Check available schedulers
  for (std::size_t i = 0; i < config_.availableCount; ++i) {
    foundNull = false;
    for (std::size_t j = 0; j < SCHEDULER_NAME_SIZE; ++j) {
      if (config_.available[i][j] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Scheduler " << i << " not null-terminated";
  }
}

/* ----------------------------- Invalid Input Tests ----------------------------- */

/** @test getIoSchedulerConfig handles invalid input. */
TEST(IoSchedulerInvalidInputTest, NullDevice) {
  const IoSchedulerConfig CONFIG = getIoSchedulerConfig(nullptr);
  EXPECT_EQ(CONFIG.current[0], '\0');
  EXPECT_EQ(CONFIG.availableCount, 0U);
}

/** @test getIoSchedulerConfig handles empty device name. */
TEST(IoSchedulerInvalidInputTest, EmptyDevice) {
  const IoSchedulerConfig CONFIG = getIoSchedulerConfig("");
  EXPECT_EQ(CONFIG.current[0], '\0');
  EXPECT_EQ(CONFIG.availableCount, 0U);
}

/** @test getIoSchedulerConfig handles non-existent device. */
TEST(IoSchedulerInvalidInputTest, NonExistentDevice) {
  const IoSchedulerConfig CONFIG = getIoSchedulerConfig("nonexistent_device_xyz");
  EXPECT_EQ(CONFIG.current[0], '\0');
  EXPECT_EQ(CONFIG.availableCount, 0U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output for valid config. */
TEST_F(IoSchedulerRealDeviceTest, ToStringNonEmpty) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const std::string OUTPUT = config_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("scheduler"), std::string::npos);
}

/** @test rtAssessment produces non-empty output. */
TEST_F(IoSchedulerRealDeviceTest, RtAssessmentNonEmpty) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const std::string OUTPUT = config_.rtAssessment();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("RT Score"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getIoSchedulerConfig returns consistent results. */
TEST_F(IoSchedulerRealDeviceTest, ConsistentResults) {
  if (!hasDevice_) {
    GTEST_SKIP() << "No block device available for testing";
  }

  const IoSchedulerConfig CONFIG2 = getIoSchedulerConfig(config_.device.data());

  EXPECT_STREQ(config_.current.data(), CONFIG2.current.data());
  EXPECT_EQ(config_.availableCount, CONFIG2.availableCount);
  EXPECT_EQ(config_.nrRequests, CONFIG2.nrRequests);
  EXPECT_EQ(config_.readAheadKb, CONFIG2.readAheadKb);
}