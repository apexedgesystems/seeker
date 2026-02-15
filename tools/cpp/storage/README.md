# Storage Diagnostics Tools

**Location:** `tools/cpp/storage/`
**Library:** [src/storage/](../../../src/storage/)
**Domain:** Storage

Command-line tools for storage diagnostics including block device info, I/O schedulers, statistics, and benchmarks.

---

## Tool Summary

| Tool              | Purpose                           | Key Options                                        |
| ----------------- | --------------------------------- | -------------------------------------------------- |
| `storage-info`    | Device and mount information dump | `--json`, `--device <name>`                        |
| `storage-rtcheck` | RT configuration validation       | `--json`, `--verbose`                              |
| `storage-iostat`  | Per-device I/O monitoring         | `--json`, `--interval`, `--count`, `--device`      |
| `storage-bench`   | Storage performance benchmarks    | `--json`, `--dir`, `--quick`, `--direct`, `--size` |

---

## storage-info

One-shot storage system dump displaying block devices, I/O schedulers, and mount configuration.

```bash
# Human-readable output
$ storage-info

# JSON for scripting
$ storage-info --json

# Single device details
$ storage-info --device nvme0n1
```

**Output includes:**

- Block device list with type (NVMe/SSD/HDD), size, sector sizes
- I/O scheduler and queue parameters per device
- RT score per device
- Block device mounts with filesystem type and options

---

## storage-rtcheck

Validates storage configuration for RT systems with pass/warn/fail checks.

```bash
# Check all devices
$ storage-rtcheck

# Show fix recommendations
$ storage-rtcheck --verbose

# JSON output for CI integration
$ storage-rtcheck --json
```

**Checks performed:**

| Check            | PASS                           | WARN            | FAIL      |
| ---------------- | ------------------------------ | --------------- | --------- |
| Device Types     | NVMe detected                  | HDD only        | -         |
| Scheduler        | `none` (NVMe) or `mq-deadline` | Other scheduler | -         |
| Queue Depth      | <= 32                          | > 128           | -         |
| Read-ahead       | 0 or <= 128 KB                 | > 128 KB        | -         |
| Mount Options    | noatime/relatime               | atime enabled   | nobarrier |
| Overall RT Score | >= 70                          | 40-69           | < 40      |

**Exit codes:** 0=pass, 1=warnings, 2=failures

---

## storage-iostat

Continuous per-device I/O statistics monitor using snapshot + delta measurement.

```bash
# Default: 1 second interval, continuous
$ storage-iostat

# Custom interval and count
$ storage-iostat --interval 2 --count 10

# Monitor specific device
$ storage-iostat --device nvme0n1 --count 5

# Fast sampling for latency analysis
$ storage-iostat --interval 0.1 --count 100

# JSON output
$ storage-iostat --count 3 --json
```

**Output columns:**

- `r/s`, `w/s` - Read/write IOPS
- `rKB/s`, `wKB/s` - Read/write throughput
- `r_lat`, `w_lat` - Average read/write latency (ms)
- `util%` - Device utilization percentage
- `qd` - Average queue depth

---

## storage-bench

Bounded storage benchmark runner for performance characterization.

```bash
# Quick test (~10 seconds)
$ storage-bench --quick

# Full benchmark in /tmp
$ storage-bench

# Test specific directory
$ storage-bench --dir /mnt/data --quick

# Bypass page cache (requires privileges)
$ sudo storage-bench --direct --quick

# Custom parameters
$ storage-bench --size 128 --iters 500 --budget 60

# JSON output
$ storage-bench --quick --json
```

**Benchmarks run:**

| Benchmark        | Description                        | Primary Metric    |
| ---------------- | ---------------------------------- | ----------------- |
| Sequential Write | Large sequential writes            | Throughput (MB/s) |
| Sequential Read  | Large sequential reads (may cache) | Throughput (MB/s) |
| fsync Latency    | Durability commit latency          | p99 latency (μs)  |
| Random Read 4K   | Small random reads                 | Avg latency (μs)  |
| Random Write 4K  | Small random writes with fsync     | Avg latency (μs)  |

---

## See Also

- [Storage Domain API](../../../src/storage/README.md) - Library API reference
