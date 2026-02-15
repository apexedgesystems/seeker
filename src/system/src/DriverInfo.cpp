/**
 * @file DriverInfo.cpp
 * @brief Implementation of kernel module inventory and driver assessment.
 * @note Uses dlopen for NVML runtime detection without linking.
 */

#include "src/system/inc/DriverInfo.hpp"
#include "src/helpers/inc/Files.hpp"

#include <dlfcn.h>  // dlopen, dlclose, RTLD_LAZY
#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <algorithm> // std::sort
#include <cstdlib>   // strtol
#include <cstring>   // strstr, strcmp, strlen, memcpy

#include <fmt/core.h>

// NVML header detection (compile-time)
#if __has_include("nvml.h") || __has_include(<nvml.h>)
#define SEEKER_NVML_HEADER_AVAILABLE 1
#else
#define SEEKER_NVML_HEADER_AVAILABLE 0
#endif

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::readFileToBuffer;

/// Copy string safely with null termination.
template <std::size_t N>
void safeCopy(std::array<char, N>& dest, const char* src, std::size_t srcLen = 0) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  if (srcLen == 0) {
    srcLen = std::strlen(src);
  }

  std::size_t copyLen = srcLen;
  if (copyLen >= N) {
    copyLen = N - 1;
  }

  std::memcpy(dest.data(), src, copyLen);
  dest[copyLen] = '\0';
}

/// Skip whitespace.
const char* skipWhitespace(const char* ptr) noexcept {
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }
  return ptr;
}

/// Find end of token (until whitespace, comma, or end).
const char* findTokenEnd(const char* ptr) noexcept {
  while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t' && *ptr != ',' && *ptr != '\n') {
    ++ptr;
  }
  return ptr;
}

/* ----------------------------- Module Parsing ----------------------------- */

/**
 * Parse a single line from /proc/modules.
 * Format: name size use_count deps state offset [taint]
 * Example: nvidia 56442880 1639 nvidia_modeset,nvidia_uvm, Live 0x0000000000000000 (POE)
 */
bool parseModuleLine(const char* line, DriverEntry& entry) noexcept {
  const char* ptr = line;

  // Parse name
  const char* nameEnd = findTokenEnd(ptr);
  if (nameEnd == ptr) {
    return false;
  }
  safeCopy(entry.name, ptr, static_cast<std::size_t>(nameEnd - ptr));
  ptr = skipWhitespace(nameEnd);

  // Parse size
  char* numEnd = nullptr;
  entry.sizeBytes = static_cast<std::size_t>(std::strtol(ptr, &numEnd, 10));
  if (numEnd == ptr) {
    return false;
  }
  ptr = skipWhitespace(numEnd);

  // Parse use count
  entry.useCount = static_cast<std::int32_t>(std::strtol(ptr, &numEnd, 10));
  if (numEnd == ptr) {
    return false;
  }
  ptr = skipWhitespace(numEnd);

  // Parse dependencies (comma-separated, may end with comma or space)
  entry.depCount = 0;
  if (*ptr != '-') {
    while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t' && entry.depCount < MAX_DRIVER_DEPS) {
      const char* depStart = ptr;
      while (*ptr != '\0' && *ptr != ',' && *ptr != ' ' && *ptr != '\t') {
        ++ptr;
      }
      if (ptr > depStart) {
        safeCopy(entry.deps[entry.depCount], depStart, static_cast<std::size_t>(ptr - depStart));
        ++entry.depCount;
      }
      if (*ptr == ',') {
        ++ptr;
      }
    }
  } else {
    // Skip single "-" (no deps)
    ++ptr;
  }
  ptr = skipWhitespace(ptr);

  // Parse state
  const char* stateEnd = findTokenEnd(ptr);
  safeCopy(entry.state, ptr, static_cast<std::size_t>(stateEnd - ptr));

  return true;
}

/// Build path to /sys/module/<name>/<file>
void buildModulePath(char* out, std::size_t outSize, const char* name, const char* file) noexcept {
  std::snprintf(out, outSize, "/sys/module/%s/%s", name, file);
}

} // namespace

/* ----------------------------- DriverEntry Methods ----------------------------- */

