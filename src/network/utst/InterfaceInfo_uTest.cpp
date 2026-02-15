/**
 * @file InterfaceInfo_uTest.cpp
 * @brief Unit tests for seeker::network::InterfaceInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - All Linux systems have at least a loopback interface (lo).
 *  - Physical NICs may or may not be present depending on hardware.
 */

#include "src/network/inc/InterfaceInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::network::formatSpeed;
using seeker::network::getAllInterfaces;
using seeker::network::getInterfaceInfo;
using seeker::network::getPhysicalInterfaces;
using seeker::network::IF_NAME_SIZE;
using seeker::network::IF_STRING_SIZE;
using seeker::network::InterfaceInfo;
using seeker::network::InterfaceList;
using seeker::network::MAC_STRING_SIZE;
using seeker::network::MAX_INTERFACES;

class InterfaceInfoTest : public ::testing::Test {
protected:
  InterfaceInfo lo_{};

  void SetUp() override { lo_ = getInterfaceInfo("lo"); }
};

/* ----------------------------- Loopback Tests ----------------------------- */

/** @test Loopback interface exists on all Linux systems. */
TEST_F(InterfaceInfoTest, LoopbackExists) { EXPECT_STREQ(lo_.ifname.data(), "lo"); }

/** @test Loopback operstate is "unknown" (no carrier concept). */
TEST_F(InterfaceInfoTest, LoopbackOperState) {
  // Loopback operstate is "unknown" because it has no carrier concept
  // This is normal Linux behavior - loopback doesn't report "up"
  EXPECT_STREQ(lo_.operState.data(), "unknown");
  // isUp() checks for "up" specifically, so loopback returns false
  EXPECT_FALSE(lo_.isUp());
}

/** @test Loopback is not a physical interface. */
TEST_F(InterfaceInfoTest, LoopbackNotPhysical) { EXPECT_FALSE(lo_.isPhysical()); }

/** @test Loopback has valid MTU. */
TEST_F(InterfaceInfoTest, LoopbackMtuValid) {
  // Loopback MTU is typically 65536
  EXPECT_GE(lo_.mtu, 1500);
  EXPECT_LE(lo_.mtu, 65536);
}

/** @test Loopback has MAC address (usually 00:00:00:00:00:00). */
TEST_F(InterfaceInfoTest, LoopbackHasMac) {
  // Loopback MAC is typically all zeros
  EXPECT_GT(std::strlen(lo_.macAddress.data()), 0U);
}

/* ----------------------------- getAllInterfaces Tests ----------------------------- */

/** @test getAllInterfaces returns at least loopback. */
TEST(InterfaceListTest, GetAllInterfacesHasLoopback) {
  const InterfaceList LIST = getAllInterfaces();

  EXPECT_GE(LIST.count, 1U);
  EXPECT_LE(LIST.count, MAX_INTERFACES);

  const InterfaceInfo* lo = LIST.find("lo");
  EXPECT_NE(lo, nullptr);
}

/** @test Interface count is within bounds. */
TEST(InterfaceListTest, CountWithinBounds) {
  const InterfaceList LIST = getAllInterfaces();
  EXPECT_LE(LIST.count, MAX_INTERFACES);
}

/** @test All interfaces have non-empty names. */
TEST(InterfaceListTest, AllInterfacesHaveNames) {
  const InterfaceList LIST = getAllInterfaces();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(std::strlen(LIST.interfaces[i].ifname.data()), 0U)
        << "Interface " << i << " has empty name";
  }
}

/** @test All interfaces have valid MTU. */
TEST(InterfaceListTest, AllInterfacesHaveMtu) {
  const InterfaceList LIST = getAllInterfaces();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_GT(LIST.interfaces[i].mtu, 0)
        << "Interface " << LIST.interfaces[i].ifname.data() << " has zero MTU";
  }
}

/** @test Interface names fit within buffer. */
TEST(InterfaceListTest, NamesWithinBounds) {
  const InterfaceList LIST = getAllInterfaces();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const std::size_t LEN = std::strlen(LIST.interfaces[i].ifname.data());
    EXPECT_LT(LEN, IF_NAME_SIZE) << "Interface name too long: " << LIST.interfaces[i].ifname.data();
  }
}

/* ----------------------------- getPhysicalInterfaces Tests ----------------------------- */

/** @test Physical interfaces list excludes loopback. */
TEST(InterfaceListTest, PhysicalExcludesLoopback) {
  const InterfaceList LIST = getPhysicalInterfaces();

  const InterfaceInfo* lo = LIST.find("lo");
  EXPECT_EQ(lo, nullptr) << "Physical list should not contain loopback";
}

