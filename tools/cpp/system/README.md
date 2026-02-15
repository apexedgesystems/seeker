# System Diagnostics Tools

**Location:** `tools/cpp/system/`
**Library:** [src/system/](../../../src/system/)
**Domain:** System

Command-line tools for system diagnostics including kernel info, limits, drivers, and RT readiness validation.

---

## Tool Summary

| Tool          | Purpose                                      | Key Options                                 |
| ------------- | -------------------------------------------- | ------------------------------------------- |
| `sys-info`    | System identification and configuration dump | `--watchdog`, `--ipc`, `--security`, `--fd` |
| `sys-rtcheck` | RT readiness validation with pass/fail       | `--watchdog`, `--ipc`, `--fd`, `--json`     |
| `sys-limits`  | Process resource limits and capabilities     | `--all`, `--json`                           |
| `sys-drivers` | Kernel module inventory and GPU assessment   | `--json`, `--nvidia`, `--brief`             |

---

## sys-info

One-shot system identification displaying kernel, virtualization, RT scheduler, capabilities, limits, and container status.

```bash
# Human-readable output
$ sys-info

# Include watchdog details
$ sys-info --watchdog

# Include IPC resource details
$ sys-info --ipc

# Include security (LSM) details
$ sys-info --security

# Include file descriptor details
$ sys-info --fd

# Full output in JSON
$ sys-info --watchdog --ipc --security --fd --json
```

**Output includes:**

- Kernel release and preemption model
- RT cmdline flags (isolcpus, nohz_full, etc.)
- Virtualization environment (bare metal, VM, container)
- RT scheduler config (bandwidth, autogroup, DEADLINE support)
- Linux capabilities (CAP_SYS_NICE, CAP_IPC_LOCK, etc.)
- Key resource limits (RTPRIO, MEMLOCK, NOFILE)
- Container detection and cgroup limits
- Watchdog device status (with `--watchdog`)
- IPC resource usage (with `--ipc`)
- Security/LSM status (with `--security`)
- File descriptor usage (with `--fd`)

---

## sys-rtcheck

Validates RT readiness with pass/warn/fail checks and actionable recommendations.

```bash
# Run standard checks
$ sys-rtcheck

# Include watchdog availability check
$ sys-rtcheck --watchdog

# Include IPC resource limit checks
$ sys-rtcheck --ipc

# Include file descriptor headroom check
$ sys-rtcheck --fd

# JSON output for CI integration
$ sys-rtcheck --json

# All optional checks
$ sys-rtcheck --watchdog --ipc --fd --json
```

**Checks performed:**

1. Kernel Preemption (PREEMPT_RT optimal)
2. Virtualization Environment (bare metal optimal)
3. RT Bandwidth (95%+ or unlimited)
4. RT Autogroup (should be disabled)
5. RT Scheduling Capability (CAP_SYS_NICE or root)
6. RTPRIO Limit (>0 required, 99 optimal)
7. Memory Locking (CAP_IPC_LOCK or root + unlimited MEMLOCK)
8. Kernel Taint Status (proprietary/out-of-tree modules)
9. RT Cmdline Flags (isolcpus, nohz_full, rcu_nocbs)
10. Container CPU Limits (warn if throttled)
11. Container Memory Limits (warn if restricted)
12. Watchdog Availability (with `--watchdog`)
13. IPC Resource Limits (with `--ipc`)
14. FD Headroom (with `--fd`) - PASS <75%, WARN 75-90%, FAIL >90%

**Exit codes:** 0=all pass, 1=warnings, 2=failures

---

## sys-limits

Displays process resource limits and Linux capabilities.

```bash
# Show RT-relevant limits
$ sys-limits

# Show all limits
$ sys-limits --all

# JSON output
$ sys-limits --json
```

---

## sys-drivers

Displays loaded kernel modules and GPU driver assessment.

```bash
# Brief summary
$ sys-drivers --brief

# NVIDIA-focused output
$ sys-drivers --nvidia

# Full module inventory
$ sys-drivers

# JSON output
$ sys-drivers --json
```

---

## See Also

- [System Domain API](../../../src/system/README.md) - Library API reference
