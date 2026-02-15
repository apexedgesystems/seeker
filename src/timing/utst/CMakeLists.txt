# ==============================================================================
# Unit Tests: seeker_timing (6 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ClockSource_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TimerConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TimeSyncStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LatencyBench_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PtpStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RtcStatus_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerTiming
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_timing
  fmt::fmt
  LABELS
  diagnostics
  timing
)
