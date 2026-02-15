/**
 * @file NetworkIsolation_uTest.cpp
 * @brief Unit tests for seeker::network::NetworkIsolation.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Network IRQ presence depends on hardware and driver configuration.
 *  - Virtual machines may have different IRQ configurations.
 */

#include "src/network/inc/NetworkIsolation.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::network::checkIrqConflict;
using seeker::network::formatCpuMask;
using seeker::network::getNetworkIsolation;
using seeker::network::IrqConflictResult;
using seeker::network::MAX_INTERFACES;
using seeker::network::MAX_NIC_IRQS;
using seeker::network::NetworkIsolation;
using seeker::network::NicIrqInfo;
using seeker::network::parseCpuListToMask;

class NetworkIsolationTest : public ::testing::Test {
protected:
  NetworkIsolation ni_{};

  void SetUp() override { ni_ = getNetworkIsolation(); }
};

/* ----------------------------- NicIrqInfo Method Tests ----------------------------- */

/** @test hasIrqOnCpu returns false for empty NIC. */
TEST(NicIrqInfoMethodsTest, HasIrqOnCpuEmptyFalse) {
  const NicIrqInfo EMPTY{};
  EXPECT_FALSE(EMPTY.hasIrqOnCpu(0));
  EXPECT_FALSE(EMPTY.hasIrqOnCpu(1));
}

/** @test hasIrqOnCpu returns true when CPU is in affinity mask. */
TEST(NicIrqInfoMethodsTest, HasIrqOnCpuDetects) {
  NicIrqInfo nic{};
  nic.irqCount = 1;
  nic.irqNumbers[0] = 42;
  nic.affinity[0] = 0b0101; // CPUs 0 and 2

  EXPECT_TRUE(nic.hasIrqOnCpu(0));
  EXPECT_FALSE(nic.hasIrqOnCpu(1));
  EXPECT_TRUE(nic.hasIrqOnCpu(2));
  EXPECT_FALSE(nic.hasIrqOnCpu(3));
}

/** @test hasIrqOnCpu handles invalid CPU numbers. */
TEST(NicIrqInfoMethodsTest, HasIrqOnCpuInvalidCpu) {
  NicIrqInfo nic{};
  nic.irqCount = 1;
  nic.affinity[0] = ~0ULL; // All CPUs

  EXPECT_FALSE(nic.hasIrqOnCpu(-1));
  EXPECT_FALSE(nic.hasIrqOnCpu(64));
  EXPECT_FALSE(nic.hasIrqOnCpu(100));
}

/** @test hasIrqOnCpuMask detects overlap. */
TEST(NicIrqInfoMethodsTest, HasIrqOnCpuMaskDetects) {
  NicIrqInfo nic{};
  nic.irqCount = 2;
  nic.affinity[0] = 0b0001; // CPU 0
  nic.affinity[1] = 0b0100; // CPU 2

  EXPECT_TRUE(nic.hasIrqOnCpuMask(0b0001));  // CPU 0
  EXPECT_FALSE(nic.hasIrqOnCpuMask(0b0010)); // CPU 1
  EXPECT_TRUE(nic.hasIrqOnCpuMask(0b0100));  // CPU 2
  EXPECT_TRUE(nic.hasIrqOnCpuMask(0b0111));  // CPUs 0-2
}

/** @test getCombinedAffinity combines all IRQ affinities. */
TEST(NicIrqInfoMethodsTest, CombinedAffinity) {
  NicIrqInfo nic{};
  nic.irqCount = 3;
  nic.affinity[0] = 0b0001;
  nic.affinity[1] = 0b0010;
  nic.affinity[2] = 0b0100;

  EXPECT_EQ(nic.getCombinedAffinity(), 0b0111ULL);
}

/* ----------------------------- NetworkIsolation Query Tests ----------------------------- */

