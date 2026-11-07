# Seeker CMake Helpers

The `seeker_*` functions in this directory construct every Seeker target. The
`CMakeLists.txt` files under `src/` and `tools/` only declare targets through
them.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [The Rule](#2-the-rule)
3. [Model Files](#3-model-files)
4. [Adding a Target](#4-adding-a-target)
5. [Helper Reference](#5-helper-reference)
6. [Coverage](#6-coverage)
7. [The Check](#7-the-check)
8. [Extending a Helper](#8-extending-a-helper)

---

## 1. Quick Start

A module is a directory under `src/` with `inc/`, `src/` and a
`CMakeLists.txt` that starts with three calls:

```cmake
include(seeker/All)
seeker_module(seeker_example)

seeker_add_library(
  NAME ${LIB_NAME}
  TYPE SHARED
  INC "${INC_DIR}"
  SRC "${SRC_DIR}/Example.cpp"
  DEPS_PUBLIC seeker_helpers fmt::fmt
)
```

Add `add_subdirectory(example)` to `src/CMakeLists.txt`, and end the file with
`seeker_add_test_subdirs()` when the module gets a `utst/` directory. Then run
the check, which `make compose-format-check` also runs:

```bash
python3 tools/cmake/check_leaf_cmake.py
```

---

## 2. The Rule

A leaf file declares, a helper constructs. Every target is one helper call that
carries everything the target needs as arguments: sources, include directory,
dependencies and their visibility, definitions, link options, labels, timing
lanes, coverage and install policy. The helpers own the mechanics: aliases,
versions, install and export, platform skips, CUDA handling, test discovery,
labels and coverage.

A leaf file contains only:

- `include(seeker/All)` (and `seeker_module(<name>)` in a module directory),
- source lists: `set(CPP_SOURCES ...)`, `set(UNIT_TEST_SOURCES ...)`,
  `set(CUDA_SOURCES ...)`, explicit file by file,
- helper calls, including explicit opt-ins after the main call
  (`seeker_standard_optins`, `seeker_nvml_enable`, `seeker_install_docs`),
- a local function that wraps a helper for a family of similar targets
  (`add_diag_tool` in `tools/cpp`),
- `seeker_add_test_subdirs()` at the end of a module file, which adds the
  module's test directories, and `add_subdirectory` in the composition files
  (`src/CMakeLists.txt`, `tools/CMakeLists.txt`).

When a helper lacks an argument a target needs, extend the helper
([section 8](#8-extending-a-helper)). Never add `add_library`,
`add_executable`, `target_*`, property calls or an include fragment to a leaf
file. A module file's banner names the module and describes it in one or two
lines; other files' banners are one line. Section titles are short, and a
comment of at most two lines explains a non-obvious choice; anything longer
belongs in the module and tool READMEs.

---

## 3. Model Files

| Shape                           | File                                                                     |
| ------------------------------- | ------------------------------------------------------------------------ |
| Library                         | [src/network/CMakeLists.txt](../../src/network/CMakeLists.txt)           |
| Unit tests, with a CUDA variant | [src/gpu/utst/CMakeLists.txt](../../src/gpu/utst/CMakeLists.txt)         |
| CPU library with a CUDA sidecar | [src/gpu/CMakeLists.txt](../../src/gpu/CMakeLists.txt)                   |
| Private test support            | [src/helpers/utst/CMakeLists.txt](../../src/helpers/utst/CMakeLists.txt) |
| Tools                           | [tools/cpp/CMakeLists.txt](../../tools/cpp/CMakeLists.txt)               |

The library model:

```cmake
# ==============================================================================
# Module: seeker_network
#
# Network diagnostics: interface info/stats, socket buffers, ethtool,
# network isolation, loopback benchmarks.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_network)

set(CPP_SOURCES
    "${SRC_DIR}/InterfaceInfo.cpp" "${SRC_DIR}/InterfaceStats.cpp"
    "${SRC_DIR}/SocketBufferConfig.cpp" "${SRC_DIR}/NetworkIsolation.cpp"
    "${SRC_DIR}/LoopbackBench.cpp" "${SRC_DIR}/EthtoolInfo.cpp"
)

seeker_add_library(
  NAME ${LIB_NAME}
  TYPE SHARED
  INC "${INC_DIR}"
  SRC ${CPP_SOURCES}
  DEPS_PUBLIC seeker_helpers fmt::fmt
)

seeker_standard_optins(${LIB_NAME})
seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/network")

seeker_add_test_subdirs()
```

The unit-test model, whose CUDA test sources are compiled only when CUDA is
active:

```cmake
include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuTopology_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PcieStatus_uTest.cpp"
)

set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/GpuBench_uTest.cu")

seeker_add_gtest(
  TARGET TestSeekerGpu
  SOURCES ${UNIT_TEST_SOURCES}
  CUDA ${CUDA_TEST_SOURCES}
  LINK seeker_gpu seeker_gpu_cuda fmt::fmt
  LABELS diagnostics gpu
)
```

The real files carry the section dividers and the complete source lists.

---

## 4. Adding a Target

### A library

Add `src/<domain>/CMakeLists.txt` in the shape of the library model and an
`add_subdirectory(<domain>)` line to `src/CMakeLists.txt`. Name the library
`seeker_<domain>`, list every source, and put public dependencies in
`DEPS_PUBLIC` and private ones in `DEPS_PRIVATE`. A header-only library uses
`seeker_add_interface_library` instead.

### A unit test

Add `src/<domain>/utst/CMakeLists.txt` in the shape of the test model, with the
library and `fmt::fmt` in `LINK` and the domain in `LABELS`; the module file's
`seeker_add_test_subdirs()` adds the directory, and likewise `ptst/` and
`dtst/` for performance and development tests. Tests that measure
wall time or share a machine-wide resource go in the serial Timing lane with
`TIMING_PATTERNS` (or `TIMING_TESTS`, `TIMING_ALL`); `RESOURCE_LOCK` serializes
tests that share a named resource.

### A CUDA variant

Library: declare the CPU library, then its sidecar with
`seeker_add_library_cuda(NAME ${LIB_NAME}_cuda CORE ${LIB_NAME} SRC ${CUDA_SOURCES})`.
Without CUDA the sidecar is an INTERFACE shim of the same name, so consumers
link it unconditionally and need no `if (CUDAToolkit_FOUND)` blocks. Tests: pass
the `.cu` test sources in `CUDA`; they and their tests are left out without
CUDA. Tools: a tool that cannot exist without CUDA declares `REQUIRES CUDA`.

### Private test support

Shared test fixtures are an interface library in the test directory that is
never installed or exported:

```cmake
seeker_add_interface_library(
  NAME seeker_test_support
  INC "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE seeker_scratch_tree seeker_helpers
  NO_INSTALL
)
```

Tests in other domains link it by name. A test that needs linker options
(symbol wrapping) passes them in `LINK_OPTIONS`.

### A tool

Add the source under `tools/cpp/<domain>/` and one line to its domain section
of `tools/cpp/CMakeLists.txt`: `add_diag_tool(<tool-name> <domain>)`. A tool
outside that pattern is its own `seeker_add_tool` call.

---

## 5. Helper Reference

### Arguments

The helpers in `Core`, `Targets`, `Testing`, `Cuda`, `Coverage` and `Tooling`
check their arguments before any configuration-dependent skip, so these
mistakes fail every configure, with the helper's name and the calling line: an
unknown argument (`seeker_add_interface_library: unknown arguments: DEPS_PUBLIC
fmt::fmt`), a keyword without a value (`seeker_add_gtest: no value given for
CUDA`, also when the value is an empty variable), a missing required keyword,
an extra argument to a helper with positional parameters, an unknown
requirement, a `TYPE` other than `STATIC` or `SHARED`, an unknown test
directory kind and a `REQUIRES_THREADS` that is not a positive integer.

A referenced target is checked where the helper acts. `seeker_standard_optins`,
`seeker_nvml_enable`, `seeker_clang_tidy_cuda`, `seeker_cupti_enable` and
`seeker_cuda_sources` look their target up first. A helper that returns early
for its configuration does not look: `seeker_add_doxygen` with docs off,
`seeker_add_upx_copy` with UPX off or outside a release build,
`seeker_coverage_register` without coverage, the CUDA sidecar's `CORE` on
bare-metal, and the `COVERAGE_FOR` of a skipped test. For a test
that is not skipped, a `COVERAGE_FOR` that coverage cannot map fails the
configure even with coverage off. An earlier lookup would reject valid
configurations that skip the referenced target.

A list keyword takes every following word up to the next keyword of the same
helper, so a keyword misspelled after a list is one of that list's values. It
then fails where the value is used (a missing source file, a library that does
not link) or, in a free-form list such as `LABELS`, not at all. The formatter
lays such a word out among the list's values
(`LINK fmt::fmt LINK_OPTION "LINKER:--wrap=write"`); an upper-case word inside
a value list is the sign to look for.

### Platform skips and requirements

The target factories skip a target on a bare-metal build
(`CMAKE_SYSTEM_NAME Generic`) unless it has `BAREMETAL`, and when a requirement
in `REQUIRES` does not hold. The CUDA sidecar is always skipped on bare-metal;
test executables too.

| Requirement | Holds on                                             |
| ----------- | ---------------------------------------------------- |
| `POSIX`     | a hosted UNIX platform (`UNIX`, not bare-metal)      |
| `LINUX`     | a hosted platform with `CMAKE_SYSTEM_NAME Linux`     |
| `CUDA`      | the CUDA toolkit found and the CUDA language enabled |

`PTHREADS` is retired, and naming it fails the configure with the migration:
POSIX means a hosted platform; link `Threads::Threads` through `DEPS_PUBLIC`,
`DEPS_PRIVATE` or `DEPS_INTERFACE` on a library, `LINK` on a test, tool or app.

`REQUIRES_TARGETS` names targets that must exist when the helper runs, so a
target built only with an optional package declares the package's target
instead of a guard in the leaf file. When one is missing, the target is
skipped. `seeker_add_gtest`, `seeker_add_ptest`, `seeker_add_tool` and
`seeker_add_app` link the named targets `PRIVATE`; `seeker_add_library` and
`seeker_add_interface_library` only check them, and the library declares the
link in `DEPS_PUBLIC`, `DEPS_PRIVATE` or `DEPS_INTERFACE` with the visibility
its headers need.

The code that owns the option providing such a target can register a hint,
`set_property(GLOBAL PROPERTY SEEKER_TARGET_HINT_<target> "<text>")` (for
example `"configure with -D<OPTION>=ON"`); a skip for that missing target then
ends with `; enabled by <text>`.

A skipped target is recorded. `seeker_standard_optins`, `seeker_nvml_enable`,
`seeker_clang_tidy_cuda` and the sidecar's `CORE` ignore a recorded name and
fail on any other name that is not a target. The configure summary lists every
skipped target once, with its reason, in a `Skipped targets` block at the end
of the configure; with `SEEKER_TARGETS_VERBOSE` each skip is also printed
where it happens.

<!-- helper reference: rendered by tools/cmake/gen_helper_reference.py -->

Rendered from the comment block above each public helper by
`tools/cmake/gen_helper_reference.py`, grouped by helper file: the call form,
the first paragraph of the description and the arguments; the rest of the
description is in the block. Edit the comment block and rerun the script; the
leaf check fails while this section and the blocks differ.

### `Core.cmake`

#### `seeker_guard`

`seeker_guard(<target>)`

Fail the configure when `<target>` does not exist.

#### `seeker_module`

`seeker_module(<name>)`

Set the module variables a leaf file uses, in the caller's scope:

```text
LIB_NAME    <name>
SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/src
INC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/inc
UTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/utst  unit tests (seeker_add_gtest,
                                              registered with CTest)
PTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/ptst  performance tests
                                              (seeker_add_ptest, run by hand)
DTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/dtst  development tests
                                              (seeker_add_devtest, run by hand)
README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md
```

#### `seeker_add_test_subdirs`

`seeker_add_test_subdirs([UTST] [PTST] [DTST])`

Add the module's test directories that have a `CMakeLists.txt`, in the
top-level project only: a project that builds seeker as a subproject gets
no seeker tests. Each kind is the directory `seeker_module()` names:

```text
UTST  ${UTST_DIR}  unit tests (seeker_add_gtest, registered with CTest)
PTST  ${PTST_DIR}  performance tests (seeker_add_ptest, run by hand)
DTST  ${DTST_DIR}  development tests (seeker_add_devtest, run by hand)
```

Without arguments every kind is considered, so a module gains or drops a
tier by adding or removing its directory; given kinds limit the call to
those. Call it at the end of a module file, after `seeker_module()`.

| Argument | Value | Notes                                                  |
| -------- | ----- | ------------------------------------------------------ |
| `UTST`   |       | optional flag: consider the unit-test directory        |
| `PTST`   |       | optional flag: consider the performance-test directory |
| `DTST`   |       | optional flag: consider the development-test directory |

#### `seeker_is_cuda_target`

`seeker_is_cuda_target(<target> <out_var>)`

Set `<out_var>` to `TRUE` if `<target>` has `.cu` or `.cuh` sources, `FALSE` otherwise.

#### `seeker_standard_optins`

`seeker_standard_optins(<target>)`

Attach the opt-in tooling to a library target:

```text
- Doxygen docs (seeker_add_doxygen; PROJECT_BUILD_DOCS)
- a UPX copy (seeker_add_upx_copy; ENABLE_UPX, release builds, executables
  and, with UPX_COMPRESS_SHARED, shared libraries)
- clang-tidy over the target's .cu files (seeker_clang_tidy_cuda;
  ENABLE_CLANG_TIDY)
```

### `Coverage.cmake`

#### `seeker_coverage_init`

`seeker_coverage_init()`

With `ENABLE_COVERAGE`, locate llvm-profdata and llvm-cov
(`SEEKER_COVERAGE_LLVM_VERSION`, by default the major version of the Clang
that compiles), create the `seeker_coverage_flags` interface
target (Clang source-based instrumentation, Debug builds) and generate the
coverage-report and coverage-clean targets at the end of the configure.
Coverage is turned off with a warning for cross, bare-metal and non-Clang
builds; the configure fails when the `LLVM` tools are missing. Call once after
`project()`.

#### `seeker_coverage_register`

`seeker_coverage_register(<test_target> <library_target>)`

Register a test:library mapping for the report. `seeker_add_gtest` calls it for
each library it instruments; a no-op without `ENABLE_COVERAGE`. A library that
coverage cannot map (see `_seeker_coverage_maps`) fails the configure.

### `Cuda.cmake`

#### `seeker_cuda_sources`

`seeker_cuda_sources(<target> FILES <files...> [NO_CUDART] [SEPARABLE] [RESOLVE_DEVICE_SYMBOLS] [DEFINE <name>])`

Add `CUDA` sources to an existing target; a no-op unless the `CUDA` toolkit is
found and the `CUDA` language enabled. Separable compilation and device-symbol
resolution are `OFF` unless `SEPARABLE` or `RESOLVE_DEVICE_SYMBOLS` is given. The
target gets the compile definition `<name>=1` (`DEFINE`, default `SEEKER_HAS_CUDA`)
and links `CUDA::cudart` unless `NO_CUDART`, `PRIVATE` (`INTERFACE` for an `INTERFACE`
target).

| Argument                 | Value        | Notes                                |
| ------------------------ | ------------ | ------------------------------------ |
| `FILES`                  | `<files...>` | required                             |
| `DEFINE`                 | `<name>`     | optional (default `SEEKER_HAS_CUDA`) |
| `NO_CUDART`              |              | optional flag                        |
| `SEPARABLE`              |              | optional flag                        |
| `RESOLVE_DEVICE_SYMBOLS` |              | optional flag                        |

#### `seeker_nvml_enable`

`seeker_nvml_enable(<target>)`

Enable `NVML` usage when libnvidia-ml and `nvml.h` are available: links the
library, adds its include directory and defines `COMPAT_NVML_AVAILABLE=1`;
defines `COMPAT_NVML_AVAILABLE=0` when `NVML` is missing and on bare-metal
builds. `PUBLIC` (`INTERFACE` for an `INTERFACE` target). The search runs once and
is cached, and every call that links the library adds its directory to the
`BUILD_RPATH` and `INSTALL_RPATH` of a target that is not `INTERFACE`, unless the
library is a stub (its real path is in a stubs directory): a stub is for
linking only, and the driver's library is found at run time. A target a
factory skipped on this platform is ignored; any other name that is not a
target fails the configure.

#### `seeker_cupti_enable`

`seeker_cupti_enable(<target>)`

Enable `CUPTI` usage when libcupti is available: links the library, adds the
`CUPTI` include directory and defines `COMPAT_CUPTI_AVAILABLE=1`; defines
`COMPAT_CUPTI_AVAILABLE=0` when `CUPTI` or the `CUDA` toolkit is missing. `PUBLIC`
(`INTERFACE` for an `INTERFACE` target). `<target>` must exist.

### `Summary.cmake`

#### `seeker_print_config_summary`

`seeker_print_config_summary()`

Emit configuration banner showing build settings, compilers, and `CUDA` status,
and at the end of the caller's directory, once the target helpers have run,
the Skipped targets block (`_seeker_print_skipped_targets`). Takes no
arguments; an argument fails the configure.

### `Targets.cmake`

#### `seeker_add_interface_library`

`seeker_add_interface_library(...)`

Header-only library: an `INTERFACE` target and its `seeker::` alias, carrying the
include directory, dependencies, definitions and features to its consumers.
The target and the headers under `INC` are installed and exported in
seekerTargets unless `NO_INSTALL` is given. Skipped on bare-metal builds unless
`BAREMETAL` is given, when a requirement does not hold, and when a
`REQUIRES_TARGETS` target does not exist.

| Argument           | Value               | Notes                                                                                          |
| ------------------ | ------------------- | ---------------------------------------------------------------------------------------------- |
| `NAME`             | `<target>`          | required                                                                                       |
| `INC`              | `<include_dir>`     | required                                                                                       |
| `DEPS_INTERFACE`   | `<targets...>`      | optional, linked `INTERFACE`                                                                   |
| `DEFS`             | `<defs...>`         | optional, `INTERFACE` compile definitions                                                      |
| `FEATURES`         | `<features...>`     | optional, `INTERFACE` compile features                                                         |
| `REQUIRES`         | `<requirements...>` | optional (`POSIX`, `LINUX`, `CUDA`)                                                            |
| `REQUIRES_TARGETS` | `<targets...>`      | optional, targets that must exist; checked, not linked (name them in `DEPS_INTERFACE` as well) |
| `BAREMETAL`        |                     | optional flag: also built on bare-metal                                                        |
| `NO_INSTALL`       |                     | optional flag: neither installed nor exported (test support, build-only)                       |

#### `seeker_add_library`

`seeker_add_library(...)`

Compiled library and its `seeker::` alias. A shared library gets `VERSION` and
`SOVERSION` from the project version. The library's public include path is `INC`
plus the project root (build tree) or the install include directory; the
target and the headers under `INC` are installed and exported in seekerTargets.
Skipped on bare-metal builds unless `BAREMETAL` is given, when a requirement
does not hold, and when a `REQUIRES_TARGETS` target does not exist; `SHARED`
becomes `STATIC` on bare-metal.

| Argument           | Value               | Notes                                                                                                                                                        |
| ------------------ | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `NAME`             | `<target>`          | required                                                                                                                                                     |
| `TYPE`             | `STATIC\|SHARED`    | required                                                                                                                                                     |
| `INC`              | `<include_dir>`     | required, public include directory                                                                                                                           |
| `SRC`              | `<files...>`        | required                                                                                                                                                     |
| `DEPS_PUBLIC`      | `<targets...>`      | optional, linked `PUBLIC`                                                                                                                                    |
| `DEPS_PRIVATE`     | `<targets...>`      | optional, linked `PRIVATE`                                                                                                                                   |
| `REQUIRES`         | `<requirements...>` | optional (`POSIX`, `LINUX`, `CUDA`)                                                                                                                          |
| `REQUIRES_TARGETS` | `<targets...>`      | optional, targets that must exist; checked, not linked (name them in `DEPS_PUBLIC` or `DEPS_PRIVATE` as well)                                                |
| `OUTPUT_NAME`      | `<file_stem>`       | optional, the library file's name without prefix and suffix; default `${PROJECT_NAME}_<NAME>` (`NAME` itself when it already starts with `${PROJECT_NAME}_`) |
| `BAREMETAL`        |                     | optional flag: also built on bare-metal                                                                                                                      |

#### `seeker_add_app`

`seeker_add_app(...)`

Application executable. Outputs to `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}`, installs
to the binary directory unless `NO_INSTALL`, and gets a `UPX` copy (`ENABLE_UPX`,
release builds) unless `NO_UPX`. Skipped on bare-metal builds unless `BAREMETAL`
is given, when a requirement does not hold, and when a `REQUIRES_TARGETS`
target does not exist.

| Argument           | Value               | Notes                                               |
| ------------------ | ------------------- | --------------------------------------------------- |
| `NAME`             | `<target>`          | required                                            |
| `SRC`              | `<files...>`        | required                                            |
| `LINK`             | `<targets...>`      | optional, linked `PRIVATE`                          |
| `INC`              | `<dirs...>`         | optional, `PRIVATE` include directories             |
| `DEFS`             | `<defs...>`         | optional, `PRIVATE` compile definitions             |
| `FEATURES`         | `<features...>`     | optional, `PRIVATE` compile features                |
| `REQUIRES`         | `<requirements...>` | optional (`POSIX`, `LINUX`, `CUDA`)                 |
| `REQUIRES_TARGETS` | `<targets...>`      | optional, targets that must exist; linked `PRIVATE` |
| `BAREMETAL`        |                     | optional flag: also built on bare-metal             |
| `NO_INSTALL`       |                     | optional flag                                       |
| `NO_UPX`           |                     | optional flag: no `UPX` copy                        |

#### `seeker_add_tool`

`seeker_add_tool(...)`

Command-line tool executable. Outputs to
`${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/cpp`, installs to the binary directory
unless `NO_INSTALL`, and joins the `${PROJECT_NAME}_cpp_tools` aggregate target
(`seeker_finalize_cpp_tools`). Sources compile in the language of their
extension (`.cu` as `CUDA`). Skipped on bare-metal builds unless `BAREMETAL` is
given, when a requirement does not hold (a `CUDA` tool declares
`REQUIRES` `CUDA`), and when a `REQUIRES_TARGETS` target does not exist.

| Argument           | Value               | Notes                                               |
| ------------------ | ------------------- | --------------------------------------------------- |
| `NAME`             | `<target>`          | required                                            |
| `SRC`              | `<files...>`        | required                                            |
| `LINK`             | `<targets...>`      | optional, linked `PRIVATE`                          |
| `INC`              | `<dirs...>`         | optional, `PRIVATE` include directories             |
| `DEFS`             | `<defs...>`         | optional, `PRIVATE` compile definitions             |
| `FEATURES`         | `<features...>`     | optional, `PRIVATE` compile features                |
| `REQUIRES`         | `<requirements...>` | optional (`POSIX`, `LINUX`, `CUDA`)                 |
| `REQUIRES_TARGETS` | `<targets...>`      | optional, targets that must exist; linked `PRIVATE` |
| `BAREMETAL`        |                     | optional flag: also built on bare-metal             |
| `NO_INSTALL`       |                     | optional flag                                       |

#### `seeker_add_library_cuda`

`seeker_add_library_cuda(...)`

`CUDA` sidecar of a `CPU` library. When the `CUDA` toolkit is found and the `CUDA`
language is enabled, a compiled library and its `seeker::` alias: `SRC` compiled
through `seeker_cuda_sources`, `CORE` linked `PRIVATE`, `CUDA::cudart` linked `PUBLIC`
unless `NO_CUDART`, `NVML` enabled unless `NO_NVML`, installed and exported. Without
`CUDA`, an `INTERFACE` shim of the same name that links `CORE` and `DEPS_PUBLIC`, so
consumers declare the sidecar unconditionally. Skipped on bare-metal builds.
Otherwise it is skipped when `CORE` was skipped, and a `CORE` that is neither a
target nor a skipped one fails the configure.

| Argument                 | Value            | Notes                                                |
| ------------------------ | ---------------- | ---------------------------------------------------- |
| `NAME`                   | `<target>`       | required                                             |
| `SRC`                    | `<files...>`     | required, `CUDA` sources                             |
| `CORE`                   | `<cpu_target>`   | optional, linked `PRIVATE` (`INTERFACE` by the shim) |
| `TYPE`                   | `STATIC\|SHARED` | optional (default `SHARED`)                          |
| `INC`                    | `<include_dir>`  | optional, `PRIVATE`                                  |
| `DEPS_PUBLIC`            | `<targets...>`   | optional, linked `PUBLIC` (`INTERFACE` by the shim)  |
| `DEPS_PRIVATE`           | `<targets...>`   | optional, linked `PRIVATE`                           |
| `SEPARABLE`              |                  | optional flag                                        |
| `RESOLVE_DEVICE_SYMBOLS` |                  | optional flag                                        |
| `NO_CUDART`              |                  | optional flag                                        |
| `NO_NVML`                |                  | optional flag                                        |

#### `seeker_finalize_cpp_tools`

`seeker_finalize_cpp_tools()`

Create the `${PROJECT_NAME}_cpp_tools` aggregate target over every tool
`seeker_add_tool` created; none when no tool was created. Call once, after the
last `seeker_add_tool`.

### `Testing.cmake`

#### `seeker_add_gtest`

`seeker_add_gtest(...)`

GoogleTest executable in `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests`. Its tests
are found in its sources (`gtest_add_tests`) and registered with CTest. `CUDA`
sources are compiled through `seeker_cuda_sources` when the `CUDA` toolkit is
found and the `CUDA` language enabled, and otherwise left out with their tests.
Skipped on bare-metal builds, and when a `REQUIRES_TARGETS` target does not
exist.

| Argument           | Value          | Notes                                                                         |
| ------------------ | -------------- | ----------------------------------------------------------------------------- |
| `TARGET`           | `<name>`       | required                                                                      |
| `SOURCES`          | `<files...>`   | required                                                                      |
| `CUDA`             | `<files...>`   | optional, `CUDA` sources                                                      |
| `LINK`             | `<targets...>` | optional, linked `PRIVATE` with `GTest::gtest_main` and `GTest::gmock`        |
| `REQUIRES_TARGETS` | `<targets...>` | optional, targets that must exist; linked `PRIVATE` (not mapped for coverage) |
| `LINK_OPTIONS`     | `<options...>` | optional, `PRIVATE` linker options                                            |
| `INC`              | `<dir>`        | optional, `PRIVATE` include directory                                         |
| `LABELS`           | `<labels...>`  | optional, CTest labels of every test                                          |
| `WORKING_DIR`      | `<dir>`        | optional (default: the binary directory)                                      |
| `RESOURCE_LOCK`    | `<name>`       | optional, CTest `RESOURCE_LOCK` of every test                                 |
| `REQUIRES_THREADS` | `<count>`      | optional, disables the tests on a host with fewer hardware threads            |
| `TIMING_ALL`       |                | optional flag: every test is timed                                            |
| `TIMING_TESTS`     | `<names...>`   | optional, timed tests (`Suite.Test`)                                          |
| `TIMING_PATTERNS`  | `<regex...>`   | optional, timed tests by name pattern                                         |
| `COVERAGE_FOR`     | `<lib_target>` | optional, the library to map instead of those found in `LINK`                 |
| `NO_COVERAGE`      |                | optional flag: no coverage for this test                                      |

#### `seeker_add_ptest`

`seeker_add_ptest(...)`

Performance test executable in `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ptests`,
built but not registered with CTest: run it by hand, for example

```text
build/native-linux-debug/bin/ptests/<TARGET> --csv results.csv
```

Links GoogleTest and, when found, the gperftools profiler and tcmalloc; the
build `RPATH` reaches the library output directory. `CUDA` sources are compiled
when `CUDA` is active and left out otherwise. Skipped on bare-metal builds, and
when a `REQUIRES_TARGETS` target does not exist (a test that measures with an
optional library names it there).

| Argument           | Value          | Notes                                               |
| ------------------ | -------------- | --------------------------------------------------- |
| `TARGET`           | `<name>`       | required                                            |
| `SOURCES`          | `<files...>`   | required                                            |
| `CUDA`             | `<files...>`   | optional, `CUDA` sources                            |
| `LINK`             | `<targets...>` | optional, linked `PRIVATE`                          |
| `REQUIRES_TARGETS` | `<targets...>` | optional, targets that must exist; linked `PRIVATE` |
| `INC`              | `<dir>`        | optional, `PRIVATE` include directory               |

#### `seeker_add_devtest`

`seeker_add_devtest(...)`

Development test executable in `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dtests`,
built but not registered with CTest, for tests that need data files or
hardware a CTest run does not have: run it by hand, for example

```text
build/native-linux-debug/bin/dtests/<TARGET> --gtest_filter="*Pattern*"
```

Links GoogleTest. `CUDA` sources are compiled when `CUDA` is active and left out
otherwise. Skipped on bare-metal builds.

| Argument  | Value          | Notes                                 |
| --------- | -------------- | ------------------------------------- |
| `TARGET`  | `<name>`       | required                              |
| `SOURCES` | `<files...>`   | required                              |
| `CUDA`    | `<files...>`   | optional, `CUDA` sources              |
| `LINK`    | `<targets...>` | optional, linked `PRIVATE`            |
| `INC`     | `<dir>`        | optional, `PRIVATE` include directory |

### `Tooling.cmake`

#### `seeker_add_doxygen`

`seeker_add_doxygen(<target> [README <path>] [SRC <dir>] [INC <dir>] [TST <dir>] [DOCS <dir>] [TOOLS <dir>] [TEMPLATES <dir>] [EXTRA_DIRS <dirs...>] [HEADERS_ONLY] [NO_TST])`

Add a `<target>_docs` target (part of the global docs target) that runs Doxygen
into `${CMAKE_BINARY_DIR}/docs/lib<target>/doxygen/html`. `SRC`, `TST`, `DOCS`, `TOOLS`,
`TEMPLATES` and `README` default to src, utst, docs, tools, templates and `README.md`
under the current source directory when those exist, `INC` to inc; `EXTRA_DIRS`
adds directories, `HEADERS_ONLY` leaves out the sources and `NO_TST` the tests.
The Doxyfile comes from the layer's template, `Doxyfile.common.in` beside this
module, or from the project's `docs/Doxyfile.common.in` when that exists. A
no-op unless `PROJECT_BUILD_DOCS` is `ON`; a warning and no docs target when
Doxygen is not found.

| Argument       | Value       | Notes                                    |
| -------------- | ----------- | ---------------------------------------- |
| `README`       | `<path>`    | optional                                 |
| `SRC`          | `<dir>`     | optional                                 |
| `INC`          | `<dir>`     | optional (default inc, even when absent) |
| `TST`          | `<dir>`     | optional                                 |
| `DOCS`         | `<dir>`     | optional                                 |
| `TOOLS`        | `<dir>`     | optional                                 |
| `TEMPLATES`    | `<dir>`     | optional                                 |
| `EXTRA_DIRS`   | `<dirs...>` | optional                                 |
| `HEADERS_ONLY` |             | optional flag                            |
| `NO_TST`       |             | optional flag                            |

#### `seeker_install_docs`

`seeker_install_docs(<path> [DESTINATION <subdir>])`

Install a markdown file, or a directory recursively, to `CMAKE_INSTALL_DOCDIR`
or the `DESTINATION` subdirectory of it. A no-op unless `CMAKE_BUILD_TYPE` is
empty, Release, MinSizeRel or RelWithDebInfo.

| Argument      | Value      | Notes    |
| ------------- | ---------- | -------- |
| `DESTINATION` | `<subdir>` | optional |

#### `seeker_add_upx_copy`

`seeker_add_upx_copy(<target>)`

After each build of `<target>`, write a UPX-compressed copy to
`<artifact_dir>/upx/<file>.upx` (a plain copy when `UPX` fails). Executables only,
and shared or module libraries with `UPX_COMPRESS_SHARED`. A no-op unless
`ENABLE_UPX` is `ON` and `CMAKE_BUILD_TYPE` is empty, Release or MinSizeRel; a
warning when upx is not found.

#### `seeker_clang_tidy_cuda`

`seeker_clang_tidy_cuda(<target> FILES <files...> [ALLOW_FAILURE])`

After each build of `<target>`, run clang-tidy on the given `CUDA` sources through
Clang's `CUDA` frontend; `ALLOW_FAILURE` keeps findings from failing the build. A
no-op for an `INTERFACE` target, without the `CUDA` toolkit, with
`ENABLE_CLANG_TIDY` `OFF`, or when clang-tidy is not found. A target a factory
skipped on this platform is ignored; any other name that is not a target
fails the configure.

| Argument        | Value        | Notes         |
| --------------- | ------------ | ------------- |
| `FILES`         | `<files...>` | required      |
| `ALLOW_FAILURE` |              | optional flag |

<!-- end of helper reference -->

---

## 6. Coverage

With `ENABLE_COVERAGE` (Clang, native Debug builds; `make coverage`),
`seeker_add_gtest` maps a test to each library in its `LINK` that coverage can
map, or to its `COVERAGE_FOR` library, instruments both and adds a
`<test>_coverage` test labelled `Coverage`. The LLVM tools follow the
compiler: `llvm-profdata-<N>` and `llvm-cov-<N>` for Clang `<N>`, unless
`SEEKER_COVERAGE_LLVM_VERSION` names another version.

| Target kind                                        | Participation                                                                  |
| -------------------------------------------------- | ------------------------------------------------------------------------------ |
| Shared library built from C or C++ sources         | mapped; the report reads the library's own file                                |
| Header-only library                                | not mapped; its inline code appears where a mapped library or test compiles it |
| CUDA-only library                                  | not mapped; nvcc compiles carry no coverage mapping                            |
| Production sources compiled into a test            | reported with the libraries that test maps                                     |
| Test that maps no library                          | not instrumented and not part of coverage                                      |
| Static or object library, imported or alias target | not mapped                                                                     |

For a test that is not skipped, a `COVERAGE_FOR` naming anything that coverage
cannot map fails the configure, even with coverage off. The `coverage-report`
target writes one report per library, `html/`, `summary.txt` and `lcov.info`
under `<build>/coverage/<library>/`, read from the library's file and every
test executable mapped to it, so the sources each test compiles directly are
kept. A report leaves out fetched dependencies (any `_deps` directory), test
sources and `/usr/local` (`SEEKER_COVERAGE_IGNORE_REGEX`). When a report
cannot be produced it still writes the others and then fails, naming each
failed library.
The coverage flags are never installed or exported.

---

## 7. The Check

`tools/cmake/check_leaf_cmake.py` keeps leaf files declarative. It reads every
`CMakeLists.txt` and `*.cmake` under `src/` and `tools/` (`LEAF_DIRS`), outside
hidden directories, prints each file's commands, and fails on:

- a command that constructs or changes a target or the build settings outside
  the helpers: `add_library`, `add_executable`, any `target_*`,
  `set_target_properties`, `set_property`, `set_source_files_properties`,
  `set_tests_properties`, `set_directory_properties`, `add_custom_target`,
  `add_custom_command`, `add_dependencies`, `add_test`, `include_directories`,
  `link_directories`, `link_libraries`, `add_compile_options`,
  `add_compile_definitions`, `add_definitions`, `remove_definitions`,
  `add_link_options`, `cmake_language`;
- a call to an internal helper (`_seeker_*`);
- an `include` of anything but a `seeker/` helper module;
- a leaf file whose tokens do not read as commands, such as one with an
  unterminated bracket argument, named with its line;
- an escaped parenthesis that the lexer reads as structural, such as `\(` or
  `\)` at the start of an unquoted argument, named with its line: a form the
  check does not support; write the argument quoted, `"("` or `")"`;
- single quotes around a parenthesis, such as `'('`, which the lexer reads as
  one quoted argument while CMake reads `'` as an ordinary character, named
  with its line: a form the check does not support; write the argument in
  double quotes;
- helper signatures in `.cmake-format.yaml` that differ from the helpers, and a
  helper comment block that does not name one of its keywords;
- a [helper reference](#5-helper-reference) that differs from the rendering of
  the helpers' comment blocks, or a public helper's block that does not read
  (`tools/cmake/gen_helper_reference.py` names the block and the problem).

It reads the files with the lexer of cmakelang, the package of cmake-format, so
comments, quoted and bracket arguments are not commands. The helpers
themselves, the root `CMakeLists.txt` and `ExternalDependencies.cmake` are not
leaf files.

It runs as a pre-commit hook, so `make format-check`,
`make compose-format-check` and the CI format step run it; by hand:

```bash
python3 tools/cmake/check_leaf_cmake.py
```

It needs Python 3 and cmakelang with its YAML extra (PyYAML), as
`pip install "cmakelang[YAML]"` installs them: cmakelang supplies the lexer,
cmake-format and `cmake-genparsers`, and cmake-format and
`tools/cmake/gen_signatures.py` read YAML through PyYAML. The development
containers have both.

---

## 8. Extending a Helper

1. Add the keyword to the helper's `cmake_parse_arguments` call: flags,
   one-value keywords or list keywords. `_seeker_check_arguments` then rejects
   its misspellings and an empty value; add it after `REQUIRED` there if the
   helper cannot work without it.
2. Implement it in the helper, after the argument check, with the visibility the
   target needs.
3. Add its row to the `Arguments:` table of the comment block above the
   function: two spaces, the keyword, its value placeholder (none for a flag),
   then `required` or `optional` and the notes, continued on lines indented to
   the notes. The block opens with the call form and a blank line, and its
   description, prose with literal lines indented further, comes before the
   table; the description's first paragraph is what section 5 shows.
4. Regenerate the formatter signatures under `parse.additional_commands` in
   `.cmake-format.yaml` with `python3 tools/cmake/gen_signatures.py`, and the
   [helper reference](#5-helper-reference) with
   `python3 tools/cmake/gen_helper_reference.py`.
5. Use it in the leaf file, run `make compose-format-check`, and configure once
   with a misspelled or empty use to see the helper's message.

Add a new helper only for a target family the existing ones cannot express; it
follows the same rules.