/** @test Physical interfaces are all marked as physical. */
TEST(InterfaceListTest, PhysicalInterfacesArePhysical) {
  const InterfaceList LIST = getPhysicalInterfaces();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    EXPECT_TRUE(LIST.interfaces[i].isPhysical())
        << "Interface " << LIST.interfaces[i].ifname.data()
        << " in physical list but isPhysical() returns false";
  }
}

/* ----------------------------- InterfaceInfo Helper Methods ----------------------------- */

/** @test isUp returns correct value for known states. */
TEST(InterfaceInfoMethodsTest, IsUpCorrect) {
  InterfaceInfo info{};

  std::strcpy(info.operState.data(), "up");
  EXPECT_TRUE(info.isUp());

  std::strcpy(info.operState.data(), "down");
  EXPECT_FALSE(info.isUp());

  std::strcpy(info.operState.data(), "unknown");
  EXPECT_FALSE(info.isUp());
}

/** @test hasLink requires both up state and speed. */
TEST(InterfaceInfoMethodsTest, HasLinkRequiresBoth) {
  InterfaceInfo info{};

  // Neither up nor speed
  info.operState[0] = '\0';
  info.speedMbps = 0;
  EXPECT_FALSE(info.hasLink());

  // Up but no speed
  std::strcpy(info.operState.data(), "up");
  info.speedMbps = 0;
  EXPECT_FALSE(info.hasLink());

  // Speed but not up
  std::strcpy(info.operState.data(), "down");
  info.speedMbps = 1000;
  EXPECT_FALSE(info.hasLink());

  // Both up and speed
  std::strcpy(info.operState.data(), "up");
  info.speedMbps = 1000;
  EXPECT_TRUE(info.hasLink());
}

/* ----------------------------- InterfaceList::find Tests ----------------------------- */

/** @test find returns nullptr for non-existent interface. */
TEST(InterfaceListFindTest, NotFoundReturnsNull) {
  const InterfaceList LIST = getAllInterfaces();

  EXPECT_EQ(LIST.find("nonexistent_interface_xyz"), nullptr);
  EXPECT_EQ(LIST.find(nullptr), nullptr);
  EXPECT_EQ(LIST.find(""), nullptr);
}

/** @test find returns correct interface. */
TEST(InterfaceListFindTest, FindsExisting) {
  const InterfaceList LIST = getAllInterfaces();

  if (LIST.count > 0) {
    const char* FIRST_NAME = LIST.interfaces[0].ifname.data();
    const InterfaceInfo* found = LIST.find(FIRST_NAME);

    EXPECT_NE(found, nullptr);
    EXPECT_STREQ(found->ifname.data(), FIRST_NAME);
  }
}

/* ----------------------------- getInterfaceInfo Error Handling ----------------------------- */

/** @test Non-existent interface returns empty info. */
TEST(InterfaceInfoErrorTest, NonExistentReturnsEmpty) {
  const InterfaceInfo INFO = getInterfaceInfo("noexist_if0");

  // Name should be set (we copy input)
  EXPECT_STREQ(INFO.ifname.data(), "noexist_if0");

  // But other fields should be empty/zero (interface doesn't exist)
  EXPECT_EQ(INFO.mtu, 0);
  EXPECT_EQ(INFO.speedMbps, 0);
}

/** @test Null interface name returns empty info. */
TEST(InterfaceInfoErrorTest, NullReturnsEmpty) {
  const InterfaceInfo INFO = getInterfaceInfo(nullptr);

  EXPECT_EQ(INFO.ifname[0], '\0');
  EXPECT_EQ(INFO.mtu, 0);
}

/** @test Empty interface name returns empty info. */
TEST(InterfaceInfoErrorTest, EmptyReturnsEmpty) {
  const InterfaceInfo INFO = getInterfaceInfo("");

  EXPECT_EQ(INFO.ifname[0], '\0');
  EXPECT_EQ(INFO.mtu, 0);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test InterfaceInfo toString produces non-empty output. */
TEST_F(InterfaceInfoTest, ToStringNonEmpty) {
  const std::string OUTPUT = lo_.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("lo"), std::string::npos);
}

/** @test InterfaceInfo toString contains key fields. */
TEST_F(InterfaceInfoTest, ToStringContainsFields) {
  const std::string OUTPUT = lo_.toString();

  EXPECT_NE(OUTPUT.find("state="), std::string::npos);
  EXPECT_NE(OUTPUT.find("mtu="), std::string::npos);
}

/** @test InterfaceList toString produces non-empty output. */
TEST(InterfaceListToStringTest, NonEmpty) {
  const InterfaceList LIST = getAllInterfaces();
  const std::string OUTPUT = LIST.toString();

  EXPECT_FALSE(OUTPUT.empty());
}