/** @test getNetworkIsolation returns valid structure. */
TEST_F(NetworkIsolationTest, ReturnsValidStructure) {
  // NIC count should be within bounds
  EXPECT_LE(ni_.nicCount, MAX_INTERFACES);

  // Each NIC should have valid data
  for (std::size_t i = 0; i < ni_.nicCount; ++i) {
    EXPECT_GT(std::strlen(ni_.nics[i].ifname.data()), 0U) << "NIC " << i << " has empty name";
    EXPECT_LE(ni_.nics[i].irqCount, MAX_NIC_IRQS)
        << "NIC " << ni_.nics[i].ifname.data() << " has too many IRQs";
  }
}

/** @test Each NIC's IRQs have positive IRQ numbers. */
TEST_F(NetworkIsolationTest, IrqNumbersPositive) {
  for (std::size_t i = 0; i < ni_.nicCount; ++i) {
    for (std::size_t j = 0; j < ni_.nics[i].irqCount; ++j) {
      EXPECT_GE(ni_.nics[i].irqNumbers[j], 0)
          << "NIC " << ni_.nics[i].ifname.data() << " IRQ " << j << " is negative";
    }
  }
}

/** @test Affinity masks are non-zero when IRQs exist. */
TEST_F(NetworkIsolationTest, AffinityNonZero) {
  for (std::size_t i = 0; i < ni_.nicCount; ++i) {
    for (std::size_t j = 0; j < ni_.nics[i].irqCount; ++j) {
      EXPECT_NE(ni_.nics[i].affinity[j], 0ULL)
          << "NIC " << ni_.nics[i].ifname.data() << " IRQ " << j << " has zero affinity";
    }
  }
}

/* ----------------------------- NetworkIsolation::find Tests ----------------------------- */

/** @test find returns nullptr for non-existent NIC. */
TEST_F(NetworkIsolationTest, FindNonExistentNull) {
  EXPECT_EQ(ni_.find("nonexistent_xyz_123"), nullptr);
  EXPECT_EQ(ni_.find(nullptr), nullptr);
}

/** @test find returns correct NIC when present. */
TEST_F(NetworkIsolationTest, FindExisting) {
  if (ni_.nicCount > 0) {
    const char* FIRST_NAME = ni_.nics[0].ifname.data();
    const NicIrqInfo* found = ni_.find(FIRST_NAME);

    EXPECT_NE(found, nullptr);
    if (found != nullptr) {
      EXPECT_STREQ(found->ifname.data(), FIRST_NAME);
    }
  }
}

/* ----------------------------- checkIrqConflict Tests ----------------------------- */

/** @test checkIrqConflict with zero mask returns no conflict. */
TEST_F(NetworkIsolationTest, ConflictZeroMaskNoConflict) {
  const IrqConflictResult RESULT = checkIrqConflict(ni_, 0);
  EXPECT_FALSE(RESULT.hasConflict);
  EXPECT_EQ(RESULT.conflictCount, 0U);
}

/** @test checkIrqConflict detects conflicts. */
TEST(NetworkIsolationConflictTest, DetectsConflict) {
  NetworkIsolation ni{};
  ni.nicCount = 1;
  std::strcpy(ni.nics[0].ifname.data(), "eth0");
  ni.nics[0].irqCount = 1;
  ni.nics[0].irqNumbers[0] = 42;
  ni.nics[0].affinity[0] = 0b0101; // CPUs 0 and 2

  // Check for conflict with CPU 0
  IrqConflictResult result = checkIrqConflict(ni, 0b0001);
  EXPECT_TRUE(result.hasConflict);
  EXPECT_EQ(result.conflictCount, 1U);
  EXPECT_NE(std::strstr(result.conflictingNics.data(), "eth0"), nullptr);

  // Check for conflict with CPU 1 (no conflict)
  result = checkIrqConflict(ni, 0b0010);
  EXPECT_FALSE(result.hasConflict);
  EXPECT_EQ(result.conflictCount, 0U);
}

