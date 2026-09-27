/**
 * @file CpuFeatures.cpp
 * @brief CPU ISA feature detection (x86 CPUID, aarch64 HWCAP + MIDR).
 * @note x86: compiler CPUID intrinsics. aarch64: getauxval(AT_HWCAP/AT_HWCAP2)
 *       with /proc/cpuinfo "Features" fallback; MIDR from /proc/cpuinfo or sysfs.
 *       Returns safe defaults on other architectures.
 */

#include "src/cpu/inc/CpuFeatures.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <cpuid.h>
#define SEEKER_HAS_CPUID 1
#else
#define SEEKER_HAS_CPUID 0
#endif

#if defined(__aarch64__) && defined(__linux__)
#include <asm/hwcap.h> // HWCAP_*, HWCAP2_*
#include <fcntl.h>     // open, O_RDONLY, O_CLOEXEC
#include <sys/auxv.h>  // getauxval, AT_HWCAP, AT_HWCAP2
#include <unistd.h>    // read, close
#define SEEKER_HAS_HWCAP 1
#else
#define SEEKER_HAS_HWCAP 0
#endif

#include <cerrno>  // errno, EINTR
#include <cstdio>  // std::snprintf
#include <cstdlib> // std::strtoul, std::strtoull
#include <cstring> // std::memcpy, std::strchr, std::strlen, std::strncmp

#include <fmt/core.h>

