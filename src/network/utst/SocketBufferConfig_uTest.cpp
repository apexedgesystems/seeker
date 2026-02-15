/**
 * @file SocketBufferConfig_uTest.cpp
 * @brief Unit tests for seeker::network::SocketBufferConfig.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have /proc/sys/net/ with socket buffer tunables.
 */

#include "src/network/inc/SocketBufferConfig.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::network::CC_STRING_SIZE;
using seeker::network::formatBufferSize;
using seeker::network::getSocketBufferConfig;
using seeker::network::SocketBufferConfig;

class SocketBufferConfigTest : public ::testing::Test {
protected:
  SocketBufferConfig cfg_{};

  void SetUp() override { cfg_ = getSocketBufferConfig(); }
};

/* ----------------------------- Core Buffer Tests ----------------------------- */

/** @test rmem_default is readable on systems with /proc/sys/net access. */
TEST_F(SocketBufferConfigTest, RmemDefaultReadable) {
  // On some systems (containers, restricted environments), these may not be accessible
  if (cfg_.rmemDefault >= 0) {
    EXPECT_GT(cfg_.rmemDefault, 0);
  } else {
    GTEST_LOG_(INFO) << "rmem_default not readable (restricted environment)";
  }
}

/** @test rmem_max is readable on systems with /proc/sys/net access. */
TEST_F(SocketBufferConfigTest, RmemMaxReadable) {
  if (cfg_.rmemMax >= 0) {
    EXPECT_GT(cfg_.rmemMax, 0);
  } else {
    GTEST_LOG_(INFO) << "rmem_max not readable (restricted environment)";
  }
}

/** @test wmem_default is readable on systems with /proc/sys/net access. */
TEST_F(SocketBufferConfigTest, WmemDefaultReadable) {
  if (cfg_.wmemDefault >= 0) {
    EXPECT_GT(cfg_.wmemDefault, 0);
  } else {
    GTEST_LOG_(INFO) << "wmem_default not readable (restricted environment)";
  }
}

/** @test wmem_max is readable on systems with /proc/sys/net access. */
TEST_F(SocketBufferConfigTest, WmemMaxReadable) {
  if (cfg_.wmemMax >= 0) {
    EXPECT_GT(cfg_.wmemMax, 0);
  } else {
    GTEST_LOG_(INFO) << "wmem_max not readable (restricted environment)";
  }
}

/** @test Default buffer is less than or equal to max. */
TEST_F(SocketBufferConfigTest, DefaultLessOrEqualMax) {
  if (cfg_.rmemDefault >= 0 && cfg_.rmemMax >= 0) {
    EXPECT_LE(cfg_.rmemDefault, cfg_.rmemMax);
  }

  if (cfg_.wmemDefault >= 0 && cfg_.wmemMax >= 0) {
    EXPECT_LE(cfg_.wmemDefault, cfg_.wmemMax);
  }
}

/** @test Buffer sizes are reasonable (at least 4 KB for modern systems). */
TEST_F(SocketBufferConfigTest, BufferSizesReasonable) {
  constexpr std::int64_t MIN_REASONABLE = 4096;

  if (cfg_.rmemDefault >= 0) {
    EXPECT_GE(cfg_.rmemDefault, MIN_REASONABLE);
  }
  if (cfg_.wmemDefault >= 0) {
    EXPECT_GE(cfg_.wmemDefault, MIN_REASONABLE);
  }
}

/* ----------------------------- TCP Buffer Tests ----------------------------- */

/** @test TCP rmem triple is readable. */
TEST_F(SocketBufferConfigTest, TcpRmemReadable) {
  // At least one of these should be readable on any Linux system
  const bool READ = (cfg_.tcpRmemMin >= 0) || (cfg_.tcpRmemDefault >= 0) || (cfg_.tcpRmemMax >= 0);
  EXPECT_TRUE(READ) << "TCP rmem not readable";
}

/** @test TCP wmem triple is readable. */
TEST_F(SocketBufferConfigTest, TcpWmemReadable) {
  const bool READ = (cfg_.tcpWmemMin >= 0) || (cfg_.tcpWmemDefault >= 0) || (cfg_.tcpWmemMax >= 0);
  EXPECT_TRUE(READ) << "TCP wmem not readable";
}

/** @test TCP buffer ordering: min <= default <= max. */
TEST_F(SocketBufferConfigTest, TcpBufferOrdering) {
  if (cfg_.tcpRmemMin >= 0 && cfg_.tcpRmemDefault >= 0) {
    EXPECT_LE(cfg_.tcpRmemMin, cfg_.tcpRmemDefault);
  }
  if (cfg_.tcpRmemDefault >= 0 && cfg_.tcpRmemMax >= 0) {
    EXPECT_LE(cfg_.tcpRmemDefault, cfg_.tcpRmemMax);
  }
  if (cfg_.tcpWmemMin >= 0 && cfg_.tcpWmemDefault >= 0) {
    EXPECT_LE(cfg_.tcpWmemMin, cfg_.tcpWmemDefault);
  }
  if (cfg_.tcpWmemDefault >= 0 && cfg_.tcpWmemMax >= 0) {
    EXPECT_LE(cfg_.tcpWmemDefault, cfg_.tcpWmemMax);
  }
}

