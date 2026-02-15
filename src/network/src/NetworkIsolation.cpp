/**
 * @file NetworkIsolation.cpp
 * @brief Implementation of NIC IRQ isolation analysis.
 */

#include "src/network/inc/NetworkIsolation.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace network {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr std::size_t PATH_BUFFER_SIZE = 256;
constexpr std::size_t LINE_BUFFER_SIZE = 1024;

/**
 * Parse hexadecimal affinity mask from /proc/irq/<n>/smp_affinity.
 * Format: comma-separated 32-bit hex values, MSB first.
 * Example: "00000000,00000001" means CPU 0 only.
 */
inline std::uint64_t parseAffinityMask(const char* buf) noexcept {
  std::uint64_t mask = 0;

  // Skip leading whitespace
  while (*buf == ' ' || *buf == '\t') {
    ++buf;
  }

  // Parse comma-separated hex values (we only handle up to 64 CPUs)
  // The format is MSB-first, comma-separated 32-bit chunks
  // For simplicity, parse from right to left
  const char* end = buf + std::strlen(buf);

  // Strip trailing whitespace
  while (end > buf && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
    --end;
  }

  // Find last comma (or start of string)
  int shift = 0;
  const char* chunk = end;

  while (chunk > buf) {
    // Find start of this chunk (after comma or at string start)
    const char* start = chunk;
    while (start > buf && start[-1] != ',') {
      --start;
    }

    // Parse this chunk as hex
    char* parseEnd = nullptr;
    const unsigned long VAL = std::strtoul(start, &parseEnd, 16);

    if (parseEnd != start && shift < 64) {
      mask |= (static_cast<std::uint64_t>(VAL) << shift);
      shift += 32;
    }

    // Move to previous chunk
    if (start > buf) {
      chunk = start - 1; // Skip the comma
    } else {
      break;
    }
  }

  return mask;
}

/**
 * Check if an interface exists in /sys/class/net/.
 */
inline bool interfaceExists(const char* ifname) noexcept {
  if (ifname == nullptr || ifname[0] == '\0') {
    return false;
  }

  char pathBuf[PATH_BUFFER_SIZE];
  std::snprintf(pathBuf, sizeof(pathBuf), "/sys/class/net/%s", ifname);

  // Check if directory exists
  return ::access(pathBuf, F_OK) == 0;
}

/**
 * Check if string matches a network driver IRQ pattern AND is a real interface.
 * Only returns true if the extracted interface name exists in /sys/class/net/.
 */
inline bool isNetworkIrq(const char* irqDesc, char* outIfname, std::size_t outSize) noexcept {
  outIfname[0] = '\0';

  // Skip leading whitespace
  const char* p = irqDesc;
  while (*p == ' ' || *p == '\t') {
    ++p;
  }

  // Get the last token (device name is usually last)
  const char* lastSpace = std::strrchr(p, ' ');
  const char* devName = (lastSpace != nullptr) ? (lastSpace + 1) : p;

  // Strip trailing whitespace/newlines from devName
  char nameBuf[IF_NAME_SIZE];
  std::size_t len = 0;
  for (; len < IF_NAME_SIZE - 1 && devName[len] != '\0' && devName[len] != '\n' &&
         devName[len] != '\r' && devName[len] != ' ';
       ++len) {
    nameBuf[len] = devName[len];
  }
  nameBuf[len] = '\0';

  // Extract base interface name (strip queue suffixes like -TxRx-0, -rx-0, etc.)
  // Also strip :exception, :default suffixes (iwlwifi uses these)
  char baseName[IF_NAME_SIZE];
  std::size_t baseLen = 0;
  for (std::size_t i = 0; i < len && baseLen < IF_NAME_SIZE - 1; ++i) {
    // Stop at queue indicator or iwlwifi suffix
    if (nameBuf[i] == '-' || nameBuf[i] == ':') {
      break;
    }
    baseName[baseLen++] = nameBuf[i];
  }
  baseName[baseLen] = '\0';

  // Check if this base name exists as an actual network interface
  if (interfaceExists(baseName)) {
    std::snprintf(outIfname, outSize, "%s", baseName);
    return true;
  }

  // Also check the full name (some drivers don't use suffixes)
  if (interfaceExists(nameBuf)) {
    std::snprintf(outIfname, outSize, "%s", nameBuf);
    return true;
  }

  // Special case: iwlwifi registers IRQs as "iwlwifi" but interface is wlan0/wlp*
  // These won't match, which is fine - we'll miss wifi IRQs but that's acceptable
  // for RT systems where wifi isn't typically used

  return false;
}

} // namespace

