# ==============================================================================
# Tools: Diagnostics
#
# CLI tools for system diagnostics. All tools output to bin/tools/.
# Each tool links its domain library, seeker_helpers (arg parsing), and fmt.
# ==============================================================================

include(seeker/All)

# ------------------------------------------------------------------------------
# Helper Function
# ------------------------------------------------------------------------------
# add_diag_tool(<tool-name> <domain>)
#
# Creates a diagnostic tool with standard configuration:
#   - Source: <domain>/<tool-name>.cpp
#   - Links:  seeker_<domain>, seeker_helpers, fmt::fmt
# ------------------------------------------------------------------------------

function (add_diag_tool TOOL_NAME DOMAIN)
  seeker_add_tool(
    NAME
    ${TOOL_NAME}
    SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/${DOMAIN}/${TOOL_NAME}.cpp"
    LINK
    seeker_${DOMAIN}
    seeker_helpers
    fmt::fmt
  )
endfunction ()

# ------------------------------------------------------------------------------
# CPU Tools (7)
# ------------------------------------------------------------------------------

add_diag_tool(cpu-info cpu)
add_diag_tool(cpu-rtcheck cpu)
add_diag_tool(cpu-affinity cpu)
add_diag_tool(cpu-corestat cpu)
add_diag_tool(cpu-irqmap cpu)
add_diag_tool(cpu-snapshot cpu)
add_diag_tool(cpu-thermal cpu)

# ------------------------------------------------------------------------------
# Memory Tools (3)
# ------------------------------------------------------------------------------

add_diag_tool(mem-info memory)
add_diag_tool(mem-rtcheck memory)
add_diag_tool(mem-numa memory)

# ------------------------------------------------------------------------------
# Storage Tools (4)
# ------------------------------------------------------------------------------

add_diag_tool(storage-info storage)
add_diag_tool(storage-bench storage)
add_diag_tool(storage-iostat storage)
add_diag_tool(storage-rtcheck storage)

# ------------------------------------------------------------------------------
# Network Tools (3)
# ------------------------------------------------------------------------------

add_diag_tool(net-info network)
add_diag_tool(net-rtcheck network)
add_diag_tool(net-stat network)

# ------------------------------------------------------------------------------
# Timing Tools (4)
# ------------------------------------------------------------------------------

add_diag_tool(timing-info timing)
add_diag_tool(timing-rtcheck timing)
add_diag_tool(timing-bench timing)
add_diag_tool(timing-sync timing)

# ------------------------------------------------------------------------------
# System Tools (4)
# ------------------------------------------------------------------------------

add_diag_tool(sys-info system)
add_diag_tool(sys-rtcheck system)
add_diag_tool(sys-drivers system)
add_diag_tool(sys-limits system)

# ------------------------------------------------------------------------------
# Device Tools (5)
# ------------------------------------------------------------------------------

add_diag_tool(device-info device)
add_diag_tool(device-rtcheck device)
add_diag_tool(device-serial device)
add_diag_tool(device-i2c device)
add_diag_tool(device-can device)

# ------------------------------------------------------------------------------
# GPU Tools (4)
# ------------------------------------------------------------------------------

# Standard GPU tools (C++ only)
function (add_gpu_tool TOOL_NAME)
  seeker_add_tool(
    NAME
    ${TOOL_NAME}
    SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/${TOOL_NAME}.cpp"
    LINK
    seeker_gpu
    seeker_helpers
    fmt::fmt
  )
endfunction ()

add_gpu_tool(gpu-info)
add_gpu_tool(gpu-stat)
add_gpu_tool(gpu-rtcheck)

# GPU benchmark tool (CUDA) - only built when CUDA is available
if (CMAKE_CUDA_COMPILER)
  seeker_add_tool(
    NAME
    gpu-bench
    SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/gpu-bench.cu"
    LINK
    seeker_gpu
    seeker_gpu_cuda
    seeker_helpers
    fmt::fmt
  )
  set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/gpu-bench.cu" PROPERTIES LANGUAGE CUDA
  )
endif ()

# ------------------------------------------------------------------------------
# Production documentation (release builds only)
# ------------------------------------------------------------------------------

foreach (
  _domain
  cpu
  memory
  storage
  network
  timing
  system
  device
  gpu
)
  seeker_install_docs(
    "${CMAKE_CURRENT_SOURCE_DIR}/${_domain}/README.md" DESTINATION "tools/${_domain}"
  )
endforeach ()

# ------------------------------------------------------------------------------
# Finalize (aggregate cpp_tools target)
# ------------------------------------------------------------------------------

seeker_finalize_cpp_tools()
