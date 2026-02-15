# ==============================================================================
# Module: seeker_system
#
# System diagnostics: kernel info, capabilities, process limits, container
# limits, drivers, watchdog, virtualization, RT scheduler, IPC, security,
# file descriptors.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_system)

# ------------------------------------------------------------------------------
# Sources (11 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/CapabilityStatus.cpp"
    "${SRC_DIR}/ContainerLimits.cpp"
    "${SRC_DIR}/DriverInfo.cpp"
    "${SRC_DIR}/KernelInfo.cpp"
    "${SRC_DIR}/ProcessLimits.cpp"
    "${SRC_DIR}/WatchdogStatus.cpp"
    "${SRC_DIR}/VirtualizationInfo.cpp"
    "${SRC_DIR}/RtSchedConfig.cpp"
    "${SRC_DIR}/IpcStatus.cpp"
    "${SRC_DIR}/SecurityStatus.cpp"
    "${SRC_DIR}/FileDescriptorStatus.cpp"
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

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/system")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
