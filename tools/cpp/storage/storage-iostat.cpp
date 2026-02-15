/**
 * @file storage-iostat.cpp
 * @brief Real-time I/O statistics monitor using snapshot+delta pattern.
 *
 * Displays per-device IOPS, throughput, latency, and utilization.
 * Similar to iostat but focused on RT-relevant metrics.
 */

#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/storage/inc/IoStats.hpp"
#include "src/helpers/inc/Args.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/core.h>

namespace storage = seeker::storage;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_INTERVAL = 2,
  ARG_COUNT = 3,
  ARG_DEVICE = 4,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Monitor per-device I/O statistics (IOPS, throughput, latency).";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_INTERVAL] = {"--interval", 1, false, "Sample interval in seconds (default: 1)"};
  map[ARG_COUNT] = {"--count", 1, false, "Number of samples (default: infinite)"};
  map[ARG_DEVICE] = {"--device", 1, false, "Monitor specific device only"};
  return map;
}

/// Format throughput in human-readable form.
std::string formatThroughput(double bytesPerSec) {
  if (bytesPerSec < 1000.0) {
    return fmt::format("{:6.0f} B/s", bytesPerSec);
  }
  if (bytesPerSec < 1000000.0) {
    return fmt::format("{:6.1f} KB/s", bytesPerSec / 1000.0);
  }
  if (bytesPerSec < 1000000000.0) {
    return fmt::format("{:6.1f} MB/s", bytesPerSec / 1000000.0);
  }
  return fmt::format("{:6.2f} GB/s", bytesPerSec / 1000000000.0);
}

/* ----------------------------- Monitoring ----------------------------- */

struct DeviceState {
  std::array<char, storage::IOSTAT_DEVICE_NAME_SIZE> name{};
  storage::IoStatsSnapshot lastSnap{};
};

void printHeader() {
  fmt::print("{:<12} {:>8} {:>8} {:>12} {:>12} {:>8} {:>8} {:>6} {:>5}\n", "Device", "r/s", "w/s",
             "rKB/s", "wKB/s", "r_lat", "w_lat", "util%", "qd");
  fmt::print("{:-<12} {:->8} {:->8} {:->12} {:->12} {:->8} {:->8} {:->6} {:->5}\n", "", "", "", "",
             "", "", "", "", "");
}

void printDelta(const storage::IoStatsDelta& delta) {
  fmt::print("{:<12} {:>8.1f} {:>8.1f} {:>12} {:>12} {:>7.2f}ms {:>7.2f}ms {:>5.1f}% {:>5.1f}\n",
             delta.device.data(), delta.readIops, delta.writeIops,
             formatThroughput(delta.readBytesPerSec), formatThroughput(delta.writeBytesPerSec),
             delta.avgReadLatencyMs, delta.avgWriteLatencyMs, delta.utilizationPct,
             delta.avgQueueDepth);
}

void printJsonDelta(const storage::IoStatsDelta& delta, bool first) {
  if (!first)
    fmt::print(",\n");

  fmt::print("    {{\n");
  fmt::print("      \"device\": \"{}\",\n", delta.device.data());
  fmt::print("      \"readIops\": {:.2f},\n", delta.readIops);
  fmt::print("      \"writeIops\": {:.2f},\n", delta.writeIops);
  fmt::print("      \"readBytesPerSec\": {:.0f},\n", delta.readBytesPerSec);
  fmt::print("      \"writeBytesPerSec\": {:.0f},\n", delta.writeBytesPerSec);
  fmt::print("      \"avgReadLatencyMs\": {:.3f},\n", delta.avgReadLatencyMs);
  fmt::print("      \"avgWriteLatencyMs\": {:.3f},\n", delta.avgWriteLatencyMs);
  fmt::print("      \"utilizationPct\": {:.2f},\n", delta.utilizationPct);
  fmt::print("      \"avgQueueDepth\": {:.2f}\n", delta.avgQueueDepth);
  fmt::print("    }}");
}

void runMonitor(const std::vector<std::string>& deviceNames, double intervalSec, int count,
                bool jsonOutput) {
  // Initialize state for each device
  std::vector<DeviceState> states;
  states.reserve(deviceNames.size());

  for (const auto& NAME : deviceNames) {
    DeviceState state;
    std::strncpy(state.name.data(), NAME.c_str(), storage::IOSTAT_DEVICE_NAME_SIZE - 1);
    state.lastSnap = storage::getIoStatsSnapshot(NAME.c_str());
    states.push_back(state);
  }

  const auto INTERVAL = std::chrono::milliseconds(static_cast<int>(intervalSec * 1000.0));

  if (jsonOutput) {
    fmt::print("{{\n  \"samples\": [\n");
  } else {
    printHeader();
  }

  int sampleNum = 0;
  bool firstSample = true;

  while (count == 0 || sampleNum < count) {
    std::this_thread::sleep_for(INTERVAL);

    if (jsonOutput) {
      if (!firstSample)
        fmt::print(",\n");
      fmt::print("  {{\n    \"sample\": {},\n    \"devices\": [\n", sampleNum);
    }

    bool firstDevice = true;
    for (auto& state : states) {
      const storage::IoStatsSnapshot SNAP = storage::getIoStatsSnapshot(state.name.data());
      const storage::IoStatsDelta DELTA = storage::computeIoStatsDelta(state.lastSnap, SNAP);

      if (jsonOutput) {
        printJsonDelta(DELTA, firstDevice);
        firstDevice = false;
      } else {
        printDelta(DELTA);
      }

      state.lastSnap = SNAP;
    }

    if (jsonOutput) {
      fmt::print("\n    ]\n  }}");
    } else if (states.size() > 1) {
      fmt::print("\n"); // Blank line between intervals for multi-device
    }

    firstSample = false;
    ++sampleNum;
  }

  if (jsonOutput) {
    fmt::print("\n  ]\n}}\n");
  }
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  double intervalSec = 1.0;
  int count = 0; // 0 = infinite
  const char* deviceFilter = nullptr;

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

    if (pargs.count(ARG_INTERVAL) != 0) {
      intervalSec = std::stod(std::string(pargs[ARG_INTERVAL][0]));
      if (intervalSec < 0.1)
        intervalSec = 0.1;
    }

    if (pargs.count(ARG_COUNT) != 0) {
      count = std::stoi(std::string(pargs[ARG_COUNT][0]));
    }

    if (pargs.count(ARG_DEVICE) != 0) {
      deviceFilter = pargs[ARG_DEVICE][0].data();
    }
  }

  // Build device list
  std::vector<std::string> deviceNames;

  if (deviceFilter != nullptr) {
    // Single device mode
    const storage::IoStatsSnapshot TEST = storage::getIoStatsSnapshot(deviceFilter);
    if (TEST.timestampNs == 0) {
      fmt::print(stderr, "Error: Device '{}' not found or no stats available\n", deviceFilter);
      return 1;
    }
    deviceNames.emplace_back(deviceFilter);
  } else {
    // All devices
    const storage::BlockDeviceList DEVICES = storage::getBlockDevices();
    for (std::size_t i = 0; i < DEVICES.count; ++i) {
      deviceNames.emplace_back(DEVICES.devices[i].name.data());
    }

    if (deviceNames.empty()) {
      fmt::print(stderr, "Error: No block devices found\n");
      return 1;
    }
  }

  runMonitor(deviceNames, intervalSec, count, jsonOutput);
  return 0;
}