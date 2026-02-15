/**
 * @file device-serial.cpp
 * @brief Detailed serial port inspection and configuration display.
 *
 * Shows comprehensive serial port information including USB details,
 * termios configuration, and RS485 settings.
 */

#include "src/device/inc/SerialPortInfo.hpp"
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
  ARG_PORT = 2,
  ARG_CONFIG = 3,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Display detailed serial port information, configuration, and RS485 settings.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_PORT] = {"--port", 1, false, "Show details for specific port (e.g., ttyUSB0)"};
  map[ARG_CONFIG] = {"--config", 0, false, "Include termios configuration details"};
  return map;
}

/* ----------------------------- Human Output ----------------------------- */

void printPortDetails(const device::SerialPortInfo& port, bool showConfig) {
  fmt::print("=== {} ===\n", port.name.data());

  if (!port.exists) {
    fmt::print("  Status: not found\n");
    return;
  }

  fmt::print("  Type:     {}\n", device::toString(port.type));
  fmt::print("  Path:     {}\n", port.devicePath.data());

  // Access permissions
  std::string access;
  if (port.readable)
    access += "readable ";
  if (port.writable)
    access += "writable ";
  if (access.empty())
    access = "no access";
  fmt::print("  Access:   {}\n", access);

  // Driver info
  if (port.driver[0] != '\0') {
    fmt::print("  Driver:   {}\n", port.driver.data());
  }

  // USB-serial details
  if (port.isUsb() && port.usbInfo.isAvailable()) {
    fmt::print("\n  USB Information:\n");
    fmt::print("    Vendor ID:    {:04x}\n", port.usbInfo.vendorId);
    fmt::print("    Product ID:   {:04x}\n", port.usbInfo.productId);

    if (port.usbInfo.manufacturer[0] != '\0') {
      fmt::print("    Manufacturer: {}\n", port.usbInfo.manufacturer.data());
    }
    if (port.usbInfo.product[0] != '\0') {
      fmt::print("    Product:      {}\n", port.usbInfo.product.data());
    }
    if (port.usbInfo.serial[0] != '\0') {
      fmt::print("    Serial:       {}\n", port.usbInfo.serial.data());
    }

    fmt::print("    Bus/Dev:      {:03d}/{:03d}\n", port.usbInfo.busNum, port.usbInfo.devNum);
  }

  // RS485 status
  if (port.supportsRs485()) {
    fmt::print("\n  RS485:\n");
    fmt::print("    Enabled:      {}\n", port.rs485.enabled ? "yes" : "no");
    if (port.rs485.enabled) {
      fmt::print("    RTS on send:  {}\n", port.rs485.rtsOnSend ? "high" : "low");
      fmt::print("    RTS after:    {}\n", port.rs485.rtsAfterSend ? "high" : "low");
      fmt::print("    Rx during Tx: {}\n", port.rs485.rxDuringTx ? "enabled" : "disabled");
      if (port.rs485.delayRtsBeforeSend > 0 || port.rs485.delayRtsAfterSend > 0) {
        fmt::print("    Delays:       {} ms before, {} ms after\n", port.rs485.delayRtsBeforeSend,
                   port.rs485.delayRtsAfterSend);
      }
    }
  }

  // Termios configuration
  if (showConfig && port.isAccessible()) {
    const device::SerialConfig CFG = device::getSerialConfig(port.name.data());

    fmt::print("\n  Configuration:\n");
    fmt::print("    Line:         {}\n", CFG.notation().data());

    if (CFG.baudRate.isSet()) {
      fmt::print("    Baud rate:    {} bps\n", CFG.baudRate.input);
    }

    std::string flowCtrl;
    if (CFG.hwFlowControl)
      flowCtrl += "RTS/CTS ";
    if (CFG.swFlowControl)
      flowCtrl += "XON/XOFF ";
    if (flowCtrl.empty())
      flowCtrl = "none";
    fmt::print("    Flow control: {}\n", flowCtrl);

    fmt::print("    Local mode:   {}\n", CFG.localMode ? "yes" : "no");
    fmt::print("    Raw mode:     {}\n", CFG.rawMode ? "yes" : "no");
  }
}

void printAllPorts(const device::SerialPortList& ports, bool showConfig) {
  fmt::print("=== Serial Ports ({} found) ===\n\n", ports.count);

  if (ports.count == 0) {
    fmt::print("No serial ports found.\n");
    return;
  }

  // Summary table
  fmt::print("{:<12} {:<12} {:<8} {:<20}\n", "PORT", "TYPE", "ACCESS", "INFO");
  fmt::print("{:-<12} {:-<12} {:-<8} {:-<20}\n", "", "", "", "");

  for (std::size_t i = 0; i < ports.count; ++i) {
    const device::SerialPortInfo& PORT = ports.ports[i];

    std::string access;
    if (PORT.isAccessible()) {
      access = std::string(PORT.readable ? "r" : "-") + (PORT.writable ? "w" : "-");
    } else {
      access = "--";
    }

    std::string info;
    if (PORT.isUsb() && PORT.usbInfo.isAvailable()) {
      info = fmt::format("{:04x}:{:04x}", PORT.usbInfo.vendorId, PORT.usbInfo.productId);
    } else if (PORT.driver[0] != '\0') {
      info = PORT.driver.data();
    }

    fmt::print("{:<12} {:<12} {:<8} {:<20}\n", PORT.name.data(), device::toString(PORT.type),
               access, info);
  }

  // Detailed output if config requested
  if (showConfig) {
    fmt::print("\n");
    for (std::size_t i = 0; i < ports.count; ++i) {
      if (i > 0)
        fmt::print("\n");
      printPortDetails(ports.ports[i], showConfig);
    }
  }
}

