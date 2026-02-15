/**
 * @file EdacStatus.cpp
 * @brief EDAC status collection from sysfs.
 * @note Reads /sys/devices/system/edac/mc/ hierarchy.
 */

#include "src/memory/inc/EdacStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <dirent.h>   // opendir, readdir, closedir
#include <fcntl.h>    // open, O_RDONLY
#include <sys/stat.h> // stat
#include <unistd.h>   // read, close

#include <array>   // std::array
#include <cstdlib> // strtoull, atoi
#include <cstring> // strlen, strncmp, strcmp, memcpy

#include <fmt/core.h>

namespace seeker {

namespace memory {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToFixedArray;
using seeker::helpers::strings::sortFixedStrings;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* EDAC_MC_PATH = "/sys/devices/system/edac/mc";
constexpr std::size_t PATH_BUF_SIZE = 512;
constexpr std::size_t READ_BUF_SIZE = 128;

/* ----------------------------- File Helpers ----------------------------- */

/// Read uint64 from file.
std::uint64_t readFileUint64(const char* path) noexcept {
  std::array<char, READ_BUF_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return 0;
  }
  return std::strtoull(buf.data(), nullptr, 10);
}

/// Read string into fixed-size array.
template <std::size_t N> void readFileString(const char* path, std::array<char, N>& out) noexcept {
  out[0] = '\0';
  std::array<char, READ_BUF_SIZE> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN > 0) {
    const std::size_t COPY_LEN = (LEN < N - 1) ? LEN : (N - 1);
    std::memcpy(out.data(), buf.data(), COPY_LEN);
    out[COPY_LEN] = '\0';
  }
}

/// Check if path is a directory.
bool isDirectory(const char* path) noexcept {
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

/* ----------------------------- Path Building ----------------------------- */

/// Build path: /sys/devices/system/edac/mc/<mc>/<file>
void buildMcPath(char* out, std::size_t outSize, const char* mcName, const char* file) noexcept {
  std::snprintf(out, outSize, "%s/%s/%s", EDAC_MC_PATH, mcName, file);
}

/// Build path: /sys/devices/system/edac/mc/<mc>/<subdir>/<file>
void buildMcSubPath(char* out, std::size_t outSize, const char* mcName, const char* subdir,
                    const char* file) noexcept {
  std::snprintf(out, outSize, "%s/%s/%s/%s", EDAC_MC_PATH, mcName, subdir, file);
}

/* ----------------------------- Parsing Helpers ----------------------------- */

/// Extract index from directory name like "mc0" or "csrow1" or "dimm0"
std::int32_t parseIndex(const char* name, const char* prefix) noexcept {
  const std::size_t PREFIX_LEN = std::strlen(prefix);
  if (std::strncmp(name, prefix, PREFIX_LEN) != 0) {
    return -1;
  }
  return std::atoi(name + PREFIX_LEN);
}

/* ----------------------------- Data Collection ----------------------------- */

/// Collect memory controller information.
void collectMemoryController(const char* mcName, MemoryController& mc) noexcept {
  std::array<char, PATH_BUF_SIZE> pathBuf{};

  // Set name
  std::snprintf(mc.name.data(), mc.name.size(), "%s", mcName);

  // Parse index from name
  mc.mcIndex = parseIndex(mcName, "mc");

  // Read type info
  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "mc_name");
  readFileString(pathBuf.data(), mc.mcType);

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "edac_mc_type");
  if (mc.mcType[0] == '\0') {
    readFileString(pathBuf.data(), mc.mcType);
  }

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "edac_mode");
  readFileString(pathBuf.data(), mc.edacMode);

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "mem_type");
  readFileString(pathBuf.data(), mc.memType);

  // Read size
  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "size_mb");
  mc.sizeMb = static_cast<std::size_t>(readFileUint64(pathBuf.data()));

  // Read error counts
  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "ce_count");
  mc.ceCount = readFileUint64(pathBuf.data());

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "ce_noinfo_count");
  mc.ceNoInfoCount = readFileUint64(pathBuf.data());

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "ue_count");
  mc.ueCount = readFileUint64(pathBuf.data());

  buildMcPath(pathBuf.data(), pathBuf.size(), mcName, "ue_noinfo_count");
  mc.ueNoInfoCount = readFileUint64(pathBuf.data());

  // Count csrows
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s", EDAC_MC_PATH, mcName);
  DIR* mcDir = ::opendir(pathBuf.data());
  if (mcDir != nullptr) {
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(mcDir)) != nullptr) {
      if (std::strncmp(entry->d_name, "csrow", 5) == 0) {
        ++mc.csrowCount;
      }
    }
    ::closedir(mcDir);
  }
}

