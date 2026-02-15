/**
 * @file WatchdogStatus_uTest.cpp
 * @brief Unit tests for seeker::system::WatchdogStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Watchdog availability varies by system; tests handle absence gracefully.
 *  - Does NOT open /dev/watchdog (which would arm the watchdog).
 */

#include "src/system/inc/WatchdogStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::getWatchdogDevice;
using seeker::system::getWatchdogStatus;
using seeker::system::isSoftdogLoaded;
using seeker::system::WatchdogCapabilities;
using seeker::system::WatchdogDevice;
using seeker::system::WatchdogStatus;

class WatchdogStatusTest : public ::testing::Test {
protected:
  WatchdogStatus status_{};

  void SetUp() override { status_ = getWatchdogStatus(); }
};

/* ----------------------------- WatchdogStatus Query Tests ----------------------------- */

/** @test getWatchdogStatus returns valid structure. */
TEST_F(WatchdogStatusTest, QueryReturnsValidStructure) {
  // Device count should be within bounds
  EXPECT_LE(status_.deviceCount, seeker::system::MAX_WATCHDOG_DEVICES);
}

/** @test Device count is consistent with array contents. */
TEST_F(WatchdogStatusTest, DeviceCountConsistent) {
  // All devices up to deviceCount should have valid index or be marked invalid
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    // Valid devices should have valid=true from sysfs reads
    // Even if a device exists but couldn't be fully read, it should have a path
    EXPECT_GT(std::strlen(status_.devices[i].devicePath.data()), 0U)
        << "Device " << i << " has empty path";
  }
}

/** @test hasWatchdog is consistent with deviceCount. */
TEST_F(WatchdogStatusTest, HasWatchdogConsistent) {
  EXPECT_EQ(status_.hasWatchdog(), status_.deviceCount > 0);
}

/** @test find returns nullptr for non-existent device. */
TEST_F(WatchdogStatusTest, FindNonExistent) {
  // Index 99 is unlikely to exist
  const auto* DEV = status_.find(99);
  EXPECT_EQ(DEV, nullptr);
}

/** @test primary returns device at index 0 if present. */
TEST_F(WatchdogStatusTest, PrimaryReturnsIndex0) {
  const auto* PRIMARY = status_.primary();
  if (PRIMARY != nullptr) {
    EXPECT_EQ(PRIMARY->index, 0U);
  }
}

/** @test anyActive returns consistent result. */
TEST_F(WatchdogStatusTest, AnyActiveConsistent) {
  bool foundActive = false;
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    if (status_.devices[i].active) {
      foundActive = true;
      break;
    }
  }
  EXPECT_EQ(status_.anyActive(), foundActive);
}

/* ----------------------------- WatchdogDevice Tests ----------------------------- */

/** @test getWatchdogDevice returns invalid for non-existent device. */
TEST(WatchdogDeviceTest, NonExistentDevice) {
  // Index 99 is unlikely to exist
  const auto DEV = getWatchdogDevice(99);
  EXPECT_FALSE(DEV.valid);
}

/** @test Device path format is correct. */
TEST_F(WatchdogStatusTest, DevicePathFormat) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const std::string PATH{status_.devices[i].devicePath.data()};
    EXPECT_NE(PATH.find("/dev/watchdog"), std::string::npos) << "Unexpected path: " << PATH;
  }
}

/** @test Timeout values are reasonable if device is valid. */
TEST_F(WatchdogStatusTest, TimeoutRangeReasonable) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    if (status_.devices[i].valid) {
      // Max timeout should be >= current timeout
      if (status_.devices[i].maxTimeout > 0) {
        EXPECT_LE(status_.devices[i].timeout, status_.devices[i].maxTimeout)
            << "Device " << i << " timeout exceeds max";
      }
      // Min timeout should be <= current timeout
      if (status_.devices[i].minTimeout > 0) {
        EXPECT_GE(status_.devices[i].timeout, status_.devices[i].minTimeout)
            << "Device " << i << " timeout below min";
      }
    }
  }
}

/** @test isPrimary returns true only for index 0. */
TEST_F(WatchdogStatusTest, IsPrimaryLogic) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    if (status_.devices[i].index == 0) {
      EXPECT_TRUE(status_.devices[i].isPrimary());
    } else {
      EXPECT_FALSE(status_.devices[i].isPrimary());
    }
  }
}

/** @test canSetTimeout is consistent with capabilities. */
TEST_F(WatchdogStatusTest, CanSetTimeoutConsistent) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    EXPECT_EQ(status_.devices[i].canSetTimeout(), status_.devices[i].capabilities.settimeout);
  }
}

/** @test hasPretimeout is consistent with capabilities and value. */
TEST_F(WatchdogStatusTest, HasPretimeoutConsistent) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const bool EXPECTED =
        status_.devices[i].capabilities.pretimeout && status_.devices[i].pretimeout > 0;
    EXPECT_EQ(status_.devices[i].hasPretimeout(), EXPECTED);
  }
}

/* ----------------------------- WatchdogCapabilities Tests ----------------------------- */

/** @test Capabilities hasAny is consistent with raw. */
TEST_F(WatchdogStatusTest, CapabilitiesHasAnyConsistent) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const auto& CAPS = status_.devices[i].capabilities;
    EXPECT_EQ(CAPS.hasAny(), CAPS.raw != 0);
  }
}