namespace seeker {

namespace cpu {

namespace {

/* ----------------------------- ARM ID Tables ----------------------------- */

/*
 * Sources (verified values only):
 *  - Linux kernel arch/arm64/include/asm/cputype.h
 *  - util-linux sys-utils/lscpu-arm.c (names follow lscpu spelling)
 */

/// MIDR implementer code to vendor name (must fit VENDOR_STRING_SIZE).
struct ArmImplementer {
  std::uint16_t id;
  const char* name;
};

constexpr ArmImplementer ARM_IMPLEMENTERS[] = {
    {0x41, "ARM"},       {0x42, "Broadcom"},  {0x43, "Cavium"},   {0x44, "DEC"},
    {0x46, "FUJITSU"},   {0x48, "HiSilicon"}, {0x49, "Infineon"}, {0x4d, "Freescale"},
    {0x4e, "NVIDIA"},    {0x50, "APM"},       {0x51, "Qualcomm"}, {0x53, "Samsung"},
    {0x56, "Marvell"},   {0x61, "Apple"},     {0x66, "Faraday"},  {0x69, "Intel"},
    {0x6d, "Microsoft"}, {0x70, "Phytium"},   {0xc0, "Ampere"},
};

/// MIDR implementer + part number to core name (AArch64-capable cores).
struct ArmPart {
  std::uint16_t implementer;
  std::uint16_t part;
  const char* name;
};

constexpr ArmPart ARM_PARTS[] = {
    // ARM Ltd (0x41)
    {0x41, 0xd03, "Cortex-A53"},
    {0x41, 0xd04, "Cortex-A35"},
    {0x41, 0xd05, "Cortex-A55"},
    {0x41, 0xd06, "Cortex-A65"},
    {0x41, 0xd07, "Cortex-A57"},
    {0x41, 0xd08, "Cortex-A72"},
    {0x41, 0xd09, "Cortex-A73"},
    {0x41, 0xd0a, "Cortex-A75"},
    {0x41, 0xd0b, "Cortex-A76"},
    {0x41, 0xd0c, "Neoverse-N1"},
    {0x41, 0xd0d, "Cortex-A77"},
    {0x41, 0xd0e, "Cortex-A76AE"},
    {0x41, 0xd40, "Neoverse-V1"},
    {0x41, 0xd41, "Cortex-A78"},
    {0x41, 0xd42, "Cortex-A78AE"},
    {0x41, 0xd43, "Cortex-A65AE"},
    {0x41, 0xd44, "Cortex-X1"},
    {0x41, 0xd46, "Cortex-A510"},
    {0x41, 0xd47, "Cortex-A710"},
    {0x41, 0xd48, "Cortex-X2"},
    {0x41, 0xd49, "Neoverse-N2"},
    {0x41, 0xd4a, "Neoverse-E1"},
    {0x41, 0xd4b, "Cortex-A78C"},
    {0x41, 0xd4c, "Cortex-X1C"},
    {0x41, 0xd4d, "Cortex-A715"},
    {0x41, 0xd4e, "Cortex-X3"},
    {0x41, 0xd4f, "Neoverse-V2"},
    {0x41, 0xd80, "Cortex-A520"},
    {0x41, 0xd81, "Cortex-A720"},
    {0x41, 0xd82, "Cortex-X4"},
    {0x41, 0xd83, "Neoverse-V3AE"},
    {0x41, 0xd84, "Neoverse-V3"},
    {0x41, 0xd85, "Cortex-X925"},
    {0x41, 0xd87, "Cortex-A725"},
    {0x41, 0xd88, "Cortex-A520AE"},
    {0x41, 0xd89, "Cortex-A720AE"},
    {0x41, 0xd8a, "C1-Nano"},
    {0x41, 0xd8b, "C1-Pro"},
    {0x41, 0xd8c, "C1-Ultra"},
    {0x41, 0xd8e, "Neoverse-N3"},
    {0x41, 0xd90, "C1-Premium"},

    // Broadcom (0x42)
    {0x42, 0x100, "Brahma-B53"},
    {0x42, 0x516, "ThunderX2"},

    // Cavium (0x43)
    {0x43, 0x0a1, "ThunderX-88XX"},
    {0x43, 0x0a2, "ThunderX-81XX"},
    {0x43, 0x0a3, "ThunderX-83XX"},
    {0x43, 0x0af, "ThunderX2-99xx"},

    // Fujitsu (0x46)
    {0x46, 0x001, "A64FX"},

    // HiSilicon (0x48)
    {0x48, 0xd01, "Kunpeng-920"},

    // NVIDIA (0x4e)
    {0x4e, 0x000, "Denver"},
    {0x4e, 0x003, "Denver-2"},
    {0x4e, 0x004, "Carmel"},
    {0x4e, 0x010, "Olympus"},

    // APM (0x50)
    {0x50, 0x000, "X-Gene"},

    // Qualcomm (0x51)
    {0x51, 0x001, "Oryon"},
    {0x51, 0x800, "Falkor-V1/Kryo"},
    {0x51, 0x801, "Kryo-V2"},
    {0x51, 0x802, "Kryo-3XX-Gold"},
    {0x51, 0x803, "Kryo-3XX-Silver"},
    {0x51, 0x804, "Kryo-4XX-Gold"},
    {0x51, 0x805, "Kryo-4XX-Silver"},
    {0x51, 0xc00, "Falkor"},
    {0x51, 0xc01, "Saphira"},

    // Apple (0x61)
    {0x61, 0x022, "Icestorm-M1"},
    {0x61, 0x023, "Firestorm-M1"},
    {0x61, 0x024, "Icestorm-M1-Pro"},
    {0x61, 0x025, "Firestorm-M1-Pro"},
    {0x61, 0x028, "Icestorm-M1-Max"},
    {0x61, 0x029, "Firestorm-M1-Max"},
    {0x61, 0x032, "Blizzard-M2"},
    {0x61, 0x033, "Avalanche-M2"},
    {0x61, 0x034, "Blizzard-M2-Pro"},
    {0x61, 0x035, "Avalanche-M2-Pro"},
    {0x61, 0x038, "Blizzard-M2-Max"},
    {0x61, 0x039, "Avalanche-M2-Max"},

    // Microsoft (0x6d)
    {0x6d, 0xd49, "Azure-Cobalt-100"},

    // Ampere (0xc0)
    {0xc0, 0xac3, "Ampere-1"},
    {0xc0, 0xac4, "Ampere-1a"},
};

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

#if SEEKER_HAS_HWCAP

/*
 * Fallback bit values for older <asm/hwcap.h>. These are Linux uapi ABI
 * (arch/arm64/include/uapi/asm/hwcap.h) and never change once assigned.
 */
#ifndef HWCAP_ASIMD
#define HWCAP_ASIMD (1UL << 1)
#endif
#ifndef HWCAP_AES
#define HWCAP_AES (1UL << 3)
#endif
#ifndef HWCAP_PMULL
#define HWCAP_PMULL (1UL << 4)
#endif
#ifndef HWCAP_SHA1
#define HWCAP_SHA1 (1UL << 5)
#endif
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1UL << 6)
#endif
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1UL << 7)
#endif
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1UL << 8)
#endif
#ifndef HWCAP_SHA3
#define HWCAP_SHA3 (1UL << 17)
#endif
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1UL << 20)
#endif
#ifndef HWCAP_SHA512
#define HWCAP_SHA512 (1UL << 21)
#endif
#ifndef HWCAP_SVE
#define HWCAP_SVE (1UL << 22)
#endif
#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1UL << 1)
#endif
#ifndef HWCAP2_I8MM
#define HWCAP2_I8MM (1UL << 13)
#endif
#ifndef HWCAP2_BF16
#define HWCAP2_BF16 (1UL << 14)
#endif
#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif

