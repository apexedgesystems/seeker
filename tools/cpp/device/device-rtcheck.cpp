/**
 * @file device-rtcheck.cpp
 * @brief Device permissions and real-time configuration checker.
 *
 * Verifies device access permissions and identifies configuration issues
 * that could affect real-time embedded applications.
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
  ARG_VERBOSE = 2,
};

/// Tool description for --help.
constexpr std::string_view DESCRIPTION =
    "Check device permissions and real-time configuration for embedded applications.";

/// Build argument definitions.
seeker::helpers::args::ArgMap buildArgMap() {
  seeker::helpers::args::ArgMap map;
  map[ARG_HELP] = {"--help", 0, false, "Show this help message"};
  map[ARG_JSON] = {"--json", 0, false, "Output in JSON format"};
  map[ARG_VERBOSE] = {"--verbose", 0, false, "Show all devices, not just issues"};
  return map;
}

/// Check result with severity.
struct CheckResult {
  const char* device;
  const char* issue;
  bool isWarning; // true = warning, false = error
};

/* ----------------------------- Checks ----------------------------- */

std::vector<CheckResult> checkSerialPorts(const device::SerialPortList& ports, bool verbose) {
  std::vector<CheckResult> results;

  for (std::size_t i = 0; i < ports.count; ++i) {
    const device::SerialPortInfo& PORT = ports.ports[i];

    if (!PORT.exists) {
      continue;
    }

    if (!PORT.readable && !PORT.writable) {
      results.push_back({PORT.name.data(), "no read/write access", false});
    } else if (!PORT.writable && PORT.readable) {
      results.push_back({PORT.name.data(), "read-only access (cannot transmit)", true});
    } else if (verbose && PORT.isAccessible()) {
      results.push_back({PORT.name.data(), "OK", true});
    }
  }

  return results;
}

std::vector<CheckResult> checkI2cBuses(const device::I2cBusList& buses, bool verbose) {
  std::vector<CheckResult> results;

  for (std::size_t i = 0; i < buses.count; ++i) {
    const device::I2cBusInfo& BUS = buses.buses[i];

    if (!BUS.exists) {
      continue;
    }

    if (!BUS.accessible) {
      results.push_back({BUS.name.data(), "no access to device node", false});
    } else if (!BUS.functionality.i2c && !BUS.functionality.smbusByte) {
      results.push_back({BUS.name.data(), "limited functionality (no I2C or SMBus)", true});
    } else if (verbose) {
      results.push_back({BUS.name.data(), "OK", true});
    }
  }

  return results;
}

std::vector<CheckResult> checkSpiDevices(const device::SpiDeviceList& devices, bool verbose) {
  std::vector<CheckResult> results;

  for (std::size_t i = 0; i < devices.count; ++i) {
    const device::SpiDeviceInfo& DEV = devices.devices[i];

    if (!DEV.exists) {
      continue;
    }

    if (!DEV.accessible) {
      results.push_back({DEV.name.data(), "no access to device node", false});
    } else if (!DEV.config.isValid()) {
      results.push_back({DEV.name.data(), "cannot read configuration", true});
    } else if (verbose) {
      results.push_back({DEV.name.data(), "OK", true});
    }
  }

  return results;
}

std::vector<CheckResult> checkCanInterfaces(const device::CanInterfaceList& interfaces,
                                            bool verbose) {
  std::vector<CheckResult> results;

  for (std::size_t i = 0; i < interfaces.count; ++i) {
    const device::CanInterfaceInfo& CAN = interfaces.interfaces[i];

    if (!CAN.exists) {
      continue;
    }

    if (CAN.state == device::CanBusState::BUS_OFF) {
      results.push_back({CAN.name.data(), "bus-off state (controller disconnected)", false});
    } else if (CAN.state == device::CanBusState::ERROR_PASSIVE) {
      results.push_back({CAN.name.data(), "error-passive state (high error count)", false});
    } else if (CAN.state == device::CanBusState::ERROR_WARNING) {
      results.push_back({CAN.name.data(), "error-warning state (elevated errors)", true});
    } else if (!CAN.isUp) {
      results.push_back({CAN.name.data(), "interface is DOWN", true});
    } else if (CAN.bitTiming.bitrate == 0) {
      results.push_back({CAN.name.data(), "bitrate not configured", true});
    } else if (verbose && CAN.isUsable()) {
      results.push_back({CAN.name.data(), "OK", true});
    }
  }

  return results;
}

std::vector<CheckResult> checkGpioChips(const device::GpioChipList& chips, bool verbose) {
  std::vector<CheckResult> results;

  for (std::size_t i = 0; i < chips.count; ++i) {
    const device::GpioChipInfo& CHIP = chips.chips[i];

    if (!CHIP.exists) {
      continue;
    }

    if (!CHIP.accessible) {
      results.push_back({CHIP.name.data(), "no access to character device", false});
    } else if (verbose) {
      results.push_back({CHIP.name.data(), "OK", true});
    }
  }

  return results;
}

/* ----------------------------- Human Output ----------------------------- */

void printSection(const char* title, const std::vector<CheckResult>& results) {
  fmt::print("=== {} ===\n", title);

  if (results.empty()) {
    fmt::print("  (no issues found)\n");
    return;
  }

  for (const auto& R : results) {
    const char* prefix = R.isWarning ? "[WARN]" : "[FAIL]";
    if (std::strcmp(R.issue, "OK") == 0) {
      prefix = "[ OK ]";
    }
    fmt::print("  {} {}: {}\n", prefix, R.device, R.issue);
  }
}

