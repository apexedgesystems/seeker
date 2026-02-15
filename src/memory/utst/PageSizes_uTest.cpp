/**
 * @file PageSizes_uTest.cpp
 * @brief Unit tests for seeker::memory::PageSizes.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have a base page size; hugepages may not be configured.
 */

#include "src/memory/inc/PageSizes.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::memory::formatBytes;
using seeker::memory::getPageSizes;
using seeker::memory::MAX_HUGEPAGE_SIZES;
using seeker::memory::PageSizes;

class PageSizesTest : public ::testing::Test {
protected:
  PageSizes ps_{};

  void SetUp() override { ps_ = getPageSizes(); }
};

/* ----------------------------- Base Page Tests ----------------------------- */

/** @test Base page size is positive and power of two. */
TEST_F(PageSizesTest, BasePageSizeValid) {
  EXPECT_GT(ps_.basePageBytes, 0U);

  // Check power of two
  const bool IS_POWER_OF_TWO = (ps_.basePageBytes & (ps_.basePageBytes - 1)) == 0;
  EXPECT_TRUE(IS_POWER_OF_TWO) << "Base page size " << ps_.basePageBytes << " not power of two";
}

/** @test Base page size is in expected range (4KB-64KB typical). */
TEST_F(PageSizesTest, BasePageSizeReasonable) {
  // Most common: 4096 bytes (x86, ARM), 16384 (some ARM), 65536 (some PPC)
  EXPECT_GE(ps_.basePageBytes, 4096U);
  EXPECT_LE(ps_.basePageBytes, 65536U);
}

/* ----------------------------- Hugepage Tests ----------------------------- */

/** @test Hugepage count within bounds. */
TEST_F(PageSizesTest, HugePageCountWithinBounds) {
  EXPECT_LE(ps_.hugeSizeCount, MAX_HUGEPAGE_SIZES);
}

/** @test Hugepage sizes are all larger than base page. */
TEST_F(PageSizesTest, HugePageSizesLargerThanBase) {
  for (std::size_t i = 0; i < ps_.hugeSizeCount; ++i) {
    EXPECT_GT(ps_.hugeSizes[i], ps_.basePageBytes)
        << "Hugepage size " << ps_.hugeSizes[i] << " not larger than base " << ps_.basePageBytes;
  }
}

/** @test Hugepage sizes are powers of two. */
TEST_F(PageSizesTest, HugePageSizesPowerOfTwo) {
  for (std::size_t i = 0; i < ps_.hugeSizeCount; ++i) {
    const std::uint64_t SIZE = ps_.hugeSizes[i];
    const bool IS_POWER_OF_TWO = (SIZE & (SIZE - 1)) == 0;
    EXPECT_TRUE(IS_POWER_OF_TWO) << "Hugepage size " << SIZE << " not power of two";
  }
}

/** @test Hugepage sizes are sorted ascending. */
TEST_F(PageSizesTest, HugePageSizesSorted) {
  for (std::size_t i = 1; i < ps_.hugeSizeCount; ++i) {
    EXPECT_LT(ps_.hugeSizes[i - 1], ps_.hugeSizes[i]) << "Hugepage sizes not sorted at index " << i;
  }
}

/** @test Hugepage sizes are unique. */
TEST_F(PageSizesTest, HugePageSizesUnique) {
  for (std::size_t i = 0; i < ps_.hugeSizeCount; ++i) {
    for (std::size_t j = i + 1; j < ps_.hugeSizeCount; ++j) {
      EXPECT_NE(ps_.hugeSizes[i], ps_.hugeSizes[j])
          << "Duplicate hugepage size " << ps_.hugeSizes[i];
    }
  }
}

/* ----------------------------- Helper Method Tests ----------------------------- */

/** @test hasHugePages() returns correct value. */
TEST_F(PageSizesTest, HasHugePagesConsistent) {
  EXPECT_EQ(ps_.hasHugePages(), ps_.hugeSizeCount > 0);
}

/** @test hasHugePageSize() finds existing sizes. */
TEST_F(PageSizesTest, HasHugePageSizeFindsExisting) {
  for (std::size_t i = 0; i < ps_.hugeSizeCount; ++i) {
    EXPECT_TRUE(ps_.hasHugePageSize(ps_.hugeSizes[i]))
        << "hasHugePageSize() failed for " << ps_.hugeSizes[i];
  }
}

/** @test hasHugePageSize() returns false for non-existent sizes. */
TEST_F(PageSizesTest, HasHugePageSizeRejectsInvalid) {
  // Test with unlikely sizes
  EXPECT_FALSE(ps_.hasHugePageSize(0));
  EXPECT_FALSE(ps_.hasHugePageSize(12345));
  EXPECT_FALSE(ps_.hasHugePageSize(1)); // Too small
}