/// /proc/cpuinfo read chunk size.
inline constexpr std::size_t CPUINFO_CHUNK_SIZE = 4096;

/// Maximum /proc/cpuinfo line length kept (longer lines are truncated).
inline constexpr std::size_t CPUINFO_LINE_SIZE = 1024;

/// sysfs MIDR_EL1 read buffer size ("0x00000000410fd083\n").
inline constexpr std::size_t MIDR_BUFFER_SIZE = 32;

/// One HWCAP bit mapped to its /proc/cpuinfo token and CpuFeatures field.
struct HwcapFlag {
  std::uint8_t word;         ///< 1 = AT_HWCAP, 2 = AT_HWCAP2
  unsigned long mask;        ///< HWCAP_* / HWCAP2_* bit
  const char* token;         ///< /proc/cpuinfo "Features" token
  bool CpuFeatures::* field; ///< Destination flag
};

constexpr HwcapFlag HWCAP_FLAGS[] = {
    {1, HWCAP_ASIMD, "asimd", &CpuFeatures::neon},
    {1, HWCAP_AES, "aes", &CpuFeatures::aes},
    {1, HWCAP_PMULL, "pmull", &CpuFeatures::pmull},
    {1, HWCAP_SHA1, "sha1", &CpuFeatures::sha1},
    {1, HWCAP_SHA2, "sha2", &CpuFeatures::sha2},
    {1, HWCAP_CRC32, "crc32", &CpuFeatures::crc32},
    {1, HWCAP_ATOMICS, "atomics", &CpuFeatures::atomics},
    {1, HWCAP_SHA3, "sha3", &CpuFeatures::sha3},
    {1, HWCAP_ASIMDDP, "asimddp", &CpuFeatures::dotprod},
    {1, HWCAP_SHA512, "sha512", &CpuFeatures::sha512},
    {1, HWCAP_SVE, "sve", &CpuFeatures::sve},
    {2, HWCAP2_SVE2, "sve2", &CpuFeatures::sve2},
    {2, HWCAP2_I8MM, "i8mm", &CpuFeatures::i8mm},
    {2, HWCAP2_BF16, "bf16", &CpuFeatures::bf16},
};

/// Subset of /proc/cpuinfo relevant to aarch64 identification.
struct ArmCpuinfo {
  std::uint16_t implementer{0};
  bool haveImplementer{false};
  std::array<std::uint16_t, ARM_MAX_CORE_TYPES> parts{};
  std::uint8_t partCount{0};
  std::array<char, CPUINFO_LINE_SIZE> features{}; ///< First "Features" value
};

/// True if whitespace-separated token list contains TOKEN exactly.
inline bool hasToken(const char* list, const char* token) noexcept {
  const std::size_t TOKEN_LEN = std::strlen(token);
  const char* p = list;
  while (*p != '\0') {
    while (*p == ' ' || *p == '\t') {
      ++p;
    }
    const char* start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      ++p;
    }
    const std::size_t LEN = static_cast<std::size_t>(p - start);
    if (LEN == TOKEN_LEN && LEN > 0 && std::strncmp(start, token, LEN) == 0) {
      return true;
    }
  }
  return false;
}

/// Record a MIDR part number if not already seen (bounded by ARM_MAX_CORE_TYPES).
inline void addPart(ArmCpuinfo& info, std::uint16_t part) noexcept {
  for (std::uint8_t i = 0; i < info.partCount; ++i) {
    if (info.parts[i] == part) {
      return;
    }
  }
  if (info.partCount < ARM_MAX_CORE_TYPES) {
    info.parts[info.partCount++] = part;
  }
}

