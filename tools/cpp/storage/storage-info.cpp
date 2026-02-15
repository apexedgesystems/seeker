/**
 * @file storage-info.cpp
 * @brief One-shot storage system identification and configuration dump.
 *
 * Displays block devices, mount table, and I/O scheduler configuration.
 * Designed for quick storage assessment.
 */

#include "src/storage/inc/BlockDeviceInfo.hpp"
#include "src/storage/inc/IoScheduler.hpp"
#include "src/storage/inc/MountInfo.hpp"
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
  ARG_DEVICE = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display block devices, mounts, and I/O scheduler configuration.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_DEVICE] = {"--device", 1, false, "Show details for specific device only"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printDevices(const storage::BlockDeviceList& devices) {
  fmt::print("=== Block Devices ({}) ===\n", devices.count);

  for (std::size_t i = 0; i < devices.count; ++i) {
    const storage::BlockDevice& DEV = devices.devices[i];
    fmt::print("  {}: {} {} [{}]\n", DEV.name.data(), DEV.vendor.data(), DEV.model.data(),
               DEV.deviceType());
    fmt::print("      Size: {}  Sectors: {}/{} (log/phys)\n",
               storage::formatCapacity(DEV.sizeBytes), DEV.logicalBlockSize, DEV.physicalBlockSize);
    fmt::print("      TRIM: {}  Removable: {}\n", DEV.hasTrim ? "yes" : "no",
               DEV.removable ? "yes" : "no");
  }
}

void printSchedulers(const storage::BlockDeviceList& devices) {
  fmt::print("\n=== I/O Schedulers ===\n");

  for (std::size_t i = 0; i < devices.count; ++i) {
    const storage::IoSchedulerConfig CFG =
        storage::getIoSchedulerConfig(devices.devices[i].name.data());

    if (CFG.current[0] == '\0') {
      continue;
    }

    fmt::print("  {}: scheduler={}", CFG.device.data(), CFG.current.data());
    fmt::print(" nr_req={} read_ahead={}KB", CFG.nrRequests, CFG.readAheadKb);
    fmt::print(" [RT score: {}]\n", CFG.rtScore());
  }
}

void printMounts(const storage::MountTable& mounts) {
  fmt::print("\n=== Block Device Mounts ({}) ===\n", mounts.countBlockDevices());

  for (std::size_t i = 0; i < mounts.count; ++i) {
    const storage::MountEntry& M = mounts.mounts[i];
    if (!M.isBlockDevice()) {
      continue;
    }

    fmt::print("  {} on {} ({})\n", M.device.data(), M.mountPoint.data(), M.fsType.data());

    std::string flags;
    if (M.isReadOnly())
      flags += "ro ";
    if (M.hasNoAtime())
      flags += "noatime ";
    if (M.hasNoDirAtime())
      flags += "nodiratime ";
    if (M.hasRelAtime())
      flags += "relatime ";
    if (M.hasNoBarrier())
      flags += "nobarrier ";
    if (M.isSync())
      flags += "sync ";

    if (!flags.empty()) {
      fmt::print("      Flags: {}\n", flags);
    }

    if (std::strcmp(M.fsType.data(), "ext4") == 0) {
      const char* dataMode = M.ext4DataMode();
      if (dataMode[0] != '\0') {
        fmt::print("      ext4 data mode: {}\n", dataMode);
      }
    }
  }
}

void printSingleDevice(const char* deviceName) {
  const storage::BlockDevice DEV = storage::getBlockDevice(deviceName);
  if (DEV.sizeBytes == 0) {
    fmt::print(stderr, "Error: Device '{}' not found\n", deviceName);
    return;
  }

  fmt::print("=== Device: {} ===\n", DEV.name.data());
  fmt::print("  Type:     {}\n", DEV.deviceType());
  fmt::print("  Vendor:   {}\n", DEV.vendor.data());
  fmt::print("  Model:    {}\n", DEV.model.data());
  fmt::print("  Size:     {}\n", storage::formatCapacity(DEV.sizeBytes));
  fmt::print("  Logical:  {} bytes\n", DEV.logicalBlockSize);
  fmt::print("  Physical: {} bytes\n", DEV.physicalBlockSize);
  fmt::print("  TRIM:     {}\n", DEV.hasTrim ? "supported" : "not supported");
  fmt::print("  Advanced Format: {}\n", DEV.isAdvancedFormat() ? "yes (4K)" : "no (512)");

  const storage::IoSchedulerConfig CFG = storage::getIoSchedulerConfig(deviceName);
  if (CFG.current[0] != '\0') {
    fmt::print("\n=== Scheduler ===\n");
    fmt::print("  Current:     {}\n", CFG.current.data());
    fmt::print("  Available:   ");
    for (std::size_t i = 0; i < CFG.availableCount; ++i) {
      if (i > 0)
        fmt::print(", ");
      fmt::print("{}", CFG.available[i].data());
    }
    fmt::print("\n");
    fmt::print("  Queue depth: {}\n", CFG.nrRequests);
    fmt::print("  Read-ahead:  {} KB\n", CFG.readAheadKb);
    fmt::print("  RT-friendly: {}\n", CFG.isRtFriendly() ? "yes" : "no");
    fmt::print("  RT Score:    {}/100\n", CFG.rtScore());
  }
}

void printHuman(const storage::BlockDeviceList& devices, const storage::MountTable& mounts) {
  printDevices(devices);
  printSchedulers(devices);
  printMounts(mounts);
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const storage::BlockDeviceList& devices, const storage::MountTable& mounts) {
  fmt::print("{{\n");

  // Devices
  fmt::print("  \"devices\": [\n");
  for (std::size_t i = 0; i < devices.count; ++i) {
    const storage::BlockDevice& D = devices.devices[i];
    const storage::IoSchedulerConfig CFG = storage::getIoSchedulerConfig(D.name.data());

    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", D.name.data());
    fmt::print("      \"vendor\": \"{}\",\n", D.vendor.data());
    fmt::print("      \"model\": \"{}\",\n", D.model.data());
    fmt::print("      \"type\": \"{}\",\n", D.deviceType());
    fmt::print("      \"sizeBytes\": {},\n", D.sizeBytes);
    fmt::print("      \"logicalBlockSize\": {},\n", D.logicalBlockSize);
    fmt::print("      \"physicalBlockSize\": {},\n", D.physicalBlockSize);
    fmt::print("      \"rotational\": {},\n", D.rotational);
    fmt::print("      \"removable\": {},\n", D.removable);
    fmt::print("      \"trim\": {},\n", D.hasTrim);
    fmt::print("      \"scheduler\": \"{}\",\n", CFG.current.data());
    fmt::print("      \"nrRequests\": {},\n", CFG.nrRequests);
    fmt::print("      \"readAheadKb\": {},\n", CFG.readAheadKb);
    fmt::print("      \"rtScore\": {}\n", CFG.rtScore());
    fmt::print("    }}");
  }
  fmt::print("\n  ],\n");

  // Mounts
  fmt::print("  \"mounts\": [\n");
  bool first = true;
  for (std::size_t i = 0; i < mounts.count; ++i) {
    const storage::MountEntry& M = mounts.mounts[i];
    if (!M.isBlockDevice())
      continue;

    if (!first)
      fmt::print(",\n");
    first = false;

    fmt::print("    {{\n");
    fmt::print("      \"device\": \"{}\",\n", M.device.data());
    fmt::print("      \"mountPoint\": \"{}\",\n", M.mountPoint.data());
    fmt::print("      \"fsType\": \"{}\",\n", M.fsType.data());
    fmt::print("      \"readOnly\": {},\n", M.isReadOnly());
    fmt::print("      \"noatime\": {},\n", M.hasNoAtime());
    fmt::print("      \"nobarrier\": {}\n", M.hasNoBarrier());
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
    if (pargs.count(ARG_DEVICE) != 0) {
      deviceFilter = pargs[ARG_DEVICE][0].data();
    }
  }

  // Single device mode
  if (deviceFilter != nullptr) {
    printSingleDevice(deviceFilter);
    return 0;
  }

  // Full dump
  const storage::BlockDeviceList DEVICES = storage::getBlockDevices();
  const storage::MountTable MOUNTS = storage::getMountTable();

  if (jsonOutput) {
    printJson(DEVICES, MOUNTS);
  } else {
    printHuman(DEVICES, MOUNTS);
  }

  return 0;
}