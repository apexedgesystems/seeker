/**
 * @file EdacStatus_uTest.cpp
 * @brief Unit tests for seeker::memory::EdacStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert ranges/relations, not exact values.
 *  - Systems without EDAC/ECC may have edacSupported=false (valid result).
 *  - Error counts depend on actual hardware state.
 */

#include "src/memory/inc/EdacStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::memory::CsRow;
using seeker::memory::DimmInfo;
using seeker::memory::EDAC_MAX_CSROW;
using seeker::memory::EDAC_MAX_DIMM;
using seeker::memory::EDAC_MAX_MC;
using seeker::memory::EdacStatus;
using seeker::memory::getEdacStatus;
using seeker::memory::isEdacSupported;
using seeker::memory::MemoryController;

class EdacStatusTest : public ::testing::Test {
protected:
  EdacStatus status_{};
  bool edacAvailable_{false};

  void SetUp() override {
    status_ = getEdacStatus();
    edacAvailable_ = status_.edacSupported;
  }
};

/* ----------------------------- MemoryController Tests ----------------------------- */

/** @test Default MemoryController is zeroed. */
TEST(MemoryControllerDefaultTest, DefaultZero) {
  const MemoryController DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.mcType[0], '\0');
  EXPECT_EQ(DEFAULT.edacMode[0], '\0');
  EXPECT_EQ(DEFAULT.memType[0], '\0');
  EXPECT_EQ(DEFAULT.sizeMb, 0U);
  EXPECT_EQ(DEFAULT.ceCount, 0U);
  EXPECT_EQ(DEFAULT.ceNoInfoCount, 0U);
  EXPECT_EQ(DEFAULT.ueCount, 0U);
  EXPECT_EQ(DEFAULT.ueNoInfoCount, 0U);
  EXPECT_EQ(DEFAULT.csrowCount, 0U);
  EXPECT_EQ(DEFAULT.mcIndex, -1);
}

/** @test hasErrors() detects CE errors. */
TEST(MemoryControllerHelperTest, HasErrorsCE) {
  MemoryController mc{};
  mc.ceCount = 5;
  mc.ueCount = 0;

  EXPECT_TRUE(mc.hasErrors());
  EXPECT_FALSE(mc.hasCriticalErrors());
}

/** @test hasErrors() detects UE errors. */
TEST(MemoryControllerHelperTest, HasErrorsUE) {
  MemoryController mc{};
  mc.ceCount = 0;
  mc.ueCount = 1;

  EXPECT_TRUE(mc.hasErrors());
  EXPECT_TRUE(mc.hasCriticalErrors());
}

/** @test hasErrors() false when no errors. */
TEST(MemoryControllerHelperTest, HasErrorsNone) {
  MemoryController mc{};

  EXPECT_FALSE(mc.hasErrors());
  EXPECT_FALSE(mc.hasCriticalErrors());
}

/* ----------------------------- CsRow Tests ----------------------------- */

/** @test Default CsRow is zeroed. */
TEST(CsRowDefaultTest, DefaultZero) {
  const CsRow DEFAULT{};

  EXPECT_EQ(DEFAULT.label[0], '\0');
  EXPECT_EQ(DEFAULT.mcIndex, 0U);
  EXPECT_EQ(DEFAULT.csrowIndex, 0U);
  EXPECT_EQ(DEFAULT.ceCount, 0U);
  EXPECT_EQ(DEFAULT.ueCount, 0U);
  EXPECT_EQ(DEFAULT.sizeMb, 0U);
}

/* ----------------------------- DimmInfo Tests ----------------------------- */

/** @test Default DimmInfo is zeroed. */
TEST(DimmInfoDefaultTest, DefaultZero) {
  const DimmInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.label[0], '\0');
  EXPECT_EQ(DEFAULT.location[0], '\0');
  EXPECT_EQ(DEFAULT.mcIndex, 0U);
  EXPECT_EQ(DEFAULT.dimmIndex, 0U);
  EXPECT_EQ(DEFAULT.ceCount, 0U);
  EXPECT_EQ(DEFAULT.ueCount, 0U);
  EXPECT_EQ(DEFAULT.sizeMb, 0U);
}

/* ----------------------------- EdacStatus Helper Tests ----------------------------- */

/** @test Default EdacStatus is zeroed. */
TEST(EdacStatusDefaultTest, DefaultZero) {
  const EdacStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.mcCount, 0U);
  EXPECT_EQ(DEFAULT.csrowCount, 0U);
  EXPECT_EQ(DEFAULT.dimmCount, 0U);
  EXPECT_EQ(DEFAULT.totalCeCount, 0U);
  EXPECT_EQ(DEFAULT.totalUeCount, 0U);
  EXPECT_FALSE(DEFAULT.edacSupported);
  EXPECT_FALSE(DEFAULT.eccEnabled);
  EXPECT_EQ(DEFAULT.pollIntervalMs, 0U);
  EXPECT_EQ(DEFAULT.lastCeTime, 0);
  EXPECT_EQ(DEFAULT.lastUeTime, 0);
}

