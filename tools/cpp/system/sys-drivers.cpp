/**
 * @file sys-drivers.cpp
 * @brief Display kernel module inventory and driver assessment.
 *
 * Lists loaded kernel modules with versions and provides GPU driver
 * assessment for RT/CUDA workloads.
 */

#include "src/system/inc/DriverInfo.hpp"
#include "src/helpers/inc/Format.hpp"
#include "src/helpers/inc/Args.hpp"
#include "src/helpers/inc/Format.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace sys = seeker::system;

using seeker::helpers::format::bytesBinary;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_NVIDIA = 2,
  ARG_BRIEF = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display loaded kernel modules and driver assessment.\n"
    "Use --nvidia for GPU-focused output, --brief for summary only.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_NVIDIA] = {"--nvidia", 0, false, "Show NVIDIA driver details only"};
  map[ARG_BRIEF] = {"--brief", 0, false, "Show brief summary only"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printBriefSummary(const sys::DriverInventory& inv, const sys::DriverAssessment& asmt) {
  fmt::print("=== Driver Summary ===\n");
  fmt::print("  Modules loaded:  {}\n", inv.entryCount);
  fmt::print("  Kernel tainted:  {}\n", inv.tainted ? "yes" : "no");

  // GPU info
  fmt::print("\n=== GPU Drivers ===\n");
  fmt::print("  NVIDIA:    {}\n", asmt.nvidiaLoaded ? "loaded" : "not loaded");
  if (asmt.nvidiaLoaded) {
    fmt::print("  NVML:      {}\n", asmt.nvmlRuntimePresent ? "available" : "not available");
  }
  fmt::print("  Nouveau:   {}\n", asmt.nouveauLoaded ? "loaded" : "not loaded");
  fmt::print("  i915:      {}\n", asmt.i915Loaded ? "loaded" : "not loaded");
  fmt::print("  amdgpu:    {}\n", asmt.amdgpuLoaded ? "loaded" : "not loaded");

  // Notes
  if (asmt.noteCount > 0) {
    fmt::print("\n=== Notes ===\n");
    for (std::size_t i = 0; i < asmt.noteCount; ++i) {
      fmt::print("  - {}\n", asmt.notes[i].data());
    }
  }
}

void printNvidiaDetails(const sys::DriverInventory& inv, const sys::DriverAssessment& asmt) {
  fmt::print("=== NVIDIA Driver Status ===\n");

  if (!asmt.nvidiaLoaded) {
    fmt::print("  NVIDIA driver is NOT loaded.\n");
    if (asmt.nouveauLoaded) {
      fmt::print("  Nouveau (open-source) driver is loaded instead.\n");
      fmt::print("  -> For CUDA/RT workloads, install proprietary NVIDIA driver.\n");
    }
    return;
  }

  // Find and display NVIDIA modules
  const char* NVIDIA_MODULES[] = {"nvidia", "nvidia_uvm", "nvidia_drm", "nvidia_modeset"};

  fmt::print("  Status: LOADED\n");
  fmt::print("\n  Modules:\n");

  for (const char* modName : NVIDIA_MODULES) {
    const sys::DriverEntry* entry = inv.find(modName);
    if (entry != nullptr) {
      fmt::print("    {:<16} ", entry->name.data());
      if (entry->version[0] != '\0') {
        fmt::print("v{:<12} ", entry->version.data());
      } else {
        fmt::print("{:<13} ", "");
      }
      fmt::print("{:>10}  refs={}\n", bytesBinary(entry->sizeBytes), entry->useCount);
    }
  }

  // NVML status
  fmt::print("\n  NVML:\n");
  fmt::print("    Header available:  {}\n", asmt.nvmlHeaderAvailable ? "yes" : "no");
  fmt::print("    Runtime present:   {}\n", asmt.nvmlRuntimePresent ? "yes" : "no");

  // Notes
  if (asmt.noteCount > 0) {
    fmt::print("\n  Notes:\n");
    for (std::size_t i = 0; i < asmt.noteCount; ++i) {
      fmt::print("    - {}\n", asmt.notes[i].data());
    }
  }
}

void printFullInventory(const sys::DriverInventory& inv, const sys::DriverAssessment& asmt) {
  fmt::print("=== Kernel Module Inventory ===\n");
  fmt::print("  Total modules: {}\n", inv.entryCount);
  fmt::print("  Kernel taint:  {} (mask={:#x})\n", inv.tainted ? "yes" : "no", inv.taintMask);

  fmt::print("\n  {:<20} {:<12} {:>10}  {:<6}  {}\n", "Module", "Version", "Size", "Refs", "State");
  fmt::print("  {:-<20} {:-<12} {:-^10}  {:-^6}  {:-<8}\n", "", "", "", "", "");

  for (std::size_t i = 0; i < inv.entryCount; ++i) {
    const auto& ENTRY = inv.entries[i];
    fmt::print("  {:<20} {:<12} {:>10}  {:>6}  {}\n", ENTRY.name.data(),
               ENTRY.version[0] != '\0' ? ENTRY.version.data() : "-", bytesBinary(ENTRY.sizeBytes),
               ENTRY.useCount, ENTRY.state.data());
  }

  // GPU assessment
  fmt::print("\n=== GPU Assessment ===\n");
  fmt::print("  NVIDIA:  {}{}\n", asmt.nvidiaLoaded ? "loaded" : "not loaded",
             asmt.nvmlRuntimePresent ? " (NVML available)" : "");
  fmt::print("  Nouveau: {}\n", asmt.nouveauLoaded ? "loaded" : "not loaded");
  fmt::print("  i915:    {}\n", asmt.i915Loaded ? "loaded" : "not loaded");
  fmt::print("  amdgpu:  {}\n", asmt.amdgpuLoaded ? "loaded" : "not loaded");

  // Notes
  if (asmt.noteCount > 0) {
    fmt::print("\n=== Notes ===\n");
    for (std::size_t i = 0; i < asmt.noteCount; ++i) {
      fmt::print("  - {}\n", asmt.notes[i].data());
    }
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const sys::DriverInventory& inv, const sys::DriverAssessment& asmt) {
  fmt::print("{{\n");

  // Summary
  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"moduleCount\": {},\n", inv.entryCount);
  fmt::print("    \"tainted\": {},\n", inv.tainted);
  fmt::print("    \"taintMask\": {}\n", inv.taintMask);
  fmt::print("  }},\n");

  // Modules array
  fmt::print("  \"modules\": [\n");
  for (std::size_t i = 0; i < inv.entryCount; ++i) {
    const auto& ENTRY = inv.entries[i];
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", ENTRY.name.data());
    fmt::print("      \"version\": \"{}\",\n", ENTRY.version.data());
    fmt::print("      \"srcVersion\": \"{}\",\n", ENTRY.srcVersion.data());
    fmt::print("      \"state\": \"{}\",\n", ENTRY.state.data());
    fmt::print("      \"sizeBytes\": {},\n", ENTRY.sizeBytes);
    fmt::print("      \"useCount\": {},\n", ENTRY.useCount);

    // Dependencies
    fmt::print("      \"deps\": [");
    for (std::size_t d = 0; d < ENTRY.depCount; ++d) {
      if (d > 0)
        fmt::print(", ");
      fmt::print("\"{}\"", ENTRY.deps[d].data());
    }
    fmt::print("]\n");

    fmt::print("    }}{}\n", (i + 1 < inv.entryCount) ? "," : "");
  }
  fmt::print("  ],\n");

  // Assessment
  fmt::print("  \"assessment\": {{\n");
  fmt::print("    \"nvidiaLoaded\": {},\n", asmt.nvidiaLoaded);
  fmt::print("    \"nvmlHeaderAvailable\": {},\n", asmt.nvmlHeaderAvailable);
  fmt::print("    \"nvmlRuntimePresent\": {},\n", asmt.nvmlRuntimePresent);
  fmt::print("    \"nouveauLoaded\": {},\n", asmt.nouveauLoaded);
  fmt::print("    \"i915Loaded\": {},\n", asmt.i915Loaded);
  fmt::print("    \"amdgpuLoaded\": {},\n", asmt.amdgpuLoaded);

  // Notes
  fmt::print("    \"notes\": [");
  for (std::size_t i = 0; i < asmt.noteCount; ++i) {
    if (i > 0)
      fmt::print(", ");
    fmt::print("\"{}\"", asmt.notes[i].data());
  }
  fmt::print("]\n");

  fmt::print("  }}\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool nvidiaOnly = false;
  bool briefOnly = false;

  if (argc > 1) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    std::string error;
    if (!seeker::helpers::args::parseArgs(args, ARG_MAP, pargs, error)) {
      fmt::print(stderr, "Error: {}\n\n", error);
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 1;
    }

    if (pargs.count(ARG_HELP) != 0) {
      seeker::helpers::args::printUsage(argv[0], DESCRIPTION, ARG_MAP);
      return 0;
    }

    jsonOutput = (pargs.count(ARG_JSON) != 0);
    nvidiaOnly = (pargs.count(ARG_NVIDIA) != 0);
    briefOnly = (pargs.count(ARG_BRIEF) != 0);
  }

  // Gather data
  const sys::DriverInventory INV = sys::getDriverInventory();
  const sys::DriverAssessment ASMT = sys::assessDrivers(INV);

  if (jsonOutput) {
    printJson(INV, ASMT);
  } else if (nvidiaOnly) {
    printNvidiaDetails(INV, ASMT);
  } else if (briefOnly) {
    printBriefSummary(INV, ASMT);
  } else {
    printFullInventory(INV, ASMT);
  }

  return 0;
}