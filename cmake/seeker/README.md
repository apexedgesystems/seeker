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
`seeker_print_config_summary()`, which the root file calls, takes no arguments
and checks none.

A referenced target is checked where the helper acts. `seeker_standard_optins`,
`seeker_nvml_enable`, `seeker_clang_tidy_cuda`, `seeker_cupti_enable` and
`seeker_cuda_sources` look their target up first. A helper that returns early
for its configuration does not look: `seeker_add_doxygen` with docs off,
`seeker_add_upx_copy` with UPX off or outside a release build,
`seeker_coverage_register` without coverage, the CUDA sidecar's `CORE` on
bare-metal, and the `COVERAGE_FOR` of a test skipped on bare-metal. For a test
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
| `POSIX`     | a hosted (not bare-metal) platform                   |
| `PTHREADS`  | a hosted platform                                    |
| `LINUX`     | a hosted platform with `CMAKE_SYSTEM_NAME Linux`     |
| `CUDA`      | the CUDA toolkit found and the CUDA language enabled |

A skipped target is recorded. `seeker_standard_optins`, `seeker_nvml_enable`,
`seeker_clang_tidy_cuda` and the sidecar's `CORE` ignore a recorded name and
fail on any other name that is not a target.

### Libraries

`seeker_add_library(NAME TYPE INC SRC ...)`: compiled library with a `seeker::`
alias; a shared library gets `VERSION` and `SOVERSION` from the project version.
The target and the headers under `INC` are installed and exported in
`seekerTargets`.

| Argument       | Value                | Notes                                                     |
| -------------- | -------------------- | --------------------------------------------------------- |
| `NAME`         | target               | required                                                  |
| `TYPE`         | `STATIC` or `SHARED` | required; `SHARED` becomes `STATIC` on bare-metal         |
| `INC`          | directory            | required; public, with the project root in the build tree |
| `SRC`          | files                | required                                                  |
| `DEPS_PUBLIC`  | targets              | linked `PUBLIC`                                           |
| `DEPS_PRIVATE` | targets              | linked `PRIVATE`                                          |
| `REQUIRES`     | requirements         | see above                                                 |
| `BAREMETAL`    | flag                 | also built on bare-metal                                  |

`seeker_add_interface_library(NAME INC ...)`: header-only `INTERFACE` library
with a `seeker::` alias, installed and exported unless `NO_INSTALL`.

| Argument         | Value        | Notes                                         |
| ---------------- | ------------ | --------------------------------------------- |
| `NAME`           | target       | required                                      |
| `INC`            | directory    | required                                      |
| `DEPS_INTERFACE` | targets      | linked `INTERFACE`                            |
| `DEFS`           | definitions  | `INTERFACE` compile definitions               |
| `FEATURES`       | features     | `INTERFACE` compile features                  |
| `REQUIRES`       | requirements | see above                                     |
| `BAREMETAL`      | flag         | also built on bare-metal                      |
| `NO_INSTALL`     | flag         | neither installed nor exported (test support) |

`seeker_add_library_cuda(NAME SRC ...)`: CUDA sidecar of a CPU library. With
CUDA, a compiled library whose sources go through `seeker_cuda_sources`, with
`CUDA::cudart` linked `PUBLIC` and NVML enabled; without CUDA, an `INTERFACE`
shim of the same name linking `CORE` and `DEPS_PUBLIC`. Both are exported.

| Argument                              | Value                | Notes                                                       |
| ------------------------------------- | -------------------- | ----------------------------------------------------------- |
| `NAME`                                | target               | required                                                    |
| `SRC`                                 | files                | required, CUDA sources                                      |
| `CORE`                                | target               | the CPU library; linked `PRIVATE` (`INTERFACE` by the shim) |
| `TYPE`                                | `STATIC` or `SHARED` | default `SHARED`                                            |
| `INC`                                 | directory            | `PRIVATE`                                                   |
| `DEPS_PUBLIC`                         | targets              | linked `PUBLIC` (`INTERFACE` by the shim)                   |
| `DEPS_PRIVATE`                        | targets              | linked `PRIVATE`                                            |
| `SEPARABLE`, `RESOLVE_DEVICE_SYMBOLS` | flags                | device-link properties, `OFF` by default                    |
| `NO_CUDART`, `NO_NVML`                | flags                | leave out the CUDA runtime or NVML                          |