void printSummary(int errors, int warnings) {
  fmt::print("\n=== Summary ===\n");

  if (errors == 0 && warnings == 0) {
    fmt::print("  All device checks passed.\n");
  } else {
    if (errors > 0) {
      fmt::print("  Errors:   {}\n", errors);
    }
    if (warnings > 0) {
      fmt::print("  Warnings: {}\n", warnings);
    }
  }
}

void printHuman(const device::SerialPortList& serial, const device::I2cBusList& i2c,
                const device::SpiDeviceList& spi, const device::CanInterfaceList& can,
                const device::GpioChipList& gpio, bool verbose) {
  const auto SERIAL_RESULTS = checkSerialPorts(serial, verbose);
  const auto I2C_RESULTS = checkI2cBuses(i2c, verbose);
  const auto SPI_RESULTS = checkSpiDevices(spi, verbose);
  const auto CAN_RESULTS = checkCanInterfaces(can, verbose);
  const auto GPIO_RESULTS = checkGpioChips(gpio, verbose);

  printSection("Serial Ports", SERIAL_RESULTS);
  fmt::print("\n");
  printSection("I2C Buses", I2C_RESULTS);
  fmt::print("\n");
  printSection("SPI Devices", SPI_RESULTS);
  fmt::print("\n");
  printSection("CAN Interfaces", CAN_RESULTS);
  fmt::print("\n");
  printSection("GPIO Chips", GPIO_RESULTS);

  // Count totals
  int errors = 0;
  int warnings = 0;

  auto count = [&](const std::vector<CheckResult>& results) {
    for (const auto& R : results) {
      if (std::strcmp(R.issue, "OK") != 0) {
        if (R.isWarning) {
          ++warnings;
        } else {
          ++errors;
        }
      }
    }
  };

  count(SERIAL_RESULTS);
  count(I2C_RESULTS);
  count(SPI_RESULTS);
  count(CAN_RESULTS);
  count(GPIO_RESULTS);

  printSummary(errors, warnings);
}

/* ----------------------------- JSON Output ----------------------------- */

void printResultsJson(const char* key, const std::vector<CheckResult>& results) {
  fmt::print("  \"{}\": [\n", key);
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& R = results[i];
    if (i > 0)
      fmt::print(",\n");
    fmt::print("    {{\n");
    fmt::print("      \"device\": \"{}\",\n", R.device);
    fmt::print("      \"issue\": \"{}\",\n", R.issue);
    fmt::print("      \"severity\": \"{}\"\n",
               std::strcmp(R.issue, "OK") == 0 ? "ok" : (R.isWarning ? "warning" : "error"));
    fmt::print("    }}");
  }
  fmt::print("\n  ]");
}

void printJson(const device::SerialPortList& serial, const device::I2cBusList& i2c,
               const device::SpiDeviceList& spi, const device::CanInterfaceList& can,
               const device::GpioChipList& gpio, bool verbose) {
  const auto SERIAL_RESULTS = checkSerialPorts(serial, verbose);
  const auto I2C_RESULTS = checkI2cBuses(i2c, verbose);
  const auto SPI_RESULTS = checkSpiDevices(spi, verbose);
  const auto CAN_RESULTS = checkCanInterfaces(can, verbose);
  const auto GPIO_RESULTS = checkGpioChips(gpio, verbose);

  // Count totals
  int errors = 0;
  int warnings = 0;

  auto count = [&](const std::vector<CheckResult>& results) {
    for (const auto& R : results) {
      if (std::strcmp(R.issue, "OK") != 0) {
        if (R.isWarning) {
          ++warnings;
        } else {
          ++errors;
        }
      }
    }
  };

  count(SERIAL_RESULTS);
  count(I2C_RESULTS);
  count(SPI_RESULTS);
  count(CAN_RESULTS);
  count(GPIO_RESULTS);

  fmt::print("{{\n");

  printResultsJson("serialPorts", SERIAL_RESULTS);
  fmt::print(",\n");
  printResultsJson("i2cBuses", I2C_RESULTS);
  fmt::print(",\n");
  printResultsJson("spiDevices", SPI_RESULTS);
  fmt::print(",\n");
  printResultsJson("canInterfaces", CAN_RESULTS);
  fmt::print(",\n");
  printResultsJson("gpioChips", GPIO_RESULTS);
  fmt::print(",\n");

  fmt::print("  \"summary\": {{\n");
  fmt::print("    \"errors\": {},\n", errors);
  fmt::print("    \"warnings\": {},\n", warnings);
  fmt::print("    \"passed\": {}\n", errors == 0);
  fmt::print("  }}\n");

  fmt::print("}}\n");
}

} // namespace

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const seeker::helpers::args::ArgMap ARG_MAP = buildArgMap();
  seeker::helpers::args::ParsedArgs pargs;
  bool jsonOutput = false;
  bool verbose = false;

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
    verbose = (pargs.count(ARG_VERBOSE) != 0);
  }

  // Gather data from all device domains
  const device::SerialPortList SERIAL = device::getAllSerialPorts();
  const device::I2cBusList I2C = device::getAllI2cBuses();
  const device::SpiDeviceList SPI = device::getAllSpiDevices();
  const device::CanInterfaceList CAN = device::getAllCanInterfaces();
  const device::GpioChipList GPIO = device::getAllGpioChips();

  if (jsonOutput) {
    printJson(SERIAL, I2C, SPI, CAN, GPIO, verbose);
  } else {
    printHuman(SERIAL, I2C, SPI, CAN, GPIO, verbose);
  }

  return 0;
}