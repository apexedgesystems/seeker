# Network Diagnostics Tools

**Location:** `tools/cpp/network/`
**Library:** [src/network/](../../../src/network/)
**Domain:** Network

Command-line tools for network diagnostics including interface info, statistics, and RT configuration validation.

---

## Tool Summary

| Tool          | Purpose                                  | Key Options                                                    |
| ------------- | ---------------------------------------- | -------------------------------------------------------------- |
| `net-info`    | Network interface and config dump        | `--json`, `--physical`, `--verbose`, `--ethtool`               |
| `net-rtcheck` | RT network config validation             | `--json`, `--cpus <list>`, `--verbose`                         |
| `net-stat`    | Continuous per-interface traffic monitor | `--json`, `--interval`, `--count`, `--interface`, `--physical` |

---

## net-info

One-shot network system dump displaying interfaces, socket buffers, busy polling status, NIC IRQ affinities, and optionally ethtool settings.

```bash
# Human-readable output
$ net-info

# With ethtool info (ring buffers, coalescing, offloads)
$ net-info --ethtool

# Verbose ethtool (shows all features)
$ net-info --ethtool --verbose

# JSON for scripting
$ net-info --json

# Physical NICs only with details
$ net-info --physical --verbose
```

**Output includes:**

- Interface list with link status, speed, MTU, MAC
- Socket buffer limits (rmem, wmem, TCP buffers)
- Busy polling configuration
- NIC IRQ affinity summary
- Ethtool info (with `--ethtool`): ring buffers, coalescing, offloads, RT score
- Configuration assessment (low-latency/high-throughput ready)

---

## net-rtcheck

Validates RT network configuration with pass/warn/fail checks and actionable recommendations.

```bash
# Check all (no RT CPU specified = skip IRQ check)
$ net-rtcheck

# Check with specific RT CPUs
$ net-rtcheck --cpus 2-4,6

# Verbose output with details
$ net-rtcheck --cpus 2-3 --verbose

# JSON output for CI integration
$ net-rtcheck --json
```

**Checks performed:**

| Check            | PASS                    | WARN                       | FAIL              |
| ---------------- | ----------------------- | -------------------------- | ----------------- |
| NIC IRQ Affinity | No IRQs on RT CPUs      | IRQs present on RT CPUs    | -                 |
| Busy Polling     | Enabled                 | Disabled                   | -                 |
| Socket Buffers   | >= 16 MiB               | < 16 MiB                   | -                 |
| Netdev Backlog   | >= 10000                | < 10000                    | -                 |
| Link State       | All physical NICs up    | Some down                  | -                 |
| Packet Drops     | Zero drops              | Low rate (<10/s)           | High rate (>10/s) |
| IRQ Coalescing   | Low values, no adaptive | Adaptive or >50us delay    | -                 |
| LRO Status       | Disabled                | Enabled (adds latency)     | -                 |
| Pause Frames     | Disabled                | Enabled (can cause stalls) | -                 |
| NIC RT Score     | Average >= 80           | Average 60-79              | -                 |

**Exit codes:** 0=pass, 1=warnings, 2=failures

---

## net-stat

Continuous per-interface traffic monitoring with throughput and packet rates.

```bash
# Default: 1 second interval, continuous (Ctrl+C to stop)
$ net-stat

# Custom interval and count
$ net-stat --interval 500 --count 10

# Monitor specific interface
$ net-stat --interface eth0

# Physical interfaces only, JSON output
$ net-stat --physical --json --count 5
```

**Output columns:** Interface, RX Mbps, TX Mbps, RX pps, TX pps, Drops/s, Errors/s

---

## See Also

- [Network Domain API](../../../src/network/README.md) - Library API reference