void printHuman(const device::SerialPortList& ports, const char* portFilter, bool showConfig) {
  if (portFilter != nullptr) {
    const device::SerialPortInfo* found = ports.find(portFilter);
    if (found == nullptr) {
      // Try getting directly
      const device::SerialPortInfo PORT = device::getSerialPortInfo(portFilter);
      if (!PORT.exists) {
        fmt::print(stderr, "Error: Port '{}' not found\n", portFilter);
        return;
      }
      printPortDetails(PORT, showConfig);
    } else {
      printPortDetails(*found, showConfig);
    }
  } else {
    printAllPorts(ports, showConfig);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printPortJson(const device::SerialPortInfo& port, bool showConfig) {
  fmt::print("  {{\n");
  fmt::print("    \"name\": \"{}\",\n", port.name.data());
  fmt::print("    \"type\": \"{}\",\n", device::toString(port.type));
  fmt::print("    \"devicePath\": \"{}\",\n", port.devicePath.data());
  fmt::print("    \"exists\": {},\n", port.exists);
  fmt::print("    \"readable\": {},\n", port.readable);
  fmt::print("    \"writable\": {},\n", port.writable);
  fmt::print("    \"driver\": \"{}\",\n", port.driver.data());

  // USB info
  fmt::print("    \"usb\": {{\n");
  fmt::print("      \"isUsbSerial\": {},\n", port.isUsb());
  fmt::print("      \"vendorId\": {},\n", port.usbInfo.vendorId);
  fmt::print("      \"productId\": {},\n", port.usbInfo.productId);
  fmt::print("      \"manufacturer\": \"{}\",\n", port.usbInfo.manufacturer.data());
  fmt::print("      \"product\": \"{}\",\n", port.usbInfo.product.data());
  fmt::print("      \"serial\": \"{}\"\n", port.usbInfo.serial.data());
  fmt::print("    }},\n");

  // RS485 info
  fmt::print("    \"rs485\": {{\n");
  fmt::print("      \"enabled\": {},\n", port.rs485.enabled);
  fmt::print("      \"rtsOnSend\": {},\n", port.rs485.rtsOnSend);
  fmt::print("      \"rtsAfterSend\": {},\n", port.rs485.rtsAfterSend);
  fmt::print("      \"rxDuringTx\": {}\n", port.rs485.rxDuringTx);
  fmt::print("    }}");

  // Config info
  if (showConfig && port.isAccessible()) {
    const device::SerialConfig CFG = device::getSerialConfig(port.name.data());
    fmt::print(",\n");
    fmt::print("    \"config\": {{\n");
    fmt::print("      \"dataBits\": {},\n", CFG.dataBits);
    fmt::print("      \"parity\": \"{}\",\n", CFG.parity);
    fmt::print("      \"stopBits\": {},\n", CFG.stopBits);
    fmt::print("      \"baudRate\": {},\n", CFG.baudRate.input);
    fmt::print("      \"hwFlowControl\": {},\n", CFG.hwFlowControl);
    fmt::print("      \"swFlowControl\": {},\n", CFG.swFlowControl);
    fmt::print("      \"localMode\": {},\n", CFG.localMode);
    fmt::print("      \"rawMode\": {}\n", CFG.rawMode);
    fmt::print("    }}\n");
  } else {
    fmt::print("\n");
  }

  fmt::print("  }}");
}

void printJson(const device::SerialPortList& ports, const char* portFilter, bool showConfig) {
  fmt::print("{{\n");
  fmt::print("\"serialPorts\": [\n");

  if (portFilter != nullptr) {
    const device::SerialPortInfo* found = ports.find(portFilter);
    if (found != nullptr) {
      printPortJson(*found, showConfig);
    } else {
      const device::SerialPortInfo PORT = device::getSerialPortInfo(portFilter);
      printPortJson(PORT, showConfig);
    }
  } else {
    for (std::size_t i = 0; i < ports.count; ++i) {
      if (i > 0)
        fmt::print(",\n");
      printPortJson(ports.ports[i], showConfig);
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
  bool showConfig = false;
  const char* portFilter = nullptr;

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
    showConfig = (pargs.count(ARG_CONFIG) != 0);

    if (pargs.count(ARG_PORT) != 0) {
      portFilter = pargs[ARG_PORT][0].data();
    }
  }

  // Gather data
  const device::SerialPortList PORTS = device::getAllSerialPorts();

  if (jsonOutput) {
    printJson(PORTS, portFilter, showConfig);
  } else {
    printHuman(PORTS, portFilter, showConfig);
  }

  return 0;
}