/* ----------------------------- NicIrqInfo Methods ----------------------------- */

bool NicIrqInfo::hasIrqOnCpu(int cpu) const noexcept {
  if (cpu < 0 || cpu >= 64) {
    return false;
  }

  const std::uint64_t CPU_BIT = 1ULL << cpu;
  for (std::size_t i = 0; i < irqCount; ++i) {
    if ((affinity[i] & CPU_BIT) != 0) {
      return true;
    }
  }

  return false;
}

bool NicIrqInfo::hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept {
  for (std::size_t i = 0; i < irqCount; ++i) {
    if ((affinity[i] & cpuMask) != 0) {
      return true;
    }
  }

  return false;
}

std::uint64_t NicIrqInfo::getCombinedAffinity() const noexcept {
  std::uint64_t combined = 0;
  for (std::size_t i = 0; i < irqCount; ++i) {
    combined |= affinity[i];
  }
  return combined;
}

std::string NicIrqInfo::getAffinityCpuList() const { return formatCpuMask(getCombinedAffinity()); }

std::string NicIrqInfo::toString() const {
  std::string out;
  out += fmt::format("{}: {} IRQs", ifname.data(), irqCount);

  if (irqCount > 0) {
    out += " [";
    for (std::size_t i = 0; i < irqCount && i < 5; ++i) {
      if (i > 0) {
        out += ",";
      }
      out += fmt::format("{}", irqNumbers[i]);
    }
    if (irqCount > 5) {
      out += fmt::format(",... ({} more)", irqCount - 5);
    }
    out += "]";

    out += fmt::format(" affinity=[{}]", getAffinityCpuList());
  }

  if (numaNode >= 0) {
    out += fmt::format(" numa={}", numaNode);
  }

  return out;
}

/* ----------------------------- NetworkIsolation Methods ----------------------------- */

const NicIrqInfo* NetworkIsolation::find(const char* ifname) const noexcept {
  if (ifname == nullptr) {
    return nullptr;
  }

  for (std::size_t i = 0; i < nicCount; ++i) {
    if (std::strcmp(nics[i].ifname.data(), ifname) == 0) {
      return &nics[i];
    }
  }

  return nullptr;
}

bool NetworkIsolation::hasIrqOnCpu(int cpu) const noexcept {
  for (std::size_t i = 0; i < nicCount; ++i) {
    if (nics[i].hasIrqOnCpu(cpu)) {
      return true;
    }
  }
  return false;
}

bool NetworkIsolation::hasIrqOnCpuMask(std::uint64_t cpuMask) const noexcept {
  for (std::size_t i = 0; i < nicCount; ++i) {
    if (nics[i].hasIrqOnCpuMask(cpuMask)) {
      return true;
    }
  }
  return false;
}

std::string NetworkIsolation::getConflictingNics(std::uint64_t cpuMask) const {
  std::string result;

  for (std::size_t i = 0; i < nicCount; ++i) {
    if (nics[i].hasIrqOnCpuMask(cpuMask)) {
      if (!result.empty()) {
        result += ", ";
      }
      result += nics[i].ifname.data();
    }
  }

  return result.empty() ? "(none)" : result;
}

std::size_t NetworkIsolation::getTotalIrqCount() const noexcept {
  std::size_t total = 0;
  for (std::size_t i = 0; i < nicCount; ++i) {
    total += nics[i].irqCount;
  }
  return total;
}

