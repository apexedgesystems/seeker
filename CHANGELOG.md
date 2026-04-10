# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## v1.0.1

### Fixed

- `cpp_tools` aggregate target scoped with `PROJECT_NAME` to avoid collisions
  in multi-project builds
- Test and benchmark subdirectories guarded behind `PROJECT_IS_TOP_LEVEL` to
  prevent CTest registration and coverage manifest pollution when consumed via
  FetchContent

---

## v1.0.0

Initial release. System diagnostics library with hardware introspection and
RT-safety annotations covering CPU, memory, storage, network, timing, device,
GPU, and system domains.
