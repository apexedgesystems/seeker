# ==============================================================================
# Module: seeker_cpu
#
# CPU diagnostics: topology, frequency, utilization, idle states, IRQ,
# softirq, thermal, features, affinity, isolation.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_cpu)

# ------------------------------------------------------------------------------
# Sources (11 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/Affinity.cpp"
    "${SRC_DIR}/CpuFeatures.cpp"
    "${SRC_DIR}/CpuFreq.cpp"
    "${SRC_DIR}/CpuTopology.cpp"
    "${SRC_DIR}/CpuStats.cpp"
    "${SRC_DIR}/ThermalStatus.cpp"
    "${SRC_DIR}/CpuUtilization.cpp"
    "${SRC_DIR}/IrqStats.cpp"
    "${SRC_DIR}/CpuIdle.cpp"
    "${SRC_DIR}/SoftirqStats.cpp"
    "${SRC_DIR}/CpuIsolation.cpp"
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

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/cpu")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