/** @test Empty InterfaceList toString handles gracefully. */
TEST(InterfaceListToStringTest, EmptyListHandled) {
  const InterfaceList EMPTY{};
  const std::string OUTPUT = EMPTY.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("No interfaces"), std::string::npos);
}

/* ----------------------------- formatSpeed Tests ----------------------------- */

/** @test formatSpeed handles common speeds. */
TEST(FormatSpeedTest, CommonSpeeds) {
  EXPECT_EQ(formatSpeed(10), "10 Mbps");
  EXPECT_EQ(formatSpeed(100), "100 Mbps");
  EXPECT_EQ(formatSpeed(1000), "1 Gbps");
  EXPECT_EQ(formatSpeed(10000), "10 Gbps");
  EXPECT_EQ(formatSpeed(25000), "25 Gbps");
  EXPECT_EQ(formatSpeed(40000), "40 Gbps");
  EXPECT_EQ(formatSpeed(100000), "100 Gbps");
}

/** @test formatSpeed handles non-aligned speeds. */
TEST(FormatSpeedTest, NonAlignedSpeeds) {
  EXPECT_EQ(formatSpeed(2500), "2500 Mbps");
  EXPECT_EQ(formatSpeed(5000), "5 Gbps");
}

/** @test formatSpeed handles invalid speeds. */
TEST(FormatSpeedTest, InvalidSpeeds) {
  EXPECT_EQ(formatSpeed(0), "unknown");
  EXPECT_EQ(formatSpeed(-1), "unknown");
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default InterfaceInfo is zeroed. */
TEST(InterfaceInfoDefaultTest, DefaultZeroed) {
  const InterfaceInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.ifname[0], '\0');
  EXPECT_EQ(DEFAULT.operState[0], '\0');
  EXPECT_EQ(DEFAULT.speedMbps, 0);
  EXPECT_EQ(DEFAULT.mtu, 0);
  EXPECT_EQ(DEFAULT.rxQueues, 0);
  EXPECT_EQ(DEFAULT.txQueues, 0);
  EXPECT_EQ(DEFAULT.numaNode, -1);
}

/** @test Default InterfaceList is empty. */
TEST(InterfaceListDefaultTest, DefaultEmpty) {
  const InterfaceList DEFAULT{};

  EXPECT_EQ(DEFAULT.count, 0U);
  EXPECT_TRUE(DEFAULT.empty());
  EXPECT_EQ(DEFAULT.find("anything"), nullptr);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getInterfaceInfo returns consistent results. */
TEST(InterfaceInfoDeterminismTest, ConsistentResults) {
  const InterfaceInfo INFO1 = getInterfaceInfo("lo");
  const InterfaceInfo INFO2 = getInterfaceInfo("lo");

  EXPECT_STREQ(INFO1.ifname.data(), INFO2.ifname.data());
  EXPECT_EQ(INFO1.mtu, INFO2.mtu);
  EXPECT_STREQ(INFO1.macAddress.data(), INFO2.macAddress.data());
}

/** @test getAllInterfaces returns consistent count. */
TEST(InterfaceListDeterminismTest, ConsistentCount) {
  const InterfaceList LIST1 = getAllInterfaces();
  const InterfaceList LIST2 = getAllInterfaces();

  EXPECT_EQ(LIST1.count, LIST2.count);
}

/* ----------------------------- Physical NIC Tests (Conditional) ----------------------------- */

/** @test Physical NICs have queues if present. */
TEST(PhysicalNicTest, QueuesIfPresent) {
  const InterfaceList LIST = getPhysicalInterfaces();

  for (std::size_t i = 0; i < LIST.count; ++i) {
    const InterfaceInfo& NIC = LIST.interfaces[i];

    // Physical NICs typically have at least 1 rx and 1 tx queue
    if (NIC.hasLink()) {
      EXPECT_GE(NIC.rxQueues, 1) << "NIC " << NIC.ifname.data() << " has no rx queues";
      EXPECT_GE(NIC.txQueues, 1) << "NIC " << NIC.ifname.data() << " has no tx queues";
    }
  }
}

/** @test Physical NICs have driver info if present. */
TEST(PhysicalNicTest, DriverInfoIfPresent) {
  const InterfaceList LIST = getPhysicalInterfaces();

  // On standard x86 systems, physical NICs typically have driver info.
  // On embedded/ARM platforms (Jetson, RPi), driver symlinks may not exist.
  // We just verify the field is valid (not garbage) - empty string is acceptable.
  for (std::size_t i = 0; i < LIST.count; ++i) {
    const InterfaceInfo& NIC = LIST.interfaces[i];
    // Driver field should be null-terminated (valid C string)
    EXPECT_LE(std::strlen(NIC.driver.data()), IF_STRING_SIZE - 1)
        << "NIC " << NIC.ifname.data() << " has invalid driver string";
  }
}