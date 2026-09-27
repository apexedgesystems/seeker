/**
 * @file CpuFeatures_uTest.cpp
 * @brief Unit tests for seeker::cpu::CpuFeatures.
 *
 * Notes:
 *  - Tests verify invariants and feature dependencies, not specific flags.
 *  - Fields for the other architecture must stay false.
 *  - On unsupported architectures, features default to false (valid behavior).
 */

#include "src/cpu/inc/CpuFeatures.hpp"

#include <gtest/gtest.h>

#include <cstdint> // std::uint16_t
#include <cstdlib> // std::strtoull
#include <cstring> // std::strlen
#include <fstream>
#include <string>

using seeker::cpu::armImplementerName;
using seeker::cpu::armPartName;
using seeker::cpu::BRAND_STRING_SIZE;
using seeker::cpu::CpuArch;
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

/** @test On x86, architecture is X86 and aarch64-only fields stay false. */
TEST_F(CpuFeaturesTest, X86ArchAndArmFieldsFalse) {
  EXPECT_EQ(features_.arch, CpuArch::X86);
  EXPECT_TRUE(features_.isX86());
  EXPECT_FALSE(features_.isArm());

  EXPECT_FALSE(features_.neon);
  EXPECT_FALSE(features_.sve);
  EXPECT_FALSE(features_.sve2);
  EXPECT_FALSE(features_.pmull);
  EXPECT_FALSE(features_.crc32);
  EXPECT_FALSE(features_.atomics);
  EXPECT_EQ(features_.armImplementer, 0U);
  EXPECT_EQ(features_.armPartCount, 0U);
}

#endif // x86

#if defined(__aarch64__)

/** @test On aarch64, vendor and brand come from the MIDR implementer and part tables. */
TEST_F(CpuFeaturesTest, Aarch64IdentityFromMidr) {
  EXPECT_EQ(features_.arch, CpuArch::AARCH64);
  EXPECT_TRUE(features_.isArm());
  EXPECT_FALSE(features_.isX86());

  EXPECT_NE(features_.armImplementer, 0U);
  ASSERT_GE(features_.armPartCount, 1U);
  EXPECT_LE(features_.armPartCount, seeker::cpu::ARM_MAX_CORE_TYPES);
  EXPECT_EQ(features_.armParts[0], features_.armPart);

  const std::string VENDOR = features_.vendor.data();
  const std::string BRAND = features_.brand.data();
  const char* IMPL_NAME = armImplementerName(features_.armImplementer);
  if (IMPL_NAME != nullptr) {
    EXPECT_EQ(VENDOR, IMPL_NAME);
    EXPECT_EQ(BRAND.rfind(IMPL_NAME, 0), 0U) << BRAND;
  } else {
    EXPECT_EQ(VENDOR.rfind("0x", 0), 0U) << VENDOR;
  }
  const char* PART_NAME = armPartName(features_.armImplementer, features_.armPart);
  if (PART_NAME != nullptr) {
    EXPECT_NE(BRAND.find(PART_NAME), std::string::npos) << BRAND;
  }

  GTEST_LOG_(INFO) << "Vendor: " << VENDOR << "  Brand: " << BRAND;
}

/** @test On aarch64, implementer and part match cpu0's MIDR_EL1 register when sysfs exposes it. */
TEST_F(CpuFeaturesTest, Aarch64MidrMatchesSysfsRegister) {
  std::ifstream file("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1");
  std::string line;
  if (!file || !std::getline(file, line) || line.empty()) {
    GTEST_SKIP() << "midr_el1 not exposed in sysfs";
  }
  const unsigned long long MIDR = std::strtoull(line.c_str(), nullptr, 16);

  // MIDR_EL1: implementer [31:24], primary part number [15:4]
  EXPECT_EQ(features_.armImplementer, static_cast<std::uint16_t>((MIDR >> 24) & 0xFFULL)) << line;
  EXPECT_EQ(features_.armPart, static_cast<std::uint16_t>((MIDR >> 4) & 0xFFFULL)) << line;
}

/** @test On aarch64 Linux, Advanced SIMD (NEON) is mandatory for ARMv8-A. */
TEST_F(CpuFeaturesTest, Aarch64BaselineNeon) { EXPECT_TRUE(features_.neon); }

/** @test On aarch64, x86-only fields stay false (invariant TSC not applicable). */
TEST_F(CpuFeaturesTest, Aarch64X86FieldsFalse) {
  EXPECT_FALSE(features_.sse);
  EXPECT_FALSE(features_.sse2);
  EXPECT_FALSE(features_.avx);
  EXPECT_FALSE(features_.avx512f);
  EXPECT_FALSE(features_.popcnt);
  EXPECT_FALSE(features_.rdrand);
  EXPECT_FALSE(features_.invariantTsc);
}

#endif // __aarch64__

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

