/**
 * @file HugepageStatus_uTest.cpp
 * @brief Unit tests for seeker::memory::HugepageStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Systems without hugepage support will have empty results (valid).
 */

#include "src/memory/inc/HugepageStatus.hpp"

#include <gtest/gtest.h>

#include <string>

using seeker::memory::getHugepageStatus;
using seeker::memory::HP_MAX_NUMA_NODES;
using seeker::memory::HP_MAX_SIZES;
using seeker::memory::HugepageNodeStatus;
using seeker::memory::HugepageSizeStatus;
using seeker::memory::HugepageStatus;

class HugepageStatusTest : public ::testing::Test {
protected:
  HugepageStatus status_{};
  bool hasHugepages_{false};

  void SetUp() override {
    status_ = getHugepageStatus();
    hasHugepages_ = (status_.sizeCount > 0);
  }
};

/* ----------------------------- HugepageSizeStatus Tests ----------------------------- */

/** @test Default HugepageSizeStatus is zeroed. */
TEST(HugepageSizeStatusTest, DefaultZero) {
  const HugepageSizeStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.pageSize, 0U);
  EXPECT_EQ(DEFAULT.total, 0U);
  EXPECT_EQ(DEFAULT.free, 0U);
  EXPECT_EQ(DEFAULT.reserved, 0U);
  EXPECT_EQ(DEFAULT.surplus, 0U);
}

/** @test used() calculates correctly. */
TEST(HugepageSizeStatusTest, UsedCalculation) {
  HugepageSizeStatus status{};
  status.pageSize = 2 * 1024 * 1024; // 2 MiB
  status.total = 100;
  status.free = 30;
  status.surplus = 10;

  // used = total + surplus - free = 100 + 10 - 30 = 80
  EXPECT_EQ(status.used(), 80U);
}

/** @test used() handles edge case where free > total + surplus. */
TEST(HugepageSizeStatusTest, UsedUnderflow) {
  HugepageSizeStatus status{};
  status.total = 10;
  status.free = 100; // More free than total (shouldn't happen, but be safe)
  status.surplus = 0;

  EXPECT_EQ(status.used(), 0U);
}

/** @test totalBytes() calculates correctly. */
TEST(HugepageSizeStatusTest, TotalBytes) {
  HugepageSizeStatus status{};
  status.pageSize = 2 * 1024 * 1024; // 2 MiB
  status.total = 50;
  status.surplus = 5;

  // totalBytes = (total + surplus) * pageSize = 55 * 2MiB = 110 MiB
  EXPECT_EQ(status.totalBytes(), 55ULL * 2 * 1024 * 1024);
}

/** @test freeBytes() calculates correctly. */
TEST(HugepageSizeStatusTest, FreeBytes) {
  HugepageSizeStatus status{};
  status.pageSize = 1024 * 1024 * 1024; // 1 GiB
  status.free = 2;

  EXPECT_EQ(status.freeBytes(), 2ULL * 1024 * 1024 * 1024);
}

/** @test usedBytes() calculates correctly. */
TEST(HugepageSizeStatusTest, UsedBytes) {
  HugepageSizeStatus status{};
  status.pageSize = 2 * 1024 * 1024;
  status.total = 100;
  status.free = 40;
  status.surplus = 0;

  // used = 60 pages, usedBytes = 60 * 2MiB
  EXPECT_EQ(status.usedBytes(), 60ULL * 2 * 1024 * 1024);
}

/** @test toString() produces valid output. */
TEST(HugepageSizeStatusTest, ToStringValid) {
  HugepageSizeStatus status{};
  status.pageSize = 2 * 1024 * 1024;
  status.total = 100;
  status.free = 50;
  status.reserved = 10;
  status.surplus = 5;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("MiB"), std::string::npos);
  EXPECT_NE(OUTPUT.find("total="), std::string::npos);
  EXPECT_NE(OUTPUT.find("free="), std::string::npos);
}

/* ----------------------------- HugepageNodeStatus Tests ----------------------------- */

/** @test Default HugepageNodeStatus is zeroed. */
TEST(HugepageNodeStatusTest, DefaultZero) {
  const HugepageNodeStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.nodeId, -1);
  EXPECT_EQ(DEFAULT.total, 0U);
  EXPECT_EQ(DEFAULT.free, 0U);
  EXPECT_EQ(DEFAULT.surplus, 0U);
}

/* ----------------------------- HugepageStatus Tests ----------------------------- */

