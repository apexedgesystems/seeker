# ==============================================================================
# Unit Tests: seeker_helpers (3 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Files_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ScratchDirectory_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ScratchTree_uTest.cpp"
)

seeker_add_gtest(
  TARGET TestSeekerHelpers
  SOURCES ${UNIT_TEST_SOURCES}
  LINK seeker_helpers seeker_test_support
  LABELS diagnostics helpers
)

# ------------------------------------------------------------------------------
# Test Support
# ------------------------------------------------------------------------------

seeker_add_interface_library(
  NAME seeker_scratch_tree
  INC "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE GTest::gtest
  NO_INSTALL
)
seeker_add_interface_library(
  NAME seeker_test_support
  INC "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE seeker_scratch_tree seeker_helpers
  NO_INSTALL
)
