# ==============================================================================
# Unit Tests: seeker_memory (6 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PageSizes_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MemoryStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/NumaTopology_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HugepageStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MemoryLocking_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EdacStatus_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerMemory
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_memory
  fmt::fmt
  LABELS
  diagnostics
  memory
)