/** @test checkIrqConflict tracks conflicting CPUs. */
TEST(NetworkIsolationConflictTest, TracksConflictingCpus) {
  NetworkIsolation ni{};
  ni.nicCount = 1;
  std::strcpy(ni.nics[0].ifname.data(), "eth0");
  ni.nics[0].irqCount = 1;
  ni.nics[0].affinity[0] = 0b0111; // CPUs 0, 1, 2

  const IrqConflictResult RESULT = checkIrqConflict(ni, 0b0101); // CPUs 0, 2

  EXPECT_TRUE(RESULT.hasConflict);
  EXPECT_EQ(RESULT.conflictingCpuCount, 2U);

  // Should have CPUs 0 and 2
  bool hasCpu0 = false;
  bool hasCpu2 = false;
  for (std::size_t i = 0; i < RESULT.conflictingCpuCount; ++i) {
    if (RESULT.conflictingCpus[i] == 0) {
      hasCpu0 = true;
    }
    if (RESULT.conflictingCpus[i] == 2) {
      hasCpu2 = true;
    }
  }
  EXPECT_TRUE(hasCpu0);
  EXPECT_TRUE(hasCpu2);
}

/* ----------------------------- parseCpuListToMask Tests ----------------------------- */

/** @test parseCpuListToMask handles single CPU. */
TEST(ParseCpuListTest, SingleCpu) {
  EXPECT_EQ(parseCpuListToMask("0"), 0b0001ULL);
  EXPECT_EQ(parseCpuListToMask("3"), 0b1000ULL);
}

/** @test parseCpuListToMask handles range. */
TEST(ParseCpuListTest, Range) {
  EXPECT_EQ(parseCpuListToMask("0-3"), 0b1111ULL);
  EXPECT_EQ(parseCpuListToMask("2-4"), 0b11100ULL);
}

/** @test parseCpuListToMask handles comma list. */
TEST(ParseCpuListTest, CommaList) {
  EXPECT_EQ(parseCpuListToMask("0,2,4"), 0b10101ULL);
  EXPECT_EQ(parseCpuListToMask("1,3"), 0b1010ULL);
}

/** @test parseCpuListToMask handles mixed format. */
TEST(ParseCpuListTest, MixedFormat) {
  EXPECT_EQ(parseCpuListToMask("0,2-4,6"), 0b1011101ULL);
  EXPECT_EQ(parseCpuListToMask("0-2,4,6-8"), 0b111010111ULL);
}

/** @test parseCpuListToMask handles empty/null. */
TEST(ParseCpuListTest, EmptyNull) {
  EXPECT_EQ(parseCpuListToMask(nullptr), 0ULL);
  EXPECT_EQ(parseCpuListToMask(""), 0ULL);
}

/** @test parseCpuListToMask handles leading/trailing whitespace. */
TEST(ParseCpuListTest, Whitespace) {
  // Kernel cpu lists have commas but no spaces within ranges
  // Leading/trailing whitespace should be handled
  EXPECT_EQ(parseCpuListToMask(" 0,2 "), 0b0101ULL);
  EXPECT_EQ(parseCpuListToMask("  0-2  "), 0b0111ULL);
  EXPECT_EQ(parseCpuListToMask(" 1 "), 0b0010ULL);
}

/* ----------------------------- formatCpuMask Tests ----------------------------- */

/** @test formatCpuMask handles zero. */
TEST(FormatCpuMaskTest, Zero) { EXPECT_EQ(formatCpuMask(0), "(none)"); }

/** @test formatCpuMask handles single CPU. */
TEST(FormatCpuMaskTest, SingleCpu) {
  EXPECT_EQ(formatCpuMask(0b0001), "0");
  EXPECT_EQ(formatCpuMask(0b1000), "3");
}

/** @test formatCpuMask handles consecutive CPUs as range. */
TEST(FormatCpuMaskTest, Range) {
  EXPECT_EQ(formatCpuMask(0b1111), "0-3");
  EXPECT_EQ(formatCpuMask(0b11100), "2-4");
}

