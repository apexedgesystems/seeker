/**
 * @file SerialPortInfo_uTest.cpp
 * @brief Unit tests for seeker::device::SerialPortInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Serial port availability varies by hardware configuration.
 *  - Tests handle missing serial ports gracefully.
 *  - Permission-dependent tests may require elevated privileges.
 */

#include "src/device/inc/SerialPortInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::device::getAllSerialPorts;
using seeker::device::getRs485Config;
using seeker::device::getSerialConfig;
using seeker::device::getSerialPortInfo;
using seeker::device::isSerialPortName;
using seeker::device::MAX_SERIAL_PORTS;
using seeker::device::Rs485Config;
using seeker::device::SerialBaudRate;
using seeker::device::SerialConfig;
using seeker::device::SerialPortInfo;
using seeker::device::SerialPortList;
using seeker::device::SerialPortType;
using seeker::device::toString;
using seeker::device::UsbSerialInfo;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SerialBaudRate is zeroed. */
TEST(SerialBaudRateDefaultTest, DefaultZeroed) {
  const SerialBaudRate DEFAULT{};

  EXPECT_EQ(DEFAULT.input, 0U);
  EXPECT_EQ(DEFAULT.output, 0U);
  EXPECT_FALSE(DEFAULT.isSet());
  EXPECT_TRUE(DEFAULT.isSymmetric());
}

/** @test Default SerialConfig has standard 8N1 defaults. */
TEST(SerialConfigDefaultTest, Default8N1) {
  const SerialConfig DEFAULT{};

  EXPECT_EQ(DEFAULT.dataBits, 8U);
  EXPECT_EQ(DEFAULT.parity, 'N');
  EXPECT_EQ(DEFAULT.stopBits, 1U);
  EXPECT_FALSE(DEFAULT.hwFlowControl);
  EXPECT_FALSE(DEFAULT.swFlowControl);
  EXPECT_TRUE(DEFAULT.isValid());
}

/** @test Default Rs485Config is disabled. */
TEST(Rs485ConfigDefaultTest, DefaultDisabled) {
  const Rs485Config DEFAULT{};

  EXPECT_FALSE(DEFAULT.enabled);
  EXPECT_FALSE(DEFAULT.rtsOnSend);
  EXPECT_FALSE(DEFAULT.rtsAfterSend);
  EXPECT_FALSE(DEFAULT.rxDuringTx);
  EXPECT_FALSE(DEFAULT.isConfigured());
}

/** @test Default UsbSerialInfo is empty. */
TEST(UsbSerialInfoDefaultTest, DefaultEmpty) {
  const UsbSerialInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.vendorId, 0U);
  EXPECT_EQ(DEFAULT.productId, 0U);
  EXPECT_FALSE(DEFAULT.isAvailable());
}

/** @test Default SerialPortInfo is empty. */
TEST(SerialPortInfoDefaultTest, DefaultEmpty) {
  const SerialPortInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.devicePath[0], '\0');
  EXPECT_EQ(DEFAULT.type, SerialPortType::UNKNOWN);
  EXPECT_FALSE(DEFAULT.exists);
  EXPECT_FALSE(DEFAULT.readable);
  EXPECT_FALSE(DEFAULT.writable);
  EXPECT_FALSE(DEFAULT.isAccessible());
}

/** @test Default SerialPortList is empty. */
TEST(SerialPortListDefaultTest, DefaultEmpty) {
  const SerialPortList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_EQ(DEFAULT.find("anything"), nullptr);
  EXPECT_EQ(DEFAULT.findByPath("/dev/anything"), nullptr);
}

/* ----------------------------- SerialPortType Tests ----------------------------- */

/** @test SerialPortType toString covers all values. */
TEST(SerialPortTypeTest, ToStringCoversAll) {
  EXPECT_STREQ(toString(SerialPortType::UNKNOWN), "unknown");
  EXPECT_STREQ(toString(SerialPortType::BUILTIN_UART), "builtin-uart");
  EXPECT_STREQ(toString(SerialPortType::USB_SERIAL), "usb-serial");
  EXPECT_STREQ(toString(SerialPortType::USB_ACM), "usb-acm");
  EXPECT_STREQ(toString(SerialPortType::PLATFORM), "platform");
  EXPECT_STREQ(toString(SerialPortType::VIRTUAL), "virtual");
}

/** @test Invalid SerialPortType returns "unknown". */
TEST(SerialPortTypeTest, InvalidReturnsUnknown) {
  const auto INVALID = static_cast<SerialPortType>(255);
  EXPECT_STREQ(toString(INVALID), "unknown");
}