/** @test Default HugepageStatus is zeroed. */
TEST(HugepageStatusDefaultTest, DefaultZero) {
  const HugepageStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.sizeCount, 0U);
  EXPECT_EQ(DEFAULT.nodeCount, 0U);
  EXPECT_FALSE(DEFAULT.hasHugepages());
  EXPECT_EQ(DEFAULT.totalBytes(), 0U);
  EXPECT_EQ(DEFAULT.freeBytes(), 0U);
  EXPECT_EQ(DEFAULT.usedBytes(), 0U);
}

/** @test hasHugepages() detects configured pages. */
TEST(HugepageStatusHelperTest, HasHugepagesDetection) {
  HugepageStatus status{};

  // Empty
  EXPECT_FALSE(status.hasHugepages());

  // With total pages
  status.sizeCount = 1;
  status.sizes[0].total = 10;
  EXPECT_TRUE(status.hasHugepages());

  // With only surplus
  status.sizes[0].total = 0;
  status.sizes[0].surplus = 5;
  EXPECT_TRUE(status.hasHugepages());
}

/** @test totalBytes() sums across all sizes. */
TEST(HugepageStatusHelperTest, TotalBytesSum) {
  HugepageStatus status{};
  status.sizeCount = 2;

  status.sizes[0].pageSize = 2 * 1024 * 1024; // 2 MiB
  status.sizes[0].total = 100;
  status.sizes[0].surplus = 0;

  status.sizes[1].pageSize = 1024 * 1024 * 1024; // 1 GiB
  status.sizes[1].total = 2;
  status.sizes[1].surplus = 0;

  const std::uint64_t EXPECTED = (100ULL * 2 * 1024 * 1024) + (2ULL * 1024 * 1024 * 1024);
  EXPECT_EQ(status.totalBytes(), EXPECTED);
}

/** @test findSize() returns correct pointer. */
TEST(HugepageStatusHelperTest, FindSize) {
  HugepageStatus status{};
  status.sizeCount = 2;
  status.sizes[0].pageSize = 2 * 1024 * 1024;
  status.sizes[0].total = 100;
  status.sizes[1].pageSize = 1024 * 1024 * 1024;
  status.sizes[1].total = 2;

  const auto* FOUND_2M = status.findSize(2 * 1024 * 1024);
  ASSERT_NE(FOUND_2M, nullptr);
  EXPECT_EQ(FOUND_2M->total, 100U);

  const auto* FOUND_1G = status.findSize(1024 * 1024 * 1024);
  ASSERT_NE(FOUND_1G, nullptr);
  EXPECT_EQ(FOUND_1G->total, 2U);

  const auto* NOT_FOUND = status.findSize(4 * 1024);
  EXPECT_EQ(NOT_FOUND, nullptr);
}

/** @test toString() produces valid output with no hugepages. */
TEST(HugepageStatusHelperTest, ToStringEmpty) {
  const HugepageStatus EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("none"), std::string::npos);
}

/** @test toString() produces valid output with hugepages. */
TEST(HugepageStatusHelperTest, ToStringWithPages) {
  HugepageStatus status{};
  status.sizeCount = 1;
  status.sizes[0].pageSize = 2 * 1024 * 1024;
  status.sizes[0].total = 50;
  status.sizes[0].free = 20;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Hugepages:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("MiB"), std::string::npos);
}

/* ----------------------------- Live System Tests ----------------------------- */

/** @test sizeCount is within bounds. */
TEST_F(HugepageStatusTest, SizeCountWithinBounds) { EXPECT_LE(status_.sizeCount, HP_MAX_SIZES); }

/** @test nodeCount is within bounds. */
TEST_F(HugepageStatusTest, NodeCountWithinBounds) {
  EXPECT_LE(status_.nodeCount, HP_MAX_NUMA_NODES);
}

/** @test Empty result is valid (hugepages may not be configured). */
TEST_F(HugepageStatusTest, EmptyResultValid) {
  if (!hasHugepages_) {
    GTEST_LOG_(INFO) << "No hugepages configured on this system";
    EXPECT_EQ(status_.sizeCount, 0U);
  }
}

/** @test Page sizes are valid powers of two. */
TEST_F(HugepageStatusTest, PageSizesPowersOfTwo) {
  auto isPowerOfTwo = [](std::uint64_t n) { return n > 0 && (n & (n - 1)) == 0; };

  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    EXPECT_TRUE(isPowerOfTwo(status_.sizes[i].pageSize))
        << "Size " << status_.sizes[i].pageSize << " is not a power of two";
  }
}

/** @test Page sizes are larger than base page (4KB minimum). */
TEST_F(HugepageStatusTest, PageSizesLargerThanBase) {
  constexpr std::uint64_t MIN_HUGEPAGE = 4096;

  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    EXPECT_GE(status_.sizes[i].pageSize, MIN_HUGEPAGE) << "Size index " << i;
  }
}