/// Collect CSRow information for a memory controller.
void collectCsRows(const char* mcName, std::int32_t mcIndex, EdacStatus& status) noexcept {
  std::array<char, PATH_BUF_SIZE> mcPath{};
  std::snprintf(mcPath.data(), mcPath.size(), "%s/%s", EDAC_MC_PATH, mcName);

  DIR* mcDir = ::opendir(mcPath.data());
  if (mcDir == nullptr) {
    return;
  }

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(mcDir)) != nullptr && status.csrowCount < EDAC_MAX_CSROW) {
    if (std::strncmp(entry->d_name, "csrow", 5) != 0) {
      continue;
    }

    const std::int32_t CSROW_IDX = parseIndex(entry->d_name, "csrow");
    if (CSROW_IDX < 0) {
      continue;
    }

    CsRow& row = status.csrows[status.csrowCount];
    row.mcIndex = static_cast<std::uint32_t>(mcIndex);
    row.csrowIndex = static_cast<std::uint32_t>(CSROW_IDX);

    // Read label
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "ch0_dimm_label");
    readFileString(pathBuf.data(), row.label);

    // Read error counts
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "ce_count");
    row.ceCount = readFileUint64(pathBuf.data());

    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "ue_count");
    row.ueCount = readFileUint64(pathBuf.data());

    // Read size
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "size_mb");
    row.sizeMb = static_cast<std::size_t>(readFileUint64(pathBuf.data()));

    // Read memory type
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "mem_type");
    readFileString(pathBuf.data(), row.memType);

    // Read EDAC mode
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "edac_mode");
    readFileString(pathBuf.data(), row.edacMode);

    ++status.csrowCount;
  }

  ::closedir(mcDir);
}

/// Collect DIMM information for a memory controller.
void collectDimms(const char* mcName, std::int32_t mcIndex, EdacStatus& status) noexcept {
  std::array<char, PATH_BUF_SIZE> mcPath{};
  std::snprintf(mcPath.data(), mcPath.size(), "%s/%s", EDAC_MC_PATH, mcName);

  DIR* mcDir = ::opendir(mcPath.data());
  if (mcDir == nullptr) {
    return;
  }

  std::array<char, PATH_BUF_SIZE> pathBuf{};
  struct dirent* entry = nullptr;

  while ((entry = ::readdir(mcDir)) != nullptr && status.dimmCount < EDAC_MAX_DIMM) {
    if (std::strncmp(entry->d_name, "dimm", 4) != 0) {
      continue;
    }

    const std::int32_t DIMM_IDX = parseIndex(entry->d_name, "dimm");
    if (DIMM_IDX < 0) {
      continue;
    }

    DimmInfo& dimm = status.dimms[status.dimmCount];
    dimm.mcIndex = static_cast<std::uint32_t>(mcIndex);
    dimm.dimmIndex = static_cast<std::uint32_t>(DIMM_IDX);

    // Read label
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "dimm_label");
    readFileString(pathBuf.data(), dimm.label);

    // Read location
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "dimm_location");
    readFileString(pathBuf.data(), dimm.location);

    // Read error counts
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "dimm_ce_count");
    dimm.ceCount = readFileUint64(pathBuf.data());

    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "dimm_ue_count");
    dimm.ueCount = readFileUint64(pathBuf.data());

    // Read size
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "size");
    dimm.sizeMb = static_cast<std::size_t>(readFileUint64(pathBuf.data()));

    // Read memory type
    buildMcSubPath(pathBuf.data(), pathBuf.size(), mcName, entry->d_name, "dimm_mem_type");
    readFileString(pathBuf.data(), dimm.memType);

    ++status.dimmCount;
  }

  ::closedir(mcDir);
}

/* ----------------------------- Formatting ----------------------------- */

