# ==============================================================================
# seeker/Targets.cmake - Target factory functions
# ==============================================================================

include_guard(GLOBAL)
include(GNUInstallDirs)
include(seeker/Core)

option(SEEKER_TARGETS_VERBOSE "Print per-target creation details" OFF)

# ------------------------------------------------------------------------------
# Platform capability detection
# ------------------------------------------------------------------------------

# SEEKER_PLATFORM_BAREMETAL: True when targeting bare-metal (no OS)
# Set as CACHE variable so it's available in all scopes
if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
  set(SEEKER_PLATFORM_BAREMETAL
      TRUE
      CACHE INTERNAL "Bare-metal platform detected"
  )
  message(STATUS "[seeker] Detected bare-metal platform (CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})")
else ()
  set(SEEKER_PLATFORM_BAREMETAL
      FALSE
      CACHE INTERNAL "Not a bare-metal platform"
  )
endif ()

# ------------------------------------------------------------------------------
# _seeker_platform_supports(<out_var> <helper> <target> <baremetal_ok>
#                           [<requirement>...])
#
# Set <out_var> to TRUE when <target> is built on this platform. A bare-metal
# build (CMAKE_SYSTEM_NAME Generic) skips it unless <baremetal_ok> is true, and
# a requirement that does not hold skips it; either skip is recorded with
# _seeker_skip. An unknown requirement fails the configure on every platform.
#
# Requirements:
#   POSIX     hosted (not bare-metal) platform
#   PTHREADS  hosted platform (POSIX threads)
#   LINUX     hosted platform with CMAKE_SYSTEM_NAME Linux
#   CUDA      CUDA toolkit found and the CUDA language enabled
# ------------------------------------------------------------------------------
function (_seeker_platform_supports _out_var _helper _target _baremetal_ok)
  set(_missing "")
  foreach (_req IN LISTS ARGN)
    if (_req STREQUAL "POSIX" OR _req STREQUAL "PTHREADS")
      set(_met TRUE)
      if (SEEKER_PLATFORM_BAREMETAL)
        set(_met FALSE)
      endif ()
    elseif (_req STREQUAL "LINUX")
      set(_met FALSE)
      if (NOT SEEKER_PLATFORM_BAREMETAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_met TRUE)
      endif ()
    elseif (_req STREQUAL "CUDA")
      set(_met FALSE)
      if (CUDAToolkit_FOUND AND CMAKE_CUDA_COMPILER)
        set(_met TRUE)
      endif ()
    else ()
      message(FATAL_ERROR "${_helper}: unknown requirement '${_req}' for '${_target}' "
                          "(supported: POSIX PTHREADS LINUX CUDA)"
      )
    endif ()
    if (NOT _met)
      list(APPEND _missing ${_req})
    endif ()
  endforeach ()

  set(_supported TRUE)
  if (SEEKER_PLATFORM_BAREMETAL AND NOT _baremetal_ok)
    _seeker_skip(${_target} "bare-metal, no BAREMETAL flag")
    set(_supported FALSE)
  elseif (_missing)
    list(JOIN _missing ", " _missing_str)
    _seeker_skip(${_target} "missing: ${_missing_str}")
    set(_supported FALSE)
  endif ()

  set(${_out_var}
      ${_supported}
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_interface_library(...)
#
# Header-only library: an INTERFACE target and its seeker:: alias, carrying the
# include directory, dependencies, definitions and features to its consumers.
# The target and the headers under INC are installed and exported in
# seekerTargets unless NO_INSTALL is given. Skipped on bare-metal builds unless
# BAREMETAL is given, and when a requirement does not hold.
#
# Arguments:
#   NAME            <target>           required
#   INC             <include_dir>      required
#   DEPS_INTERFACE  <targets...>       optional, linked INTERFACE
#   DEFS            <defs...>          optional, INTERFACE compile definitions
#   FEATURES        <features...>      optional, INTERFACE compile features
#   REQUIRES        <requirements...>  optional (POSIX, PTHREADS, LINUX, CUDA)
#   BAREMETAL                          optional flag: also built on bare-metal
#   NO_INSTALL                         optional flag: neither installed nor
#                                      exported (test support, build-only)
# ------------------------------------------------------------------------------
function (seeker_add_interface_library)
  cmake_parse_arguments(
    IL "BAREMETAL;NO_INSTALL" "NAME;INC" "DEPS_INTERFACE;DEFS;FEATURES;REQUIRES" ${ARGN}
  )
  _seeker_check_arguments(seeker_add_interface_library IL REQUIRED NAME INC)
  _seeker_platform_supports(
    _supported seeker_add_interface_library ${IL_NAME} ${IL_BAREMETAL} ${IL_REQUIRES}
  )
  if (NOT _supported)
    return()
  endif ()

  add_library(${IL_NAME} INTERFACE)
  add_library(seeker::${IL_NAME} ALIAS ${IL_NAME})

  target_include_directories(
    ${IL_NAME} INTERFACE $<BUILD_INTERFACE:${IL_INC}>
                         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  if (IL_DEPS_INTERFACE)
    target_link_libraries(${IL_NAME} INTERFACE ${IL_DEPS_INTERFACE})
  endif ()

  if (IL_DEFS)
    target_compile_definitions(${IL_NAME} INTERFACE ${IL_DEFS})
  endif ()

  if (IL_FEATURES)
    target_compile_features(${IL_NAME} INTERFACE ${IL_FEATURES})
  endif ()

  if (NOT IL_NO_INSTALL)
    install(TARGETS ${IL_NAME} EXPORT seekerTargets)
    install(DIRECTORY "${IL_INC}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  endif ()

  if (SEEKER_TARGETS_VERBOSE)
    message(STATUS "[seeker] INTERFACE ${IL_NAME} inc='${IL_INC}' deps='${IL_DEPS_INTERFACE}'")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_library(...)
#
# Compiled library and its seeker:: alias. A shared library gets VERSION and
# SOVERSION from the project version. The library's public include path is INC
# plus the project root (build tree) or the install include directory; the
# target and the headers under INC are installed and exported in seekerTargets.
# Skipped on bare-metal builds unless BAREMETAL is given, and when a
# requirement does not hold; SHARED becomes STATIC on bare-metal.
#
# Arguments:
#   NAME         <target>           required
#   TYPE         STATIC|SHARED      required
#   INC          <include_dir>      required, public include directory
#   SRC          <files...>         required
#   DEPS_PUBLIC  <targets...>       optional, linked PUBLIC
#   DEPS_PRIVATE <targets...>       optional, linked PRIVATE
#   REQUIRES     <requirements...>  optional (POSIX, PTHREADS, LINUX, CUDA)
#   BAREMETAL                       optional flag: also built on bare-metal
# ------------------------------------------------------------------------------
function (seeker_add_library)
  cmake_parse_arguments(
    AL "BAREMETAL" "NAME;TYPE;INC" "SRC;DEPS_PUBLIC;DEPS_PRIVATE;REQUIRES" ${ARGN}
  )
  _seeker_check_arguments(
    seeker_add_library
    AL
    REQUIRED
    NAME
    TYPE
    INC
    SRC
  )
  if (NOT AL_TYPE MATCHES "^(STATIC|SHARED)$")
    message(FATAL_ERROR "seeker_add_library: TYPE must be STATIC or SHARED, not '${AL_TYPE}'")
  endif ()
  _seeker_platform_supports(_supported seeker_add_library ${AL_NAME} ${AL_BAREMETAL} ${AL_REQUIRES})
  if (NOT _supported)
    return()
  endif ()

  # Bare-metal: force STATIC (shared libraries not supported on freestanding)
  set(_type ${AL_TYPE})
  if (SEEKER_PLATFORM_BAREMETAL AND _type STREQUAL "SHARED")
    set(_type STATIC)
  endif ()

  add_library(${AL_NAME} ${_type})
  add_library(seeker::${AL_NAME} ALIAS ${AL_NAME})

  if (_type STREQUAL "SHARED")
    set_target_properties(
      ${AL_NAME} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    )
  endif ()

  target_sources(${AL_NAME} PRIVATE ${AL_SRC})
  target_include_directories(
    ${AL_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}> $<BUILD_INTERFACE:${AL_INC}>
                      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  if (AL_DEPS_PUBLIC)
    target_link_libraries(${AL_NAME} PUBLIC ${AL_DEPS_PUBLIC})
  endif ()

  if (AL_DEPS_PRIVATE)
    target_link_libraries(${AL_NAME} PRIVATE ${AL_DEPS_PRIVATE})
  endif ()

  install(
    TARGETS ${AL_NAME}
    EXPORT seekerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY "${AL_INC}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  if (SEEKER_TARGETS_VERBOSE)
    list(LENGTH AL_SRC _src_count)
    message(STATUS "[seeker] LIB ${AL_NAME} (${AL_TYPE}) inc='${AL_INC}' srcs=${_src_count}")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# _seeker_add_executable(...)
#
# Executable target shared by seeker_add_app and seeker_add_tool: sources,
# private includes, links, definitions and features, the output directory, a
# link-time search hint for the CUDA runtime, install to the binary directory
# unless NO_INSTALL, and a UPX copy when ENABLE_UPX.
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   OUTPUT_DIR  <dir>              required
#   KIND        APP|TOOL           required (verbose output)
#   LINK        <targets...>       optional, linked PRIVATE
#   INC         <dirs...>          optional, PRIVATE include directories
#   DEFS        <defs...>          optional, PRIVATE compile definitions
#   FEATURES    <features...>      optional, PRIVATE compile features
#   NO_INSTALL                     optional flag
#   ENABLE_UPX                     optional flag
# ------------------------------------------------------------------------------
function (_seeker_add_executable)
  cmake_parse_arguments(
    EXE "NO_INSTALL;ENABLE_UPX" "NAME;OUTPUT_DIR;KIND" "SRC;LINK;INC;DEFS;FEATURES" ${ARGN}
  )
  _seeker_check_arguments(
    _seeker_add_executable
    EXE
    REQUIRED
    NAME
    SRC
    OUTPUT_DIR
    KIND
  )

  add_executable(${EXE_NAME})
  target_sources(${EXE_NAME} PRIVATE ${EXE_SRC})

  # Linker hint for CUDA runtime resolution
  target_link_options(
    ${EXE_NAME} PRIVATE
    $<$<TARGET_EXISTS:CUDA::cudart>:-Wl,-rpath-link,$<TARGET_FILE_DIR:CUDA::cudart>>
  )

  if (EXE_INC)
    target_include_directories(${EXE_NAME} PRIVATE ${EXE_INC})
  endif ()

  if (EXE_LINK)
    target_link_libraries(${EXE_NAME} PRIVATE ${EXE_LINK})
  endif ()

  if (EXE_DEFS)
    target_compile_definitions(${EXE_NAME} PRIVATE ${EXE_DEFS})
  endif ()

  if (EXE_FEATURES)
    target_compile_features(${EXE_NAME} PRIVATE ${EXE_FEATURES})
  endif ()

  set_target_properties(${EXE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EXE_OUTPUT_DIR}")

  if (NOT EXE_NO_INSTALL)
    install(TARGETS ${EXE_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif ()

  if (EXE_ENABLE_UPX)
    seeker_add_upx_copy(${EXE_NAME})
  endif ()

  if (SEEKER_TARGETS_VERBOSE)
    list(LENGTH EXE_SRC _src_count)
    message(STATUS "[seeker] ${EXE_KIND} ${EXE_NAME} srcs=${_src_count} link='${EXE_LINK}'")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_app(...)
#
# Application executable. Outputs to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}, installs
# to the binary directory unless NO_INSTALL, and gets a UPX copy (ENABLE_UPX,
# release builds) unless NO_UPX. Skipped on bare-metal builds unless BAREMETAL
# is given, and when a requirement does not hold.
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   LINK        <targets...>       optional, linked PRIVATE
#   INC         <dirs...>          optional, PRIVATE include directories
#   DEFS        <defs...>          optional, PRIVATE compile definitions
#   FEATURES    <features...>      optional, PRIVATE compile features
#   REQUIRES    <requirements...>  optional (POSIX, PTHREADS, LINUX, CUDA)
#   BAREMETAL                      optional flag: also built on bare-metal
#   NO_INSTALL                     optional flag
#   NO_UPX                         optional flag: no UPX copy
# ------------------------------------------------------------------------------
function (seeker_add_app)
  cmake_parse_arguments(
    APP "NO_INSTALL;NO_UPX;BAREMETAL" "NAME" "SRC;LINK;INC;DEFS;FEATURES;REQUIRES" ${ARGN}
  )
  _seeker_check_arguments(seeker_add_app APP REQUIRED NAME SRC)
  _seeker_platform_supports(_supported seeker_add_app ${APP_NAME} ${APP_BAREMETAL} ${APP_REQUIRES})
  if (NOT _supported)
    return()
  endif ()

  set(_args
      NAME
      ${APP_NAME}
      SRC
      ${APP_SRC}
      OUTPUT_DIR
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      KIND
      "APP"
  )

  if (APP_LINK)
    list(APPEND _args LINK ${APP_LINK})
  endif ()
  if (APP_INC)
    list(APPEND _args INC ${APP_INC})
  endif ()
  if (APP_DEFS)
    list(APPEND _args DEFS ${APP_DEFS})
  endif ()
  if (APP_FEATURES)
    list(APPEND _args FEATURES ${APP_FEATURES})
  endif ()
  if (APP_NO_INSTALL)
    list(APPEND _args NO_INSTALL)
  endif ()
  if (NOT APP_NO_UPX)
    list(APPEND _args ENABLE_UPX)
  endif ()

  _seeker_add_executable(${_args})
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_tool(...)
#
# Command-line tool executable. Outputs to
# ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/cpp, installs to the binary directory
# unless NO_INSTALL, and joins the ${PROJECT_NAME}_cpp_tools aggregate target
# (seeker_finalize_cpp_tools). Sources compile in the language of their
# extension (.cu as CUDA). Skipped on bare-metal builds unless BAREMETAL is
# given, and when a requirement does not hold: a CUDA tool declares
# REQUIRES CUDA.
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   LINK        <targets...>       optional, linked PRIVATE
#   INC         <dirs...>          optional, PRIVATE include directories
#   DEFS        <defs...>          optional, PRIVATE compile definitions
#   FEATURES    <features...>      optional, PRIVATE compile features
#   REQUIRES    <requirements...>  optional (POSIX, PTHREADS, LINUX, CUDA)
#   BAREMETAL                      optional flag: also built on bare-metal
#   NO_INSTALL                     optional flag
# ------------------------------------------------------------------------------
function (seeker_add_tool)
  cmake_parse_arguments(
    TOOL "NO_INSTALL;BAREMETAL" "NAME" "SRC;LINK;INC;DEFS;FEATURES;REQUIRES" ${ARGN}
  )
  _seeker_check_arguments(seeker_add_tool TOOL REQUIRED NAME SRC)
  _seeker_platform_supports(
    _supported seeker_add_tool ${TOOL_NAME} ${TOOL_BAREMETAL} ${TOOL_REQUIRES}
  )
  if (NOT _supported)
    return()
  endif ()

  set(_args
      NAME
      ${TOOL_NAME}
      SRC
      ${TOOL_SRC}
      OUTPUT_DIR
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/cpp"
      KIND
      "TOOL"
  )

  if (TOOL_LINK)
    list(APPEND _args LINK ${TOOL_LINK})
  endif ()
  if (TOOL_INC)
    list(APPEND _args INC ${TOOL_INC})
  endif ()
  if (TOOL_DEFS)
    list(APPEND _args DEFS ${TOOL_DEFS})
  endif ()
  if (TOOL_FEATURES)
    list(APPEND _args FEATURES ${TOOL_FEATURES})
  endif ()
  if (TOOL_NO_INSTALL)
    list(APPEND _args NO_INSTALL)
  endif ()

  _seeker_add_executable(${_args})

  # Track tool for aggregate target
  set_property(GLOBAL APPEND PROPERTY SEEKER_CPP_TOOLS ${TOOL_NAME})
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_library_cuda(...)
#
# CUDA sidecar of a CPU library. When the CUDA toolkit is found and the CUDA
# language is enabled, a compiled library and its seeker:: alias: SRC compiled
# through seeker_cuda_sources, CORE linked PRIVATE, CUDA::cudart linked PUBLIC
# unless NO_CUDART, NVML enabled unless NO_NVML, installed and exported. Without
# CUDA, an INTERFACE shim of the same name that links CORE and DEPS_PUBLIC, so
# consumers declare the sidecar unconditionally. Skipped on bare-metal builds.
# Otherwise it is skipped when CORE was skipped, and a CORE that is neither a
# target nor a skipped one fails the configure.
#
# Arguments:
#   NAME                   <target>           required
#   SRC                    <files...>         required, CUDA sources
#   CORE                   <cpu_target>       optional, linked PRIVATE
#                                             (INTERFACE by the shim)
#   TYPE                   STATIC|SHARED      optional (default SHARED)
#   INC                    <include_dir>      optional, PRIVATE
#   DEPS_PUBLIC            <targets...>       optional, linked PUBLIC
#                                             (INTERFACE by the shim)
#   DEPS_PRIVATE           <targets...>       optional, linked PRIVATE
#   SEPARABLE                                 optional flag
#   RESOLVE_DEVICE_SYMBOLS                    optional flag
#   NO_CUDART                                 optional flag
#   NO_NVML                                   optional flag
# ------------------------------------------------------------------------------
function (seeker_add_library_cuda)
  cmake_parse_arguments(
    ACL "SEPARABLE;RESOLVE_DEVICE_SYMBOLS;NO_CUDART;NO_NVML" "NAME;CORE;TYPE;INC"
    "SRC;DEPS_PUBLIC;DEPS_PRIVATE" ${ARGN}
  )
  _seeker_check_arguments(seeker_add_library_cuda ACL REQUIRED NAME SRC)
  if (NOT DEFINED ACL_TYPE)
    set(ACL_TYPE "SHARED")
  elseif (NOT ACL_TYPE MATCHES "^(STATIC|SHARED)$")
    message(FATAL_ERROR "seeker_add_library_cuda: TYPE must be STATIC or SHARED, not '${ACL_TYPE}'")
  endif ()

  _seeker_platform_supports(_supported seeker_add_library_cuda ${ACL_NAME} FALSE)
  if (NOT _supported)
    return()
  endif ()

  if (DEFINED ACL_CORE)
    _seeker_target_exists(seeker_add_library_cuda "${ACL_CORE}" _core_exists)
    if (NOT _core_exists)
      _seeker_skip(${ACL_NAME} "CORE '${ACL_CORE}' skipped")
      return()
    endif ()
  endif ()

  # CUDA active when toolkit found AND language enabled
  set(_cuda_active FALSE)
  if (CUDAToolkit_FOUND AND CMAKE_CUDA_COMPILER)
    set(_cuda_active TRUE)
  endif ()

  if (_cuda_active)
    # Real CUDA library
    add_library(${ACL_NAME} ${ACL_TYPE})
    add_library(seeker::${ACL_NAME} ALIAS ${ACL_NAME})

    if (ACL_TYPE STREQUAL "SHARED")
      set_target_properties(
        ${ACL_NAME} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
      )
    endif ()

    if (ACL_INC)
      target_include_directories(${ACL_NAME} PRIVATE ${ACL_INC})
    endif ()

    if (ACL_CORE)
      target_link_libraries(${ACL_NAME} PRIVATE ${ACL_CORE})
    endif ()

    # Wire CUDA sources
    set(_cuda_args FILES ${ACL_SRC})
    if (ACL_SEPARABLE)
      list(APPEND _cuda_args SEPARABLE)
    endif ()
    if (ACL_RESOLVE_DEVICE_SYMBOLS)
      list(APPEND _cuda_args RESOLVE_DEVICE_SYMBOLS)
    endif ()
    if (ACL_NO_CUDART)
      list(APPEND _cuda_args NO_CUDART)
    endif ()
    seeker_cuda_sources(${ACL_NAME} ${_cuda_args})

    # Public cudart linkage for consumers
    if (NOT ACL_NO_CUDART AND TARGET CUDA::cudart)
      target_link_libraries(${ACL_NAME} PUBLIC CUDA::cudart)
      target_link_options(${ACL_NAME} INTERFACE -Wl,-rpath-link,$<TARGET_FILE_DIR:CUDA::cudart>)
    endif ()

    # NVML integration
    if (NOT ACL_NO_NVML)
      seeker_nvml_enable(${ACL_NAME})
    endif ()

    if (ACL_DEPS_PUBLIC)
      target_link_libraries(${ACL_NAME} PUBLIC ${ACL_DEPS_PUBLIC})
    endif ()

    if (ACL_DEPS_PRIVATE)
      target_link_libraries(${ACL_NAME} PRIVATE ${ACL_DEPS_PRIVATE})
    endif ()

    install(
      TARGETS ${ACL_NAME}
      EXPORT seekerTargets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    if (SEEKER_TARGETS_VERBOSE)
      list(LENGTH ACL_SRC _src_count)
      message(
        STATUS "[seeker] CUDA LIB ${ACL_NAME} (${ACL_TYPE}) core='${ACL_CORE}' srcs=${_src_count}"
      )
    endif ()

  else ()
    # INTERFACE shim when CUDA inactive
    add_library(${ACL_NAME} INTERFACE)
    add_library(seeker::${ACL_NAME} ALIAS ${ACL_NAME})

    if (ACL_CORE)
      target_link_libraries(${ACL_NAME} INTERFACE ${ACL_CORE})
    endif ()

    if (ACL_DEPS_PUBLIC)
      target_link_libraries(${ACL_NAME} INTERFACE ${ACL_DEPS_PUBLIC})
    endif ()

    install(TARGETS ${ACL_NAME} EXPORT seekerTargets)

    if (SEEKER_TARGETS_VERBOSE)
      message(STATUS "[seeker] CUDA SHIM ${ACL_NAME} (CUDA inactive) core='${ACL_CORE}'")
    endif ()
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# seeker_finalize_cpp_tools()
#
# Create the ${PROJECT_NAME}_cpp_tools aggregate target over every tool
# seeker_add_tool created; none when no tool was created. Call once, after the
# last seeker_add_tool.
# ------------------------------------------------------------------------------
function (seeker_finalize_cpp_tools)
  _seeker_reject_extra_arguments(seeker_finalize_cpp_tools ${ARGN})
  get_property(_tools GLOBAL PROPERTY SEEKER_CPP_TOOLS)
  if (_tools)
    add_custom_target(${PROJECT_NAME}_cpp_tools DEPENDS ${_tools})
    if (SEEKER_TARGETS_VERBOSE)
      list(LENGTH _tools _count)
      message(STATUS "[seeker] ${PROJECT_NAME}_cpp_tools aggregate target: ${_count} tools")
    endif ()
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# Export configuration (emitted once)
# ------------------------------------------------------------------------------
# Skip for bare-metal builds (no installable targets)
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
  install(
    EXPORT seekerTargets
    NAMESPACE seeker::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/seeker
  )
endif ()
