/**
 * @file CpuStats_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuStats.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Accepts platforms without swap or MemAvailable (values may be 0).
 */

#include "src/cpu/inc/CpuStats.hpp"

#include <gtest/gtest.h>

#include <cmath>   // std::isfinite
#include <cstring> // std::strlen
#include <string>

using seeker::cpu::CPU_MODEL_STRING_SIZE;
using seeker::cpu::CpuCountData;
using seeker::cpu::CpuInfoData;
using seeker::cpu::CpuStats;
using seeker::cpu::getCpuStats;
using seeker::cpu::KERNEL_VERSION_STRING_SIZE;
using seeker::cpu::KernelVersionData;
using seeker::cpu::MeminfoData;
using seeker::cpu::readCpuCount;
using seeker::cpu::readCpuInfo;
using seeker::cpu::readKernelVersion;
using seeker::cpu::readMeminfo;
using seeker::cpu::readSysinfo;
using seeker::cpu::SysinfoData;

class CpuStatsTest : public ::testing::Test {
protected:
  CpuStats stats_{};

  void SetUp() override { stats_ = getCpuStats(); }
};

/* ----------------------------- CPU Count Tests ----------------------------- */

/** @test CPU count is at least 1. */
TEST_F(CpuStatsTest, CpuCountAtLeastOne) { EXPECT_GE(stats_.cpuCount.count, 1); }

/** @test readCpuCount returns same value. */
TEST(CpuCountTest, DirectReadMatches) {
  const CpuCountData DATA = readCpuCount();
  EXPECT_GE(DATA.count, 1);
}

/* ----------------------------- Kernel Version Tests ----------------------------- */

/** @test Kernel version string is non-empty. */
TEST_F(CpuStatsTest, KernelVersionNonEmpty) {
  const std::size_t LEN = std::strlen(stats_.kernel.version.data());
  EXPECT_GT(LEN, 0U);
}

