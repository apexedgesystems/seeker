# ==============================================================================
# Module: seeker_storage
#
# Storage diagnostics: block devices, mount info, I/O scheduler, I/O stats,
# storage benchmarks.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_storage)

# ------------------------------------------------------------------------------
# Sources (5 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/BlockDeviceInfo.cpp" "${SRC_DIR}/MountInfo.cpp"
                "${SRC_DIR}/IoScheduler.cpp" "${SRC_DIR}/IoStats.cpp" "${SRC_DIR}/StorageBench.cpp"
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

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/storage")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