bool DriverEntry::isNamed(const char* targetName) const noexcept {
  if (targetName == nullptr) {
    return false;
  }
  return std::strcmp(name.data(), targetName) == 0;
}

std::string DriverEntry::toString() const {
  std::string out;
  out.reserve(256);

  out += fmt::format("{:<20} {:>10} refs={:<4} state={}", name.data(), sizeBytes, useCount,
                     state.data());

  if (version[0] != '\0') {
    out += fmt::format(" ver={}", version.data());
  }

  if (depCount > 0) {
    out += " deps=[";
    for (std::size_t i = 0; i < depCount; ++i) {
      if (i > 0) {
        out += ",";
      }
      out += deps[i].data();
    }
    out += "]";
  }

  return out;
}

/* ----------------------------- DriverInventory Methods ----------------------------- */

const DriverEntry* DriverInventory::find(const char* name) const noexcept {
  if (name == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < entryCount; ++i) {
    if (entries[i].isNamed(name)) {
      return &entries[i];
    }
  }
  return nullptr;
}

bool DriverInventory::isLoaded(const char* name) const noexcept { return find(name) != nullptr; }

bool DriverInventory::hasNvidiaDriver() const noexcept {
  return isLoaded("nvidia") || isLoaded("nvidia_uvm") || isLoaded("nvidia_drm");
}

std::string DriverInventory::toString() const {
  std::string out;
  out.reserve(4096);

  out += fmt::format("Driver Inventory ({} modules):\n", entryCount);
  out += fmt::format("  Kernel tainted: {} (mask={})\n", tainted ? "yes" : "no", taintMask);
  out += "\n";

  for (std::size_t i = 0; i < entryCount; ++i) {
    out += "  ";
    out += entries[i].toString();
    out += "\n";
  }

  return out;
}

std::string DriverInventory::toBriefSummary() const {
  return fmt::format("Modules: {} loaded, tainted={} (mask={})", entryCount, tainted ? "yes" : "no",
                     taintMask);
}

/* ----------------------------- DriverAssessment Methods ----------------------------- */

void DriverAssessment::addNote(const char* note) noexcept {
  if (note == nullptr || noteCount >= MAX_ASSESSMENT_NOTES) {
    return;
  }
  safeCopy(notes[noteCount], note);
  ++noteCount;
}

