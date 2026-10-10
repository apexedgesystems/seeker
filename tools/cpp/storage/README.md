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

- Block device list with type (NVMe, SSD, HDD, SD/eMMC, Removable or Unknown), size, sector sizes
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

| Check            | PASS                           | WARN                                       | FAIL      |
| ---------------- | ------------------------------ | ------------------------------------------ | --------- |
| Device Types     | NVMe or SSD detected           | No NVMe or SSD (e.g., SD/eMMC or HDD only) | -         |
| Scheduler        | `none` (NVMe) or `mq-deadline` | Other scheduler                            | -         |
| Queue Depth      | <= 32                          | > 128                                      | -         |
| Read-ahead       | 0 or <= 128 KB                 | > 128 KB                                   | -         |
| Mount Options    | noatime/relatime               | atime enabled                              | nobarrier |
| Overall RT Score | >= 70                          | 40-69                                      | < 40      |

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

Bounded storage benchmark runner for performance characterization. Each of the
five benchmarks stops at its time budget (`--budget`, default 30 s), setup and
syncs included, so a run takes at most about five budgets. `--size` caps the
data each benchmark writes: whole 4 KiB blocks, never past it; a tight budget
stops it first.

```bash
# Quick run: 8 MB, 100 iterations, 10 s budget per benchmark
$ storage-bench --quick

# Full benchmark in /tmp
$ storage-bench

# Test specific directory
$ storage-bench --dir /mnt/data --quick

# Bypass the page cache with O_DIRECT (the filesystem must support it)
$ storage-bench --direct --quick

# Custom parameters
$ storage-bench --size 128 --iters 500 --budget 60

# Tight budget: about 1 s per benchmark whatever the size
$ storage-bench --size 1024 --budget 1

# JSON output
$ storage-bench --quick --json
```

**Benchmarks run:**

| Benchmark        | Description                                 | Primary Metric    |
| ---------------- | ------------------------------------------- | ----------------- |
| Sequential Write | 4 KiB sequential writes, synced every 4 MiB | Throughput (MB/s) |
| Sequential Read  | 4 KiB sequential reads (may hit the cache)  | Throughput (MB/s) |
| fsync Latency    | Durability commit latency                   | p99 latency (us)  |
| Random Read 4K   | Small random reads (may hit the cache)      | Avg latency (us)  |
| Random Write 4K  | Small random writes with fsync              | Avg latency (us)  |

Sequential-write throughput includes its syncs: it is the rate at which data is
written and synced, not the rate of filling the page cache. Both read
benchmarks first ask the kernel to drop their file's cached pages, so where it
does (not on tmpfs) they measure the device.

---

## See Also

- [Storage Domain API](../../../src/storage/README.md) - Library API reference
