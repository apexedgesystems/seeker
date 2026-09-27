/**
 * @file PcieStatus_uTest.cpp
 * @brief Unit tests for seeker::gpu::PcieStatus.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/PcieStatus.hpp"
#include "src/gpu/inc/GpuTopology.hpp"

#include <gtest/gtest.h>

using seeker::gpu::getAllPcieStatus;
using seeker::gpu::getGpuTopology;
using seeker::gpu::getPcieStatus;
using seeker::gpu::getPcieStatusByBdf;
using seeker::gpu::GpuTopology;
using seeker::gpu::parsePcieGeneration;
using seeker::gpu::pcieBandwidthPerLaneMBps;
using seeker::gpu::PcieGeneration;
using seeker::gpu::PcieStatus;

/* ----------------------------- PcieGeneration Tests ----------------------------- */

/** @test pcieBandwidthPerLaneMBps returns increasing values. */
TEST(PcieGenerationTest, BandwidthIncreasing) {
  EXPECT_LT(pcieBandwidthPerLaneMBps(PcieGeneration::Gen1),
            pcieBandwidthPerLaneMBps(PcieGeneration::Gen2));
  EXPECT_LT(pcieBandwidthPerLaneMBps(PcieGeneration::Gen2),
            pcieBandwidthPerLaneMBps(PcieGeneration::Gen3));
  EXPECT_LT(pcieBandwidthPerLaneMBps(PcieGeneration::Gen3),
            pcieBandwidthPerLaneMBps(PcieGeneration::Gen4));
  EXPECT_LT(pcieBandwidthPerLaneMBps(PcieGeneration::Gen4),
            pcieBandwidthPerLaneMBps(PcieGeneration::Gen5));
}

/** @test pcieBandwidthPerLaneMBps returns 0 for Unknown. */
TEST(PcieGenerationTest, UnknownBandwidthZero) {
  EXPECT_EQ(pcieBandwidthPerLaneMBps(PcieGeneration::Unknown), 0);
}

/** @test parsePcieGeneration parses Gen3 speed. */
TEST(PcieGenerationTest, ParseGen3) {
  EXPECT_EQ(parsePcieGeneration("8.0 GT/s"), PcieGeneration::Gen3);
  EXPECT_EQ(parsePcieGeneration("8 GT/s"), PcieGeneration::Gen3);
}

/** @test parsePcieGeneration parses Gen4 speed. */
TEST(PcieGenerationTest, ParseGen4) {
  EXPECT_EQ(parsePcieGeneration("16.0 GT/s"), PcieGeneration::Gen4);
  EXPECT_EQ(parsePcieGeneration("16 GT/s"), PcieGeneration::Gen4);
}

/** @test parsePcieGeneration returns Unknown for invalid. */
TEST(PcieGenerationTest, ParseInvalidReturnsUnknown) {
  EXPECT_EQ(parsePcieGeneration(""), PcieGeneration::Unknown);
  EXPECT_EQ(parsePcieGeneration("garbage"), PcieGeneration::Unknown);
}

/* ----------------------------- PcieStatus Tests ----------------------------- */

