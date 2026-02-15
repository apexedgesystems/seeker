# ==============================================================================
# Unit Tests: seeker_device (5 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CanBusInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GpioInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/I2cBusInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SerialPortInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpiBusInfo_uTest.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerDevice
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_device
  fmt::fmt
  LABELS
  diagnostics
  device
)
