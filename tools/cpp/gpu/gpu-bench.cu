/**
 * @file gpu-bench.cu
 * @brief GPU performance benchmarks.
 *
 * Measures GPU performance characteristics:
 *  - Host-to-device and device-to-host bandwidth
 *  - Device-to-device copy bandwidth
 *  - Kernel launch overhead
 *  - Memory allocation timing
 */

#include "src/gpu/inc/GpuBench.cuh"
#include "src/gpu/inc/GpuTopology.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace gpu = seeker::gpu;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_DEVICE = 2,
  ARG_BUDGET = 3,
  ARG_SIZE = 4,
  ARG_QUICK = 5,
  ARG_THOROUGH = 6,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "GPU performance benchmarks: bandwidth, latency, and allocation timing.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DEVICE] = {"--device", 1, false, "GPU device index (default: 0)"};
  map[ARG_BUDGET] = {"--budget", 1, false, "Time budget in ms (default: 1000)"};
  map[ARG_SIZE] = {"--size", 1, false, "Transfer size in MiB (default: 64)"};
  map[ARG_QUICK] = {"--quick", 0, false, "Quick benchmark preset (500ms budget)"};
  map[ARG_THOROUGH] = {"--thorough", 0, false, "Thorough benchmark preset (5s budget)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printBandwidth(const char* name, const gpu::BandwidthResult& result) {
  if (result.iterations > 0) {
    fmt::print("  {:12}  {:>10.1f} MiB/s  ({} iters, {:.1f} us avg)\n", name, result.bandwidthMiBps,
               result.iterations, result.latencyUs);
  } else {
    fmt::print("  {:12}  (not measured)\n", name);
  }
}