/** @test hasErrors() detects aggregate errors. */
TEST(EdacStatusHelperTest, HasErrorsAggregate) {
  EdacStatus status{};
  EXPECT_FALSE(status.hasErrors());

  status.totalCeCount = 10;
  EXPECT_TRUE(status.hasErrors());
  EXPECT_FALSE(status.hasCriticalErrors());

  status.totalUeCount = 1;
  EXPECT_TRUE(status.hasCriticalErrors());
}

/** @test findController() returns correct pointer. */
TEST(EdacStatusHelperTest, FindController) {
  EdacStatus status{};
  status.mcCount = 2;
  status.controllers[0].mcIndex = 0;
  std::strncpy(status.controllers[0].name.data(), "mc0", 4);
  status.controllers[1].mcIndex = 1;
  std::strncpy(status.controllers[1].name.data(), "mc1", 4);

  const MemoryController* MC0 = status.findController(0);
  ASSERT_NE(MC0, nullptr);
  EXPECT_STREQ(MC0->name.data(), "mc0");

  const MemoryController* MC1 = status.findController(1);
  ASSERT_NE(MC1, nullptr);
  EXPECT_STREQ(MC1->name.data(), "mc1");

  const MemoryController* NOT_FOUND = status.findController(99);
  EXPECT_EQ(NOT_FOUND, nullptr);
}

/** @test toString() produces valid output when EDAC not supported. */
TEST(EdacStatusHelperTest, ToStringNotSupported) {
  EdacStatus status{};
  status.edacSupported = false;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Not supported"), std::string::npos);
}

/** @test toString() produces valid output with EDAC data. */
TEST(EdacStatusHelperTest, ToStringWithData) {
  EdacStatus status{};
  status.edacSupported = true;
  status.eccEnabled = true;
  status.mcCount = 1;
  status.controllers[0].mcIndex = 0;
  std::strncpy(status.controllers[0].name.data(), "mc0", 4);
  status.controllers[0].ceCount = 5;
  status.totalCeCount = 5;

  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("EDAC Status:"), std::string::npos);
  EXPECT_NE(OUTPUT.find("mc0"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Correctable errors"), std::string::npos);
}

/** @test toJson() produces valid JSON structure. */
TEST(EdacStatusHelperTest, ToJsonValid) {
  EdacStatus status{};
  status.edacSupported = true;
  status.eccEnabled = true;
  status.mcCount = 1;
  status.controllers[0].mcIndex = 0;
  std::strncpy(status.controllers[0].name.data(), "mc0", 4);

  const std::string OUTPUT = status.toJson();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("\"edacSupported\": true"), std::string::npos);
  EXPECT_NE(OUTPUT.find("\"controllers\":"), std::string::npos);
  EXPECT_NE(OUTPUT.find("\"mc0\""), std::string::npos);
}

/* ----------------------------- Live System Tests ----------------------------- */

/** @test mcCount is within bounds. */
TEST_F(EdacStatusTest, McCountWithinBounds) { EXPECT_LE(status_.mcCount, EDAC_MAX_MC); }

/** @test csrowCount is within bounds. */
TEST_F(EdacStatusTest, CsrowCountWithinBounds) { EXPECT_LE(status_.csrowCount, EDAC_MAX_CSROW); }

/** @test dimmCount is within bounds. */
TEST_F(EdacStatusTest, DimmCountWithinBounds) { EXPECT_LE(status_.dimmCount, EDAC_MAX_DIMM); }

/** @test Empty result is valid (EDAC may not be available). */
TEST_F(EdacStatusTest, EmptyResultValid) {
  if (!edacAvailable_) {
    GTEST_LOG_(INFO) << "EDAC not available on this system (no ECC memory or module not loaded)";
    EXPECT_FALSE(status_.eccEnabled);
    EXPECT_EQ(status_.mcCount, 0U);
  }
}

/** @test eccEnabled implies mcCount > 0. */
TEST_F(EdacStatusTest, EccEnabledImpliesMc) {
  if (status_.eccEnabled) {
    EXPECT_GT(status_.mcCount, 0U);
  }
}

/** @test mcCount > 0 implies eccEnabled. */
TEST_F(EdacStatusTest, McCountImpliesEccEnabled) {
  if (status_.mcCount > 0) {
    EXPECT_TRUE(status_.eccEnabled);
  }
}

/** @test Controller indices are valid. */
TEST_F(EdacStatusTest, ControllerIndicesValid) {
  for (std::size_t i = 0; i < status_.mcCount; ++i) {
    EXPECT_GE(status_.controllers[i].mcIndex, 0) << "Controller " << i << " has invalid index";
  }
}

