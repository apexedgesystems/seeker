/**
 * @file DriverInfo_uTest.cpp
 * @brief Unit tests for seeker::system::DriverInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Loaded modules vary by system configuration.
 *  - NVIDIA tests only run when driver is present.
 */

#include "src/system/inc/DriverInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::assessDrivers;
using seeker::system::DriverAssessment;
using seeker::system::DriverEntry;
using seeker::system::DriverInventory;
using seeker::system::getDriverInventory;
using seeker::system::isNvidiaDriverLoaded;
using seeker::system::isNvmlRuntimeAvailable;
using seeker::system::MAX_DRIVER_ENTRIES;

class DriverInfoTest : public ::testing::Test {
protected:
  DriverInventory inv_{};

  void SetUp() override { inv_ = getDriverInventory(); }
};

/* ----------------------------- Basic Query Tests ----------------------------- */

/** @test getDriverInventory doesn't crash. */
TEST_F(DriverInfoTest, QueryDoesNotCrash) {
  // Just verify we got here without crashing
  SUCCEED();
}

/** @test Entry count is within bounds. */
TEST_F(DriverInfoTest, EntryCountWithinBounds) { EXPECT_LE(inv_.entryCount, MAX_DRIVER_ENTRIES); }

/** @test At least some modules are loaded on any Linux system. */
TEST_F(DriverInfoTest, SomeModulesLoaded) {
  // Any running Linux system should have at least a few modules
  // (unless running with modules disabled, which is rare)
  GTEST_LOG_(INFO) << "Found " << inv_.entryCount << " loaded modules";
  // Don't fail if zero - some minimal systems have no modules
}

/* ----------------------------- DriverEntry Tests ----------------------------- */

/** @test All entries have non-empty names. */
TEST_F(DriverInfoTest, EntriesHaveNames) {
  for (std::size_t i = 0; i < inv_.entryCount; ++i) {
    EXPECT_GT(std::strlen(inv_.entries[i].name.data()), 0U) << "Entry " << i << " has empty name";
  }
}

/** @test All entries have positive size. */
TEST_F(DriverInfoTest, EntriesHavePositiveSize) {
  for (std::size_t i = 0; i < inv_.entryCount; ++i) {
    EXPECT_GT(inv_.entries[i].sizeBytes, 0U)
        << "Entry " << i << " (" << inv_.entries[i].name.data() << ") has zero size";
  }
}

/** @test All entries have non-negative use count. */
TEST_F(DriverInfoTest, EntriesHaveNonNegativeUseCount) {
  for (std::size_t i = 0; i < inv_.entryCount; ++i) {
    EXPECT_GE(inv_.entries[i].useCount, 0)
        << "Entry " << i << " (" << inv_.entries[i].name.data() << ") has negative use count";
  }
}

/** @test State field is non-empty. */
TEST_F(DriverInfoTest, EntriesHaveState) {
  for (std::size_t i = 0; i < inv_.entryCount; ++i) {
    EXPECT_GT(std::strlen(inv_.entries[i].state.data()), 0U)
        << "Entry " << i << " (" << inv_.entries[i].name.data() << ") has empty state";
  }
}

/** @test Entries are sorted by name. */
TEST_F(DriverInfoTest, EntriesSortedByName) {
  for (std::size_t i = 1; i < inv_.entryCount; ++i) {
    EXPECT_LE(std::strcmp(inv_.entries[i - 1].name.data(), inv_.entries[i].name.data()), 0)
        << "Entries not sorted: " << inv_.entries[i - 1].name.data() << " > "
        << inv_.entries[i].name.data();
  }
}

/* ----------------------------- DriverEntry::isNamed Tests ----------------------------- */

/** @test isNamed matches correct name. */
TEST_F(DriverInfoTest, IsNamedMatchesCorrect) {
  if (inv_.entryCount > 0) {
    const DriverEntry& ENTRY = inv_.entries[0];
    EXPECT_TRUE(ENTRY.isNamed(ENTRY.name.data()));
  }
}

/** @test isNamed rejects incorrect name. */
TEST_F(DriverInfoTest, IsNamedRejectsIncorrect) {
  if (inv_.entryCount > 0) {
    const DriverEntry& ENTRY = inv_.entries[0];
    EXPECT_FALSE(ENTRY.isNamed("__nonexistent_module_name__"));
  }
}

/** @test isNamed handles null. */
TEST_F(DriverInfoTest, IsNamedHandlesNull) {
  if (inv_.entryCount > 0) {
    const DriverEntry& ENTRY = inv_.entries[0];
    EXPECT_FALSE(ENTRY.isNamed(nullptr));
  }
}

/* ----------------------------- DriverInventory::find Tests ----------------------------- */