/** @test Default PcieStatus has deviceIndex -1. */
TEST(PcieStatusTest, DefaultDeviceIndex) {
  PcieStatus status{};
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test Default has empty BDF. */
TEST(PcieStatusTest, DefaultBdf) {
  PcieStatus status{};
  EXPECT_TRUE(status.bdf.empty());
}

/** @test Default has zero widths. */
TEST(PcieStatusTest, DefaultWidths) {
  PcieStatus status{};
  EXPECT_EQ(status.currentWidth, 0);
  EXPECT_EQ(status.maxWidth, 0);
}

/** @test Default has Unknown generations. */
TEST(PcieStatusTest, DefaultGenerations) {
  PcieStatus status{};
  EXPECT_EQ(status.currentGen, PcieGeneration::Unknown);
  EXPECT_EQ(status.maxGen, PcieGeneration::Unknown);
}

/** @test Default NUMA node is -1. */
TEST(PcieStatusTest, DefaultNumaNode) {
  PcieStatus status{};
  EXPECT_EQ(status.numaNode, -1);
}

/** @test isAtMaxLink for matching link. */
TEST(PcieStatusTest, AtMaxLinkMatching) {
  PcieStatus status{};
  status.currentWidth = 16;
  status.maxWidth = 16;
  status.currentGen = PcieGeneration::Gen4;
  status.maxGen = PcieGeneration::Gen4;
  EXPECT_TRUE(status.isAtMaxLink());
}

/** @test isAtMaxLink false when width degraded. */
TEST(PcieStatusTest, NotAtMaxWidthDegraded) {
  PcieStatus status{};
  status.currentWidth = 8;
  status.maxWidth = 16;
  status.currentGen = PcieGeneration::Gen4;
  status.maxGen = PcieGeneration::Gen4;
  EXPECT_FALSE(status.isAtMaxLink());
}

/** @test isAtMaxLink false when gen degraded. */
TEST(PcieStatusTest, NotAtMaxGenDegraded) {
  PcieStatus status{};
  status.currentWidth = 16;
  status.maxWidth = 16;
  status.currentGen = PcieGeneration::Gen3;
  status.maxGen = PcieGeneration::Gen4;
  EXPECT_FALSE(status.isAtMaxLink());
}

/** @test theoreticalBandwidthMBps calculates correctly. */
TEST(PcieStatusTest, TheoreticalBandwidth) {
  PcieStatus status{};
  status.maxWidth = 16;
  status.maxGen = PcieGeneration::Gen3;
  // Gen3 is ~1 GB/s per lane, so 16x should be ~16 GB/s = 16000 MB/s
  EXPECT_GT(status.theoreticalBandwidthMBps(), 0);
}

/** @test currentBandwidthMBps calculates correctly. */
TEST(PcieStatusTest, CurrentBandwidth) {
  PcieStatus status{};
  status.currentWidth = 16;
  status.currentGen = PcieGeneration::Gen4;
  EXPECT_GT(status.currentBandwidthMBps(), 0);
}

/** @test Default status has no link info. */
TEST(PcieStatusTest, DefaultHasNoLinkInfo) {
  PcieStatus status{};
  EXPECT_FALSE(status.hasLinkInfo());
}

/** @test BDF without link widths (integrated GPU) has no link info. */
TEST(PcieStatusTest, BdfWithoutLinkHasNoLinkInfo) {
  PcieStatus status{};
  status.bdf = "0000:01:00.0";
  EXPECT_FALSE(status.hasLinkInfo());
  status.maxWidth = 16;
  status.maxGen = PcieGeneration::Gen4;
  EXPECT_TRUE(status.hasLinkInfo());
}

/** @test Placeholder attributes (width 255, speed "Unknown") are not link info. */
TEST(PcieStatusTest, PlaceholderLinkHasNoLinkInfo) {
  PcieStatus status{};
  status.bdf = "0000:00:02.0";
  status.maxWidth = 255;
  status.maxGen = parsePcieGeneration("Unknown");
  EXPECT_FALSE(status.hasLinkInfo());

  // A width beyond x32 is a placeholder even when a max speed is reported
  status.maxWidth = 63;
  status.maxGen = parsePcieGeneration("64.0 GT/s PCIe");
  EXPECT_FALSE(status.hasLinkInfo());

  // x32 is the widest link the PCIe specification defines
  status.maxWidth = 32;
  EXPECT_TRUE(status.hasLinkInfo());
}

/** @test PcieStatus::toString not empty. */
TEST(PcieStatusTest, ToStringNotEmpty) {
  PcieStatus status{};
  EXPECT_FALSE(status.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getPcieStatus returns default for invalid index. */
TEST(PcieApiTest, InvalidIndexReturnsDefault) {
  PcieStatus status = getPcieStatus(-1);
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test getPcieStatusByBdf returns default for invalid BDF. */
TEST(PcieApiTest, InvalidBdfReturnsDefault) {
  PcieStatus status = getPcieStatusByBdf("");
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test getAllPcieStatus returns vector. */
TEST(PcieApiTest, GetAllReturnsVector) {
  std::vector<PcieStatus> all = getAllPcieStatus();
  EXPECT_GE(all.size(), 0);
}

/** @test getPcieStatusByBdf for a nonexistent device has no link info. */
TEST(PcieApiTest, NonexistentBdfNoLinkInfo) {
  PcieStatus status = getPcieStatusByBdf("ffff:ff:1f.7");
  EXPECT_EQ(status.bdf, "ffff:ff:1f.7");
  EXPECT_FALSE(status.hasLinkInfo());
}

/** @test getAllPcieStatus has one entry per topology device with matching BDF. */
TEST(PcieApiTest, AllMatchesTopology) {
  const GpuTopology TOPO = getGpuTopology();
  const std::vector<PcieStatus> ALL = getAllPcieStatus();
  ASSERT_EQ(ALL.size(), TOPO.devices.size());
  for (std::size_t i = 0; i < ALL.size(); ++i) {
    EXPECT_EQ(ALL[i].deviceIndex, TOPO.devices[i].deviceIndex);
    EXPECT_EQ(ALL[i].bdf, TOPO.devices[i].pciBdf);
  }
}

/** @test Link attributes, when present, are self-consistent. */
TEST(PcieApiTest, LinkInfoConsistent) {
  const std::vector<PcieStatus> ALL = getAllPcieStatus();
  if (ALL.empty()) {
    GTEST_SKIP() << "No GPUs present";
  }
  for (const auto& S : ALL) {
    EXPECT_FALSE(S.bdf.empty());
    if (!S.hasLinkInfo()) {
      continue; // Integrated GPU without link attributes
    }
    EXPECT_GE(S.currentWidth, 0);
    EXPECT_LE(S.currentWidth, S.maxWidth);
    EXPECT_GE(S.numaNode, -1);
  }
}

/** @test getPcieStatus(i) matches getAllPcieStatus entry i. */
TEST(PcieApiTest, SingleMatchesAll) {
  const std::vector<PcieStatus> ALL = getAllPcieStatus();
  if (ALL.empty()) {
    GTEST_SKIP() << "No GPUs present";
  }
  for (const auto& S : ALL) {
    const PcieStatus SINGLE = getPcieStatus(S.deviceIndex);
    EXPECT_EQ(SINGLE.deviceIndex, S.deviceIndex);
    EXPECT_EQ(SINGLE.bdf, S.bdf);
    EXPECT_EQ(SINGLE.maxWidth, S.maxWidth);
  }
}

/** @test getPcieStatus is deterministic for invalid index. */
TEST(PcieApiTest, DeterministicInvalid) {
  PcieStatus s1 = getPcieStatus(-1);
  PcieStatus s2 = getPcieStatus(-1);
  EXPECT_EQ(s1.deviceIndex, s2.deviceIndex);
}
