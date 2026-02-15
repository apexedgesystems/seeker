# ==============================================================================
# Module: seeker_network
#
# Network diagnostics: interface info/stats, socket buffers, ethtool,
# network isolation, loopback benchmarks.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_network)

# ------------------------------------------------------------------------------
# Sources (6 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/InterfaceInfo.cpp" "${SRC_DIR}/InterfaceStats.cpp"
    "${SRC_DIR}/SocketBufferConfig.cpp" "${SRC_DIR}/NetworkIsolation.cpp"
    "${SRC_DIR}/LoopbackBench.cpp" "${SRC_DIR}/EthtoolInfo.cpp"
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

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/network")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
