# ==============================================================================
# Module: seeker_device
#
# Device diagnostics: CAN bus, GPIO, I2C, serial ports, SPI.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_device)

# ------------------------------------------------------------------------------
# Sources (5 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/CanBusInfo.cpp" "${SRC_DIR}/GpioInfo.cpp" "${SRC_DIR}/I2cBusInfo.cpp"
                "${SRC_DIR}/SerialPortInfo.cpp" "${SRC_DIR}/SpiBusInfo.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

seeker_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  seeker_helpers
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

seeker_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Production documentation (release builds only)
# ------------------------------------------------------------------------------

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/device")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
