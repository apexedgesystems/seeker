/**
 * @file storage-bench.cpp
 * @brief Bounded storage benchmark runner for performance characterization.
 *
 * Runs sequential and random I/O benchmarks with configurable parameters.
 * Designed for quick storage characterization, not exhaustive testing.
 */

#include "src/storage/inc/StorageBench.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace storage = seeker::storage;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_DIR = 2,
  ARG_SIZE = 3,
  ARG_ITERS = 4,
  ARG_BUDGET = 5,
  ARG_DIRECT = 6,
  ARG_QUICK = 7,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Run bounded storage benchmarks (throughput, latency, fsync).";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DIR] = {"--dir", 1, false, "Directory to run benchmarks in (default: /tmp)"};
  map[ARG_SIZE] = {"--size", 1, false, "Data size in MB for throughput tests (default: 64)"};
  map[ARG_ITERS] = {"--iters", 1, false, "Iterations for latency tests (default: 1000)"};
  map[ARG_BUDGET] = {"--budget", 1, false, "Time budget per test in seconds (default: 30)"};
  map[ARG_DIRECT] = {"--direct", 0, false, "Use O_DIRECT to bypass page cache"};
  map[ARG_QUICK] = {"--quick", 0, false, "Quick mode: small data, few iterations"};
  return map;
}

/* ----------------------------- Output Helpers ----------------------------- */

const char* statusStr(bool success) { return success ? "PASS" : "FAIL"; }

const char* statusColor(bool success) { return success ? "\033[32m" : "\033[31m"; }

constexpr const char* RESET = "\033[0m";

std::string formatThroughput(double bytesPerSec) {
  if (bytesPerSec < 1000.0) {
    return fmt::format("{:.0f} B/s", bytesPerSec);
  }
  if (bytesPerSec < 1000000.0) {
    return fmt::format("{:.1f} KB/s", bytesPerSec / 1000.0);
  }
  if (bytesPerSec < 1000000000.0) {
    return fmt::format("{:.1f} MB/s", bytesPerSec / 1000000.0);
  }
  return fmt::format("{:.2f} GB/s", bytesPerSec / 1000000000.0);
}

/* ----------------------------- Human Output ----------------------------- */

void printResult(const char* name, const storage::BenchResult& result) {
  fmt::print("[{}{}{}] {}: ", statusColor(result.success), statusStr(result.success), RESET, name);

  if (!result.success) {
    fmt::print("FAILED\n");
    return;
  }

  if (result.throughputBytesPerSec > 0 && result.avgLatencyUs == 0) {
    // Throughput test
    fmt::print("{} ({:.1f} MB in {:.2f}s)\n", formatThroughput(result.throughputBytesPerSec),
               static_cast<double>(result.bytesTransferred) / 1000000.0, result.elapsedSec);
  } else if (result.avgLatencyUs > 0) {
    // Latency test
    fmt::print("avg={:.2f}us min={:.2f}us max={:.2f}us p99={:.2f}us ({} ops)\n",
               result.avgLatencyUs, result.minLatencyUs, result.maxLatencyUs, result.p99LatencyUs,
               result.operations);
  } else {
    fmt::print("{} ops in {:.2f}s\n", result.operations, result.elapsedSec);
  }
}

void printHuman(const storage::BenchSuite& suite, const storage::BenchConfig& config) {
  fmt::print("=== Storage Benchmark ===\n");
  fmt::print("Directory: {}\n", config.directory.data());
  fmt::print("Data size: {:.1f} MB\n", static_cast<double>(config.dataSize) / 1000000.0);
  fmt::print("Iterations: {}\n", config.iterations);
  fmt::print("Direct I/O: {}\n\n", config.useDirectIo ? "yes" : "no");

  fmt::print("--- Throughput Tests ---\n");
  printResult("Sequential Write", suite.seqWrite);
  printResult("Sequential Read ", suite.seqRead);

  fmt::print("\n--- Latency Tests ---\n");
  printResult("fsync Latency   ", suite.fsyncLatency);
  printResult("Random Read 4K  ", suite.randRead);
  printResult("Random Write 4K ", suite.randWrite);

  // Summary
  fmt::print("\n=== Summary ===\n");
  if (suite.seqWrite.success && suite.seqRead.success) {
    fmt::print("  Sequential:  Write {}, Read {}\n",
               formatThroughput(suite.seqWrite.throughputBytesPerSec),
               formatThroughput(suite.seqRead.throughputBytesPerSec));
  }
  if (suite.fsyncLatency.success) {
    fmt::print("  fsync p99:   {:.2f} us\n", suite.fsyncLatency.p99LatencyUs);
  }
  if (suite.randRead.success && suite.randWrite.success) {
    fmt::print("  Random 4K:   Read {:.0f} IOPS, Write {:.0f} IOPS\n",
               static_cast<double>(suite.randRead.operations) / suite.randRead.elapsedSec,
               static_cast<double>(suite.randWrite.operations) / suite.randWrite.elapsedSec);
  }

  const int PASSED = (suite.seqWrite.success ? 1 : 0) + (suite.seqRead.success ? 1 : 0) +
                     (suite.fsyncLatency.success ? 1 : 0) + (suite.randRead.success ? 1 : 0) +
                     (suite.randWrite.success ? 1 : 0);
  fmt::print("\nTests passed: {}/5\n", PASSED);
}

