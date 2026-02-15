/**
 * @file CpuFeatures.cpp
 * @brief CPU ISA feature detection via CPUID (x86/x86_64).
 * @note Uses compiler intrinsics; returns safe defaults on non-x86.
 */

#include "src/cpu/inc/CpuFeatures.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <cpuid.h>
#define SEEKER_HAS_CPUID 1
#else
#define SEEKER_HAS_CPUID 0
#endif

#include <cstring> // std::memcpy

#include <fmt/core.h>

namespace seeker {

namespace cpu {

namespace {

#if SEEKER_HAS_CPUID

/// Execute CPUID with leaf and subleaf.
inline void cpuidEx(unsigned int leaf, unsigned int subleaf, unsigned int& eax, unsigned int& ebx,
                    unsigned int& ecx, unsigned int& edx) noexcept {
  __get_cpuid_count(leaf, subleaf, &eax, &ebx, &ecx, &edx);
}

/// Execute CPUID with leaf only (subleaf = 0).
inline void cpuid(unsigned int leaf, unsigned int& eax, unsigned int& ebx, unsigned int& ecx,
                  unsigned int& edx) noexcept {
  __get_cpuid(leaf, &eax, &ebx, &ecx, &edx);
}

/// Extract vendor string from CPUID leaf 0 (EBX-EDX-ECX order).
inline void extractVendor(std::array<char, VENDOR_STRING_SIZE>& out) noexcept {
  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
  cpuid(0U, eax, ebx, ecx, edx);

  std::memcpy(out.data() + 0, &ebx, 4);
  std::memcpy(out.data() + 4, &edx, 4);
  std::memcpy(out.data() + 8, &ecx, 4);
  out[12] = '\0';
}

/// Extract brand string from CPUID leaves 0x80000002-0x80000004.
inline void extractBrand(std::array<char, BRAND_STRING_SIZE>& out) noexcept {
  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

  // Check extended leaf availability
  cpuid(0x80000000U, eax, ebx, ecx, edx);
  if (eax < 0x80000004U) {
    out[0] = '\0';
    return;
  }

  // 3 leaves x 16 bytes = 48 bytes
  std::array<unsigned int, 12> words{};
  cpuid(0x80000002U, words[0], words[1], words[2], words[3]);
  cpuid(0x80000003U, words[4], words[5], words[6], words[7]);
  cpuid(0x80000004U, words[8], words[9], words[10], words[11]);

  std::memcpy(out.data(), words.data(), 48);
  out[48] = '\0';
}

#endif // SEEKER_HAS_CPUID

} // namespace

/* ----------------------------- API ----------------------------- */

std::string CpuFeatures::toString() const {
  return fmt::format("Vendor: {}\n"
                     "Brand:  {}\n"
                     "SSE: {} {} {} {} {} {}  |  AVX: {} {}  |  AVX-512: {} {} {} {} {}\n"
                     "FMA: {}  BMI1: {}  BMI2: {}  AES: {}  SHA: {}  POPCNT: {}\n"
                     "RDRAND: {}  RDSEED: {}  Invariant TSC: {}",
                     vendor.data(), brand.data(), sse, sse2, sse3, ssse3, sse41, sse42, avx, avx2,
                     avx512f, avx512dq, avx512cd, avx512bw, avx512vl, fma, bmi1, bmi2, aes, sha,
                     popcnt, rdrand, rdseed, invariantTsc);
}

CpuFeatures getCpuFeatures() noexcept {
  CpuFeatures f{};

#if SEEKER_HAS_CPUID
  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

  // Leaf 0: Vendor and max basic leaf
  cpuid(0U, eax, ebx, ecx, edx);
  const unsigned int MAX_BASIC = eax;

  extractVendor(f.vendor);
  extractBrand(f.brand);

  // Leaf 1: Basic feature flags
  if (MAX_BASIC >= 1U) {
    cpuid(1U, eax, ebx, ecx, edx);

    // EDX flags
    f.sse = (edx & (1U << 25)) != 0U;
    f.sse2 = (edx & (1U << 26)) != 0U;

    // ECX flags
    f.sse3 = (ecx & (1U << 0)) != 0U;
    f.ssse3 = (ecx & (1U << 9)) != 0U;
    f.fma = (ecx & (1U << 12)) != 0U;
    f.sse41 = (ecx & (1U << 19)) != 0U;
    f.sse42 = (ecx & (1U << 20)) != 0U;
    f.popcnt = (ecx & (1U << 23)) != 0U;
    f.aes = (ecx & (1U << 25)) != 0U;
    f.avx = (ecx & (1U << 28)) != 0U;
    f.rdrand = (ecx & (1U << 30)) != 0U;
  }

  // Leaf 7: Extended feature flags
  if (MAX_BASIC >= 7U) {
    cpuidEx(7U, 0U, eax, ebx, ecx, edx);

    // EBX flags
    f.bmi1 = (ebx & (1U << 3)) != 0U;
    f.avx2 = (ebx & (1U << 5)) != 0U;
    f.bmi2 = (ebx & (1U << 8)) != 0U;
    f.avx512f = (ebx & (1U << 16)) != 0U;
    f.avx512dq = (ebx & (1U << 17)) != 0U;
    f.rdseed = (ebx & (1U << 18)) != 0U;
    f.avx512cd = (ebx & (1U << 28)) != 0U;
    f.sha = (ebx & (1U << 29)) != 0U;
    f.avx512bw = (ebx & (1U << 30)) != 0U;
    f.avx512vl = (ebx & (1U << 31)) != 0U;
  }

  // Extended leaf 0x80000007: Invariant TSC
  cpuid(0x80000000U, eax, ebx, ecx, edx);
  if (eax >= 0x80000007U) {
    cpuid(0x80000007U, eax, ebx, ecx, edx);
    f.invariantTsc = (edx & (1U << 8)) != 0U;
  }

#endif // SEEKER_HAS_CPUID

  return f;
}

} // namespace cpu

} // namespace seeker