void printHuman(const gpu::GpuBenchResult& result) {
  fmt::print("=== GPU {} Benchmark Results ===\n", result.deviceIndex);
  fmt::print("Device: {}\n\n", result.name);

  // Transfer benchmarks
  fmt::print("Transfer Bandwidth (pinned memory):\n");
  printBandwidth("H2D", result.h2d);
  printBandwidth("D2H", result.d2h);
  printBandwidth("D2D", result.d2d);

  if (result.h2dPageable.iterations > 0) {
    fmt::print("\nTransfer Bandwidth (pageable memory):\n");
    printBandwidth("H2D pageable", result.h2dPageable);
    printBandwidth("D2H pageable", result.d2hPageable);
  }

  // Launch overhead
  fmt::print("\nKernel Launch:\n");
  if (result.launchIterations > 0) {
    fmt::print("  Launch overhead:  {:.2f} us ({} iterations)\n", result.launchOverheadUs,
               result.launchIterations);
  }

  // Allocation timing
  fmt::print("\nMemory Operations:\n");
  if (result.deviceAllocUs > 0) {
    fmt::print("  cudaMalloc:      {:.1f} us\n", result.deviceAllocUs);
    fmt::print("  cudaFree:        {:.1f} us\n", result.deviceFreeUs);
    fmt::print("  cudaMallocHost:  {:.1f} us\n", result.pinnedAllocUs);
    fmt::print("  cudaFreeHost:    {:.1f} us\n", result.pinnedFreeUs);
  }

  // Stream operations
  if (result.streamCreateUs > 0) {
    fmt::print("\nStream Operations:\n");
    fmt::print("  Stream create:   {:.1f} us\n", result.streamCreateUs);
    fmt::print("  Stream sync:     {:.1f} us\n", result.streamSyncUs);
    fmt::print("  Event create:    {:.1f} us\n", result.eventCreateUs);
  }

  // Occupancy
  if (result.maxActiveBlocksPerSm > 0) {
    fmt::print("\nOccupancy (empty kernel):\n");
    fmt::print("  Max blocks/SM:   {}\n", result.maxActiveBlocksPerSm);
    fmt::print("  Max warps/SM:    {}\n", result.maxActiveWarpsPerSm);
  }

  // Summary
  fmt::print("\n");
  if (result.completed) {
    fmt::print("\033[32mBenchmark completed successfully\033[0m\n");
  } else {
    fmt::print("\033[33mBenchmark incomplete (time budget exceeded)\033[0m\n");
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const gpu::GpuBenchResult& result) {
  fmt::print("{{\n");
  fmt::print("  \"deviceIndex\": {},\n", result.deviceIndex);
  fmt::print("  \"name\": \"{}\",\n", result.name);
  fmt::print("  \"completed\": {},\n", result.completed);
  fmt::print("  \"budgetMs\": {},\n", result.budgetMs.count());

  // Transfer benchmarks
  fmt::print("  \"bandwidth\": {{\n");
  fmt::print("    \"h2d\": {{\"mibps\": {:.1f}, \"latencyUs\": {:.1f}, \"iterations\": {}}},\n",
             result.h2d.bandwidthMiBps, result.h2d.latencyUs, result.h2d.iterations);
  fmt::print("    \"d2h\": {{\"mibps\": {:.1f}, \"latencyUs\": {:.1f}, \"iterations\": {}}},\n",
             result.d2h.bandwidthMiBps, result.d2h.latencyUs, result.d2h.iterations);
  fmt::print("    \"d2d\": {{\"mibps\": {:.1f}, \"latencyUs\": {:.1f}, \"iterations\": {}}},\n",
             result.d2d.bandwidthMiBps, result.d2d.latencyUs, result.d2d.iterations);
  fmt::print(
      "    \"h2dPageable\": {{\"mibps\": {:.1f}, \"latencyUs\": {:.1f}, \"iterations\": {}}},\n",
      result.h2dPageable.bandwidthMiBps, result.h2dPageable.latencyUs,
      result.h2dPageable.iterations);
  fmt::print(
      "    \"d2hPageable\": {{\"mibps\": {:.1f}, \"latencyUs\": {:.1f}, \"iterations\": {}}}\n",
      result.d2hPageable.bandwidthMiBps, result.d2hPageable.latencyUs,
      result.d2hPageable.iterations);
  fmt::print("  }},\n");

  // Launch overhead
  fmt::print("  \"launch\": {{\n");
  fmt::print("    \"overheadUs\": {:.2f},\n", result.launchOverheadUs);
  fmt::print("    \"iterations\": {}\n", result.launchIterations);
  fmt::print("  }},\n");

  // Allocation timing
  fmt::print("  \"allocation\": {{\n");
  fmt::print("    \"deviceAllocUs\": {:.1f},\n", result.deviceAllocUs);
  fmt::print("    \"deviceFreeUs\": {:.1f},\n", result.deviceFreeUs);
  fmt::print("    \"pinnedAllocUs\": {:.1f},\n", result.pinnedAllocUs);
  fmt::print("    \"pinnedFreeUs\": {:.1f}\n", result.pinnedFreeUs);
  fmt::print("  }},\n");

  // Stream operations
  fmt::print("  \"streams\": {{\n");
  fmt::print("    \"createUs\": {:.1f},\n", result.streamCreateUs);
  fmt::print("    \"syncUs\": {:.1f},\n", result.streamSyncUs);
  fmt::print("    \"eventCreateUs\": {:.1f}\n", result.eventCreateUs);
  fmt::print("  }},\n");

  // Occupancy
  fmt::print("  \"occupancy\": {{\n");
  fmt::print("    \"maxBlocksPerSm\": {},\n", result.maxActiveBlocksPerSm);
  fmt::print("    \"maxWarpsPerSm\": {}\n", result.maxActiveWarpsPerSm);
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  int targetDevice = 0;

  // Default options
  gpu::BenchmarkOptions options;

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

    // Presets
    if (pargs.count(ARG_QUICK) != 0) {
      options.budget = std::chrono::milliseconds{500};
      options.skipPageable = true;
    }
    if (pargs.count(ARG_THOROUGH) != 0) {
      options.budget = std::chrono::milliseconds{5000};
    }

    // Custom options override presets
    if (pargs.count(ARG_DEVICE) != 0 && !pargs[ARG_DEVICE].empty()) {
      targetDevice = static_cast<int>(std::strtol(pargs[ARG_DEVICE][0].data(), nullptr, 10));
    }

    if (pargs.count(ARG_BUDGET) != 0 && !pargs[ARG_BUDGET].empty()) {
      const long MS = std::strtol(pargs[ARG_BUDGET][0].data(), nullptr, 10);
      if (MS > 0) {
        options.budget = std::chrono::milliseconds{MS};
      }
    }

    if (pargs.count(ARG_SIZE) != 0 && !pargs[ARG_SIZE].empty()) {
      const long MIB = std::strtol(pargs[ARG_SIZE][0].data(), nullptr, 10);
      if (MIB > 0) {
        options.transferSize = static_cast<std::size_t>(MIB) * 1024 * 1024;
      }
    }
  }

  // Check if device exists
  const gpu::GpuTopology TOPO = gpu::getGpuTopology();
  if (TOPO.deviceCount == 0) {
    if (jsonOutput) {
      fmt::print("{{\"error\": \"No GPUs detected\"}}\n");
    } else {
      fmt::print("No GPUs detected.\n");
    }
    return 1;
  }

  if (targetDevice >= TOPO.deviceCount) {
    if (jsonOutput) {
      fmt::print("{{\"error\": \"Invalid device index {}\"}}\n", targetDevice);
    } else {
      fmt::print("Error: Invalid device index {} (only {} GPU(s) available)\n", targetDevice,
                 TOPO.deviceCount);
    }
    return 1;
  }

  // Print configuration before running
  if (!jsonOutput) {
    fmt::print("Running GPU benchmark on device {}...\n", targetDevice);
    fmt::print("  Budget: {} ms, Transfer size: {} MiB\n\n", options.budget.count(),
               options.transferSize / (1024 * 1024));
  }

  // Run benchmark
  const gpu::GpuBenchResult RESULT = gpu::runGpuBench(targetDevice, options);

  if (jsonOutput) {
    printJson(RESULT);
  } else {
    printHuman(RESULT);
  }

  return RESULT.completed ? 0 : 1;
}
