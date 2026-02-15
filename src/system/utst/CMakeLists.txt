# ==============================================================================
# Unit Tests: seeker_system (11 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CapabilityStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ContainerLimits_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DriverInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/KernelInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ProcessLimits_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/WatchdogStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VirtualizationInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RtSchedConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IpcStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SecurityStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FileDescriptorStatus_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerSystem
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_system
  fmt::fmt
  LABELS
  diagnostics
  system
)