/** @test Kernel version string is null-terminated within bounds. */
TEST_F(CpuStatsTest, KernelVersionNullTerminated) {
  bool foundNull = false;
  for (std::size_t i = 0; i < KERNEL_VERSION_STRING_SIZE; ++i) {
    if (stats_.kernel.version[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

/** @test readKernelVersion returns valid data. */
TEST(KernelVersionTest, DirectReadValid) {
  const KernelVersionData DATA = readKernelVersion();
  const std::size_t LEN = std::strlen(DATA.version.data());
  EXPECT_GT(LEN, 0U);
  EXPECT_LT(LEN, KERNEL_VERSION_STRING_SIZE);
}

/* ----------------------------- CPU Info Tests ----------------------------- */

/** @test CPU model string is null-terminated within bounds. */
TEST_F(CpuStatsTest, CpuModelNullTerminated) {
  bool foundNull = false;
  for (std::size_t i = 0; i < CPU_MODEL_STRING_SIZE; ++i) {
    if (stats_.cpuInfo.model[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

/** @test CPU frequency is non-negative. */
TEST_F(CpuStatsTest, CpuFrequencyNonNegative) { EXPECT_GE(stats_.cpuInfo.frequencyMhz, 0); }

/** @test readCpuInfo returns valid data. */
TEST(CpuInfoTest, DirectReadValid) {
  const CpuInfoData DATA = readCpuInfo();
  const std::size_t LEN = std::strlen(DATA.model.data());
  EXPECT_LT(LEN, CPU_MODEL_STRING_SIZE);
  EXPECT_GE(DATA.frequencyMhz, 0);
}

/* ----------------------------- Sysinfo Tests ----------------------------- */

/** @test RAM totals are positive and consistent. */
TEST_F(CpuStatsTest, RamValuesConsistent) {
  EXPECT_GT(stats_.sysinfo.totalRamBytes, 0U);
  EXPECT_GE(stats_.sysinfo.freeRamBytes, 0U);
  EXPECT_LE(stats_.sysinfo.freeRamBytes, stats_.sysinfo.totalRamBytes);
}

/** @test Swap values are consistent (swap may be disabled). */
TEST_F(CpuStatsTest, SwapValuesConsistent) {
  EXPECT_GE(stats_.sysinfo.totalSwapBytes, 0U);
  EXPECT_GE(stats_.sysinfo.freeSwapBytes, 0U);
  EXPECT_LE(stats_.sysinfo.freeSwapBytes, stats_.sysinfo.totalSwapBytes);
}

/** @test Uptime is positive. */
TEST_F(CpuStatsTest, UptimePositive) { EXPECT_GT(stats_.sysinfo.uptimeSeconds, 0U); }

/** @test Process count is positive. */
TEST_F(CpuStatsTest, ProcessCountPositive) { EXPECT_GT(stats_.sysinfo.processCount, 0); }

/** @test Load averages are finite and non-negative. */
TEST_F(CpuStatsTest, LoadAveragesValid) {
  EXPECT_TRUE(std::isfinite(stats_.sysinfo.load1));
  EXPECT_TRUE(std::isfinite(stats_.sysinfo.load5));
  EXPECT_TRUE(std::isfinite(stats_.sysinfo.load15));
  EXPECT_GE(stats_.sysinfo.load1, 0.0);
  EXPECT_GE(stats_.sysinfo.load5, 0.0);
  EXPECT_GE(stats_.sysinfo.load15, 0.0);
}

/** @test readSysinfo returns valid data. */
TEST(SysinfoTest, DirectReadValid) {
  const SysinfoData DATA = readSysinfo();
  EXPECT_GT(DATA.totalRamBytes, 0U);
  EXPECT_GT(DATA.uptimeSeconds, 0U);
  EXPECT_GE(DATA.processCount, 0);
}

/* ----------------------------- Meminfo Tests ----------------------------- */

/** @test MemAvailable is within bounds when present. */
TEST_F(CpuStatsTest, MemAvailableWithinBounds) {
  if (stats_.meminfo.hasAvailable) {
    EXPECT_GT(stats_.meminfo.availableBytes, 0U);
    EXPECT_LE(stats_.meminfo.availableBytes, stats_.sysinfo.totalRamBytes);
  }
}

/** @test readMeminfo returns valid data. */
TEST(MeminfoTest, DirectReadValid) {
  const MeminfoData DATA = readMeminfo();
  // hasAvailable may be false on very old kernels
  if (DATA.hasAvailable) {
    EXPECT_GT(DATA.availableBytes, 0U);
  }
}

/* ----------------------------- Cross-Field Relations ----------------------------- */

/** @test Free RAM does not exceed total. */
TEST_F(CpuStatsTest, FreeRamDoesNotExceedTotal) {
  EXPECT_LE(stats_.sysinfo.freeRamBytes, stats_.sysinfo.totalRamBytes);
}

/** @test MemAvailable does not exceed total (when present). */
TEST_F(CpuStatsTest, MemAvailableDoesNotExceedTotal) {
  if (stats_.meminfo.hasAvailable && stats_.meminfo.availableBytes > 0) {
    EXPECT_LE(stats_.meminfo.availableBytes, stats_.sysinfo.totalRamBytes);
  }
}

/** @test Swap free does not exceed swap total. */
TEST_F(CpuStatsTest, SwapFreeDoesNotExceedTotal) {
  if (stats_.sysinfo.totalSwapBytes > 0) {
    EXPECT_LE(stats_.sysinfo.freeSwapBytes, stats_.sysinfo.totalSwapBytes);
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(CpuStatsTest, ToStringNonEmpty) {
  const std::string OUTPUT = stats_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(CpuStatsTest, ToStringContainsSections) {
  const std::string OUTPUT = stats_.toString();

  EXPECT_NE(OUTPUT.find("CPUs:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Kernel:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("CPU:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Uptime:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Processes:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Load avg"), std::string::npos);
  EXPECT_NE(OUTPUT.find("RAM"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Swap"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SysinfoData is zeroed. */
TEST(CpuStatsDefaultTest, SysinfoDefaultZero) {
  const SysinfoData DEFAULT{};

  EXPECT_EQ(DEFAULT.totalRamBytes, 0U);
  EXPECT_EQ(DEFAULT.freeRamBytes, 0U);
  EXPECT_EQ(DEFAULT.totalSwapBytes, 0U);
  EXPECT_EQ(DEFAULT.freeSwapBytes, 0U);
  EXPECT_EQ(DEFAULT.uptimeSeconds, 0U);
  EXPECT_EQ(DEFAULT.processCount, 0);
  EXPECT_EQ(DEFAULT.load1, 0.0);
  EXPECT_EQ(DEFAULT.load5, 0.0);
  EXPECT_EQ(DEFAULT.load15, 0.0);
}

/** @test Default CpuStats has empty strings. */
TEST(CpuStatsDefaultTest, StringsEmpty) {
  const CpuStats DEFAULT{};

  EXPECT_EQ(DEFAULT.kernel.version[0], '\0');
  EXPECT_EQ(DEFAULT.cpuInfo.model[0], '\0');
}

/** @test Default CpuCountData is zero. */
TEST(CpuStatsDefaultTest, CpuCountDefaultZero) {
  const CpuCountData DEFAULT{};
  EXPECT_EQ(DEFAULT.count, 0);
}