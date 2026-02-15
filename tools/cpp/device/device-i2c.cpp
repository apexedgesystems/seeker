/**
 * @file device-i2c.cpp
 * @brief I2C bus enumeration and device scanning.
 *
 * Shows I2C buses, functionality flags, and optionally scans for devices.
 * Device scanning requires appropriate permissions.
 */

#include "src/device/inc/I2cBusInfo.hpp"
#include "src/helpers/inc/Args.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

namespace device = seeker::device;

namespace {

/// Argument keys.
enum ArgKey : std::uint8_t {
  ARG_HELP = 0,
  ARG_JSON = 1,
  ARG_BUS = 2,
  ARG_SCAN = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display I2C bus information and optionally scan for devices.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_BUS] = {"--bus", 1, false, "Show details for specific bus (e.g., 0 or i2c-0)"};
  map[ARG_SCAN] = {"--scan", 0, false, "Scan buses for connected devices (requires access)"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printFunctionality(const device::I2cFunctionality& func) {
  fmt::print("  Functionality:\n");

  std::string features;
  if (func.i2c)
    features += "I2C ";
  if (func.tenBitAddr)
    features += "10-bit ";
  if (func.protocolMangling)
    features += "MANGLE ";
  if (func.smbusQuick)
    features += "QUICK ";
  if (func.smbusByte)
    features += "BYTE ";
  if (func.smbusWord)
    features += "WORD ";
  if (func.smbusBlock)
    features += "BLOCK ";
  if (func.smbusI2cBlock)
    features += "I2C-BLOCK ";
  if (func.smbusPec)
    features += "PEC ";

  fmt::print("    {}\n", features.empty() ? "(none)" : features);
}

void printDeviceList(const device::I2cDeviceList& devices) {
  if (devices.count == 0) {
    fmt::print("  Devices: (none found)\n");
    return;
  }

  fmt::print("  Devices ({} found):\n    ", devices.count);

  // Print addresses in hex grid format (like i2cdetect)
  for (std::size_t i = 0; i < devices.count; ++i) {
    if (i > 0 && i % 16 == 0)
      fmt::print("\n    ");
    fmt::print("0x{:02x} ", devices.devices[i].address);
  }
  fmt::print("\n");
}

void printBusDetails(const device::I2cBusInfo& bus, bool doScan) {
  fmt::print("=== {} ===\n", bus.name.data());

  if (!bus.exists) {
    fmt::print("  Status: not found\n");
    return;
  }

  fmt::print("  Device:   {}\n", bus.devicePath.data());
  fmt::print("  Access:   {}\n", bus.accessible ? "yes" : "no");

  if (bus.adapterName[0] != '\0') {
    fmt::print("  Adapter:  {}\n", bus.adapterName.data());
  }

  printFunctionality(bus.functionality);

  if (doScan && bus.accessible) {
    fmt::print("\n  Scanning for devices...\n");
    const device::I2cDeviceList DEVICES = device::scanI2cBus(bus.busNumber);
    printDeviceList(DEVICES);
  }
}

void printAllBuses(const device::I2cBusList& buses, bool doScan) {
  fmt::print("=== I2C Buses ({} found) ===\n\n", buses.count);

  if (buses.count == 0) {
    fmt::print("No I2C buses found.\n");
    return;
  }

  // Summary table
  fmt::print("{:<10} {:<8} {:<30}\n", "BUS", "ACCESS", "ADAPTER");
  fmt::print("{:-<10} {:-<8} {:-<30}\n", "", "", "");

  for (std::size_t i = 0; i < buses.count; ++i) {
    const device::I2cBusInfo& BUS = buses.buses[i];
    fmt::print("{:<10} {:<8} {:<30}\n", BUS.name.data(), BUS.accessible ? "yes" : "no",
               BUS.adapterName.data());
  }

  if (doScan) {
    fmt::print("\n");
    for (std::size_t i = 0; i < buses.count; ++i) {
      if (i > 0)
        fmt::print("\n");
      printBusDetails(buses.buses[i], true);
    }
  }
}

void printHuman(const device::I2cBusList& buses, const char* busFilter, bool doScan) {
  if (busFilter != nullptr) {
    // Parse bus number
    std::uint32_t busNum = 0;
    if (!device::parseI2cBusNumber(busFilter, busNum)) {
      fmt::print(stderr, "Error: Invalid bus '{}'\n", busFilter);
      return;
    }

    const device::I2cBusInfo BUS = device::getI2cBusInfo(busNum);
    if (!BUS.exists) {
      fmt::print(stderr, "Error: Bus '{}' not found\n", busFilter);
      return;
    }
    printBusDetails(BUS, doScan);
  } else {
    printAllBuses(buses, doScan);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printBusJson(const device::I2cBusInfo& bus, bool doScan) {
  fmt::print("  {{\n");
  fmt::print("    \"name\": \"{}\",\n", bus.name.data());
  fmt::print("    \"busNumber\": {},\n", bus.busNumber);
  fmt::print("    \"devicePath\": \"{}\",\n", bus.devicePath.data());
  fmt::print("    \"exists\": {},\n", bus.exists);
  fmt::print("    \"accessible\": {},\n", bus.accessible);
  fmt::print("    \"adapter\": \"{}\",\n", bus.adapterName.data());
  fmt::print("    \"functionality\": {{\n");
  fmt::print("      \"i2c\": {},\n", bus.functionality.i2c);
  fmt::print("      \"tenBitAddr\": {},\n", bus.functionality.tenBitAddr);
  fmt::print("      \"smbusQuick\": {},\n", bus.functionality.smbusQuick);
  fmt::print("      \"smbusByte\": {},\n", bus.functionality.smbusByte);
  fmt::print("      \"smbusWord\": {},\n", bus.functionality.smbusWord);
  fmt::print("      \"smbusBlock\": {},\n", bus.functionality.smbusBlock);
  fmt::print("      \"smbusI2cBlock\": {},\n", bus.functionality.smbusI2cBlock);
  fmt::print("      \"pec\": {}\n", bus.functionality.smbusPec);
  fmt::print("    }}");

  if (doScan && bus.accessible) {
    const device::I2cDeviceList DEVICES = device::scanI2cBus(bus.busNumber);
    fmt::print(",\n    \"devices\": [");
    for (std::size_t i = 0; i < DEVICES.count; ++i) {
      if (i > 0)
        fmt::print(", ");
      fmt::print("{}", DEVICES.devices[i].address);
    }
    fmt::print("]\n");
  } else {
    fmt::print("\n");
  }

  fmt::print("  }}");
}

void printJson(const device::I2cBusList& buses, const char* busFilter, bool doScan) {
  fmt::print("{{\n");
  fmt::print("\"i2cBuses\": [\n");

  if (busFilter != nullptr) {
    std::uint32_t busNum = 0;
    if (device::parseI2cBusNumber(busFilter, busNum)) {
      const device::I2cBusInfo BUS = device::getI2cBusInfo(busNum);
      printBusJson(BUS, doScan);
    }
  } else {
    for (std::size_t i = 0; i < buses.count; ++i) {
      if (i > 0)
        fmt::print(",\n");
      printBusJson(buses.buses[i], doScan);
    }
  }

  fmt::print("\n]\n");
  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool doScan = false;
  const char* busFilter = nullptr;

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
    doScan = (pargs.count(ARG_SCAN) != 0);

    if (pargs.count(ARG_BUS) != 0) {
      busFilter = pargs[ARG_BUS][0].data();
    }
  }

  // Gather data
  const device::I2cBusList BUSES = device::getAllI2cBuses();

  if (jsonOutput) {
    printJson(BUSES, busFilter, doScan);
  } else {
    printHuman(BUSES, busFilter, doScan);
  }

  return 0;
}