/** @test aarch64 ID register dependencies (trivially true on x86). */
TEST_F(CpuFeaturesTest, ArmDependencyChain) {
  if (features_.sve2) {
    EXPECT_TRUE(features_.sve) << "SVE2 requires SVE";
  }
  if (features_.pmull) {
    EXPECT_TRUE(features_.aes) << "PMULL implies AES (ID_AA64ISAR0.AES)";
  }
  if (features_.sha512) {
    EXPECT_TRUE(features_.sha2) << "SHA512 implies SHA256 (ID_AA64ISAR0.SHA2)";
  }
  if (features_.isArm()) {
    EXPECT_EQ(features_.sha, features_.sha1 && features_.sha2) << "sha maps to SHA1 && SHA2";
  }
}

/* ----------------------------- ARM ID Lookup ----------------------------- */

/** @test Implementer lookup returns known vendors and nullptr for unknown. */
TEST(CpuFeaturesArmIdTest, ImplementerNames) {
  EXPECT_STREQ(armImplementerName(0x41), "ARM");
  EXPECT_STREQ(armImplementerName(0x4e), "NVIDIA");
  EXPECT_STREQ(armImplementerName(0x51), "Qualcomm");
  EXPECT_STREQ(armImplementerName(0x61), "Apple");
  EXPECT_EQ(armImplementerName(0x00), nullptr);

  // Every known name must fit the vendor string
  for (std::uint16_t id = 0; id <= 0xff; ++id) {
    const char* NAME = armImplementerName(id);
    if (NAME != nullptr) {
      EXPECT_LT(std::strlen(NAME), VENDOR_STRING_SIZE) << NAME;
    }
  }
}

/** @test Part lookup is implementer-scoped and covers common cores. */
TEST(CpuFeaturesArmIdTest, PartNames) {
  EXPECT_STREQ(armPartName(0x41, 0xd03), "Cortex-A53");
  EXPECT_STREQ(armPartName(0x41, 0xd08), "Cortex-A72");
  EXPECT_STREQ(armPartName(0x41, 0xd0c), "Neoverse-N1");
  EXPECT_STREQ(armPartName(0x41, 0xd83), "Neoverse-V3AE");
  EXPECT_STREQ(armPartName(0x4e, 0x004), "Carmel");
  EXPECT_EQ(armPartName(0x41, 0xfff), nullptr);
  EXPECT_EQ(armPartName(0x51, 0xd08), nullptr);
}

/** @test Architecture names are stable. */
TEST(CpuFeaturesArmIdTest, ArchToString) {
  EXPECT_STREQ(seeker::cpu::toString(CpuArch::X86), "x86");
  EXPECT_STREQ(seeker::cpu::toString(CpuArch::AARCH64), "aarch64");
  EXPECT_STREQ(seeker::cpu::toString(CpuArch::UNKNOWN), "unknown");
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

  if (features_.isArm()) {
    EXPECT_NE(OUTPUT.find("NEON:"), std::string::npos);
    EXPECT_NE(OUTPUT.find("SVE:"), std::string::npos);
    EXPECT_NE(OUTPUT.find("PMULL:"), std::string::npos);
    EXPECT_EQ(OUTPUT.find("Invariant TSC:"), std::string::npos);
  } else {
    EXPECT_NE(OUTPUT.find("SSE:"), std::string::npos);
    EXPECT_NE(OUTPUT.find("AVX:"), std::string::npos);
    EXPECT_NE(OUTPUT.find("Invariant TSC:"), std::string::npos);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed CpuFeatures has all flags false. */
TEST(CpuFeaturesDefaultTest, AllFlagsFalse) {
  const CpuFeatures DEFAULT{};

  EXPECT_EQ(DEFAULT.arch, CpuArch::UNKNOWN);
  EXPECT_FALSE(DEFAULT.isX86());
  EXPECT_FALSE(DEFAULT.isArm());
  EXPECT_FALSE(DEFAULT.sse);
  EXPECT_FALSE(DEFAULT.sse2);
  EXPECT_FALSE(DEFAULT.avx);
  EXPECT_FALSE(DEFAULT.avx512f);
  EXPECT_FALSE(DEFAULT.fma);
  EXPECT_FALSE(DEFAULT.aes);
  EXPECT_FALSE(DEFAULT.invariantTsc);
  EXPECT_FALSE(DEFAULT.neon);
  EXPECT_FALSE(DEFAULT.sve);
  EXPECT_FALSE(DEFAULT.crc32);
  EXPECT_EQ(DEFAULT.armPartCount, 0U);
}

/** @test Default-constructed CpuFeatures has empty strings. */
TEST(CpuFeaturesDefaultTest, EmptyStrings) {
  const CpuFeatures DEFAULT{};

  EXPECT_EQ(DEFAULT.vendor[0], '\0');
  EXPECT_EQ(DEFAULT.brand[0], '\0');
}
