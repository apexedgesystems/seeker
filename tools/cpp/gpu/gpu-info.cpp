/**
 * @file gpu-info.cpp
 * @brief GPU system information and topology overview.
 *
 * Displays GPU topology, driver versions, PCIe links, and device capabilities.
 * Designed for quick GPU subsystem assessment.
 */

#include "src/gpu/inc/GpuDriverStatus.hpp"
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/gpu/inc/PcieStatus.hpp"
#include "src/helpers/inc/Format.hpp"
#include "src/helpers/inc/Args.hpp"
#include "src/helpers/inc/Format.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace gpu = seeker::gpu;

using seeker::helpers::format::bytesBinary;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_DEVICE = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display GPU topology, drivers, PCIe links, and device capabilities.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DEVICE] = {"--device", 1, false, "GPU device index (default: all)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printDevice(const gpu::GpuDevice& dev, const gpu::PcieStatus& pcie,
                 const gpu::GpuDriverStatus& drv) {
  fmt::print("=== GPU {} ===\n", dev.deviceIndex);
  fmt::print("  Name:        {}\n", dev.name);
  fmt::print("  Vendor:      {}\n", gpu::toString(dev.vendor));

  if (!dev.uuid.empty()) {
    fmt::print("  UUID:        {}\n", dev.uuid);
  }

  // Compute capability
  if (dev.smMajor > 0) {
    fmt::print("  Compute:     SM {} ({} SMs, {} CUDA cores)\n", dev.computeCapability(),
               dev.smCount, dev.cudaCores);
  }

  // Memory
  fmt::print("  Memory:      {} ({}-bit bus)\n", bytesBinary(dev.totalMemoryBytes),
             dev.memoryBusWidth);

  // Execution limits
  if (dev.maxThreadsPerBlock > 0) {
    fmt::print("  Max threads: {} per block, {} per SM\n", dev.maxThreadsPerBlock,
               dev.maxThreadsPerSm);
    fmt::print("  Shared mem:  {} per block, {} per SM\n", bytesBinary(dev.sharedMemPerBlock),
               bytesBinary(dev.sharedMemPerSm));
  }

  // PCIe
  if (!pcie.bdf.empty()) {
    fmt::print("  PCIe:        {} (x{} Gen{})\n", pcie.bdf, pcie.currentWidth,
               static_cast<int>(pcie.currentGen));
    if (!pcie.isAtMaxLink()) {
      fmt::print("               -> Max: x{} Gen{}\n", pcie.maxWidth,
                 static_cast<int>(pcie.maxGen));
    }
    if (pcie.numaNode >= 0) {
      fmt::print("  NUMA node:   {}\n", pcie.numaNode);
    }
  }

  // Driver info
  if (!drv.driverVersion.empty()) {
    fmt::print("  Driver:      {} (CUDA {})\n", drv.driverVersion,
               gpu::GpuDriverStatus::formatCudaVersion(drv.cudaDriverVersion));
  }

  // Configuration
  fmt::print("  Compute mode: {}\n", gpu::toString(drv.computeMode));
  fmt::print("  Persistence:  {}\n", drv.persistenceMode ? "enabled" : "disabled");

  // Capabilities
  std::string caps;
  if (dev.concurrentKernels) {
    caps += "ConcurrentKernels ";
  }
  if (dev.managedMemory) {
    caps += "ManagedMem ";
  }
  if (dev.unifiedAddressing) {
    caps += "UnifiedAddr ";
  }
  if (dev.asyncEngines) {
    caps += "AsyncEngines ";
  }
  if (!caps.empty()) {
    fmt::print("  Capabilities: {}\n", caps);
  }
}

void printTopologySummary(const gpu::GpuTopology& topo) {
  fmt::print("\n=== Summary ===\n");
  fmt::print("  Total GPUs:   {}\n", topo.deviceCount);

  if (topo.nvidiaCount > 0) {
    fmt::print("  NVIDIA:       {}\n", topo.nvidiaCount);
  }
  if (topo.amdCount > 0) {
    fmt::print("  AMD:          {}\n", topo.amdCount);
  }
  if (topo.intelCount > 0) {
    fmt::print("  Intel:        {}\n", topo.intelCount);
  }
}