/// Parse one null-terminated "key<tab>: value" line from /proc/cpuinfo.
inline void parseCpuinfoLine(const char* line, ArmCpuinfo& info) noexcept {
  const char* colon = std::strchr(line, ':');
  if (colon == nullptr) {
    return;
  }

  std::size_t keyLen = static_cast<std::size_t>(colon - line);
  while (keyLen > 0 && (line[keyLen - 1] == ' ' || line[keyLen - 1] == '\t')) {
    --keyLen;
  }

  const char* value = colon + 1;
  while (*value == ' ' || *value == '\t') {
    ++value;
  }

  auto keyIs = [line, keyLen](const char* key) noexcept {
    return std::strlen(key) == keyLen && std::strncmp(line, key, keyLen) == 0;
  };

  if (keyIs("CPU implementer")) {
    // Heterogeneous implementers are not tracked; the first CPU wins.
    if (!info.haveImplementer) {
      info.implementer = static_cast<std::uint16_t>(std::strtoul(value, nullptr, 0));
      info.haveImplementer = true;
    }
  } else if (keyIs("CPU part")) {
    addPart(info, static_cast<std::uint16_t>(std::strtoul(value, nullptr, 0)));
  } else if (keyIs("Features") && info.features[0] == '\0') {
    std::snprintf(info.features.data(), info.features.size(), "%s", value);
  }
}

/// Stream /proc/cpuinfo through a fixed chunk buffer, line by line (no heap).
inline void readArmCpuinfo(ArmCpuinfo& info) noexcept {
  const int FD = ::open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return;
  }

  std::array<char, CPUINFO_CHUNK_SIZE> chunk{};
  std::array<char, CPUINFO_LINE_SIZE> line{};
  std::size_t lineLen = 0;

  for (;;) {
    const ssize_t N = ::read(FD, chunk.data(), chunk.size());
    if (N < 0 && errno == EINTR) {
      continue;
    }
    if (N <= 0) {
      break;
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
      const char C = chunk[i];
      if (C == '\n') {
        line[lineLen] = '\0';
        parseCpuinfoLine(line.data(), info);
        lineLen = 0;
      } else if (lineLen < line.size() - 1) {
        line[lineLen++] = C;
      }
    }
  }

  if (lineLen > 0) {
    line[lineLen] = '\0';
    parseCpuinfoLine(line.data(), info);
  }

  ::close(FD);
}

/// Fallback: read MIDR_EL1 of cpu0 from sysfs when /proc/cpuinfo lacks it.
inline void readSysfsMidr(ArmCpuinfo& info) noexcept {
  const int FD =
      ::open("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1", O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return;
  }

  std::array<char, MIDR_BUFFER_SIZE> buf{};
  const ssize_t N = ::read(FD, buf.data(), buf.size() - 1);
  ::close(FD);
  if (N <= 0) {
    return;
  }

  char* end = nullptr;
  const unsigned long long MIDR = std::strtoull(buf.data(), &end, 16);
  if (end == buf.data()) {
    return;
  }

  // MIDR_EL1: implementer [31:24], primary part number [15:4]
  info.implementer = static_cast<std::uint16_t>((MIDR >> 24) & 0xFFULL);
  info.haveImplementer = true;
  info.partCount = 0;
  addPart(info, static_cast<std::uint16_t>((MIDR >> 4) & 0xFFFULL));
}

/// Advance write offset after snprintf, clamping on truncation.
inline void advance(std::size_t& used, int written, std::size_t cap) noexcept {
  if (written < 0) {
    return;
  }
  used += static_cast<std::size_t>(written);
  if (used >= cap) {
    used = cap - 1;
  }
}

