/**
 * @file device-info.cpp
 * @brief One-shot device bus enumeration and overview.
 *
 * Displays serial ports, I2C buses, SPI devices, CAN interfaces, and GPIO chips.
 * Designed for quick device assessment on embedded systems.
 */

#include "src/device/inc/CanBusInfo.hpp"
#include "src/device/inc/GpioInfo.hpp"
#include "src/device/inc/I2cBusInfo.hpp"
#include "src/device/inc/SerialPortInfo.hpp"
#include "src/device/inc/SpiBusInfo.hpp"
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
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display overview of serial ports, I2C buses, SPI devices, CAN interfaces, and GPIO chips.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printSerialPorts(const device::SerialPortList& ports) {
  fmt::print("=== Serial Ports ({}) ===\n", ports.count);

  if (ports.count == 0) {
    fmt::print("  (none found)\n");
    return;
  }

  for (std::size_t i = 0; i < ports.count; ++i) {
    const device::SerialPortInfo& PORT = ports.ports[i];
    fmt::print("  {}: {}", PORT.name.data(), device::toString(PORT.type));

    if (PORT.isAccessible()) {
      fmt::print(" [{}{}]", PORT.readable ? "r" : "", PORT.writable ? "w" : "");
    } else {
      fmt::print(" [no access]");
    }

    if (PORT.isUsb() && PORT.usbInfo.isAvailable()) {
      fmt::print(" USB {:04x}:{:04x}", PORT.usbInfo.vendorId, PORT.usbInfo.productId);
    }

    if (PORT.driver[0] != '\0') {
      fmt::print(" ({})", PORT.driver.data());
    }

    fmt::print("\n");
  }
}

void printI2cBuses(const device::I2cBusList& buses) {
  fmt::print("\n=== I2C Buses ({}) ===\n", buses.count);

  if (buses.count == 0) {
    fmt::print("  (none found)\n");
    return;
  }

  for (std::size_t i = 0; i < buses.count; ++i) {
    const device::I2cBusInfo& BUS = buses.buses[i];
    fmt::print("  {}: ", BUS.name.data());

    if (!BUS.exists) {
      fmt::print("not found\n");
      continue;
    }

    if (!BUS.accessible) {
      fmt::print("no access\n");
      continue;
    }

    // Show functionality summary
    std::string funcs;
    if (BUS.functionality.smbusByte)
      funcs += "SMBus ";
    if (BUS.functionality.tenBitAddr)
      funcs += "10-bit ";
    if (BUS.functionality.smbusPec)
      funcs += "PEC ";

    if (funcs.empty()) {
      fmt::print("basic I2C");
    } else {
      fmt::print("{}", funcs);
    }

    if (BUS.adapterName[0] != '\0') {
      fmt::print("({})", BUS.adapterName.data());
    }

    fmt::print("\n");
  }
}

void printSpiDevices(const device::SpiDeviceList& devices) {
  fmt::print("\n=== SPI Devices ({}) ===\n", devices.count);

  if (devices.count == 0) {
    fmt::print("  (none found)\n");
    return;
  }

  for (std::size_t i = 0; i < devices.count; ++i) {
    const device::SpiDeviceInfo& DEV = devices.devices[i];
    fmt::print("  {}: ", DEV.name.data());

    if (!DEV.exists) {
      fmt::print("not found\n");
      continue;
    }

    if (!DEV.accessible) {
      fmt::print("no access\n");
      continue;
    }

    fmt::print("bus {} cs {}", DEV.busNumber, DEV.chipSelect);

    if (DEV.config.isValid()) {
      fmt::print(", {}", device::toString(DEV.config.mode));
      if (DEV.config.maxSpeedHz > 0) {
        fmt::print(", {:.1f} MHz", DEV.config.speedMHz());
      }
    }

    fmt::print("\n");
  }
}

void printCanInterfaces(const device::CanInterfaceList& interfaces) {
  fmt::print("\n=== CAN Interfaces ({}) ===\n", interfaces.count);

  if (interfaces.count == 0) {
    fmt::print("  (none found)\n");
    return;
  }

  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const device::CanInterfaceInfo& CAN = interfaces.interfaces[i];
    fmt::print("  {}: ", CAN.name.data());

    if (!CAN.exists) {
      fmt::print("not found\n");
      continue;
    }

    fmt::print("{}", device::toString(CAN.type));

    if (CAN.isUp) {
      fmt::print(" UP");
    } else {
      fmt::print(" DOWN");
    }

    fmt::print(" {}", device::toString(CAN.state));

    if (CAN.bitTiming.bitrate > 0) {
      fmt::print(" {} kbps", CAN.bitTiming.bitrate / 1000);
    }

    if (CAN.isFd()) {
      fmt::print(" (FD)");
    }

    fmt::print("\n");
  }
}

