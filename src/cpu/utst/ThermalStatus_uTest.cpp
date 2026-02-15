/**
 * @file ThermalStatus_uTest.cpp
 * @brief Unit tests for seeker::cpu::ThermalStatus.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Systems without thermal sensors or RAPL may have empty results (valid).
 */

#include "src/cpu/inc/ThermalStatus.hpp"

#include <gtest/gtest.h>

#include <cstring> // std::strlen
#include <string>

using seeker::cpu::getThermalStatus;
using seeker::cpu::PowerLimit;
using seeker::cpu::TemperatureSensor;
using seeker::cpu::THERMAL_NAME_SIZE;
using seeker::cpu::ThermalStatus;
using seeker::cpu::ThrottleHints;

class ThermalStatusTest : public ::testing::Test {
protected:
  ThermalStatus status_{};

  void SetUp() override { status_ = getThermalStatus(); }
};

/* ----------------------------- Sensor Tests ----------------------------- */

/** @test Empty sensor list is valid (some systems have no sensors). */
TEST_F(ThermalStatusTest, EmptySensorsValid) {
  if (status_.sensors.empty()) {
    GTEST_LOG_(INFO) << "No temperature sensors detected";
  }
  // Just verify no crash
  SUCCEED();
}

/** @test Sensor names are null-terminated. */
TEST_F(ThermalStatusTest, SensorNamesNullTerminated) {
  for (const TemperatureSensor& SENSOR : status_.sensors) {
    bool foundNull = false;
    for (std::size_t i = 0; i < THERMAL_NAME_SIZE; ++i) {
      if (SENSOR.name[i] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Sensor name not null-terminated";
  }
}

/** @test Sensor names are within bounds. */
TEST_F(ThermalStatusTest, SensorNamesWithinBounds) {
  for (const TemperatureSensor& SENSOR : status_.sensors) {
    const std::size_t LEN = std::strlen(SENSOR.name.data());
    EXPECT_LT(LEN, THERMAL_NAME_SIZE);
  }
}

/** @test Temperature values are reasonable. */
TEST_F(ThermalStatusTest, TemperaturesReasonable) {
  for (const TemperatureSensor& SENSOR : status_.sensors) {
    // Temps should be > absolute zero and < boiling point of components
    // Allow negative for some exotic sensors, but cap at reasonable limits
    EXPECT_GT(SENSOR.tempCelsius, -50.0) << "Sensor " << SENSOR.name.data() << " too cold";
    EXPECT_LT(SENSOR.tempCelsius, 150.0) << "Sensor " << SENSOR.name.data() << " too hot";
  }
}

/** @test Non-zero temperatures are positive (when present). */
TEST_F(ThermalStatusTest, NonZeroTempsPositive) {
  for (const TemperatureSensor& SENSOR : status_.sensors) {
    if (SENSOR.tempCelsius != 0.0) {
      // Most real sensors report positive temps at room temp or higher
      // Allow for cooled systems, but warn if negative
      if (SENSOR.tempCelsius < 0.0) {
        GTEST_LOG_(INFO) << "Sensor " << SENSOR.name.data()
                         << " reports negative temp: " << SENSOR.tempCelsius;
      }
    }
  }
}

/** @test TemperatureSensor::toString produces valid output. */
TEST_F(ThermalStatusTest, SensorToStringValid) {
  for (const TemperatureSensor& SENSOR : status_.sensors) {
    const std::string OUTPUT = SENSOR.toString();
    EXPECT_FALSE(OUTPUT.empty());
    EXPECT_NE(OUTPUT.find("C"), std::string::npos); // Contains "C" for Celsius
  }
}

/* ----------------------------- Power Limit Tests ----------------------------- */

/** @test Empty power limits is valid (non-Intel or RAPL unavailable). */
TEST_F(ThermalStatusTest, EmptyPowerLimitsValid) {
  if (status_.powerLimits.empty()) {
    GTEST_LOG_(INFO) << "No RAPL power limits detected";
  }
  SUCCEED();
}

/** @test Power limit domain names are null-terminated. */
TEST_F(ThermalStatusTest, PowerLimitNamesNullTerminated) {
  for (const PowerLimit& LIMIT : status_.powerLimits) {
    bool foundNull = false;
    for (std::size_t i = 0; i < THERMAL_NAME_SIZE; ++i) {
      if (LIMIT.domain[i] == '\0') {
        foundNull = true;
        break;
      }
    }
    EXPECT_TRUE(foundNull) << "Power limit domain not null-terminated";
  }
}

/** @test Power limit values are non-negative. */
TEST_F(ThermalStatusTest, PowerLimitsNonNegative) {
  for (const PowerLimit& LIMIT : status_.powerLimits) {
    EXPECT_GE(LIMIT.watts, 0.0) << "Domain " << LIMIT.domain.data() << " has negative power limit";
  }
}

/** @test Power limit values are reasonable (< 1000W for CPU). */
TEST_F(ThermalStatusTest, PowerLimitsReasonable) {
  for (const PowerLimit& LIMIT : status_.powerLimits) {
    EXPECT_LT(LIMIT.watts, 1000.0)
        << "Domain " << LIMIT.domain.data() << " power limit unreasonably high";
  }
}

/** @test PowerLimit::toString produces valid output. */
TEST_F(ThermalStatusTest, PowerLimitToStringValid) {
  for (const PowerLimit& LIMIT : status_.powerLimits) {
    const std::string OUTPUT = LIMIT.toString();
    EXPECT_FALSE(OUTPUT.empty());
    EXPECT_NE(OUTPUT.find("W"), std::string::npos); // Contains "W" for Watts
  }
}

/* ----------------------------- Throttle Hints Tests ----------------------------- */

/** @test Throttle hints are boolean flags (no validation needed, just access). */
TEST_F(ThermalStatusTest, ThrottleHintsAccessible) {
  // Just verify we can read the flags without crash
  (void)status_.throttling.powerLimit;
  (void)status_.throttling.thermal;
  (void)status_.throttling.current;
  SUCCEED();
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test ThermalStatus::toString produces non-empty output. */
TEST_F(ThermalStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test ThermalStatus::toString contains expected sections. */
TEST_F(ThermalStatusTest, ToStringContainsSections) {
  const std::string OUTPUT = status_.toString();

  EXPECT_NE(OUTPUT.find("Temperatures:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Power limits:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Throttle hints:"), std::string::npos);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default TemperatureSensor is zeroed. */
TEST(ThermalStatusDefaultTest, DefaultSensorZeroed) {
  const TemperatureSensor DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.tempCelsius, 0.0);
}

/** @test Default PowerLimit is zeroed. */
TEST(ThermalStatusDefaultTest, DefaultPowerLimitZeroed) {
  const PowerLimit DEFAULT{};

  EXPECT_EQ(DEFAULT.domain[0], '\0');
  EXPECT_EQ(DEFAULT.watts, 0.0);
  EXPECT_FALSE(DEFAULT.enforced);
}

/** @test Default ThrottleHints is all false. */
TEST(ThermalStatusDefaultTest, DefaultThrottleHintsFalse) {
  const ThrottleHints DEFAULT{};

  EXPECT_FALSE(DEFAULT.powerLimit);
  EXPECT_FALSE(DEFAULT.thermal);
  EXPECT_FALSE(DEFAULT.current);
}

/** @test Default ThermalStatus has empty vectors. */
TEST(ThermalStatusDefaultTest, DefaultStatusEmpty) {
  const ThermalStatus DEFAULT{};

  EXPECT_TRUE(DEFAULT.sensors.empty());
  EXPECT_TRUE(DEFAULT.powerLimits.empty());
  EXPECT_FALSE(DEFAULT.throttling.powerLimit);
  EXPECT_FALSE(DEFAULT.throttling.thermal);
  EXPECT_FALSE(DEFAULT.throttling.current);
}