std::string NetworkIsolation::toString() const {
  std::string out;
  out +=
      fmt::format("Network IRQ Isolation: {} NICs, {} total IRQs\n", nicCount, getTotalIrqCount());

  for (std::size_t i = 0; i < nicCount; ++i) {
    out += "  ";
    out += nics[i].toString();
    out += '\n';
  }

  return out;
}

/* ----------------------------- IrqConflictResult Methods ----------------------------- */

std::string IrqConflictResult::toString() const {
  if (!hasConflict) {
    return "No IRQ conflicts detected";
  }

  std::string out;
  out += fmt::format("IRQ CONFLICT: {} IRQs on RT CPUs\n", conflictCount);
  out += fmt::format("  Conflicting NICs: {}\n", conflictingNics.data());
  out += "  Conflicting CPUs: ";

  for (std::size_t i = 0; i < conflictingCpuCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += fmt::format("{}", conflictingCpus[i]);
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

NetworkIsolation getNetworkIsolation() noexcept {
  NetworkIsolation ni{};

  // Read /proc/interrupts to find network IRQs
  char lineBuf[LINE_BUFFER_SIZE];
  FILE* fp = ::fopen("/proc/interrupts", "r");
  if (fp == nullptr) {
    return ni;
  }

  // Skip header line
  if (::fgets(lineBuf, sizeof(lineBuf), fp) == nullptr) {
    ::fclose(fp);
    return ni;
  }

  char pathBuf[PATH_BUFFER_SIZE];
  char affinityBuf[128];
  char ifnameBuf[IF_NAME_SIZE];

  while (::fgets(lineBuf, sizeof(lineBuf), fp) != nullptr) {
    // Parse IRQ number (first field)
    char* endPtr = nullptr;
    const long IRQ = std::strtol(lineBuf, &endPtr, 10);
    if (endPtr == lineBuf || IRQ < 0) {
      continue;
    }

    // Find device description (last field, after all the per-CPU counts)
    // Skip to end of line and work backwards
    char* lineEnd = lineBuf + std::strlen(lineBuf);
    while (lineEnd > lineBuf && (lineEnd[-1] == '\n' || lineEnd[-1] == '\r')) {
      --lineEnd;
      *lineEnd = '\0';
    }

    // Find last column (device name)
    char* lastField = lineEnd;
    while (lastField > lineBuf && lastField[-1] != ' ') {
      --lastField;
    }

    // Check if this is a network IRQ (validates against actual interfaces)
    if (!isNetworkIrq(lastField, ifnameBuf, sizeof(ifnameBuf))) {
      continue;
    }

    // Find or create NIC entry
    NicIrqInfo* nic = nullptr;
    for (std::size_t i = 0; i < ni.nicCount; ++i) {
      if (std::strcmp(ni.nics[i].ifname.data(), ifnameBuf) == 0) {
        nic = &ni.nics[i];
        break;
      }
    }

    if (nic == nullptr && ni.nicCount < MAX_INTERFACES) {
      nic = &ni.nics[ni.nicCount];
      copyToFixedArray(nic->ifname, ifnameBuf);
      ++ni.nicCount;
    }

    if (nic == nullptr || nic->irqCount >= MAX_NIC_IRQS) {
      continue;
    }

    // Add IRQ to NIC
    nic->irqNumbers[nic->irqCount] = static_cast<int>(IRQ);

    // Read affinity for this IRQ
    std::snprintf(pathBuf, sizeof(pathBuf), "/proc/irq/%ld/smp_affinity", IRQ);
    if (readFileToBuffer(pathBuf, affinityBuf, sizeof(affinityBuf)) > 0) {
      nic->affinity[nic->irqCount] = parseAffinityMask(affinityBuf);
    } else {
      // Default to all CPUs if can't read
      nic->affinity[nic->irqCount] = ~0ULL;
    }

    ++nic->irqCount;
  }

  ::fclose(fp);

  // Read NUMA node for each NIC
  for (std::size_t i = 0; i < ni.nicCount; ++i) {
    std::snprintf(pathBuf, sizeof(pathBuf), "/sys/class/net/%s/device/numa_node",
                  ni.nics[i].ifname.data());
    char numaBuf[16];
    if (readFileToBuffer(pathBuf, numaBuf, sizeof(numaBuf)) > 0) {
      ni.nics[i].numaNode = static_cast<int>(std::strtol(numaBuf, nullptr, 10));
    }
  }

  return ni;
}

IrqConflictResult checkIrqConflict(const NetworkIsolation& ni, std::uint64_t rtCpuMask) noexcept {
  IrqConflictResult result{};

  if (rtCpuMask == 0) {
    return result;
  }

  std::string conflictNics;
  std::uint64_t conflictCpuMask = 0;

  for (std::size_t i = 0; i < ni.nicCount; ++i) {
    const NicIrqInfo& NIC = ni.nics[i];

    for (std::size_t j = 0; j < NIC.irqCount; ++j) {
      const std::uint64_t OVERLAP = NIC.affinity[j] & rtCpuMask;
      if (OVERLAP != 0) {
        result.hasConflict = true;
        ++result.conflictCount;
        conflictCpuMask |= OVERLAP;

        // Add NIC to list if not already there
        if (conflictNics.find(NIC.ifname.data()) == std::string::npos) {
          if (!conflictNics.empty()) {
            conflictNics += ", ";
          }
          conflictNics += NIC.ifname.data();
        }
      }
    }
  }

  // Copy conflicting NIC names
  if (!conflictNics.empty()) {
    copyToFixedArray(result.conflictingNics, conflictNics.c_str());
  }

  // Extract conflicting CPUs from mask
  for (int cpu = 0; cpu < 64 && result.conflictingCpuCount < MAX_CPUS; ++cpu) {
    if ((conflictCpuMask & (1ULL << cpu)) != 0) {
      result.conflictingCpus[result.conflictingCpuCount] = cpu;
      ++result.conflictingCpuCount;
    }
  }

  return result;
}

std::uint64_t parseCpuListToMask(const char* cpuList) noexcept {
  if (cpuList == nullptr || cpuList[0] == '\0') {
    return 0;
  }

  std::uint64_t mask = 0;
  const char* p = cpuList;

  while (*p != '\0') {
    // Skip whitespace and commas
    while (*p == ' ' || *p == ',' || *p == '\t') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    // Parse first number
    char* endPtr = nullptr;
    const long START = std::strtol(p, &endPtr, 10);
    if (endPtr == p) {
      break; // Parse error
    }

    p = endPtr;
    long end = START;

    // Check for range
    if (*p == '-') {
      ++p;
      end = std::strtol(p, &endPtr, 10);
      if (endPtr == p) {
        end = START; // Parse error, treat as single
      }
      p = endPtr;
    }

    // Set bits in mask
    for (long cpu = START; cpu <= end && cpu < 64; ++cpu) {
      if (cpu >= 0) {
        mask |= (1ULL << cpu);
      }
    }
  }

  return mask;
}

std::string formatCpuMask(std::uint64_t mask) {
  if (mask == 0) {
    return "(none)";
  }

  std::string out;
  int rangeStart = -1;
  int rangeEnd = -1;

  auto flushRange = [&]() {
    if (rangeStart < 0) {
      return;
    }
    if (!out.empty()) {
      out += ',';
    }
    if (rangeStart == rangeEnd) {
      out += fmt::format("{}", rangeStart);
    } else {
      out += fmt::format("{}-{}", rangeStart, rangeEnd);
    }
    rangeStart = -1;
    rangeEnd = -1;
  };

  for (int cpu = 0; cpu < 64; ++cpu) {
    if ((mask & (1ULL << cpu)) != 0) {
      if (rangeStart < 0) {
        rangeStart = cpu;
        rangeEnd = cpu;
      } else if (cpu == rangeEnd + 1) {
        rangeEnd = cpu;
      } else {
        flushRange();
        rangeStart = cpu;
        rangeEnd = cpu;
      }
    }
  }

  flushRange();
  return out;
}

} // namespace network

} // namespace seeker