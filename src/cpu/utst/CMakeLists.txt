# ==============================================================================
# Unit Tests: seeker_cpu (11 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Affinity_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuFeatures_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuFreq_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuTopology_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThermalStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuUtilization_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IrqStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuIdle_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SoftirqStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuIsolation_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerCpu
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_cpu
  fmt::fmt
  LABELS
  diagnostics
  cpu
  TIMING_PATTERNS
  "CpuIdleDelta.*"
  "CpuUtilization.*"
)
