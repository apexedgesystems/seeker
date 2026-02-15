/**
 * @file cpu-info.cpp
 * @brief One-shot CPU system identification and capability dump.
 *
 * Displays CPU topology, ISA features, frequency/governor state, and basic
 * system statistics. Designed for quick system assessment.
 */

#include "src/cpu/inc/CpuFeatures.hpp"
#include "src/cpu/inc/CpuFreq.hpp"
#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/cpu/inc/CpuStats.hpp"
#include "src/cpu/inc/CpuTopology.hpp"
#include "src/helpers/inc/Format.hpp"
#include "src/helpers/inc/Args.hpp"
#include "src/helpers/inc/Format.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace cpu = seeker::cpu;

using seeker::helpers::format::bytesBinary;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display CPU topology, features, frequency, and system information.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printTopology(const cpu::CpuTopology& topo) {
  fmt::print("=== Topology ===\n");
  fmt::print("  Packages:       {}\n", topo.packages);
  fmt::print("  Physical cores: {}\n", topo.physicalCores);
  fmt::print("  Logical CPUs:   {}\n", topo.logicalCpus);
  fmt::print("  Threads/core:   {}\n", topo.threadsPerCore());
  fmt::print("  NUMA nodes:     {}\n", topo.numaNodes);

  for (const auto& CACHE : topo.sharedCaches) {
    fmt::print("  L{} cache:       {} ({}-byte line, {}-way)\n", CACHE.level,
               bytesBinary(CACHE.sizeBytes), CACHE.lineBytes, CACHE.associativity);
  }
}

void printFeatures(const cpu::CpuFeatures& feat) {
  fmt::print("\n=== CPU Features ===\n");
  fmt::print("  Vendor: {}\n", feat.vendor.data());
  fmt::print("  Brand:  {}\n", feat.brand.data());

  // SIMD features
  std::string simd;
  if (feat.sse)
    simd += "SSE ";
  if (feat.sse2)
    simd += "SSE2 ";
  if (feat.sse3)
    simd += "SSE3 ";
  if (feat.ssse3)
    simd += "SSSE3 ";
  if (feat.sse41)
    simd += "SSE4.1 ";
  if (feat.sse42)
    simd += "SSE4.2 ";
  if (feat.avx)
    simd += "AVX ";
  if (feat.avx2)
    simd += "AVX2 ";
  if (feat.fma)
    simd += "FMA ";
  fmt::print("  SIMD:   {}\n", simd.empty() ? "(none)" : simd);

  if (feat.avx512f) {
    std::string avx512 = "F ";
    if (feat.avx512dq)
      avx512 += "DQ ";
    if (feat.avx512cd)
      avx512 += "CD ";
    if (feat.avx512bw)
      avx512 += "BW ";
    if (feat.avx512vl)
      avx512 += "VL ";
    fmt::print("  AVX512: {}\n", avx512);
  }

  std::string crypto;
  if (feat.aes)
    crypto += "AES ";
  if (feat.sha)
    crypto += "SHA ";
  fmt::print("  Crypto: {}\n", crypto.empty() ? "(none)" : crypto);

  std::string other;
  if (feat.popcnt)
    other += "POPCNT ";
  if (feat.bmi1)
    other += "BMI1 ";
  if (feat.bmi2)
    other += "BMI2 ";
  if (feat.rdrand)
    other += "RDRAND ";
  if (feat.rdseed)
    other += "RDSEED ";
  fmt::print("  Other:  {}\n", other.empty() ? "(none)" : other);

  fmt::print("  Invariant TSC: {}\n", feat.invariantTsc ? "yes" : "no");
}

void printFrequency(const cpu::CpuFrequencySummary& freq) {
  fmt::print("\n=== Frequency ===\n");

  if (freq.cores.empty()) {
    fmt::print("  (cpufreq data unavailable)\n");
    return;
  }

  // Governor consistency check
  const char* firstGov = freq.cores[0].governor.data();
  bool uniformGov = true;
  std::int64_t minCur = freq.cores[0].curKHz;
  std::int64_t maxCur = freq.cores[0].curKHz;

  for (const auto& CORE : freq.cores) {
    if (std::strcmp(CORE.governor.data(), firstGov) != 0) {
      uniformGov = false;
    }
    if (CORE.curKHz < minCur)
      minCur = CORE.curKHz;
    if (CORE.curKHz > maxCur)
      maxCur = CORE.curKHz;
  }

  if (uniformGov) {
    fmt::print("  Governor: {} (all cores)\n", firstGov);
  } else {
    fmt::print("  Governor: (mixed)\n");
  }

  fmt::print("  Current:  {} - {} MHz\n", minCur / 1000, maxCur / 1000);

  if (freq.cores[0].maxKHz > 0) {
    fmt::print("  Range:    {} - {} MHz\n", freq.cores[0].minKHz / 1000,
               freq.cores[0].maxKHz / 1000);
  }

  if (freq.cores[0].turboAvailable) {
    fmt::print("  Turbo:    available\n");
  }
}

