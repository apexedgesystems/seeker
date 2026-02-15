# Memory Diagnostics Tools

**Location:** `tools/cpp/memory/`
**Library:** [src/memory/](../../../src/memory/)
**Domain:** Memory

Command-line tools for memory diagnostics including NUMA topology, hugepages, and RT configuration validation.

---

## Tool Summary

| Tool          | Purpose                                    | Key Options                            |
| ------------- | ------------------------------------------ | -------------------------------------- |
| `mem-info`    | Memory system identification and status    | `--json`                               |
| `mem-rtcheck` | RT memory config validation with pass/fail | `--json`, `--size <bytes>`             |
| `mem-numa`    | NUMA topology with distances               | `--json`, `--distances`, `--hugepages` |

---

## mem-info

One-shot memory system dump displaying page sizes, RAM/swap usage, VM policies, hugepage allocation, memory locking limits, NUMA topology, and ECC/EDAC status.

```bash
# Human-readable output
$ mem-info

# JSON for scripting
$ mem-info --json
```

**Output includes:**

- Base page size and available hugepage sizes
- RAM usage (total, available, used, buffers, cached)
- Swap usage
- VM policies (swappiness, overcommit, zone reclaim, THP)
- Hugepage allocation status per size
- Memory locking limits and capabilities
- NUMA node summary
- **ECC/EDAC status (controllers, CE/UE counts)**

---

## mem-rtcheck

Validates RT memory configuration with pass/warn/fail checks and actionable recommendations.

```bash
# Check current configuration
$ mem-rtcheck

# Verify can lock specific amount
$ mem-rtcheck --size 1073741824  # 1 GiB

# JSON output for CI integration
$ mem-rtcheck --json
```

**Checks performed:**

| Check          | PASS                                 | WARN                          | FAIL                       |
| -------------- | ------------------------------------ | ----------------------------- | -------------------------- |
| Hugepages      | Configured and free                  | None configured or all in use | -                          |
| Memory Locking | Unlimited or sufficient for `--size` | Low limit (<64 MiB)           | Cannot lock requested size |
| THP            | `never` or `madvise`                 | `always`                      | -                          |
| Swappiness     | <= 30                                | 31-60                         | > 60                       |
| Overcommit     | 0 (heuristic) or 2 (never)           | 1 (always)                    | -                          |
| **ECC Memory** | **ECC enabled, no errors**           | **High CE count (>100)**      | **Any UE errors**          |

**Exit codes:** 0=pass, 1=warnings, 2=failures

---

## mem-numa

NUMA-focused view with per-node memory, CPU affinity, and optional distance matrix.

```bash
# Basic topology
$ mem-numa

# Show inter-node distance matrix
$ mem-numa --distances

# Show per-node hugepage allocation
$ mem-numa --hugepages

# JSON output
$ mem-numa --json
```

**Output includes:**

- Node count and total/free memory
- Per-node memory and CPU list
- NUMA distance matrix (with `--distances`)
- Per-node hugepage breakdown (with `--hugepages`)

---

## See Also

- [Memory Domain API](../../../src/memory/README.md) - Library API reference
