# ==============================================================================
# Unit Tests: seeker_gpu (7 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuTopology_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuMemoryStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuTelemetry_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuDriverStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PcieStatus_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuIsolation_uTest.cpp"
)

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/GpuBench_uTest.cu")

seeker_add_gtest(
  TARGET
  TestSeekerGpu
  SOURCES
  ${UNIT_TEST_SOURCES}
  CUDA
  ${CUDA_TEST_SOURCES}
  LINK
  seeker_gpu
  seeker_gpu_cuda
  fmt::fmt
  LABELS
  diagnostics
  gpu
)