/* ----------------------------- SerialBaudRate Methods ----------------------------- */

/** @test SerialBaudRate isSet detects configured rates. */
TEST(SerialBaudRateMethodsTest, IsSetDetects) {
  SerialBaudRate rate{};

  EXPECT_FALSE(rate.isSet());

  rate.input = 9600;
  EXPECT_TRUE(rate.isSet());

  rate.input = 0;
  rate.output = 115200;
  EXPECT_TRUE(rate.isSet());
}

/** @test SerialBaudRate isSymmetric checks input/output equality. */
TEST(SerialBaudRateMethodsTest, IsSymmetric) {
  SerialBaudRate rate{};
  rate.input = 9600;
  rate.output = 9600;
  EXPECT_TRUE(rate.isSymmetric());

  rate.output = 115200;
  EXPECT_FALSE(rate.isSymmetric());
}

/* ----------------------------- SerialConfig Methods ----------------------------- */

/** @test SerialConfig notation formats correctly. */
TEST(SerialConfigMethodsTest, NotationFormats) {
  SerialConfig cfg{};

  // Default 8N1
  auto notation = cfg.notation();
  EXPECT_STREQ(notation.data(), "8N1");

  // 7E2
  cfg.dataBits = 7;
  cfg.parity = 'E';
  cfg.stopBits = 2;
  notation = cfg.notation();
  EXPECT_STREQ(notation.data(), "7E2");
}

/** @test SerialConfig isValid validates parameters. */
TEST(SerialConfigMethodsTest, IsValidValidates) {
  SerialConfig cfg{};
  EXPECT_TRUE(cfg.isValid());

  // Invalid data bits
  cfg.dataBits = 4;
  EXPECT_FALSE(cfg.isValid());
  cfg.dataBits = 9;
  EXPECT_FALSE(cfg.isValid());
  cfg.dataBits = 8;

  // Invalid parity
  cfg.parity = 'X';
  EXPECT_FALSE(cfg.isValid());
  cfg.parity = 'N';

  // Invalid stop bits
  cfg.stopBits = 0;
  EXPECT_FALSE(cfg.isValid());
  cfg.stopBits = 3;
  EXPECT_FALSE(cfg.isValid());
}

/** @test SerialConfig valid configurations. */
TEST(SerialConfigMethodsTest, ValidConfigurations) {
  // Test all valid data bit values
  for (std::uint8_t bits : {5, 6, 7, 8}) {
    SerialConfig cfg{};
    cfg.dataBits = bits;
    EXPECT_TRUE(cfg.isValid()) << "Data bits " << static_cast<int>(bits) << " should be valid";
  }

  // Test all valid parity values
  for (char parity : {'N', 'E', 'O'}) {
    SerialConfig cfg{};
    cfg.parity = parity;
    EXPECT_TRUE(cfg.isValid()) << "Parity '" << parity << "' should be valid";
  }

  // Test all valid stop bit values
  for (std::uint8_t stop : {1, 2}) {
    SerialConfig cfg{};
    cfg.stopBits = stop;
    EXPECT_TRUE(cfg.isValid()) << "Stop bits " << static_cast<int>(stop) << " should be valid";
  }
}

/* ----------------------------- Rs485Config Methods ----------------------------- */

/** @test Rs485Config isConfigured checks enabled flag. */
TEST(Rs485ConfigMethodsTest, IsConfiguredChecksEnabled) {
  Rs485Config cfg{};

  EXPECT_FALSE(cfg.isConfigured());

  cfg.enabled = true;
  EXPECT_TRUE(cfg.isConfigured());
}

/* ----------------------------- UsbSerialInfo Methods ----------------------------- */

/** @test UsbSerialInfo isAvailable checks IDs. */
TEST(UsbSerialInfoMethodsTest, IsAvailableChecksIds) {
  UsbSerialInfo info{};

  EXPECT_FALSE(info.isAvailable());

  info.vendorId = 0x0403; // FTDI
  EXPECT_TRUE(info.isAvailable());

  info.vendorId = 0;
  info.productId = 0x6001;
  EXPECT_TRUE(info.isAvailable());
}

/* ----------------------------- SerialPortInfo Methods ----------------------------- */

/** @test SerialPortInfo isUsb detects USB types. */
TEST(SerialPortInfoMethodsTest, IsUsbDetectsTypes) {
  SerialPortInfo info{};

  info.type = SerialPortType::UNKNOWN;
  EXPECT_FALSE(info.isUsb());

  info.type = SerialPortType::BUILTIN_UART;
  EXPECT_FALSE(info.isUsb());

  info.type = SerialPortType::USB_SERIAL;
  EXPECT_TRUE(info.isUsb());

  info.type = SerialPortType::USB_ACM;
  EXPECT_TRUE(info.isUsb());
}