### Tests

`seeker_add_gtest(TARGET SOURCES ...)`: GoogleTest executable in
`bin/tests/`, its tests found in its sources and registered with CTest, linked
with `GTest::gtest_main` and `GTest::gmock`.

| Argument           | Value     | Notes                                                                    |
| ------------------ | --------- | ------------------------------------------------------------------------ |
| `TARGET`           | name      | required                                                                 |
| `SOURCES`          | files     | required                                                                 |
| `CUDA`             | files     | compiled when CUDA is active, otherwise left out with their tests        |
| `LINK`             | targets   | linked `PRIVATE`                                                         |
| `LINK_OPTIONS`     | options   | `PRIVATE` linker options                                                 |
| `INC`              | directory | `PRIVATE` include directory                                              |
| `LABELS`           | labels    | CTest labels of every test                                               |
| `WORKING_DIR`      | directory | default: the binary directory                                            |
| `RESOURCE_LOCK`    | name      | CTest `RESOURCE_LOCK` of every test                                      |
| `REQUIRES_THREADS` | count     | disables the tests on a host with fewer hardware threads                 |
| `TIMING_ALL`       | flag      | every test in the Timing lane                                            |
| `TIMING_TESTS`     | names     | `Suite.Test` names in the Timing lane                                    |
| `TIMING_PATTERNS`  | regexes   | test names in the Timing lane                                            |
| `COVERAGE_FOR`     | library   | the library to map instead of those in `LINK` ([section 6](#6-coverage)) |
| `NO_COVERAGE`      | flag      | no coverage for this test                                                |

Timed tests get the `Timing` label and run serially unless `RESOURCE_LOCK`
already serializes them; names and patterns that match no test select nothing.

`seeker_add_ptest(TARGET SOURCES ...)` and `seeker_add_devtest(TARGET SOURCES
...)`: performance and development test executables in `bin/ptests/` and
`bin/dtests/`, built but not registered with CTest; run them by hand. Arguments:
`TARGET`, `SOURCES` (required), `CUDA`, `LINK`, `INC`. The performance tests
also link the gperftools profiler and tcmalloc when found.

### Executables

`seeker_add_tool(NAME SRC ...)`: command-line tool in `bin/tools/cpp/`,
installed to the binary directory unless `NO_INSTALL`, and part of the
`seeker_cpp_tools` target. `seeker_add_app(NAME SRC ...)`: application in
`bin/`, installed unless `NO_INSTALL`, with a UPX copy in release builds when
`ENABLE_UPX` is on, unless `NO_UPX`.

| Argument     | Value        | Notes                                                                           |
| ------------ | ------------ | ------------------------------------------------------------------------------- |
| `NAME`       | target       | required                                                                        |
| `SRC`        | files        | required; a `.cu` source compiles as CUDA, so its tool declares `REQUIRES CUDA` |
| `LINK`       | targets      | linked `PRIVATE`                                                                |
| `INC`        | directories  | `PRIVATE` include directories                                                   |
| `DEFS`       | definitions  | `PRIVATE` compile definitions                                                   |
| `FEATURES`   | features     | `PRIVATE` compile features                                                      |
| `REQUIRES`   | requirements | see above                                                                       |
| `BAREMETAL`  | flag         | also built on bare-metal                                                        |
| `NO_INSTALL` | flag         | not installed                                                                   |
| `NO_UPX`     | flag         | `seeker_add_app` only: no UPX copy                                              |

`seeker_finalize_cpp_tools()` creates the `seeker_cpp_tools` aggregate target;
call it once, after the last tool.

### Module and opt-in helpers

| Helper                                               | Arguments              | Does                                                                                                                                              |
| ---------------------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `seeker_module(<name>)`                              | name                   | sets `LIB_NAME`, `SRC_DIR`, `INC_DIR`, `UTST_DIR`, `PTST_DIR`, `DTST_DIR`, `TST_DIR`, `README_FILE` in the caller                                 |
| `seeker_add_test_subdirs([UTST] [PTST] [DTST])`      | `UTST`, `PTST`, `DTST` | adds the module's `utst/`, `ptst/`, `dtst/` directories that have a `CMakeLists.txt` (all three without arguments), in the top-level project only |
| `seeker_standard_optins(<target>)`                   | target                 | Doxygen docs (`PROJECT_BUILD_DOCS`), UPX copy (`ENABLE_UPX`), clang-tidy on `.cu` files (`ENABLE_CLANG_TIDY`); no coverage                        |
| `seeker_install_docs(<path> [DESTINATION <subdir>])` | path, `DESTINATION`    | installs a markdown file or directory under the doc directory (release builds, or no build type)                                                  |
| `seeker_nvml_enable(<target>)`                       | target                 | links NVML when found and defines `COMPAT_NVML_AVAILABLE` to 1 or 0                                                                               |
| `seeker_cupti_enable(<target>)`                      | target                 | links CUPTI when found and defines `COMPAT_CUPTI_AVAILABLE` to 1 or 0                                                                             |

### Lower-level helpers

Called by the helpers above; a leaf file rarely needs them.

| Helper                                                                 | Arguments                                                                                           |
| ---------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `seeker_cuda_sources(<target> FILES ...)`                              | `FILES` (required), `DEFINE`, `NO_CUDART`, `SEPARABLE`, `RESOLVE_DEVICE_SYMBOLS`                    |
| `seeker_add_doxygen(<target> ...)`                                     | `README`, `SRC`, `INC`, `TST`, `DOCS`, `TOOLS`, `TEMPLATES`, `EXTRA_DIRS`, `HEADERS_ONLY`, `NO_TST` |
| `seeker_add_upx_copy(<target>)`                                        | none                                                                                                |
| `seeker_clang_tidy_cuda(<target> FILES ...)`                           | `FILES` (required), `ALLOW_FAILURE`                                                                 |
| `seeker_coverage_init()`, `seeker_coverage_register(<test> <library>)` | none                                                                                                |
| `seeker_guard(<target>)`, `seeker_is_cuda_target(<target> <out_var>)`  | none                                                                                                |

Each helper's comment block in its module lists its arguments and states what
it does.

---

## 6. Coverage

With `ENABLE_COVERAGE` (Clang, native Debug builds; `make coverage`),
`seeker_add_gtest` maps a test to each library in its `LINK` that coverage can
map, or to its `COVERAGE_FOR` library, instruments both and adds a
`<test>_coverage` test labelled `Coverage`.

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
kept. When a report cannot be produced it still writes the others and then
fails, naming each failed library.
The coverage flags are never installed or exported.

---

## 7. The Check

`tools/cmake/check_leaf_cmake.py` keeps leaf files declarative. It reads every
`CMakeLists.txt` and `*.cmake` under `src/` and `tools/`, prints each file's
commands, and fails on:

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
  helper comment block that does not name one of its keywords.

It reads the files with the lexer of cmakelang, the package of cmake-format, so
comments, quoted and bracket arguments are not commands. The helpers
themselves, the root `CMakeLists.txt` and `ExternalDependencies.cmake` are not
leaf files.

It runs as a pre-commit hook, so `make format-check`,
`make compose-format-check` and the CI format step run it; by hand:

```bash
python3 tools/cmake/check_leaf_cmake.py
```

It needs Python 3 and cmakelang (the lexer, cmake-format and
`cmake-genparsers`), both in the development containers.

---

## 8. Extending a Helper

1. Add the keyword to the helper's `cmake_parse_arguments` call: flags,
   one-value keywords or list keywords. `_seeker_check_arguments` then rejects
   its misspellings and an empty value; add it after `REQUIRED` there if the
   helper cannot work without it.
2. Implement it in the helper, after the argument check, with the visibility the
   target needs.
3. List it in the comment block above the function.
4. Regenerate the formatter signatures under `parse.additional_commands` in
   `.cmake-format.yaml` with
   `cmake-genparsers -f yaml cmake/seeker/*.cmake`.
5. Use it in the leaf file, run `make compose-format-check`, and configure once
   with a misspelled or empty use to see the helper's message.

Add a new helper only for a target family the existing ones cannot express; it
follows the same rules.