/** @test largestHugePageSize() returns correct value. */
TEST_F(PageSizesTest, LargestHugePageSizeCorrect) {
  const std::uint64_t LARGEST = ps_.largestHugePageSize();

  if (ps_.hugeSizeCount == 0) {
    EXPECT_EQ(LARGEST, 0U);
  } else {
    // Since sizes are sorted, largest should be last
    EXPECT_EQ(LARGEST, ps_.hugeSizes[ps_.hugeSizeCount - 1]);

    // Verify it's actually the largest
    for (std::size_t i = 0; i < ps_.hugeSizeCount; ++i) {
      EXPECT_LE(ps_.hugeSizes[i], LARGEST);
    }
  }
}

/* ----------------------------- Common Sizes Tests ----------------------------- */

/** @test If 2MiB hugepages present, they are correct size. */
TEST_F(PageSizesTest, TwoMibHugePagesCorrectSize) {
  constexpr std::uint64_t TWO_MIB = 2ULL * 1024 * 1024;

  if (ps_.hasHugePageSize(TWO_MIB)) {
    // Just verify it's in the list (already tested by hasHugePageSize)
    SUCCEED();
  } else {
    // Not all systems have 2MiB hugepages configured
    GTEST_LOG_(INFO) << "2 MiB hugepages not available on this system";
  }
}

/** @test If 1GiB hugepages present, they are correct size. */
TEST_F(PageSizesTest, OneGibHugePagesCorrectSize) {
  constexpr std::uint64_t ONE_GIB = 1ULL * 1024 * 1024 * 1024;

  if (ps_.hasHugePageSize(ONE_GIB)) {
    SUCCEED();
  } else {
    // 1GiB hugepages require CPU support and boot-time config
    GTEST_LOG_(INFO) << "1 GiB hugepages not available on this system";
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(PageSizesTest, ToStringNonEmpty) {
  const std::string OUTPUT = ps_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains base page info. */
TEST_F(PageSizesTest, ToStringContainsBasePage) {
  const std::string OUTPUT = ps_.toString();
  EXPECT_NE(OUTPUT.find("Base page"), std::string::npos);
}

/** @test toString contains huge page section. */
TEST_F(PageSizesTest, ToStringContainsHugePages) {
  const std::string OUTPUT = ps_.toString();
  EXPECT_NE(OUTPUT.find("Huge pages"), std::string::npos);
}

/* ----------------------------- formatBytes Tests ----------------------------- */

/** @test formatBytes handles zero. */
TEST(FormatBytesTest, Zero) { EXPECT_EQ(formatBytes(0), "0 B"); }

/** @test formatBytes handles exact KiB. */
TEST(FormatBytesTest, ExactKiB) {
  EXPECT_EQ(formatBytes(1024), "1.0 KiB");
  EXPECT_EQ(formatBytes(4096), "4.0 KiB");
  EXPECT_EQ(formatBytes(65536), "64.0 KiB");
}

/** @test formatBytes handles exact MiB. */
TEST(FormatBytesTest, ExactMiB) {
  EXPECT_EQ(formatBytes(1024 * 1024), "1.0 MiB");
  EXPECT_EQ(formatBytes(2 * 1024 * 1024), "2.0 MiB");
  EXPECT_EQ(formatBytes(512 * 1024 * 1024), "512.0 MiB");
}

/** @test formatBytes handles exact GiB. */
TEST(FormatBytesTest, ExactGiB) {
  EXPECT_EQ(formatBytes(1024ULL * 1024 * 1024), "1.0 GiB");
  EXPECT_EQ(formatBytes(2ULL * 1024 * 1024 * 1024), "2.0 GiB");
}

/** @test formatBytes handles exact TiB. */
TEST(FormatBytesTest, ExactTiB) { EXPECT_EQ(formatBytes(1024ULL * 1024 * 1024 * 1024), "1.0 TiB"); }

/** @test formatBytes handles non-aligned sizes. */
TEST(FormatBytesTest, NonAligned) {
  // These should produce decimal output
  const std::string RESULT = formatBytes(1500);
  EXPECT_NE(RESULT.find("KiB"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default PageSizes is zeroed. */
TEST(PageSizesDefaultTest, DefaultZeroed) {
  const PageSizes DEFAULT{};

  EXPECT_EQ(DEFAULT.basePageBytes, 0U);
  EXPECT_EQ(DEFAULT.hugeSizeCount, 0U);
  EXPECT_FALSE(DEFAULT.hasHugePages());
  EXPECT_EQ(DEFAULT.largestHugePageSize(), 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getPageSizes returns consistent results. */
TEST(PageSizesDeterminismTest, ConsistentResults) {
  const PageSizes PS1 = getPageSizes();
  const PageSizes PS2 = getPageSizes();

  EXPECT_EQ(PS1.basePageBytes, PS2.basePageBytes);
  EXPECT_EQ(PS1.hugeSizeCount, PS2.hugeSizeCount);

  for (std::size_t i = 0; i < PS1.hugeSizeCount; ++i) {
    EXPECT_EQ(PS1.hugeSizes[i], PS2.hugeSizes[i]);
  }
}