void printGpioChips(const device::GpioChipList& chips) {
  fmt::print("\n=== GPIO Chips ({}) ===\n", chips.count);

  if (chips.count == 0) {
    fmt::print("  (none found)\n");
    return;
  }

  for (std::size_t i = 0; i < chips.count; ++i) {
    const device::GpioChipInfo& CHIP = chips.chips[i];
    fmt::print("  {}: ", CHIP.name.data());

    if (!CHIP.exists) {
      fmt::print("not found\n");
      continue;
    }

    if (!CHIP.accessible) {
      fmt::print("no access\n");
      continue;
    }

    fmt::print("{} lines", CHIP.numLines);

    if (CHIP.linesUsed > 0) {
      fmt::print(" ({} in use)", CHIP.linesUsed);
    }

    if (CHIP.label[0] != '\0') {
      fmt::print(" [{}]", CHIP.label.data());
    }

    fmt::print("\n");
  }
}

void printHuman(const device::SerialPortList& serial, const device::I2cBusList& i2c,
                const device::SpiDeviceList& spi, const device::CanInterfaceList& can,
                const device::GpioChipList& gpio) {
  printSerialPorts(serial);
  printI2cBuses(i2c);
  printSpiDevices(spi);
  printCanInterfaces(can);
  printGpioChips(gpio);
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const device::SerialPortList& serial, const device::I2cBusList& i2c,
               const device::SpiDeviceList& spi, const device::CanInterfaceList& can,
               const device::GpioChipList& gpio) {
  fmt::print("{{\n");

  // Serial ports
  fmt::print("  \"serialPorts\": [\n");
  for (std::size_t i = 0; i < serial.count; ++i) {
    const device::SerialPortInfo& P = serial.ports[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", P.name.data());
    fmt::print("      \"type\": \"{}\",\n", device::toString(P.type));
    fmt::print("      \"exists\": {},\n", P.exists);
    fmt::print("      \"readable\": {},\n", P.readable);
    fmt::print("      \"writable\": {},\n", P.writable);
    fmt::print("      \"driver\": \"{}\"\n", P.driver.data());
    fmt::print("    }}");
  }
  fmt::print("\n  ],\n");

  // I2C buses
  fmt::print("  \"i2cBuses\": [\n");
  for (std::size_t i = 0; i < i2c.count; ++i) {
    const device::I2cBusInfo& B = i2c.buses[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", B.name.data());
    fmt::print("      \"busNumber\": {},\n", B.busNumber);
    fmt::print("      \"exists\": {},\n", B.exists);
    fmt::print("      \"accessible\": {},\n", B.accessible);
    fmt::print("      \"adapter\": \"{}\"\n", B.adapterName.data());
    fmt::print("    }}");
  }
  fmt::print("\n  ],\n");

  // SPI devices
  fmt::print("  \"spiDevices\": [\n");
  for (std::size_t i = 0; i < spi.count; ++i) {
    const device::SpiDeviceInfo& D = spi.devices[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", D.name.data());
    fmt::print("      \"busNumber\": {},\n", D.busNumber);
    fmt::print("      \"chipSelect\": {},\n", D.chipSelect);
    fmt::print("      \"exists\": {},\n", D.exists);
    fmt::print("      \"accessible\": {},\n", D.accessible);
    fmt::print("      \"maxSpeedHz\": {}\n", D.config.maxSpeedHz);
    fmt::print("    }}");
  }
  fmt::print("\n  ],\n");

  // CAN interfaces
  fmt::print("  \"canInterfaces\": [\n");
  for (std::size_t i = 0; i < can.count; ++i) {
    const device::CanInterfaceInfo& C = can.interfaces[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", C.name.data());
    fmt::print("      \"type\": \"{}\",\n", device::toString(C.type));
    fmt::print("      \"exists\": {},\n", C.exists);
    fmt::print("      \"isUp\": {},\n", C.isUp);
    fmt::print("      \"state\": \"{}\",\n", device::toString(C.state));
    fmt::print("      \"bitrate\": {},\n", C.bitTiming.bitrate);
    fmt::print("      \"isFd\": {}\n", C.isFd());
    fmt::print("    }}");
  }
  fmt::print("\n  ],\n");

  // GPIO chips
  fmt::print("  \"gpioChips\": [\n");
  for (std::size_t i = 0; i < gpio.count; ++i) {
    const device::GpioChipInfo& G = gpio.chips[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"name\": \"{}\",\n", G.name.data());
    fmt::print("      \"chipNumber\": {},\n", G.chipNumber);
    fmt::print("      \"exists\": {},\n", G.exists);
    fmt::print("      \"accessible\": {},\n", G.accessible);
    fmt::print("      \"numLines\": {},\n", G.numLines);
    fmt::print("      \"linesUsed\": {},\n", G.linesUsed);
    fmt::print("      \"label\": \"{}\"\n", G.label.data());
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

  // Gather data from all device domains
  const device::SerialPortList SERIAL = device::getAllSerialPorts();
  const device::I2cBusList I2C = device::getAllI2cBuses();
  const device::SpiDeviceList SPI = device::getAllSpiDevices();
  const device::CanInterfaceList CAN = device::getAllCanInterfaces();
  const device::GpioChipList GPIO = device::getAllGpioChips();

  if (jsonOutput) {
    printJson(SERIAL, I2C, SPI, CAN, GPIO);
  } else {
    printHuman(SERIAL, I2C, SPI, CAN, GPIO);
  }

  return 0;
}