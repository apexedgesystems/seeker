# ==============================================================================
# Tools: Diagnostics
# ==============================================================================

include(seeker/All)

# ------------------------------------------------------------------------------
# Helper Function
# ------------------------------------------------------------------------------

function (add_diag_tool TOOL_NAME DOMAIN)
  seeker_add_tool(
    NAME ${TOOL_NAME}
    SRC "${CMAKE_CURRENT_SOURCE_DIR}/${DOMAIN}/${TOOL_NAME}.cpp"
    LINK seeker_${DOMAIN} seeker_helpers fmt::fmt
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

add_diag_tool(gpu-info gpu)
add_diag_tool(gpu-stat gpu)
add_diag_tool(gpu-rtcheck gpu)

seeker_add_tool(
  NAME gpu-bench
  SRC "${CMAKE_CURRENT_SOURCE_DIR}/gpu/gpu-bench.cu"
  LINK seeker_gpu seeker_gpu_cuda seeker_helpers fmt::fmt
  REQUIRES CUDA
)

# ------------------------------------------------------------------------------
# Documentation
# ------------------------------------------------------------------------------

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/cpu/README.md" DESTINATION "tools/cpu")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/memory/README.md" DESTINATION "tools/memory")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/storage/README.md" DESTINATION "tools/storage")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/network/README.md" DESTINATION "tools/network")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/timing/README.md" DESTINATION "tools/timing")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/system/README.md" DESTINATION "tools/system")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/device/README.md" DESTINATION "tools/device")
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/gpu/README.md" DESTINATION "tools/gpu")

# ------------------------------------------------------------------------------
# Aggregate Target
# ------------------------------------------------------------------------------

seeker_finalize_cpp_tools()
