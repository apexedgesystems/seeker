# ==============================================================================
# Unit Tests: seeker_storage (5 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BlockDeviceInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MountInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IoScheduler_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IoStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StorageBench_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerStorage
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_storage
  fmt::fmt
  LABELS
  diagnostics
  storage
)