/** @test Page sizes are sorted in ascending order. */
TEST_F(HugepageStatusTest, PageSizesSorted) {
  for (std::size_t i = 1; i < status_.sizeCount; ++i) {
    EXPECT_GT(status_.sizes[i].pageSize, status_.sizes[i - 1].pageSize)
        << "Sizes not sorted at index " << i;
  }
}

/** @test free <= total + surplus for each size. */
TEST_F(HugepageStatusTest, FreeDoesNotExceedTotal) {
  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    const auto& SIZE = status_.sizes[i];
    EXPECT_LE(SIZE.free, SIZE.total + SIZE.surplus)
        << "Size " << SIZE.pageSize << ": free=" << SIZE.free
        << " > total+surplus=" << (SIZE.total + SIZE.surplus);
  }
}

/** @test reserved <= total for each size. */
TEST_F(HugepageStatusTest, ReservedDoesNotExceedTotal) {
  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    const auto& SIZE = status_.sizes[i];
    EXPECT_LE(SIZE.reserved, SIZE.total)
        << "Size " << SIZE.pageSize << ": reserved=" << SIZE.reserved << " > total=" << SIZE.total;
  }
}

/** @test usedBytes() + freeBytes() == totalBytes(). */
TEST_F(HugepageStatusTest, UsedPlusFreeEqualsTotal) {
  const std::uint64_t TOTAL = status_.totalBytes();
  const std::uint64_t USED = status_.usedBytes();
  const std::uint64_t FREE = status_.freeBytes();

  EXPECT_EQ(USED + FREE, TOTAL);
}

/** @test Per-size usedBytes() + freeBytes() == totalBytes(). */
TEST_F(HugepageStatusTest, PerSizeUsedPlusFreeEqualsTotal) {
  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    const auto& SIZE = status_.sizes[i];
    EXPECT_EQ(SIZE.usedBytes() + SIZE.freeBytes(), SIZE.totalBytes()) << "Size index " << i;
  }
}

/** @test Per-NUMA node IDs are valid. */
TEST_F(HugepageStatusTest, NodeIdsValid) {
  for (std::size_t s = 0; s < status_.sizeCount; ++s) {
    for (std::size_t n = 0; n < status_.nodeCount; ++n) {
      const auto& NODE = status_.perNode[s][n];
      if (NODE.nodeId >= 0) {
        EXPECT_LT(NODE.nodeId, 256) << "Node ID unreasonably large";
      }
    }
  }
}

/** @test Per-NUMA totals sum to global total (approximately). */
TEST_F(HugepageStatusTest, PerNumaTotalsMatchGlobal) {
  if (status_.nodeCount == 0) {
    GTEST_SKIP() << "No NUMA node data available";
  }

  for (std::size_t s = 0; s < status_.sizeCount; ++s) {
    std::uint64_t nodeSum = 0;
    for (std::size_t n = 0; n < status_.nodeCount; ++n) {
      nodeSum += status_.perNode[s][n].total;
    }

    // Node totals should equal global total (may differ slightly due to timing)
    EXPECT_EQ(nodeSum, status_.sizes[s].total) << "Size index " << s << ": node sum=" << nodeSum
                                               << " != global total=" << status_.sizes[s].total;
  }
}

/** @test getHugepageStatus() is deterministic. */
TEST_F(HugepageStatusTest, Deterministic) {
  const HugepageStatus STATUS2 = getHugepageStatus();

  EXPECT_EQ(status_.sizeCount, STATUS2.sizeCount);

  for (std::size_t i = 0; i < status_.sizeCount; ++i) {
    EXPECT_EQ(status_.sizes[i].pageSize, STATUS2.sizes[i].pageSize);
    // Total should be stable (unless admin is modifying concurrently)
    EXPECT_EQ(status_.sizes[i].total, STATUS2.sizes[i].total);
  }
}

/** @test toString() produces non-empty output. */
TEST_F(HugepageStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString() contains expected sections when hugepages exist. */
TEST_F(HugepageStatusTest, ToStringContainsSections) {
  if (!hasHugepages_) {
    GTEST_SKIP() << "No hugepages configured";
  }

  const std::string OUTPUT = status_.toString();
  EXPECT_NE(OUTPUT.find("Hugepages:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Total:"), std::string::npos);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test findSize() with zero returns nullptr. */
TEST(HugepageStatusEdgeTest, FindSizeZero) {
  const HugepageStatus STATUS{};
  EXPECT_EQ(STATUS.findSize(0), nullptr);
}

/** @test findSize() with very large size returns nullptr. */
TEST(HugepageStatusEdgeTest, FindSizeLarge) {
  const HugepageStatus STATUS{};
  EXPECT_EQ(STATUS.findSize(1ULL << 40), nullptr);
}