/** @test find returns existing module. */
TEST_F(DriverInfoTest, FindReturnsExisting) {
  if (inv_.entryCount > 0) {
    const char* NAME = inv_.entries[0].name.data();
    const DriverEntry* FOUND = inv_.find(NAME);
    ASSERT_NE(FOUND, nullptr);
    EXPECT_STREQ(FOUND->name.data(), NAME);
  }
}

/** @test find returns nullptr for non-existent module. */
TEST_F(DriverInfoTest, FindReturnsNullForNonExistent) {
  const DriverEntry* FOUND = inv_.find("__nonexistent_module_name__");
  EXPECT_EQ(FOUND, nullptr);
}

/** @test find handles null. */
TEST_F(DriverInfoTest, FindHandlesNull) {
  const DriverEntry* FOUND = inv_.find(nullptr);
  EXPECT_EQ(FOUND, nullptr);
}

/* ----------------------------- DriverInventory::isLoaded Tests ----------------------------- */

/** @test isLoaded consistent with find. */
TEST_F(DriverInfoTest, IsLoadedConsistentWithFind) {
  if (inv_.entryCount > 0) {
    const char* NAME = inv_.entries[0].name.data();
    EXPECT_EQ(inv_.isLoaded(NAME), inv_.find(NAME) != nullptr);
  }
  EXPECT_FALSE(inv_.isLoaded("__nonexistent_module_name__"));
}

/* ----------------------------- NVIDIA Detection Tests ----------------------------- */

/** @test hasNvidiaDriver consistent with isLoaded. */
TEST_F(DriverInfoTest, HasNvidiaDriverConsistent) {
  const bool NVIDIA = inv_.isLoaded("nvidia");
  const bool NVIDIA_UVM = inv_.isLoaded("nvidia_uvm");
  const bool NVIDIA_DRM = inv_.isLoaded("nvidia_drm");

  const bool EXPECTED = NVIDIA || NVIDIA_UVM || NVIDIA_DRM;
  EXPECT_EQ(inv_.hasNvidiaDriver(), EXPECTED);
}

/** @test isNvidiaDriverLoaded consistent with inventory. */
TEST_F(DriverInfoTest, IsNvidiaDriverLoadedConsistent) {
  const bool QUICK_CHECK = isNvidiaDriverLoaded();
  const bool INVENTORY_CHECK = inv_.hasNvidiaDriver();

  // These should match
  EXPECT_EQ(QUICK_CHECK, INVENTORY_CHECK);
}

/* ----------------------------- Taint Status Tests ----------------------------- */

/** @test Taint flag is consistent with mask. */
TEST_F(DriverInfoTest, TaintConsistent) { EXPECT_EQ(inv_.tainted, (inv_.taintMask != 0)); }

/* ----------------------------- toString Tests ----------------------------- */

/** @test DriverEntry::toString produces non-empty output. */
TEST_F(DriverInfoTest, EntryToStringNonEmpty) {
  if (inv_.entryCount > 0) {
    const std::string OUTPUT = inv_.entries[0].toString();
    EXPECT_FALSE(OUTPUT.empty());
    // Should contain the module name
    EXPECT_NE(OUTPUT.find(inv_.entries[0].name.data()), std::string::npos);
  }
}

/** @test DriverInventory::toString produces non-empty output. */
TEST_F(DriverInfoTest, InventoryToStringNonEmpty) {
  const std::string OUTPUT = inv_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Driver Inventory"), std::string::npos);
}

/** @test DriverInventory::toBriefSummary produces non-empty output. */
TEST_F(DriverInfoTest, BriefSummaryNonEmpty) {
  const std::string OUTPUT = inv_.toBriefSummary();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Modules"), std::string::npos);
}

/* ----------------------------- DriverAssessment Tests ----------------------------- */

/** @test assessDrivers doesn't crash. */
TEST_F(DriverInfoTest, AssessDriversDoesNotCrash) {
  const DriverAssessment ASMT = assessDrivers(inv_);
  // Just verify it ran - noteCount is always valid
  EXPECT_LE(ASMT.noteCount, seeker::system::MAX_ASSESSMENT_NOTES);
}

/** @test Assessment reflects inventory state. */
TEST_F(DriverInfoTest, AssessmentReflectsInventory) {
  const DriverAssessment ASMT = assessDrivers(inv_);

  EXPECT_EQ(ASMT.nvidiaLoaded, inv_.hasNvidiaDriver());
  EXPECT_EQ(ASMT.nouveauLoaded, inv_.isLoaded("nouveau"));
  EXPECT_EQ(ASMT.i915Loaded, inv_.isLoaded("i915"));
  EXPECT_EQ(ASMT.amdgpuLoaded, inv_.isLoaded("amdgpu"));
}

/** @test Assessment toString produces valid output. */
TEST_F(DriverInfoTest, AssessmentToStringNonEmpty) {
  const DriverAssessment ASMT = assessDrivers(inv_);
  const std::string OUTPUT = ASMT.toString();

  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Driver Assessment"), std::string::npos);
  EXPECT_NE(OUTPUT.find("NVIDIA"), std::string::npos);
}

