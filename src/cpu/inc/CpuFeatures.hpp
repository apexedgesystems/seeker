#ifndef SEEKER_CPU_FEATURES_HPP
#define SEEKER_CPU_FEATURES_HPP
/**
 * @file CpuFeatures.hpp
 * @brief CPU ISA feature flags via CPUID (x86/x86_64).
 * @note x86/x86_64 only. Returns safe defaults on other architectures.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <string>  // std::string

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum vendor string length (12 chars from CPUID + null).
inline constexpr std::size_t VENDOR_STRING_SIZE = 13;

/// Maximum brand string length (48 chars from CPUID + null).
inline constexpr std::size_t BRAND_STRING_SIZE = 49;

/* ----------------------------- CpuFeatures ----------------------------- */

/**
 * @brief CPU ISA feature flags and identification.
 *
 * All boolean flags default to false when CPUID is unavailable or
 * the feature is not present. String arrays are null-terminated.
 */
struct CpuFeatures {
  // SIMD: SSE family
  bool sse{false};
  bool sse2{false};
  bool sse3{false};
  bool ssse3{false};
  bool sse41{false};
  bool sse42{false};

  // SIMD: AVX family
  bool avx{false};
  bool avx2{false};
  bool avx512f{false};
  bool avx512dq{false};
  bool avx512cd{false};
  bool avx512bw{false};
  bool avx512vl{false};

  // Math and bit manipulation
  bool fma{false};
  bool bmi1{false};
  bool bmi2{false};

  // Cryptography
  bool aes{false};
  bool sha{false};

  // Misc
  bool popcnt{false};
  bool rdrand{false};       ///< RDRAND instruction available
  bool rdseed{false};       ///< RDSEED instruction available
  bool invariantTsc{false}; ///< Invariant TSC (reliable for timing)

  // Identification (fixed-size, RT-safe)
  std::array<char, VENDOR_STRING_SIZE> vendor{}; ///< e.g., "GenuineIntel", "AuthenticAMD"
  std::array<char, BRAND_STRING_SIZE> brand{};   ///< Full model string if available

  /// @brief Human-readable summary (multi-line).
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query CPU features using CPUID.
 * @return Populated feature flags; defaults when CPUID unavailable.
 * @note RT-safe: No heap allocation, bounded CPUID calls.
 */
[[nodiscard]] CpuFeatures getCpuFeatures() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_FEATURES_HPP