void printHuman(const gpu::GpuTopology& topo, const std::vector<gpu::PcieStatus>& pcieList,
                const std::vector<gpu::GpuDriverStatus>& drvList, int targetDevice) {
  if (topo.deviceCount == 0) {
    fmt::print("No GPUs detected.\n");
    return;
  }

  // Print specified device or all
  for (std::size_t i = 0; i < topo.devices.size(); ++i) {
    const auto& DEV = topo.devices[i];

    if (targetDevice >= 0 && DEV.deviceIndex != targetDevice) {
      continue;
    }

    // Find matching PCIe and driver info
    gpu::PcieStatus pcie;
    for (const auto& PCIE_ENTRY : pcieList) {
      if (PCIE_ENTRY.deviceIndex == DEV.deviceIndex) {
        pcie = PCIE_ENTRY;
        break;
      }
    }

    gpu::GpuDriverStatus drv;
    for (const auto& DRV_ENTRY : drvList) {
      if (DRV_ENTRY.deviceIndex == DEV.deviceIndex) {
        drv = DRV_ENTRY;
        break;
      }
    }

    if (i > 0) {
      fmt::print("\n");
    }
    printDevice(DEV, pcie, drv);
  }

  if (targetDevice < 0 && topo.deviceCount > 1) {
    printTopologySummary(topo);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const gpu::GpuTopology& topo, const std::vector<gpu::PcieStatus>& pcieList,
               const std::vector<gpu::GpuDriverStatus>& drvList, int targetDevice) {
  fmt::print("{{\n");

  // Topology summary
  fmt::print("  \"deviceCount\": {},\n", topo.deviceCount);
  fmt::print("  \"nvidiaCount\": {},\n", topo.nvidiaCount);
  fmt::print("  \"amdCount\": {},\n", topo.amdCount);
  fmt::print("  \"intelCount\": {},\n", topo.intelCount);

  // Devices array
  fmt::print("  \"devices\": [\n");
  bool first = true;
  for (const auto& DEV : topo.devices) {
    if (targetDevice >= 0 && DEV.deviceIndex != targetDevice) {
      continue;
    }

    if (!first) {
      fmt::print(",\n");
    }
    first = false;

    // Find matching info
    gpu::PcieStatus pcie;
    gpu::GpuDriverStatus drv;
    for (const auto& PCIE_ENTRY : pcieList) {
      if (PCIE_ENTRY.deviceIndex == DEV.deviceIndex) {
        pcie = PCIE_ENTRY;
        break;
      }
    }
    for (const auto& DRV_ENTRY : drvList) {
      if (DRV_ENTRY.deviceIndex == DEV.deviceIndex) {
        drv = DRV_ENTRY;
        break;
      }
    }

    fmt::print("    {{\n");
    fmt::print("      \"deviceIndex\": {},\n", DEV.deviceIndex);
    fmt::print("      \"name\": \"{}\",\n", DEV.name);
    fmt::print("      \"vendor\": \"{}\",\n", gpu::toString(DEV.vendor));
    fmt::print("      \"uuid\": \"{}\",\n", DEV.uuid);
    fmt::print("      \"smMajor\": {}, \"smMinor\": {},\n", DEV.smMajor, DEV.smMinor);
    fmt::print("      \"smCount\": {}, \"cudaCores\": {},\n", DEV.smCount, DEV.cudaCores);
    fmt::print("      \"totalMemoryBytes\": {},\n", DEV.totalMemoryBytes);
    fmt::print("      \"memoryBusWidth\": {},\n", DEV.memoryBusWidth);

    // PCIe
    fmt::print("      \"pcie\": {{\n");
    fmt::print("        \"bdf\": \"{}\",\n", pcie.bdf);
    fmt::print("        \"currentWidth\": {}, \"maxWidth\": {},\n", pcie.currentWidth,
               pcie.maxWidth);
    fmt::print("        \"currentGen\": {}, \"maxGen\": {},\n", static_cast<int>(pcie.currentGen),
               static_cast<int>(pcie.maxGen));
    fmt::print("        \"numaNode\": {}\n", pcie.numaNode);
    fmt::print("      }},\n");

    // Driver
    fmt::print("      \"driver\": {{\n");
    fmt::print("        \"version\": \"{}\",\n", drv.driverVersion);
    fmt::print("        \"cudaVersion\": {},\n", drv.cudaDriverVersion);
    fmt::print("        \"persistenceMode\": {},\n", drv.persistenceMode);
    fmt::print("        \"computeMode\": \"{}\"\n", gpu::toString(drv.computeMode));
    fmt::print("      }}\n");

    fmt::print("    }}");
  }
  fmt::print("\n  ]\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int targetDevice = -1;

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

    if (pargs.count(ARG_DEVICE) != 0 && !pargs[ARG_DEVICE].empty()) {
      targetDevice = static_cast<int>(std::strtol(pargs[ARG_DEVICE][0].data(), nullptr, 10));
    }
  }

  // Gather data
  const gpu::GpuTopology TOPO = gpu::getGpuTopology();
  const std::vector<gpu::PcieStatus> PCIE_LIST = gpu::getAllPcieStatus();
  const std::vector<gpu::GpuDriverStatus> DRV_LIST = gpu::getAllGpuDriverStatus();

  if (jsonOutput) {
    printJson(TOPO, PCIE_LIST, DRV_LIST, targetDevice);
  } else {
    printHuman(TOPO, PCIE_LIST, DRV_LIST, targetDevice);
  }

  return 0;
}