/** @test SerialPortInfo isAccessible checks existence and permissions. */
TEST(SerialPortInfoMethodsTest, IsAccessibleChecks) {
  SerialPortInfo info{};

  EXPECT_FALSE(info.isAccessible());

  info.exists = true;
  EXPECT_FALSE(info.isAccessible());

  info.readable = true;
  EXPECT_TRUE(info.isAccessible());

  info.readable = false;
  info.writable = true;
  EXPECT_TRUE(info.isAccessible());
}

/* ----------------------------- SerialPortList Methods ----------------------------- */

/** @test SerialPortList find returns nullptr for missing. */
TEST(SerialPortListMethodsTest, FindMissing) {
  SerialPortList list{};

  EXPECT_EQ(list.find("nonexistent"), nullptr);
  EXPECT_EQ(list.find(nullptr), nullptr);
}

/** @test SerialPortList findByPath returns nullptr for missing. */
TEST(SerialPortListMethodsTest, FindByPathMissing) {
  SerialPortList list{};

  EXPECT_EQ(list.findByPath("/dev/nonexistent"), nullptr);
  EXPECT_EQ(list.findByPath(nullptr), nullptr);
}

/** @test SerialPortList countByType counts correctly. */
TEST(SerialPortListMethodsTest, CountByType) {
  SerialPortList list{};

  // Add some test entries
  std::strcpy(list.ports[0].name.data(), "ttyUSB0");
  list.ports[0].type = SerialPortType::USB_SERIAL;
  std::strcpy(list.ports[1].name.data(), "ttyUSB1");
  list.ports[1].type = SerialPortType::USB_SERIAL;
  std::strcpy(list.ports[2].name.data(), "ttyS0");
  list.ports[2].type = SerialPortType::BUILTIN_UART;
  list.count = 3;

  EXPECT_EQ(list.countByType(SerialPortType::USB_SERIAL), 2U);
  EXPECT_EQ(list.countByType(SerialPortType::BUILTIN_UART), 1U);
  EXPECT_EQ(list.countByType(SerialPortType::USB_ACM), 0U);
}

/** @test SerialPortList countAccessible counts correctly. */
TEST(SerialPortListMethodsTest, CountAccessible) {
  SerialPortList list{};

  list.ports[0].exists = true;
  list.ports[0].readable = true;
  list.ports[1].exists = true;
  list.ports[1].writable = true;
  list.ports[2].exists = false;
  list.count = 3;

  EXPECT_EQ(list.countAccessible(), 2U);
}

/* ----------------------------- isSerialPortName Tests ----------------------------- */

/** @test isSerialPortName recognizes UART prefixes. */
TEST(IsSerialPortNameTest, RecognizesUartPrefixes) {
  EXPECT_TRUE(isSerialPortName("ttyS0"));
  EXPECT_TRUE(isSerialPortName("ttyS3"));
  EXPECT_TRUE(isSerialPortName("ttyAMA0"));
  EXPECT_TRUE(isSerialPortName("ttySAC0"));
  EXPECT_TRUE(isSerialPortName("ttyO0"));
  EXPECT_TRUE(isSerialPortName("ttyHS0"));
  EXPECT_TRUE(isSerialPortName("ttyTHS0"));
  EXPECT_TRUE(isSerialPortName("ttymxc0"));
}

/** @test isSerialPortName recognizes USB-serial prefixes. */
TEST(IsSerialPortNameTest, RecognizesUsbPrefixes) {
  EXPECT_TRUE(isSerialPortName("ttyUSB0"));
  EXPECT_TRUE(isSerialPortName("ttyUSB15"));
  EXPECT_TRUE(isSerialPortName("ttyACM0"));
  EXPECT_TRUE(isSerialPortName("ttyACM1"));
}