/// Fill vendor and brand strings from MIDR implementer/part(s).
inline void composeArmIdentity(CpuFeatures& f) noexcept {
  const char* VENDOR_NAME = armImplementerName(f.armImplementer);

  if (VENDOR_NAME != nullptr) {
    std::snprintf(f.vendor.data(), f.vendor.size(), "%s", VENDOR_NAME);
  } else {
    std::snprintf(f.vendor.data(), f.vendor.size(), "0x%02x", f.armImplementer);
  }

  // Brand: "<vendor> <core>[ + <core>...]", one core name per distinct MIDR part
  const std::size_t CAP = f.brand.size();
  std::size_t used = 0;
  if (VENDOR_NAME != nullptr) {
    advance(used, std::snprintf(f.brand.data(), CAP, "%s", VENDOR_NAME), CAP);
  } else {
    advance(used, std::snprintf(f.brand.data(), CAP, "ARM impl 0x%02x", f.armImplementer), CAP);
  }

  for (std::uint8_t i = 0; i < f.armPartCount; ++i) {
    const char* SEP = (i == 0) ? " " : " + ";
    const char* PART_NAME = armPartName(f.armImplementer, f.armParts[i]);
    if (PART_NAME != nullptr) {
      advance(used, std::snprintf(f.brand.data() + used, CAP - used, "%s%s", SEP, PART_NAME), CAP);
    } else {
      advance(used,
              std::snprintf(f.brand.data() + used, CAP - used, "%spart 0x%03x", SEP, f.armParts[i]),
              CAP);
    }
  }
}

#endif // SEEKER_HAS_HWCAP

} // namespace

/* ----------------------------- API ----------------------------- */

const char* toString(CpuArch arch) noexcept {
  switch (arch) {
  case CpuArch::X86:
    return "x86";
  case CpuArch::AARCH64:
    return "aarch64";
  case CpuArch::UNKNOWN:
    break;
  }
  return "unknown";
}

const char* armImplementerName(std::uint16_t implementer) noexcept {
  for (const auto& IMPL : ARM_IMPLEMENTERS) {
    if (IMPL.id == implementer) {
      return IMPL.name;
    }
  }
  return nullptr;
}

const char* armPartName(std::uint16_t implementer, std::uint16_t part) noexcept {
  for (const auto& ENTRY : ARM_PARTS) {
    if (ENTRY.implementer == implementer && ENTRY.part == part) {
      return ENTRY.name;
    }
  }
  return nullptr;
}

std::string CpuFeatures::toString() const {
  if (isArm()) {
    return fmt::format("Vendor: {}\n"
                       "Brand:  {}\n"
                       "NEON: {}  SVE: {}  SVE2: {}  DOTPROD: {}  I8MM: {}  BF16: {}\n"
                       "AES: {}  PMULL: {}  SHA1: {}  SHA2: {}  SHA3: {}  SHA512: {}  CRC32: {}\n"
                       "LSE: {}",
                       vendor.data(), brand.data(), neon, sve, sve2, dotprod, i8mm, bf16, aes,
                       pmull, sha1, sha2, sha3, sha512, crc32, atomics);
  }

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
  f.arch = CpuArch::X86;

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

#if SEEKER_HAS_HWCAP
  f.arch = CpuArch::AARCH64;

  // Single bounded pass: MIDR implementer/parts and Features fallback
  ArmCpuinfo info{};
  readArmCpuinfo(info);
  if (!info.haveImplementer || info.partCount == 0) {
    readSysfsMidr(info);
  }

  // Feature flags: auxv is authoritative; /proc/cpuinfo tokens if auxv unavailable
  const unsigned long HWCAP1 = ::getauxval(AT_HWCAP);
  const unsigned long HWCAP2 = ::getauxval(AT_HWCAP2);
  const bool USE_AUXV = (HWCAP1 != 0UL);

  for (const auto& FLAG : HWCAP_FLAGS) {
    if (USE_AUXV) {
      const unsigned long WORD = (FLAG.word == 1) ? HWCAP1 : HWCAP2;
      f.*FLAG.field = (WORD & FLAG.mask) != 0UL;
    } else {
      f.*FLAG.field = hasToken(info.features.data(), FLAG.token);
    }
  }

  // Shared crypto field: x86 SHA extensions cover SHA-1 and SHA-256
  f.sha = f.sha1 && f.sha2;

  // Identification
  if (info.haveImplementer) {
    f.armImplementer = info.implementer;
    f.armParts = info.parts;
    f.armPartCount = info.partCount;
    f.armPart = (info.partCount > 0) ? info.parts[0] : 0;
    composeArmIdentity(f);
  }

#endif // SEEKER_HAS_HWCAP

  return f;
}

} // namespace cpu

} // namespace seeker