void printStats(const cpu::CpuStats& stats) {
  fmt::print("\n=== System ===\n");
  fmt::print("  Kernel:    {}\n", stats.kernel.version.data());
  fmt::print("  CPUs:      {}\n", stats.cpuCount.count);
  fmt::print("  RAM:       {} total, {} available\n", bytesBinary(stats.sysinfo.totalRamBytes),
             bytesBinary(stats.meminfo.availableBytes));
  fmt::print("  Load avg:  {:.2f} {:.2f} {:.2f}\n", stats.sysinfo.load1, stats.sysinfo.load5,
             stats.sysinfo.load15);

  // Format uptime
  const std::uint64_t UP = stats.sysinfo.uptimeSeconds;
  const std::uint64_t DAYS = UP / 86400;
  const std::uint64_t HOURS = (UP % 86400) / 3600;
  const std::uint64_t MINS = (UP % 3600) / 60;

  if (DAYS > 0) {
    fmt::print("  Uptime:    {}d {}h {}m\n", DAYS, HOURS, MINS);
  } else if (HOURS > 0) {
    fmt::print("  Uptime:    {}h {}m\n", HOURS, MINS);
  } else {
    fmt::print("  Uptime:    {}m\n", MINS);
  }
}

void printIsolation(const cpu::CpuIsolationConfig& isolation) {
  fmt::print("\n=== CPU Isolation ===\n");

  if (!isolation.hasAnyIsolation()) {
    fmt::print("  (no isolation configured)\n");
    return;
  }

  if (!isolation.isolcpus.empty()) {
    fmt::print("  isolcpus:  {}{}\n", isolation.isolcpus.toString(),
               isolation.isolcpusManaged ? " (managed_irq)" : "");
  }
  if (!isolation.nohzFull.empty()) {
    fmt::print("  nohz_full: {}\n", isolation.nohzFull.toString());
  }
  if (!isolation.rcuNocbs.empty()) {
    fmt::print("  rcu_nocbs: {}\n", isolation.rcuNocbs.toString());
  }

  const cpu::CpuSet FULLY_ISOLATED = isolation.getFullyIsolatedCpus();
  if (!FULLY_ISOLATED.empty()) {
    fmt::print("  Fully isolated: {}\n", FULLY_ISOLATED.toString());
  }
}