/** @test isSerialPortName rejects non-serial names. */
TEST(IsSerialPortNameTest, RejectsNonSerial) {
  EXPECT_FALSE(isSerialPortName("tty0"));
  EXPECT_FALSE(isSerialPortName("tty1"));
  EXPECT_FALSE(isSerialPortName("pts/0"));
  EXPECT_FALSE(isSerialPortName("console"));
  EXPECT_FALSE(isSerialPortName("null"));
  EXPECT_FALSE(isSerialPortName(""));
  EXPECT_FALSE(isSerialPortName(nullptr));
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test getSerialPortInfo handles null input. */
TEST(SerialPortInfoErrorTest, NullReturnsEmpty) {
  const SerialPortInfo INFO = getSerialPortInfo(nullptr);

  EXPECT_EQ(INFO.name[0], '\0');
  EXPECT_FALSE(INFO.exists);
}

/** @test getSerialPortInfo handles empty input. */
TEST(SerialPortInfoErrorTest, EmptyReturnsEmpty) {
  const SerialPortInfo INFO = getSerialPortInfo("");

  EXPECT_EQ(INFO.name[0], '\0');
  EXPECT_FALSE(INFO.exists);
}

/** @test getSerialPortInfo handles nonexistent device. */
TEST(SerialPortInfoErrorTest, NonexistentReturnsNotFound) {
  const SerialPortInfo INFO = getSerialPortInfo("ttyNONEXISTENT999");

  EXPECT_STREQ(INFO.name.data(), "ttyNONEXISTENT999");
  EXPECT_FALSE(INFO.exists);
  EXPECT_FALSE(INFO.isAccessible());
}

/** @test getSerialConfig handles null/empty. */
TEST(SerialConfigErrorTest, HandlesNullEmpty) {
  const SerialConfig CFG1 = getSerialConfig(nullptr);
  EXPECT_TRUE(CFG1.isValid()); // Default is valid

  const SerialConfig CFG2 = getSerialConfig("");
  EXPECT_TRUE(CFG2.isValid());
}

/** @test getRs485Config handles null/empty. */
TEST(Rs485ConfigErrorTest, HandlesNullEmpty) {
  const Rs485Config CFG1 = getRs485Config(nullptr);
  EXPECT_FALSE(CFG1.isConfigured());

  const Rs485Config CFG2 = getRs485Config("");
  EXPECT_FALSE(CFG2.isConfigured());
}

/* ----------------------------- Path Handling ----------------------------- */

/** @test getSerialPortInfo handles /dev/ prefix. */
TEST(SerialPortInfoPathTest, HandlesDevPrefix) {
  const SerialPortInfo INFO1 = getSerialPortInfo("ttyS0");
  const SerialPortInfo INFO2 = getSerialPortInfo("/dev/ttyS0");

  // Both should resolve to same name
  EXPECT_STREQ(INFO1.name.data(), "ttyS0");
  EXPECT_STREQ(INFO2.name.data(), "ttyS0");
}

/* ----------------------------- Enumeration Tests ----------------------------- */

/** @test getAllSerialPorts returns list within bounds. */
TEST(SerialPortListTest, ListWithinBounds) {
  const SerialPortList LIST = getAllSerialPorts();

  EXPECT_LE(LIST.count, MAX_SERIAL_PORTS);
}

/** @test All entries in list have non-empty names. */
TEST(SerialPortListTest, AllEntriesHaveNames) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(std::strlen(LIST.ports[i].name.data()), 0U) << "Entry " << i << " has empty name";
  }
}

/** @test All entries have consistent device paths. */
TEST(SerialPortListTest, AllEntriesHaveConsistentPaths) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SerialPortInfo& PORT = LIST.ports[i];

    // Device path should start with /dev/
    EXPECT_EQ(std::strncmp(PORT.devicePath.data(), "/dev/", 5), 0)
        << "Port " << PORT.name.data() << " has invalid device path";

    // Device path should contain the name
    EXPECT_NE(std::strstr(PORT.devicePath.data(), PORT.name.data()), nullptr)
        << "Port " << PORT.name.data() << " device path doesn't contain name";
  }
}

/** @test All entries have valid type classification. */
TEST(SerialPortListTest, AllEntriesHaveValidTypes) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SerialPortInfo& PORT = LIST.ports[i];

    // Should not have VIRTUAL or UNKNOWN types in enumerated list
    // (these are filtered during enumeration)
    EXPECT_NE(PORT.type, SerialPortType::VIRTUAL)
        << "Port " << PORT.name.data() << " has virtual type";
  }
}

/** @test USB ports have USB type classification. */
TEST(SerialPortListTest, UsbPortsHaveUsbType) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SerialPortInfo& PORT = LIST.ports[i];

    if (std::strncmp(PORT.name.data(), "ttyUSB", 6) == 0) {
      EXPECT_EQ(PORT.type, SerialPortType::USB_SERIAL)
          << "Port " << PORT.name.data() << " should be USB_SERIAL";
    }

    if (std::strncmp(PORT.name.data(), "ttyACM", 6) == 0) {
      EXPECT_EQ(PORT.type, SerialPortType::USB_ACM)
          << "Port " << PORT.name.data() << " should be USB_ACM";
    }
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test SerialConfig toString includes notation. */
TEST(SerialPortInfoToStringTest, SerialConfigIncludesNotation) {
  SerialConfig cfg{};
  cfg.dataBits = 8;
  cfg.parity = 'N';
  cfg.stopBits = 1;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("8N1"), std::string::npos);
}

