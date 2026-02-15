# ==============================================================================
# Module: seeker_gpu
#
# GPU diagnostics: topology, memory, telemetry, driver, PCIe, isolation,
# benchmarks. NVIDIA CUDA/NVML support with sysfs fallback for AMD/Intel.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_gpu)

# ------------------------------------------------------------------------------
# Sources - CPU Side (6 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/GpuTopology.cpp" "${SRC_DIR}/GpuMemoryStatus.cpp" "${SRC_DIR}/GpuTelemetry.cpp"
    "${SRC_DIR}/GpuDriverStatus.cpp" "${SRC_DIR}/PcieStatus.cpp" "${SRC_DIR}/GpuIsolation.cpp"
)

# ------------------------------------------------------------------------------
# Sources - CUDA Side (1 module)
# ------------------------------------------------------------------------------

set(CUDA_SOURCES "${SRC_DIR}/GpuBench.cu")

# ------------------------------------------------------------------------------
# Libraries
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
seeker_nvml_enable(${LIB_NAME})

seeker_add_library_cuda(
  NAME
  ${LIB_NAME}_cuda
  CORE
  ${LIB_NAME}
  SRC
  ${CUDA_SOURCES}
  DEPS_PUBLIC
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

seeker_standard_optins(${LIB_NAME})
seeker_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Production documentation (release builds only)
# ------------------------------------------------------------------------------

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/gpu")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