/// Format bytes as human-readable string.
std::string formatMb(std::size_t mb) {
  if (mb >= 1024) {
    return fmt::format("{:.1f} GB", static_cast<double>(mb) / 1024.0);
  }
  return fmt::format("{} MB", mb);
}

} // namespace

/* ----------------------------- MemoryController Methods ----------------------------- */

bool MemoryController::hasErrors() const noexcept { return ceCount > 0 || ueCount > 0; }

bool MemoryController::hasCriticalErrors() const noexcept { return ueCount > 0; }

/* ----------------------------- EdacStatus Methods ----------------------------- */

bool EdacStatus::hasErrors() const noexcept { return totalCeCount > 0 || totalUeCount > 0; }

bool EdacStatus::hasCriticalErrors() const noexcept { return totalUeCount > 0; }

const MemoryController* EdacStatus::findController(std::int32_t mcIndex) const noexcept {
  for (std::size_t i = 0; i < mcCount; ++i) {
    if (controllers[i].mcIndex == mcIndex) {
      return &controllers[i];
    }
  }
  return nullptr;
}

std::string EdacStatus::toString() const {
  std::string out;
  out.reserve(1024);

  if (!edacSupported) {
    out = "EDAC: Not supported (kernel module not loaded or no ECC memory)\n";
    return out;
  }

  out += "EDAC Status:\n";
  out += fmt::format("  Memory controllers: {}\n", mcCount);
  out += fmt::format("  ECC enabled: {}\n", eccEnabled ? "yes" : "no");

  if (pollIntervalMs > 0) {
    out += fmt::format("  Poll interval: {} ms\n", pollIntervalMs);
  }

  // Error summary
  out += fmt::format("  Correctable errors (CE): {}\n", totalCeCount);
  out += fmt::format("  Uncorrectable errors (UE): {}\n", totalUeCount);

  if (totalUeCount > 0) {
    out += "  *** WARNING: Uncorrectable memory errors detected! ***\n";
  }

  // Per-controller details
  if (mcCount > 0) {
    out += "\n  Controllers:\n";
    for (std::size_t i = 0; i < mcCount; ++i) {
      const MemoryController& MC = controllers[i];
      out += fmt::format("    {}: {} ({}, {})\n", MC.name.data(),
                         MC.mcType[0] != '\0' ? MC.mcType.data() : "unknown",
                         MC.memType[0] != '\0' ? MC.memType.data() : "unknown",
                         MC.edacMode[0] != '\0' ? MC.edacMode.data() : "unknown");
      out += fmt::format("      Size: {}, CE: {}, UE: {}\n", formatMb(MC.sizeMb), MC.ceCount,
                         MC.ueCount);
    }
  }

  // DIMM details if available
  if (dimmCount > 0) {
    out += "\n  DIMMs:\n";
    for (std::size_t i = 0; i < dimmCount; ++i) {
      const DimmInfo& D = dimms[i];
      out += fmt::format("    mc{}:{}: {} ({})\n", D.mcIndex, D.dimmIndex,
                         D.label[0] != '\0' ? D.label.data() : "unlabeled",
                         D.location[0] != '\0' ? D.location.data() : "unknown");
      out += fmt::format("      CE: {}, UE: {}\n", D.ceCount, D.ueCount);
    }
  }

  return out;
}

