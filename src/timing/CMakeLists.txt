# ==============================================================================
# Module: seeker_timing
#
# Timing diagnostics: clock sources, timer config, time sync, latency
# benchmarks, PTP status, RTC status.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_timing)

# ------------------------------------------------------------------------------
# Sources (6 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/ClockSource.cpp" "${SRC_DIR}/TimerConfig.cpp" "${SRC_DIR}/TimeSyncStatus.cpp"
    "${SRC_DIR}/LatencyBench.cpp" "${SRC_DIR}/PtpStatus.cpp" "${SRC_DIR}/RtcStatus.cpp"
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

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/timing")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
