# CPU Diagnostic Tools

**Domain:** CPU
**Tool Count:** 7
**Source:** `tools/cpp/cpu/`

Command-line tools for CPU diagnostics, monitoring, and RT readiness validation.

---

## Tool Summary

| Tool           | Purpose                                   | Key Options                                  |
| -------------- | ----------------------------------------- | -------------------------------------------- |
| `cpu-info`     | System identification and capability dump | `--json`                                     |
| `cpu-rtcheck`  | RT readiness validation with pass/fail    | `--json`, `--cpus <list>`                    |
| `cpu-corestat` | Per-core utilization monitor              | `--json`, `--interval`, `--count`, `--cpus`  |
| `cpu-irqmap`   | IRQ/softirq distribution display          | `--json`, `--interval`, `--top`, `--softirq` |
| `cpu-thermal`  | Temperature and throttling status         | `--json`, `--watch`, `--interval`            |
| `cpu-affinity` | Query/set CPU affinity                    | `--json`, `--pid`, `--set`, `--get`          |
| `cpu-snapshot` | Full diagnostic state dump                | `--json`, `--output`, `--brief`              |

---

## cpu-info

One-shot system identification displaying topology, ISA features, frequency, and isolation config.

```bash
# Human-readable output
cpu-info

# JSON for scripting
cpu-info --json
```

**Output includes:**

- CPU topology (sockets, cores, threads, NUMA nodes)
- ISA features (AVX, AVX2, AVX-512, invariant TSC)
- Frequency and governor state per core
- System stats (kernel, RAM, load, uptime)
- CPU isolation configuration

---

## cpu-rtcheck

Validates RT readiness with pass/warn/fail checks and actionable recommendations.

```bash
# Check all CPUs (or isolated CPUs if configured)
cpu-rtcheck

# Check specific CPUs
cpu-rtcheck --cpus 2-4,6

# JSON output for CI integration
cpu-rtcheck --json
```

**Checks performed:**

1. CPU Isolation (isolcpus + nohz_full + rcu_nocbs)
2. CPU Governor ("performance" required)
3. C-State Latency (<=10us threshold)
4. IRQ Affinity (no device IRQs on RT cores)
5. Softirq Load (<1000/s warn, <10000/s fail)
6. Invariant TSC (required for reliable timing)

**Exit codes:** 0=pass, 1=warnings, 2=failures

---

## cpu-corestat

Continuous per-core utilization monitor using snapshot + delta measurement.

```bash
# Default: 1 second interval, continuous
cpu-corestat

# Custom interval and count
cpu-corestat --interval 500 --count 10

# Monitor specific CPUs
cpu-corestat --cpus 2-4 --json
```

**Output columns:** CPU, user%, sys%, idle%, iowait%, active%

---

## cpu-irqmap

Displays hardware and software interrupt distribution across cores.

```bash
# One-shot IRQ measurement (1 second sample)
cpu-irqmap

# Show top 10 interrupt sources
cpu-irqmap --top 10

# Include softirq breakdown
cpu-irqmap --softirq

# Custom measurement interval
cpu-irqmap --interval 2000
```

**Output includes:**

- Per-core IRQ rates
- Top interrupt sources with distribution
- Softirq type breakdown (with `--softirq`)

---

## cpu-thermal

Displays CPU temperatures, power limits, and throttling status.

```bash
# One-shot status
cpu-thermal

# Continuous monitoring
cpu-thermal --watch

# Custom refresh interval
cpu-thermal --watch --interval 5000
```

**Output includes:**

- Throttling status (thermal, power limit, current limit)
- Temperature sensors with color-coded warnings
- RAPL power limits (Intel)

---

## cpu-affinity

Query or modify CPU affinity for processes.

```bash
# Get current process affinity
cpu-affinity

# Get affinity for specific PID
cpu-affinity --pid 1234

# Set affinity to specific CPUs
cpu-affinity --set 2-4,6

# Set affinity for another process
cpu-affinity --pid 1234 --set 2,3
```

**Output includes:**

- Current affinity mask
- Isolation context (shows overlap with isolated CPUs)

---

## cpu-snapshot

Full diagnostic state dump for bug reports, before/after comparisons, and CI baselines.

```bash
# JSON dump to stdout (default)
cpu-snapshot

# Write to file
cpu-snapshot --output system-state.json

# Brief human-readable summary
cpu-snapshot --brief
```

**Includes data from all modules:**

- Topology, features, frequency, stats
- Isolation, C-states, thermal
- Utilization (1-second sample)
- IRQ and softirq summaries

---

## See Also

- [CPU Diagnostics Library](../../../src/cpu/README.md) - API documentation