std::string DriverAssessment::toString() const {
  std::string out;
  out.reserve(512);

  out += "Driver Assessment:\n";
  out += "  GPU Drivers:\n";
  out += fmt::format("    NVIDIA:    loaded={} nvml_header={} nvml_runtime={}\n",
                     nvidiaLoaded ? "yes" : "no", nvmlHeaderAvailable ? "yes" : "no",
                     nvmlRuntimePresent ? "yes" : "no");
  out += fmt::format("    nouveau:   {}\n", nouveauLoaded ? "loaded" : "no");
  out += fmt::format("    i915:      {}\n", i915Loaded ? "loaded" : "no");
  out += fmt::format("    amdgpu:    {}\n", amdgpuLoaded ? "loaded" : "no");

  if (noteCount > 0) {
    out += "  Notes:\n";
    for (std::size_t i = 0; i < noteCount; ++i) {
      out += "    - ";
      out += notes[i].data();
      out += "\n";
    }
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

bool isNvidiaDriverLoaded() noexcept {
  std::array<char, 16384> buf{};
  if (readFileToBuffer("/proc/modules", buf.data(), buf.size()) == 0) {
    return false;
  }

  // Quick string search for nvidia modules
  return std::strstr(buf.data(), "nvidia") != nullptr;
}

bool isNvmlRuntimeAvailable() noexcept {
  void* handle = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (handle != nullptr) {
    ::dlclose(handle);
    return true;
  }
  return false;
}

DriverInventory getDriverInventory() noexcept {
  DriverInventory inv{};

  // Read kernel taint status
  std::array<char, 64> taintBuf{};
  if (readFileToBuffer("/proc/sys/kernel/tainted", taintBuf.data(), taintBuf.size()) > 0) {
    inv.taintMask = static_cast<std::int32_t>(std::strtol(taintBuf.data(), nullptr, 10));
    inv.tainted = (inv.taintMask != 0);
  }

  // Read /proc/modules
  std::array<char, 65536> modulesBuf{};
  const std::size_t LEN = readFileToBuffer("/proc/modules", modulesBuf.data(), modulesBuf.size());
  if (LEN == 0) {
    return inv;
  }

  // Parse each line
  const char* ptr = modulesBuf.data();
  while (*ptr != '\0' && inv.entryCount < MAX_DRIVER_ENTRIES) {
    // Find end of line
    const char* lineEnd = ptr;
    while (*lineEnd != '\0' && *lineEnd != '\n') {
      ++lineEnd;
    }

    // Parse line
    if (lineEnd > ptr) {
      // Temporarily null-terminate line
      char lineBuffer[512];
      std::size_t lineLen = static_cast<std::size_t>(lineEnd - ptr);
      if (lineLen >= sizeof(lineBuffer)) {
        lineLen = sizeof(lineBuffer) - 1;
      }
      std::memcpy(lineBuffer, ptr, lineLen);
      lineBuffer[lineLen] = '\0';

      if (parseModuleLine(lineBuffer, inv.entries[inv.entryCount])) {
        // Try to get version info from sysfs
        const char* modName = inv.entries[inv.entryCount].name.data();
        std::array<char, 256> pathBuf{};
        std::array<char, DRIVER_VERSION_SIZE> verBuf{};

        buildModulePath(pathBuf.data(), pathBuf.size(), modName, "version");
        if (readFileToBuffer(pathBuf.data(), verBuf.data(), verBuf.size()) > 0) {
          safeCopy(inv.entries[inv.entryCount].version, verBuf.data());
        }

        buildModulePath(pathBuf.data(), pathBuf.size(), modName, "srcversion");
        if (readFileToBuffer(pathBuf.data(), verBuf.data(), verBuf.size()) > 0) {
          safeCopy(inv.entries[inv.entryCount].srcVersion, verBuf.data());
        }

        ++inv.entryCount;
      }
    }

    // Move to next line
    ptr = (*lineEnd == '\n') ? lineEnd + 1 : lineEnd;
  }

  // Sort by name for consistent output
  std::sort(inv.entries, inv.entries + inv.entryCount,
            [](const DriverEntry& a, const DriverEntry& b) {
              return std::strcmp(a.name.data(), b.name.data()) < 0;
            });

  return inv;
}

DriverAssessment assessDrivers(const DriverInventory& inv) noexcept {
  DriverAssessment asmt{};

  // NVML header availability (compile-time)
  asmt.nvmlHeaderAvailable = (SEEKER_NVML_HEADER_AVAILABLE != 0);

  // Check for GPU-related drivers
  asmt.nvidiaLoaded = inv.hasNvidiaDriver();
  asmt.nouveauLoaded = inv.isLoaded("nouveau");
  asmt.i915Loaded = inv.isLoaded("i915");
  asmt.amdgpuLoaded = inv.isLoaded("amdgpu");

  // NVML runtime check (only if NVIDIA driver is loaded)
  if (asmt.nvidiaLoaded) {
    asmt.nvmlRuntimePresent = isNvmlRuntimeAvailable();
  }

  // Generate assessment notes
  if (asmt.nvidiaLoaded && !asmt.nvmlRuntimePresent) {
    asmt.addNote("NVIDIA driver loaded but NVML runtime not found (limited telemetry)");
  }

  if (!asmt.nvidiaLoaded && asmt.nvmlRuntimePresent) {
    asmt.addNote("NVML runtime present but NVIDIA driver not loaded");
  }

  if (asmt.nvidiaLoaded && asmt.nouveauLoaded) {
    asmt.addNote("Both nvidia and nouveau loaded (potential conflict)");
  }

  if (inv.tainted) {
    // Decode some common taint flags
    std::string taintMsg = "Kernel tainted:";
    if ((inv.taintMask & (1 << 0)) != 0) {
      taintMsg += " proprietary";
    }
    if ((inv.taintMask & (1 << 1)) != 0) {
      taintMsg += " forced-load";
    }
    if ((inv.taintMask & (1 << 12)) != 0) {
      taintMsg += " out-of-tree";
    }
    asmt.addNote(taintMsg.c_str());
  }

  return asmt;
}

} // namespace system

} // namespace seeker