void printHuman(const cpu::CpuTopology& topo, const cpu::CpuFeatures& feat,
                const cpu::CpuFrequencySummary& freq, const cpu::CpuStats& stats,
                const cpu::CpuIsolationConfig& isolation) {
  printTopology(topo);
  printFeatures(feat);
  printFrequency(freq);
  printStats(stats);
  printIsolation(isolation);
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const cpu::CpuTopology& topo, const cpu::CpuFeatures& feat,
               const cpu::CpuFrequencySummary& freq, const cpu::CpuStats& stats,
               const cpu::CpuIsolationConfig& isolation) {
  fmt::print("{{\n");

  // Topology
  fmt::print("  \"topology\": {{\n");
  fmt::print("    \"packages\": {},\n", topo.packages);
  fmt::print("    \"physicalCores\": {},\n", topo.physicalCores);
  fmt::print("    \"logicalCpus\": {},\n", topo.logicalCpus);
  fmt::print("    \"threadsPerCore\": {},\n", topo.threadsPerCore());
  fmt::print("    \"numaNodes\": {},\n", topo.numaNodes);
  fmt::print("    \"sharedCaches\": [");
  for (std::size_t i = 0; i < topo.sharedCaches.size(); ++i) {
    if (i > 0)
      fmt::print(", ");
    const auto& C = topo.sharedCaches[i];
    fmt::print("{{\"level\": {}, \"sizeBytes\": {}, \"lineBytes\": {}, \"associativity\": {}}}",
               C.level, C.sizeBytes, C.lineBytes, C.associativity);
  }
  fmt::print("]\n  }},\n");

  // Features
  fmt::print("  \"features\": {{\n");
  fmt::print("    \"vendor\": \"{}\",\n", feat.vendor.data());
  fmt::print("    \"brand\": \"{}\",\n", feat.brand.data());
  fmt::print("    \"sse\": {}, \"sse2\": {}, \"sse3\": {}, \"ssse3\": {},\n", feat.sse, feat.sse2,
             feat.sse3, feat.ssse3);
  fmt::print("    \"sse41\": {}, \"sse42\": {}, \"avx\": {}, \"avx2\": {},\n", feat.sse41,
             feat.sse42, feat.avx, feat.avx2);
  fmt::print("    \"avx512f\": {}, \"avx512dq\": {}, \"avx512cd\": {},\n", feat.avx512f,
             feat.avx512dq, feat.avx512cd);
  fmt::print("    \"avx512bw\": {}, \"avx512vl\": {},\n", feat.avx512bw, feat.avx512vl);
  fmt::print("    \"fma\": {}, \"aes\": {}, \"sha\": {},\n", feat.fma, feat.aes, feat.sha);
  fmt::print("    \"popcnt\": {}, \"bmi1\": {}, \"bmi2\": {},\n", feat.popcnt, feat.bmi1,
             feat.bmi2);
  fmt::print("    \"rdrand\": {}, \"rdseed\": {},\n", feat.rdrand, feat.rdseed);
  fmt::print("    \"invariantTsc\": {}\n", feat.invariantTsc);
  fmt::print("  }},\n");

  // Frequency
  fmt::print("  \"frequency\": {{\n");
  fmt::print("    \"cores\": [");
  for (std::size_t i = 0; i < freq.cores.size(); ++i) {
    if (i > 0)
      fmt::print(", ");
    const auto& C = freq.cores[i];
    fmt::print(
        "{{\"cpuId\": {}, \"governor\": \"{}\", \"minKHz\": {}, \"maxKHz\": {}, \"curKHz\": {}}}",
        C.cpuId, C.governor.data(), C.minKHz, C.maxKHz, C.curKHz);
  }
  fmt::print("]\n  }},\n");

  // Isolation
  fmt::print("  \"isolation\": {{\n");
  fmt::print("    \"isolcpus\": \"{}\",\n", isolation.isolcpus.toString());
  fmt::print("    \"nohzFull\": \"{}\",\n", isolation.nohzFull.toString());
  fmt::print("    \"rcuNocbs\": \"{}\",\n", isolation.rcuNocbs.toString());
  fmt::print("    \"isolcpusManaged\": {},\n", isolation.isolcpusManaged);
  fmt::print("    \"fullyIsolated\": \"{}\"\n", isolation.getFullyIsolatedCpus().toString());
  fmt::print("  }},\n");

  // System stats
  fmt::print("  \"system\": {{\n");
  fmt::print("    \"cpuCount\": {},\n", stats.cpuCount.count);
  fmt::print("    \"kernel\": \"{}\",\n", stats.kernel.version.data());
  fmt::print("    \"totalRamBytes\": {},\n", stats.sysinfo.totalRamBytes);
  fmt::print("    \"availableRamBytes\": {},\n", stats.meminfo.availableBytes);
  fmt::print("    \"uptimeSeconds\": {},\n", stats.sysinfo.uptimeSeconds);
  fmt::print("    \"load1\": {:.2f}, \"load5\": {:.2f}, \"load15\": {:.2f}\n", stats.sysinfo.load1,
             stats.sysinfo.load5, stats.sysinfo.load15);
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;

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
  }

  // Gather data
  const cpu::CpuTopology TOPO = cpu::getCpuTopology();
  const cpu::CpuFeatures FEAT = cpu::getCpuFeatures();
  const cpu::CpuFrequencySummary FREQ = cpu::getCpuFrequencySummary();
  const cpu::CpuStats STATS = cpu::getCpuStats();
  const cpu::CpuIsolationConfig ISOLATION = cpu::getCpuIsolationConfig();

  if (jsonOutput) {
    printJson(TOPO, FEAT, FREQ, STATS, ISOLATION);
  } else {
    printHuman(TOPO, FEAT, FREQ, STATS, ISOLATION);
  }

  return 0;
}