/** @test CSRow MC indices reference valid controllers. */
TEST_F(EdacStatusTest, CsrowMcIndicesValid) {
  for (std::size_t i = 0; i < status_.csrowCount; ++i) {
    const CsRow& ROW = status_.csrows[i];
    bool found = false;
    for (std::size_t j = 0; j < status_.mcCount; ++j) {
      if (static_cast<std::uint32_t>(status_.controllers[j].mcIndex) == ROW.mcIndex) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "CSRow " << i << " references non-existent MC " << ROW.mcIndex;
  }
}

/** @test DIMM MC indices reference valid controllers. */
TEST_F(EdacStatusTest, DimmMcIndicesValid) {
  for (std::size_t i = 0; i < status_.dimmCount; ++i) {
    const DimmInfo& DIMM = status_.dimms[i];
    bool found = false;
    for (std::size_t j = 0; j < status_.mcCount; ++j) {
      if (static_cast<std::uint32_t>(status_.controllers[j].mcIndex) == DIMM.mcIndex) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "DIMM " << i << " references non-existent MC " << DIMM.mcIndex;
  }
}

/** @test Total error counts match sum of controller counts. */
TEST_F(EdacStatusTest, ErrorCountsMatchSum) {
  std::uint64_t sumCe = 0;
  std::uint64_t sumUe = 0;

  for (std::size_t i = 0; i < status_.mcCount; ++i) {
    sumCe += status_.controllers[i].ceCount;
    sumUe += status_.controllers[i].ueCount;
  }

  EXPECT_EQ(status_.totalCeCount, sumCe);
  EXPECT_EQ(status_.totalUeCount, sumUe);
}

/** @test getEdacStatus() is deterministic (for static fields). */
TEST_F(EdacStatusTest, Deterministic) {
  const EdacStatus STATUS2 = getEdacStatus();

  EXPECT_EQ(status_.edacSupported, STATUS2.edacSupported);
  EXPECT_EQ(status_.eccEnabled, STATUS2.eccEnabled);
  EXPECT_EQ(status_.mcCount, STATUS2.mcCount);

  // Controller indices should be stable
  for (std::size_t i = 0; i < status_.mcCount; ++i) {
    EXPECT_EQ(status_.controllers[i].mcIndex, STATUS2.controllers[i].mcIndex);
    EXPECT_STREQ(status_.controllers[i].name.data(), STATUS2.controllers[i].name.data());
  }
}

/** @test isEdacSupported() matches edacSupported field. */
TEST_F(EdacStatusTest, IsEdacSupportedConsistent) {
  const bool SUPPORTED = isEdacSupported();
  EXPECT_EQ(status_.edacSupported, SUPPORTED);
}

/** @test toString() produces non-empty output. */
TEST_F(EdacStatusTest, ToStringNonEmpty) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test toJson() produces non-empty output. */
TEST_F(EdacStatusTest, ToJsonNonEmpty) {
  const std::string OUTPUT = status_.toJson();
  EXPECT_FALSE(OUTPUT.empty());
  // Basic JSON structure check
  EXPECT_EQ(OUTPUT.front(), '{');
  EXPECT_EQ(OUTPUT.back(), '}');
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test findController with negative index returns nullptr. */
TEST(EdacStatusEdgeTest, FindControllerNegative) {
  const EdacStatus STATUS{};
  EXPECT_EQ(STATUS.findController(-1), nullptr);
}

/** @test Status with only CE errors. */
TEST(EdacStatusEdgeTest, OnlyCeErrors) {
  EdacStatus status{};
  status.edacSupported = true;
  status.eccEnabled = true;
  status.totalCeCount = 100;
  status.totalUeCount = 0;

  EXPECT_TRUE(status.hasErrors());
  EXPECT_FALSE(status.hasCriticalErrors());
}

/** @test Status with UE errors is critical. */
TEST(EdacStatusEdgeTest, UeErrorsCritical) {
  EdacStatus status{};
  status.edacSupported = true;
  status.eccEnabled = true;
  status.totalCeCount = 0;
  status.totalUeCount = 1;

  EXPECT_TRUE(status.hasErrors());
  EXPECT_TRUE(status.hasCriticalErrors());
}

/** @test toString with UE errors shows warning. */
TEST(EdacStatusEdgeTest, ToStringUeWarning) {
  EdacStatus status{};
  status.edacSupported = true;
  status.eccEnabled = true;
  status.totalUeCount = 1;

  const std::string OUTPUT = status.toString();
  EXPECT_NE(OUTPUT.find("WARNING"), std::string::npos);
}

/* ----------------------------- Constant Tests ----------------------------- */

/** @test Constants have reasonable values. */
TEST(EdacConstantTest, ConstantsReasonable) {
  EXPECT_GE(EDAC_MAX_MC, 4U);
  EXPECT_GE(EDAC_MAX_CSROW, 16U);
  EXPECT_GE(EDAC_MAX_DIMM, 16U);
}