/** @test formatCpuMask handles non-consecutive CPUs. */
TEST(FormatCpuMaskTest, NonConsecutive) {
  EXPECT_EQ(formatCpuMask(0b10101), "0,2,4");
  EXPECT_EQ(formatCpuMask(0b1010), "1,3");
}

/** @test formatCpuMask handles mixed ranges and singles. */
TEST(FormatCpuMaskTest, Mixed) { EXPECT_EQ(formatCpuMask(0b1011101), "0,2-4,6"); }

/** @test Roundtrip: parse then format preserves semantics. */
TEST(CpuMaskRoundtripTest, Roundtrip) {
  const std::uint64_t ORIGINAL = 0b1011101ULL;
  const std::string FORMATTED = formatCpuMask(ORIGINAL);
  const std::uint64_t REPARSED = parseCpuListToMask(FORMATTED.c_str());

  EXPECT_EQ(ORIGINAL, REPARSED);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test NicIrqInfo toString produces non-empty output. */
TEST(NicIrqInfoToStringTest, NonEmpty) {
  NicIrqInfo nic{};
  std::strcpy(nic.ifname.data(), "eth0");
  nic.irqCount = 1;
  nic.irqNumbers[0] = 42;
  nic.affinity[0] = 0b0001;

  const std::string OUTPUT = nic.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("eth0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("42"), std::string::npos);
}

/** @test NetworkIsolation toString produces non-empty output. */
TEST_F(NetworkIsolationTest, ToStringNonEmpty) {
  const std::string OUTPUT = ni_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Network IRQ"), std::string::npos);
}

/** @test IrqConflictResult toString describes conflict. */
TEST(IrqConflictResultToStringTest, DescribesConflict) {
  IrqConflictResult result{};
  result.hasConflict = true;
  result.conflictCount = 2;
  std::strcpy(result.conflictingNics.data(), "eth0, eth1");

  const std::string OUTPUT = result.toString();
  EXPECT_NE(OUTPUT.find("CONFLICT"), std::string::npos);
  EXPECT_NE(OUTPUT.find("eth0"), std::string::npos);
}

/** @test IrqConflictResult toString handles no conflict. */
TEST(IrqConflictResultToStringTest, NoConflict) {
  const IrqConflictResult RESULT{};
  const std::string OUTPUT = RESULT.toString();

  EXPECT_NE(OUTPUT.find("No IRQ conflict"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default NicIrqInfo is empty. */
TEST(NicIrqInfoDefaultTest, DefaultEmpty) {
  const NicIrqInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.ifname[0], '\0');
  EXPECT_EQ(DEFAULT.irqCount, 0U);
  EXPECT_EQ(DEFAULT.numaNode, -1);
  EXPECT_FALSE(DEFAULT.hasIrqOnCpu(0));
}

/** @test Default NetworkIsolation is empty. */
TEST(NetworkIsolationDefaultTest, DefaultEmpty) {
  const NetworkIsolation DEFAULT{};

  EXPECT_EQ(DEFAULT.nicCount, 0U);
  EXPECT_EQ(DEFAULT.getTotalIrqCount(), 0U);
  EXPECT_FALSE(DEFAULT.hasIrqOnCpu(0));
}

/** @test Default IrqConflictResult shows no conflict. */
TEST(IrqConflictResultDefaultTest, DefaultNoConflict) {
  const IrqConflictResult DEFAULT{};

  EXPECT_FALSE(DEFAULT.hasConflict);
  EXPECT_EQ(DEFAULT.conflictCount, 0U);
  EXPECT_EQ(DEFAULT.conflictingCpuCount, 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getNetworkIsolation returns consistent results. */
TEST(NetworkIsolationDeterminismTest, ConsistentResults) {
  const NetworkIsolation NI1 = getNetworkIsolation();
  const NetworkIsolation NI2 = getNetworkIsolation();

  EXPECT_EQ(NI1.nicCount, NI2.nicCount);
  EXPECT_EQ(NI1.getTotalIrqCount(), NI2.getTotalIrqCount());
}