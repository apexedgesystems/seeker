/**
 * @file CpuFeatures_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuFeatures.
 *
 * Notes:
 *  - Tests verify invariants and feature dependencies, not specific flags.
 *  - On non-x86, features default to false (which is valid behavior).
 */

#include "src/cpu/inc/CpuFeatures.hpp"

#include <gtest/gtest.h>

#include <cstring> // std::strlen
#include <string>

using seeker::cpu::BRAND_STRING_SIZE;
using seeker::cpu::CpuFeatures;
using seeker::cpu::getCpuFeatures;
using seeker::cpu::VENDOR_STRING_SIZE;

class CpuFeaturesTest : public ::testing::Test {
protected:
  CpuFeatures features_{};

  void SetUp() override { features_ = getCpuFeatures(); }
};

/* ----------------------------- String Field Tests ----------------------------- */

/** @test Vendor string is null-terminated and within bounds. */
TEST_F(CpuFeaturesTest, VendorStringValid) {
  const std::size_t LEN = std::strlen(features_.vendor.data());
  EXPECT_LT(LEN, VENDOR_STRING_SIZE);

  // Verify null terminator exists within array
  bool foundNull = false;
  for (std::size_t i = 0; i < VENDOR_STRING_SIZE; ++i) {
    if (features_.vendor[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

/** @test Brand string is null-terminated and within bounds. */
TEST_F(CpuFeaturesTest, BrandStringValid) {
  const std::size_t LEN = std::strlen(features_.brand.data());
  EXPECT_LT(LEN, BRAND_STRING_SIZE);

  // Verify null terminator exists within array
  bool foundNull = false;
  for (std::size_t i = 0; i < BRAND_STRING_SIZE; ++i) {
    if (features_.brand[i] == '\0') {
      foundNull = true;
      break;
    }
  }
  EXPECT_TRUE(foundNull);
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

/** @test On x86, vendor string should be non-empty. */
TEST_F(CpuFeaturesTest, X86VendorNonEmpty) {
  const std::size_t LEN = std::strlen(features_.vendor.data());
  EXPECT_GT(LEN, 0U);

  // Should be one of the known vendors
  const std::string VENDOR(features_.vendor.data());
  const bool IS_KNOWN = (VENDOR == "GenuineIntel") || (VENDOR == "AuthenticAMD") ||
                        (VENDOR == "GenuineIntel") || (VENDOR == "HygonGenuine") ||
                        (VENDOR == "CentaurHauls") || (VENDOR == "VIA VIA VIA ");

  // Allow unknown vendors, just log a note
  if (!IS_KNOWN) {
    GTEST_LOG_(INFO) << "Unknown vendor: " << VENDOR;
  }
}

/** @test On x86, modern CPUs should have at least SSE2. */
TEST_F(CpuFeaturesTest, X86BaselineFeatures) {
  // SSE2 has been required for x86-64 since inception (2003)
  // This test may fail on very old 32-bit systems
#if defined(__x86_64__) || defined(_M_X64)
  EXPECT_TRUE(features_.sse);
  EXPECT_TRUE(features_.sse2);
#endif
}

#endif // x86

/* ----------------------------- Feature Dependency Tests ----------------------------- */

/** @test SSE dependency chain: SSE2 implies SSE. */
TEST_F(CpuFeaturesTest, SseDependencyChain) {
  if (features_.sse2) {
    EXPECT_TRUE(features_.sse) << "SSE2 requires SSE";
  }
  if (features_.sse3) {
    EXPECT_TRUE(features_.sse2) << "SSE3 requires SSE2";
  }
  if (features_.ssse3) {
    EXPECT_TRUE(features_.sse3) << "SSSE3 requires SSE3";
  }
  if (features_.sse41) {
    EXPECT_TRUE(features_.ssse3) << "SSE4.1 requires SSSE3";
  }
  if (features_.sse42) {
    EXPECT_TRUE(features_.sse41) << "SSE4.2 requires SSE4.1";
  }
}

/** @test AVX dependency chain: AVX2 implies AVX. */
TEST_F(CpuFeaturesTest, AvxDependencyChain) {
  if (features_.avx2) {
    EXPECT_TRUE(features_.avx) << "AVX2 requires AVX";
  }
}

/** @test AVX-512 variants imply AVX-512F. */
TEST_F(CpuFeaturesTest, Avx512DependencyChain) {
  if (features_.avx512dq) {
    EXPECT_TRUE(features_.avx512f) << "AVX-512DQ requires AVX-512F";
  }
  if (features_.avx512cd) {
    EXPECT_TRUE(features_.avx512f) << "AVX-512CD requires AVX-512F";
  }
  if (features_.avx512bw) {
    EXPECT_TRUE(features_.avx512f) << "AVX-512BW requires AVX-512F";
  }
  if (features_.avx512vl) {
    EXPECT_TRUE(features_.avx512f) << "AVX-512VL requires AVX-512F";
  }
}

/** @test FMA typically comes with AVX. */
TEST_F(CpuFeaturesTest, FmaImpliesAvx) {
  if (features_.fma) {
    EXPECT_TRUE(features_.avx) << "FMA typically requires AVX";
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test toString produces non-empty output. */
TEST_F(CpuFeaturesTest, ToStringNonEmpty) {
  const std::string OUTPUT = features_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toString contains expected sections. */
TEST_F(CpuFeaturesTest, ToStringContainsSections) {
  const std::string OUTPUT = features_.toString();

  EXPECT_NE(OUTPUT.find("Vendor:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Brand:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("SSE:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("AVX:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Invariant TSC:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed CpuFeatures has all flags false. */
TEST(CpuFeaturesDefaultTest, AllFlagsFalse) {
  const CpuFeatures DEFAULT{};

  EXPECT_FALSE(DEFAULT.sse);
  EXPECT_FALSE(DEFAULT.sse2);
  EXPECT_FALSE(DEFAULT.avx);
  EXPECT_FALSE(DEFAULT.avx512f);
  EXPECT_FALSE(DEFAULT.fma);
  EXPECT_FALSE(DEFAULT.aes);
  EXPECT_FALSE(DEFAULT.invariantTsc);
}

/** @test Default-constructed CpuFeatures has empty strings. */
TEST(CpuFeaturesDefaultTest, EmptyStrings) {
  const CpuFeatures DEFAULT{};

  EXPECT_EQ(DEFAULT.vendor[0], '\0');
  EXPECT_EQ(DEFAULT.brand[0], '\0');
}