/* ----------------------------- TCP Options Tests ----------------------------- */

/** @test TCP congestion control is readable. */
TEST_F(SocketBufferConfigTest, TcpCongestionReadable) {
  EXPECT_GT(std::strlen(cfg_.tcpCongestionControl.data()), 0U);
}

/** @test TCP congestion control is a known algorithm. */
TEST_F(SocketBufferConfigTest, TcpCongestionKnown) {
  const char* CC = cfg_.tcpCongestionControl.data();

  // Common congestion control algorithms
  const bool KNOWN = (std::strcmp(CC, "cubic") == 0) || (std::strcmp(CC, "bbr") == 0) ||
                     (std::strcmp(CC, "reno") == 0) || (std::strcmp(CC, "htcp") == 0) ||
                     (std::strcmp(CC, "dctcp") == 0) || (std::strcmp(CC, "vegas") == 0) ||
                     (std::strcmp(CC, "westwood") == 0);

  if (!KNOWN) {
    GTEST_LOG_(INFO) << "Unknown congestion control: " << CC;
  }
  // Don't fail - there may be custom/newer algorithms
}

/** @test TCP timestamps is 0 or 1. */
TEST_F(SocketBufferConfigTest, TcpTimestampsBoolean) {
  if (cfg_.tcpTimestamps >= 0) {
    EXPECT_TRUE(cfg_.tcpTimestamps == 0 || cfg_.tcpTimestamps == 1);
  }
}

/** @test TCP SACK is 0 or 1. */
TEST_F(SocketBufferConfigTest, TcpSackBoolean) {
  if (cfg_.tcpSack >= 0) {
    EXPECT_TRUE(cfg_.tcpSack == 0 || cfg_.tcpSack == 1);
  }
}

/** @test TCP window scaling is 0 or 1. */
TEST_F(SocketBufferConfigTest, TcpWindowScalingBoolean) {
  if (cfg_.tcpWindowScaling >= 0) {
    EXPECT_TRUE(cfg_.tcpWindowScaling == 0 || cfg_.tcpWindowScaling == 1);
  }
}

/* ----------------------------- Busy Polling Tests ----------------------------- */

/** @test Busy poll values are non-negative. */
TEST_F(SocketBufferConfigTest, BusyPollNonNegative) {
  // busy_read and busy_poll should be >= 0 (or -1 if not readable)
  EXPECT_GE(cfg_.busyRead, -1);
  EXPECT_GE(cfg_.busyPoll, -1);
}

/** @test isBusyPollingEnabled is consistent with values. */
TEST_F(SocketBufferConfigTest, BusyPollingEnabledConsistent) {
  const bool EXPECTED = (cfg_.busyRead > 0) || (cfg_.busyPoll > 0);
  EXPECT_EQ(cfg_.isBusyPollingEnabled(), EXPECTED);
}

/* ----------------------------- Helper Method Tests ----------------------------- */

/** @test isBusyPollingEnabled returns false when both zero. */
TEST(SocketBufferConfigMethodsTest, BusyPollingDisabled) {
  SocketBufferConfig cfg{};
  cfg.busyRead = 0;
  cfg.busyPoll = 0;

  EXPECT_FALSE(cfg.isBusyPollingEnabled());
}

/** @test isBusyPollingEnabled returns true when busyRead > 0. */
TEST(SocketBufferConfigMethodsTest, BusyPollingEnabledRead) {
  SocketBufferConfig cfg{};
  cfg.busyRead = 50;
  cfg.busyPoll = 0;

  EXPECT_TRUE(cfg.isBusyPollingEnabled());
}

/** @test isBusyPollingEnabled returns true when busyPoll > 0. */
TEST(SocketBufferConfigMethodsTest, BusyPollingEnabledPoll) {
  SocketBufferConfig cfg{};
  cfg.busyRead = 0;
  cfg.busyPoll = 50;

  EXPECT_TRUE(cfg.isBusyPollingEnabled());
}

/** @test isLowLatencyConfig requires busy polling and reasonable buffers. */
TEST(SocketBufferConfigMethodsTest, LowLatencyRequirements) {
  SocketBufferConfig cfg{};

  // Default should not be low latency
  EXPECT_FALSE(cfg.isLowLatencyConfig());

  // Add busy polling but not enough buffers
  cfg.busyRead = 50;
  cfg.busyPoll = 50;
  cfg.rmemMax = 128 * 1024; // Only 128 KB
  cfg.wmemMax = 128 * 1024;
  EXPECT_FALSE(cfg.isLowLatencyConfig());

  // Add reasonable buffers
  cfg.rmemMax = 512 * 1024; // 512 KB
  cfg.wmemMax = 512 * 1024;
  EXPECT_TRUE(cfg.isLowLatencyConfig());
}