/** @test Capabilities toString returns non-empty for non-zero raw. */
TEST_F(WatchdogStatusTest, CapabilitiesStringNonEmpty) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const auto& CAPS = status_.devices[i].capabilities;
    const std::string STR = CAPS.toString();
    if (CAPS.raw != 0) {
      EXPECT_FALSE(STR.empty());
      EXPECT_NE(STR, "none");
    } else {
      EXPECT_EQ(STR, "none");
    }
  }
}

/* ----------------------------- Softdog Detection Tests ----------------------------- */

/** @test isSoftdogLoaded returns boolean. */
TEST(SoftdogTest, ReturnsBoolean) {
  const bool LOADED = isSoftdogLoaded();
  // Just verify it returns without crashing
  EXPECT_TRUE(LOADED || !LOADED);
}

/** @test Softdog detection is consistent with status. */
TEST_F(WatchdogStatusTest, SoftdogConsistent) {
  EXPECT_EQ(status_.softdogLoaded, isSoftdogLoaded());
}

/* ----------------------------- RT Suitability Tests ----------------------------- */

/** @test isRtSuitable requires valid device. */
TEST(WatchdogDeviceDefaultTest, InvalidNotRtSuitable) {
  const WatchdogDevice DEV{};
  EXPECT_FALSE(DEV.isRtSuitable());
}

/** @test findRtSuitable returns device or nullptr. */
TEST_F(WatchdogStatusTest, FindRtSuitableValid) {
  const auto* DEV = status_.findRtSuitable();
  if (DEV != nullptr) {
    EXPECT_TRUE(DEV->isRtSuitable());
  }
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test WatchdogDevice toString produces output. */
TEST_F(WatchdogStatusTest, DeviceToStringNonEmpty) {
  for (std::size_t i = 0; i < status_.deviceCount; ++i) {
    const std::string OUTPUT = status_.devices[i].toString();
    EXPECT_FALSE(OUTPUT.empty());
    // Should contain device info
    if (status_.devices[i].valid) {
      EXPECT_NE(OUTPUT.find("watchdog"), std::string::npos);
    }
  }
}

/** @test WatchdogStatus toString produces output. */
TEST_F(WatchdogStatusTest, StatusToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Watchdog"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default WatchdogDevice is zeroed. */
TEST(WatchdogDeviceDefaultTest, DefaultZeroed) {
  const WatchdogDevice DEV{};

  EXPECT_EQ(DEV.index, 0U);
  EXPECT_EQ(DEV.devicePath[0], '\0');
  EXPECT_EQ(DEV.identity[0], '\0');
  EXPECT_EQ(DEV.timeout, 0U);
  EXPECT_EQ(DEV.minTimeout, 0U);
  EXPECT_EQ(DEV.maxTimeout, 0U);
  EXPECT_EQ(DEV.pretimeout, 0U);
  EXPECT_FALSE(DEV.valid);
  EXPECT_FALSE(DEV.active);
  EXPECT_FALSE(DEV.nowayout);
}

/** @test Default WatchdogStatus is zeroed. */
TEST(WatchdogStatusDefaultTest, DefaultZeroed) {
  const WatchdogStatus STATUS{};

  EXPECT_EQ(STATUS.deviceCount, 0U);
  EXPECT_FALSE(STATUS.softdogLoaded);
  EXPECT_FALSE(STATUS.hasHardwareWatchdog);
  EXPECT_FALSE(STATUS.hasWatchdog());
  EXPECT_FALSE(STATUS.anyActive());
  EXPECT_EQ(STATUS.primary(), nullptr);
}

/** @test Default WatchdogCapabilities is zeroed. */
TEST(WatchdogCapabilitiesDefaultTest, DefaultZeroed) {
  const WatchdogCapabilities CAPS{};

  EXPECT_EQ(CAPS.raw, 0U);
  EXPECT_FALSE(CAPS.settimeout);
  EXPECT_FALSE(CAPS.magicclose);
  EXPECT_FALSE(CAPS.pretimeout);
  EXPECT_FALSE(CAPS.keepaliveping);
  EXPECT_FALSE(CAPS.alarmonly);
  EXPECT_FALSE(CAPS.hasAny());
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getWatchdogStatus returns consistent results. */
TEST(WatchdogDeterminismTest, ConsistentResults) {
  const auto STATUS1 = getWatchdogStatus();
  const auto STATUS2 = getWatchdogStatus();

  // Device count should be identical
  EXPECT_EQ(STATUS1.deviceCount, STATUS2.deviceCount);

  // Softdog detection should be identical
  EXPECT_EQ(STATUS1.softdogLoaded, STATUS2.softdogLoaded);

  // Hardware watchdog detection should be identical
  EXPECT_EQ(STATUS1.hasHardwareWatchdog, STATUS2.hasHardwareWatchdog);

  // Device identities should match
  for (std::size_t i = 0; i < STATUS1.deviceCount; ++i) {
    EXPECT_STREQ(STATUS1.devices[i].identity.data(), STATUS2.devices[i].identity.data());
    EXPECT_EQ(STATUS1.devices[i].index, STATUS2.devices[i].index);
    EXPECT_EQ(STATUS1.devices[i].valid, STATUS2.devices[i].valid);
  }
}