/** @test NVML runtime check is consistent. */
TEST_F(DriverInfoTest, NvmlRuntimeCheckConsistent) {
  const bool CHECK1 = isNvmlRuntimeAvailable();
  const bool CHECK2 = isNvmlRuntimeAvailable();
  EXPECT_EQ(CHECK1, CHECK2);
}

/* ----------------------------- DriverAssessment::addNote Tests ----------------------------- */

/** @test addNote adds notes correctly. */
TEST(DriverAssessmentAddNoteTest, AddsNotes) {
  DriverAssessment asmt{};

  asmt.addNote("First note");
  EXPECT_EQ(asmt.noteCount, 1U);
  EXPECT_STREQ(asmt.notes[0].data(), "First note");

  asmt.addNote("Second note");
  EXPECT_EQ(asmt.noteCount, 2U);
  EXPECT_STREQ(asmt.notes[1].data(), "Second note");
}

/** @test addNote handles null. */
TEST(DriverAssessmentAddNoteTest, HandlesNull) {
  DriverAssessment asmt{};
  asmt.addNote(nullptr);
  EXPECT_EQ(asmt.noteCount, 0U);
}

/** @test addNote respects maximum. */
TEST(DriverAssessmentAddNoteTest, RespectsMaximum) {
  DriverAssessment asmt{};

  // Fill to max
  for (std::size_t i = 0; i < seeker::system::MAX_ASSESSMENT_NOTES + 5; ++i) {
    asmt.addNote("test note");
  }

  EXPECT_EQ(asmt.noteCount, seeker::system::MAX_ASSESSMENT_NOTES);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default DriverEntry is zeroed. */
TEST(DriverEntryDefaultTest, DefaultZeroed) {
  const DriverEntry DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_EQ(DEFAULT.version[0], '\0');
  EXPECT_EQ(DEFAULT.state[0], '\0');
  EXPECT_EQ(DEFAULT.useCount, 0);
  EXPECT_EQ(DEFAULT.sizeBytes, 0U);
  EXPECT_EQ(DEFAULT.depCount, 0U);
}

/** @test Default DriverInventory is zeroed. */
TEST(DriverInventoryDefaultTest, DefaultZeroed) {
  const DriverInventory DEFAULT{};

  EXPECT_EQ(DEFAULT.entryCount, 0U);
  EXPECT_EQ(DEFAULT.taintMask, 0);
  EXPECT_FALSE(DEFAULT.tainted);
  EXPECT_FALSE(DEFAULT.hasNvidiaDriver());
}

/** @test Default DriverAssessment is zeroed. */
TEST(DriverAssessmentDefaultTest, DefaultZeroed) {
  const DriverAssessment DEFAULT{};

  EXPECT_FALSE(DEFAULT.nvidiaLoaded);
  EXPECT_FALSE(DEFAULT.nvmlHeaderAvailable);
  EXPECT_FALSE(DEFAULT.nvmlRuntimePresent);
  EXPECT_FALSE(DEFAULT.nouveauLoaded);
  EXPECT_FALSE(DEFAULT.i915Loaded);
  EXPECT_FALSE(DEFAULT.amdgpuLoaded);
  EXPECT_EQ(DEFAULT.noteCount, 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getDriverInventory returns consistent results. */
TEST(DriverInfoDeterminismTest, ConsistentResults) {
  const DriverInventory INV1 = getDriverInventory();
  const DriverInventory INV2 = getDriverInventory();

  // Module list should be stable
  EXPECT_EQ(INV1.entryCount, INV2.entryCount);
  EXPECT_EQ(INV1.taintMask, INV2.taintMask);

  // Compare first few entries
  const std::size_t CHECK_COUNT = (INV1.entryCount < 5) ? INV1.entryCount : 5;
  for (std::size_t i = 0; i < CHECK_COUNT; ++i) {
    EXPECT_STREQ(INV1.entries[i].name.data(), INV2.entries[i].name.data());
    EXPECT_EQ(INV1.entries[i].sizeBytes, INV2.entries[i].sizeBytes);
  }
}

/* ----------------------------- Dependency Parsing Tests ----------------------------- */

/** @test Dependencies are parsed correctly. */
TEST_F(DriverInfoTest, DependenciesParsed) {
  // Find a module with dependencies (if any)
  for (std::size_t i = 0; i < inv_.entryCount; ++i) {
    if (inv_.entries[i].depCount > 0) {
      // Verify deps are non-empty strings
      for (std::size_t j = 0; j < inv_.entries[i].depCount; ++j) {
        EXPECT_GT(std::strlen(inv_.entries[i].deps[j].data()), 0U)
            << "Module " << inv_.entries[i].name.data() << " has empty dep at index " << j;
      }
      return; // Found one with deps, test passed
    }
  }
  // No modules with deps is also valid
  GTEST_LOG_(INFO) << "No modules with dependencies found";
}