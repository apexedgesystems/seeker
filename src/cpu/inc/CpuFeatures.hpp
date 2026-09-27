#ifndef SEEKER_CPU_FEATURES_HPP
#define SEEKER_CPU_FEATURES_HPP
/**
 * @file CpuFeatures.hpp
 * @brief CPU ISA feature flags and identification (x86/x86_64 and aarch64).
 * @note x86/x86_64: CPUID. aarch64: getauxval(AT_HWCAP/AT_HWCAP2) plus MIDR
 *       implementer/part from /proc/cpuinfo. Safe defaults on other architectures.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint8_t, std::uint16_t
#include <string>  // std::string

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum vendor string length (12 chars from CPUID + null).
inline constexpr std::size_t VENDOR_STRING_SIZE = 13;

/// Maximum brand string length (48 chars from CPUID + null).
inline constexpr std::size_t BRAND_STRING_SIZE = 49;

/// Maximum distinct aarch64 core types tracked (heterogeneous core clusters).
inline constexpr std::size_t ARM_MAX_CORE_TYPES = 4;

/* ----------------------------- CpuArch ----------------------------- */

/**
 * @brief CPU architecture family the library was built for.
 *
 * Use to decide which feature fields are meaningful (e.g., skip invariant
 * TSC checks when not x86).
 */
enum class CpuArch : std::uint8_t {
  UNKNOWN = 0, ///< Unsupported architecture (all flags false)
  X86,         ///< x86 / x86_64 (CPUID fields populated)
  AARCH64,     ///< ARMv8-A / ARMv9-A 64-bit (HWCAP fields populated)
};

/// @brief Human-readable architecture name ("x86", "aarch64", "unknown").
/// @note RT-safe: Returns static string.
[[nodiscard]] const char* toString(CpuArch arch) noexcept;

/* ----------------------------- CpuFeatures ----------------------------- */

/**
 * @brief CPU ISA feature flags and identification.
 *
 * All boolean flags default to false when detection is unavailable or
 * the feature is not present. String arrays are null-terminated.
 * Fields for the other architecture always remain false.
 */
struct CpuFeatures {
  /// Architecture that populated this struct (UNKNOWN when default-constructed).
  CpuArch arch{CpuArch::UNKNOWN};

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

  // Cryptography (shared: set on both x86 and aarch64)
  bool aes{false}; ///< x86 AES-NI; aarch64 AES (HWCAP_AES)
  bool sha{false}; ///< x86 SHA extensions; aarch64 SHA1 && SHA2

  // Misc
  bool popcnt{false};
  bool rdrand{false};       ///< RDRAND instruction available
  bool rdseed{false};       ///< RDSEED instruction available
  bool invariantTsc{false}; ///< Invariant TSC (reliable for timing); x86 only

  // aarch64: SIMD
  bool neon{false};    ///< Advanced SIMD (asimd)
  bool sve{false};     ///< Scalable Vector Extension
  bool sve2{false};    ///< SVE2
  bool dotprod{false}; ///< SIMD dot product (asimddp)
  bool i8mm{false};    ///< Int8 matrix multiply
  bool bf16{false};    ///< BFloat16

  // aarch64: Cryptography and CRC
  bool pmull{false};  ///< Polynomial multiply (PMULL)
  bool sha1{false};   ///< SHA-1
  bool sha2{false};   ///< SHA-256
  bool sha3{false};   ///< SHA-3
  bool sha512{false}; ///< SHA-512
  bool crc32{false};  ///< CRC32 instructions

  // aarch64: Misc
  bool atomics{false}; ///< Large System Extensions (LSE) atomic instructions

  // aarch64: MIDR identification (first CPU; 0 when unavailable)
  std::uint16_t armImplementer{0}; ///< MIDR implementer (e.g., 0x41 = ARM)
  std::uint16_t armPart{0};        ///< MIDR primary part number (e.g., 0xd08)
  std::array<std::uint16_t, ARM_MAX_CORE_TYPES> armParts{}; ///< Distinct parts, in order seen
  std::uint8_t armPartCount{0};                             ///< Valid entries in armParts

  // Identification (fixed-size, RT-safe)
  std::array<char, VENDOR_STRING_SIZE> vendor{}; ///< e.g., "GenuineIntel", "AuthenticAMD", "ARM"
  std::array<char, BRAND_STRING_SIZE> brand{};   ///< Full model string if available

  /// @brief True when x86-only fields (SSE/AVX/invariantTsc, ...) are meaningful.
  [[nodiscard]] constexpr bool isX86() const noexcept { return arch == CpuArch::X86; }

  /// @brief True when aarch64-only fields (neon/sve/pmull, ...) are meaningful.
  [[nodiscard]] constexpr bool isArm() const noexcept { return arch == CpuArch::AARCH64; }

  /// @brief Human-readable summary (multi-line).
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query CPU features for the running architecture.
 * @return Populated feature flags; defaults when detection unavailable.
 * @note x86: RT-safe (no heap allocation, bounded CPUID calls).
 * @note aarch64: No heap allocation, but reads /proc/cpuinfo (open/read into a
 *       fixed stack buffer) for MIDR identification. Syscall latency is
 *       unbounded; query once at startup, not from an RT loop.
 */
[[nodiscard]] CpuFeatures getCpuFeatures() noexcept;

/**
 * @brief Map an ARM MIDR implementer code to a vendor name.
 * @param implementer MIDR implementer (e.g., 0x41).
 * @return Static name (e.g., "ARM", "NVIDIA"), or nullptr if unknown.
 * @note RT-safe: Table lookup only. Available on all architectures.
 */
[[nodiscard]] const char* armImplementerName(std::uint16_t implementer) noexcept;

/**
 * @brief Map an ARM MIDR implementer + part number to a core name.
 * @param implementer MIDR implementer (e.g., 0x41).
 * @param part MIDR primary part number (e.g., 0xd08).
 * @return Static core name from the MIDR part table, or nullptr if unknown.
 * @note RT-safe: Table lookup only. Available on all architectures.
 */
[[nodiscard]] const char* armPartName(std::uint16_t implementer, std::uint16_t part) noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_FEATURES_HPP
