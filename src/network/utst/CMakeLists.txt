# ==============================================================================
# Unit Tests: seeker_network (6 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/InterfaceInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/InterfaceStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SocketBufferConfig_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/NetworkIsolation_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoopbackBench_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EthtoolInfo_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerNetwork
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_network
  fmt::fmt
  LABELS
  diagnostics
  network
)