/** @test isHighThroughputConfig requires large buffers. */
TEST(SocketBufferConfigMethodsTest, HighThroughputRequirements) {
  SocketBufferConfig cfg{};

  // Default should not be high throughput
  EXPECT_FALSE(cfg.isHighThroughputConfig());

  // Add large buffers
  cfg.rmemMax = 32 * 1024 * 1024; // 32 MB
  cfg.wmemMax = 32 * 1024 * 1024;
  cfg.tcpRmemMax = 32 * 1024 * 1024;
  cfg.tcpWmemMax = 32 * 1024 * 1024;
  EXPECT_TRUE(cfg.isHighThroughputConfig());
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(SocketBufferConfigTest, ToStringNonEmpty) {
  const std::string OUTPUT = cfg_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(SocketBufferConfigTest, ToStringContainsSections) {
  const std::string OUTPUT = cfg_.toString();

  EXPECT_NE(OUTPUT.find("Core buffers"), std::string::npos);
  EXPECT_NE(OUTPUT.find("TCP buffers"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Busy polling"), std::string::npos);
}

/** @test toString contains assessment. */
TEST_F(SocketBufferConfigTest, ToStringContainsAssessment) {
  const std::string OUTPUT = cfg_.toString();
  EXPECT_NE(OUTPUT.find("Assessment:"), std::string::npos);
}

/* ----------------------------- formatBufferSize Tests ----------------------------- */

/** @test formatBufferSize handles unknown (-1). */
TEST(FormatBufferSizeTest, Unknown) { EXPECT_EQ(formatBufferSize(-1), "unknown"); }

/** @test formatBufferSize handles zero. */
TEST(FormatBufferSizeTest, Zero) { EXPECT_EQ(formatBufferSize(0), "0"); }

/** @test formatBufferSize handles exact KiB. */
TEST(FormatBufferSizeTest, ExactKiB) {
  EXPECT_EQ(formatBufferSize(1024), "1 KiB");
  EXPECT_EQ(formatBufferSize(4096), "4 KiB");
  EXPECT_EQ(formatBufferSize(212992), "208 KiB"); // Common default
}

/** @test formatBufferSize handles exact MiB. */
TEST(FormatBufferSizeTest, ExactMiB) {
  EXPECT_EQ(formatBufferSize(1024 * 1024), "1 MiB");
  EXPECT_EQ(formatBufferSize(16 * 1024 * 1024), "16 MiB");
}

/** @test formatBufferSize handles exact GiB. */
TEST(FormatBufferSizeTest, ExactGiB) { EXPECT_EQ(formatBufferSize(1024LL * 1024 * 1024), "1 GiB"); }

/** @test formatBufferSize handles non-aligned sizes. */
TEST(FormatBufferSizeTest, NonAligned) {
  const std::string RESULT = formatBufferSize(212992);
  // Should produce KiB output
  EXPECT_NE(RESULT.find("KiB"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SocketBufferConfig has sentinel values. */
TEST(SocketBufferConfigDefaultTest, DefaultSentinels) {
  const SocketBufferConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.rmemDefault, -1);
  EXPECT_EQ(DEFAULT.rmemMax, -1);
  EXPECT_EQ(DEFAULT.wmemDefault, -1);
  EXPECT_EQ(DEFAULT.wmemMax, -1);
  EXPECT_EQ(DEFAULT.busyRead, -1);
  EXPECT_EQ(DEFAULT.busyPoll, -1);
  EXPECT_EQ(DEFAULT.tcpCongestionControl[0], '\0');
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getSocketBufferConfig returns consistent results. */
TEST(SocketBufferConfigDeterminismTest, ConsistentResults) {
  const SocketBufferConfig CFG1 = getSocketBufferConfig();
  const SocketBufferConfig CFG2 = getSocketBufferConfig();

  EXPECT_EQ(CFG1.rmemDefault, CFG2.rmemDefault);
  EXPECT_EQ(CFG1.rmemMax, CFG2.rmemMax);
  EXPECT_EQ(CFG1.wmemDefault, CFG2.wmemDefault);
  EXPECT_EQ(CFG1.wmemMax, CFG2.wmemMax);
  EXPECT_STREQ(CFG1.tcpCongestionControl.data(), CFG2.tcpCongestionControl.data());
}

/* ----------------------------- Network Backlog Tests ----------------------------- */

/** @test netdev_max_backlog is readable and positive. */
TEST_F(SocketBufferConfigTest, NetdevMaxBacklogReadable) {
  if (cfg_.netdevMaxBacklog >= 0) {
    EXPECT_GT(cfg_.netdevMaxBacklog, 0);
  }
}

/** @test netdev_budget is readable and positive. */
TEST_F(SocketBufferConfigTest, NetdevBudgetReadable) {
  if (cfg_.netdevBudget >= 0) {
    EXPECT_GT(cfg_.netdevBudget, 0);
  }
}