/* ----------------------------- JSON Output ----------------------------- */

void printJsonResult(const char* name, const storage::BenchResult& result, bool last) {
  fmt::print("    \"{}\": {{\n", name);
  fmt::print("      \"success\": {},\n", result.success);
  fmt::print("      \"elapsedSec\": {:.3f},\n", result.elapsedSec);
  fmt::print("      \"operations\": {},\n", result.operations);
  fmt::print("      \"bytesTransferred\": {},\n", result.bytesTransferred);
  fmt::print("      \"throughputBytesPerSec\": {:.0f},\n", result.throughputBytesPerSec);
  fmt::print("      \"avgLatencyUs\": {:.3f},\n", result.avgLatencyUs);
  fmt::print("      \"minLatencyUs\": {:.3f},\n", result.minLatencyUs);
  fmt::print("      \"maxLatencyUs\": {:.3f},\n", result.maxLatencyUs);
  fmt::print("      \"p99LatencyUs\": {:.3f}\n", result.p99LatencyUs);
  fmt::print("    }}{}\n", last ? "" : ",");
}

void printJson(const storage::BenchSuite& suite, const storage::BenchConfig& config) {
  fmt::print("{{\n");
  fmt::print("  \"config\": {{\n");
  fmt::print("    \"directory\": \"{}\",\n", config.directory.data());
  fmt::print("    \"ioSize\": {},\n", config.ioSize);
  fmt::print("    \"dataSize\": {},\n", config.dataSize);
  fmt::print("    \"iterations\": {},\n", config.iterations);
  fmt::print("    \"timeBudgetSec\": {:.1f},\n", config.timeBudgetSec);
  fmt::print("    \"useDirectIo\": {},\n", config.useDirectIo);
  fmt::print("    \"useFsync\": {}\n", config.useFsync);
  fmt::print("  }},\n");

  fmt::print("  \"results\": {{\n");
  printJsonResult("seqWrite", suite.seqWrite, false);
  printJsonResult("seqRead", suite.seqRead, false);
  printJsonResult("fsyncLatency", suite.fsyncLatency, false);
  printJsonResult("randRead", suite.randRead, false);
  printJsonResult("randWrite", suite.randWrite, true);
  fmt::print("  }}\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;

  // Default config
  storage::BenchConfig config{};
  config.setDirectory("/tmp");
  config.ioSize = storage::DEFAULT_IO_SIZE;
  config.dataSize = storage::DEFAULT_DATA_SIZE;
  config.iterations = storage::DEFAULT_ITERATIONS;
  config.timeBudgetSec = storage::MAX_BENCH_TIME_SEC;
  config.useDirectIo = false;
  config.useFsync = true;

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

    if (pargs.count(ARG_DIR) != 0) {
      config.setDirectory(std::string(pargs[ARG_DIR][0]).c_str());
    }

    if (pargs.count(ARG_SIZE) != 0) {
      const int MB = std::stoi(std::string(pargs[ARG_SIZE][0]));
      config.dataSize = static_cast<std::size_t>(MB) * 1000000ULL;
    }

    if (pargs.count(ARG_ITERS) != 0) {
      config.iterations = static_cast<std::size_t>(std::stoi(std::string(pargs[ARG_ITERS][0])));
    }

    if (pargs.count(ARG_BUDGET) != 0) {
      config.timeBudgetSec = std::stod(std::string(pargs[ARG_BUDGET][0]));
    }

    if (pargs.count(ARG_DIRECT) != 0) {
      config.useDirectIo = true;
    }

    if (pargs.count(ARG_QUICK) != 0) {
      // Quick mode: small data, few iterations
      config.dataSize = 8 * 1000000ULL; // 8 MB
      config.iterations = 100;
      config.timeBudgetSec = 10.0;
    }
  }

  // Validate config
  if (!config.isValid()) {
    fmt::print(stderr, "Error: Invalid configuration\n");
    fmt::print(stderr, "  Directory '{}' may not exist or be writable\n", config.directory.data());
    return 1;
  }

  if (!jsonOutput) {
    fmt::print("Running benchmarks (this may take a while)...\n\n");
  }

  const storage::BenchSuite SUITE = storage::runBenchSuite(config);

  if (jsonOutput) {
    printJson(SUITE, config);
  } else {
    printHuman(SUITE, config);
  }

  return SUITE.allSuccess() ? 0 : 1;
}