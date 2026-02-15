# GPU Diagnostics Tools

**Location:** `tools/cpp/gpu/`
**Library:** [src/gpu/](../../../src/gpu/)
**Domain:** GPU

Command-line tools for GPU diagnostics including topology, telemetry, and RT readiness validation.

---

## Tool Summary

| Tool          | Purpose                                | Key Options                                 |
| ------------- | -------------------------------------- | ------------------------------------------- |
| `gpu-info`    | GPU topology and capabilities          | `--json`, `--device <idx>`                  |
| `gpu-stat`    | Real-time GPU telemetry                | `--json`, `--device <idx>`, `--procs`       |
| `gpu-rtcheck` | RT readiness validation with pass/fail | `--json`, `--device <idx>`                  |
| `gpu-bench`   | GPU performance benchmarks (CUDA)      | `--json`, `--device`, `--budget`, `--quick` |

---

## gpu-info

Displays GPU topology, driver versions, PCIe links, and device capabilities.

```bash
# All GPUs
$ gpu-info

# Specific GPU
$ gpu-info --device 0

# JSON output
$ gpu-info --json
```

**Output includes:**

- Device name, vendor, UUID
- Compute capability, SM count, CUDA cores
- Memory capacity and bus width
- Execution limits (threads, shared memory)
- PCIe BDF, link width/generation, NUMA node
- Driver version, CUDA version
- Persistence mode, compute mode
- Device capabilities (concurrent kernels, managed memory, etc.)

---

## gpu-stat

Shows real-time GPU metrics: temperature, power, clocks, memory usage, throttling, and processes.

```bash
# All GPUs
$ gpu-stat

# Specific GPU
$ gpu-stat --device 0

# Include process list
$ gpu-stat --procs

# JSON output
$ gpu-stat --json
```

**Output includes:**

- Temperature (current, slowdown threshold)
- Power draw (current, limit, percentage)
- Clock speeds (SM, memory)
- Performance state (P0-P15)
- Fan speed
- Throttling status and reasons
- Memory usage (used/total, percentage)
- ECC status and errors
- MIG/MPS status
- Process count or detailed process list

---

## gpu-rtcheck

Validates GPU configuration for real-time suitability with pass/warn/fail checks.

```bash
# All GPUs
$ gpu-rtcheck

# Specific GPU
$ gpu-rtcheck --device 0

# JSON output for CI
$ gpu-rtcheck --json
```

**Checks performed:**

| Check             | PASS                  | WARN              | FAIL               |
| ----------------- | --------------------- | ----------------- | ------------------ |
| Persistence Mode  | Enabled               | Disabled          | -                  |
| Compute Mode      | Exclusive Process     | Default           | Prohibited         |
| Temperature       | <75 C                 | 75-85 C           | >85 C              |
| Throttling        | None                  | Power throttling  | Thermal throttling |
| ECC Memory        | Enabled, no errors    | Disabled          | Uncorrected errors |
| Retired Pages     | None                  | Single-bit ECC    | Double-bit ECC     |
| Driver Versions   | Compatible            | -                 | Incompatible       |
| PCIe Link         | At maximum            | Below maximum     | -                  |
| Process Isolation | Exclusive or no procs | Shared with procs | -                  |

**Exit codes:**

- 0 = All pass
- 1 = Warnings only
- 2 = Any failures

---

## gpu-bench

GPU performance benchmarks measuring bandwidth, latency, and allocation timing. **Requires CUDA.**

```bash
# Default benchmark (1s budget, 64 MiB transfers)
$ gpu-bench

# Quick preset (500ms budget, skip pageable)
$ gpu-bench --quick

# Thorough preset (5s budget)
$ gpu-bench --thorough

# Custom parameters
$ gpu-bench --device 0 --budget 2000 --size 128

# JSON output
$ gpu-bench --json
```

**Benchmarks run:**

| Benchmark     | Description                        | Primary Metric     |
| ------------- | ---------------------------------- | ------------------ |
| H2D Bandwidth | Host-to-device (pinned + pageable) | Throughput (MiB/s) |
| D2H Bandwidth | Device-to-host (pinned + pageable) | Throughput (MiB/s) |
| D2D Copy      | Device-to-device copy              | Throughput (MiB/s) |
| Kernel Launch | Empty kernel launch overhead       | Latency (μs)       |
| Memory Alloc  | cudaMalloc/cudaFree timing         | Latency (μs)       |
| Pinned Alloc  | cudaMallocHost/cudaFreeHost        | Latency (μs)       |
| Stream Ops    | Stream create/sync, event create   | Latency (μs)       |
| Occupancy     | Max blocks/warps per SM            | Count              |

**Options:**

| Option       | Description                         |
| ------------ | ----------------------------------- |
| `--device`   | GPU device index (default: 0)       |
| `--budget`   | Time budget in ms (default: 1000)   |
| `--size`     | Transfer size in MiB (default: 64)  |
| `--quick`    | Quick preset (500ms, skip pageable) |
| `--thorough` | Thorough preset (5s budget)         |
| `--json`     | JSON output                         |

**Exit codes:** 0=completed, 1=incomplete (budget exceeded) or error

---

## See Also

- [GPU Domain API](../../../src/gpu/README.md) - Library API reference