/** @test SerialConfig toString includes baud rate. */
TEST(SerialPortInfoToStringTest, SerialConfigIncludesBaud) {
  SerialConfig cfg{};
  cfg.baudRate.input = 115200;
  cfg.baudRate.output = 115200;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("115200"), std::string::npos);
}

/** @test Rs485Config toString handles disabled state. */
TEST(SerialPortInfoToStringTest, Rs485DisabledState) {
  const Rs485Config CFG{};
  const std::string OUTPUT = CFG.toString();

  EXPECT_NE(OUTPUT.find("disabled"), std::string::npos);
}

/** @test Rs485Config toString handles enabled state. */
TEST(SerialPortInfoToStringTest, Rs485EnabledState) {
  Rs485Config cfg{};
  cfg.enabled = true;

  const std::string OUTPUT = cfg.toString();
  EXPECT_NE(OUTPUT.find("enabled"), std::string::npos);
}

/** @test UsbSerialInfo toString handles unavailable. */
TEST(SerialPortInfoToStringTest, UsbSerialInfoUnavailable) {
  const UsbSerialInfo INFO{};
  const std::string OUTPUT = INFO.toString();

  EXPECT_NE(OUTPUT.find("not available"), std::string::npos);
}

/** @test UsbSerialInfo toString includes IDs. */
TEST(SerialPortInfoToStringTest, UsbSerialInfoIncludesIds) {
  UsbSerialInfo info{};
  info.vendorId = 0x0403;
  info.productId = 0x6001;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("0403"), std::string::npos);
  EXPECT_NE(OUTPUT.find("6001"), std::string::npos);
}

/** @test SerialPortInfo toString includes name and type. */
TEST(SerialPortInfoToStringTest, SerialPortInfoIncludesBasics) {
  SerialPortInfo info{};
  std::strcpy(info.name.data(), "ttyUSB0");
  info.type = SerialPortType::USB_SERIAL;
  info.exists = true;

  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("ttyUSB0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("usb-serial"), std::string::npos);
}

/** @test SerialPortList toString handles empty. */
TEST(SerialPortInfoToStringTest, SerialPortListEmpty) {
  const SerialPortList EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_NE(OUTPUT.find("No serial ports"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getAllSerialPorts returns consistent count. */
TEST(SerialPortInfoDeterminismTest, ConsistentCount) {
  const SerialPortList LIST1 = getAllSerialPorts();
  const SerialPortList LIST2 = getAllSerialPorts();

  EXPECT_EQ(LIST1.count, LIST2.count);
}

/** @test getSerialPortInfo returns consistent results. */
TEST(SerialPortInfoDeterminismTest, ConsistentInfo) {
  const SerialPortInfo INFO1 = getSerialPortInfo("ttyS0");
  const SerialPortInfo INFO2 = getSerialPortInfo("ttyS0");

  EXPECT_STREQ(INFO1.name.data(), INFO2.name.data());
  EXPECT_EQ(INFO1.exists, INFO2.exists);
  EXPECT_EQ(INFO1.type, INFO2.type);
}

/* ----------------------------- Specific Port Tests (Conditional) ----------------------------- */

/** @test ttyS0 is typically a built-in UART if present. */
TEST(SpecificPortTest, TtyS0Type) {
  const SerialPortInfo INFO = getSerialPortInfo("ttyS0");

  if (INFO.exists) {
    EXPECT_EQ(INFO.type, SerialPortType::BUILTIN_UART);
  }
}

/** @test USB ports have USB info populated. */
TEST(SpecificPortTest, UsbPortsHaveUsbInfo) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SerialPortInfo& PORT = LIST.ports[i];

    if (PORT.isUsb() && PORT.isAccessible()) {
      // USB info should typically be available for accessible USB ports
      // Note: This may not always be true in all environments
      if (PORT.usbInfo.isAvailable()) {
        EXPECT_GT(PORT.usbInfo.vendorId, 0U)
            << "USB port " << PORT.name.data() << " should have vendor ID";
      }
    }
  }
}

/** @test Accessible ports have valid configuration if openable. */
TEST(SpecificPortTest, AccessiblePortsHaveConfig) {
  const SerialPortList LIST = getAllSerialPorts();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const SerialPortInfo& PORT = LIST.ports[i];

    if (PORT.isOpen) {
      EXPECT_TRUE(PORT.config.isValid())
          << "Port " << PORT.name.data() << " opened but config invalid";
    }
  }
}