std::string EdacStatus::toJson() const {
  std::string out;
  out.reserve(2048);

  out += "{\n";
  out += fmt::format("  \"edacSupported\": {},\n", edacSupported ? "true" : "false");
  out += fmt::format("  \"eccEnabled\": {},\n", eccEnabled ? "true" : "false");
  out += fmt::format("  \"mcCount\": {},\n", mcCount);
  out += fmt::format("  \"totalCeCount\": {},\n", totalCeCount);
  out += fmt::format("  \"totalUeCount\": {},\n", totalUeCount);
  out += fmt::format("  \"pollIntervalMs\": {},\n", pollIntervalMs);
  out += fmt::format("  \"hasErrors\": {},\n", hasErrors() ? "true" : "false");
  out += fmt::format("  \"hasCriticalErrors\": {},\n", hasCriticalErrors() ? "true" : "false");

  // Controllers array
  out += "  \"controllers\": [";
  for (std::size_t i = 0; i < mcCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    const MemoryController& MC = controllers[i];
    out +=
        fmt::format("{{\"name\": \"{}\", \"mcIndex\": {}, \"mcType\": \"{}\", \"memType\": \"{}\", "
                    "\"edacMode\": \"{}\", \"sizeMb\": {}, \"ceCount\": {}, \"ueCount\": {}}}",
                    MC.name.data(), MC.mcIndex, MC.mcType.data(), MC.memType.data(),
                    MC.edacMode.data(), MC.sizeMb, MC.ceCount, MC.ueCount);
  }
  out += "],\n";

  // CSRows array
  out += "  \"csrows\": [";
  for (std::size_t i = 0; i < csrowCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    const CsRow& R = csrows[i];
    out += fmt::format("{{\"mcIndex\": {}, \"csrowIndex\": {}, \"label\": \"{}\", "
                       "\"ceCount\": {}, \"ueCount\": {}, \"sizeMb\": {}}}",
                       R.mcIndex, R.csrowIndex, R.label.data(), R.ceCount, R.ueCount, R.sizeMb);
  }
  out += "],\n";

  // DIMMs array
  out += "  \"dimms\": [";
  for (std::size_t i = 0; i < dimmCount; ++i) {
    if (i > 0) {
      out += ", ";
    }
    const DimmInfo& D = dimms[i];
    out += fmt::format("{{\"mcIndex\": {}, \"dimmIndex\": {}, \"label\": \"{}\", "
                       "\"location\": \"{}\", \"ceCount\": {}, \"ueCount\": {}, \"sizeMb\": {}}}",
                       D.mcIndex, D.dimmIndex, D.label.data(), D.location.data(), D.ceCount,
                       D.ueCount, D.sizeMb);
  }
  out += "]\n";

  out += "}";
  return out;
}

/* ----------------------------- API ----------------------------- */

bool isEdacSupported() noexcept { return isDirectory(EDAC_MC_PATH); }

EdacStatus getEdacStatus() noexcept {
  EdacStatus status{};

  // Check if EDAC subsystem exists
  if (!isDirectory(EDAC_MC_PATH)) {
    status.edacSupported = false;
    return status;
  }
  status.edacSupported = true;

  // Read poll interval (milliseconds)
  std::array<char, PATH_BUF_SIZE> pathBuf{};
  std::snprintf(pathBuf.data(), pathBuf.size(), "%s/poll_msec", EDAC_MC_PATH);
  status.pollIntervalMs = readFileUint64(pathBuf.data());

  // Enumerate memory controllers
  DIR* mcDir = ::opendir(EDAC_MC_PATH);
  if (mcDir == nullptr) {
    return status;
  }

  // Collect MC names first for sorting
  std::array<std::array<char, 16>, EDAC_MAX_MC> mcNames{};
  std::size_t mcNameCount = 0;

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(mcDir)) != nullptr && mcNameCount < EDAC_MAX_MC) {
    if (std::strncmp(entry->d_name, "mc", 2) != 0) {
      continue;
    }
    // Verify it's a directory
    std::snprintf(pathBuf.data(), pathBuf.size(), "%s/%s", EDAC_MC_PATH, entry->d_name);
    if (!isDirectory(pathBuf.data())) {
      continue;
    }

    copyToFixedArray(mcNames[mcNameCount], entry->d_name);
    ++mcNameCount;
  }
  ::closedir(mcDir);

  // Sort MC names for consistent ordering
  sortFixedStrings(mcNames, mcNameCount);

  // Collect data for each memory controller
  for (std::size_t i = 0; i < mcNameCount; ++i) {
    const char* MC_NAME = mcNames[i].data();
    MemoryController& mc = status.controllers[status.mcCount];

    collectMemoryController(MC_NAME, mc);

    // Sum error counts
    status.totalCeCount += mc.ceCount;
    status.totalUeCount += mc.ueCount;

    // Collect CSRows and DIMMs
    collectCsRows(MC_NAME, mc.mcIndex, status);
    collectDimms(MC_NAME, mc.mcIndex, status);

    ++status.mcCount;
  }

  // ECC is considered enabled if we found any memory controllers
  status.eccEnabled = (status.mcCount > 0);

  return status;
}

} // namespace memory

} // namespace seeker