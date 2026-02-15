# Timing Diagnostics Tools

**Location:** `tools/cpp/timing/`
**Library:** [src/timing/](../../../src/timing/)
**Domain:** Timing

Command-line tools for timing diagnostics including clocksource info, timer configuration, benchmarks, and time synchronization.

---

## Tool Summary

| Tool             | Purpose                             | Key Options                                       |
| ---------------- | ----------------------------------- | ------------------------------------------------- |
| `timing-info`    | Timing system configuration dump    | `--json`, `--rtc`                                 |
| `timing-rtcheck` | RT timing validation with pass/fail | `--json`, `--verbose`, `--rtc`                    |
| `timing-bench`   | Sleep jitter benchmark              | `--budget`, `--target`, `--priority`, `--abstime` |
| `timing-sync`    | Time synchronization status         | `--json`, `--verbose`, `--ptp`                    |

---

## timing-info

One-shot timing system dump displaying clocksource, timer resolutions, timer slack, and tickless configuration.

```bash
# Human-readable output
$ timing-info

# Include hardware RTC status
$ timing-info --rtc

# JSON for scripting
$ timing-info --json

# Full output with RTC in JSON
$ timing-info --rtc --json
```

**Output includes:**

- Current clocksource and available alternatives
- Timer resolution for all 6 clock types
- Timer slack value and status
- High-res timer and PREEMPT_RT status
- Tickless (nohz_full) CPU configuration
- RT suitability scores
- (with `--rtc`) Hardware RTC devices, health, time, and drift

---

## timing-rtcheck

Validates RT timing configuration with pass/warn/fail checks and actionable recommendations.

```bash
# Quick validation
$ timing-rtcheck

# Include RTC drift validation
$ timing-rtcheck --rtc

# With detailed recommendations
$ timing-rtcheck --verbose

# JSON output for CI integration
$ timing-rtcheck --json
```

**Checks performed:**

| Check           | PASS            | WARN            | FAIL             |
| --------------- | --------------- | --------------- | ---------------- |
| Clocksource     | TSC             | HPET, acpi_pm   | Unknown/unstable |
| High-Res Timers | Enabled (<=1us) | -               | Disabled (>1ms)  |
| Timer Slack     | Minimal (1ns)   | Default (~50us) | -                |
| NO_HZ Full      | Configured      | Not configured  | -                |
| PREEMPT_RT      | Yes             | No              | -                |
| RTC Drift\*     | <=5 sec         | >5 sec, no RTC  | -                |

\*Only with `--rtc` flag

**Exit codes:** 0=all pass, 1=any failures

---

## timing-bench

Measures timer overhead and sleep jitter with configurable parameters.

```bash
# Quick benchmark (default: 250ms, 1ms target)
$ timing-bench

# Thorough benchmark (5 seconds)
$ timing-bench --thorough

# Custom configuration
$ timing-bench --budget 1000 --target 100

# With TIMER_ABSTIME for reduced jitter
$ timing-bench --abstime

# Full RT characterization (requires CAP_SYS_NICE)
$ sudo timing-bench --budget 2000 --target 100 --abstime --priority 90

# JSON output
$ timing-bench --json
```

**Options:**

- `--budget <ms>` - Measurement duration (default: 250)
- `--target <us>` - Sleep target duration (default: 1000)
- `--priority <1-99>` - SCHED_FIFO priority for measurement
- `--abstime` - Use TIMER_ABSTIME mode
- `--quick` - Quick preset (250ms budget)
- `--thorough` - Thorough preset (5s budget)

**Output includes:**

- Sample count and configuration
- now() overhead
- Full percentile table (min, mean, median, p90, p95, p99, p99.9, max, stddev)
- Jitter analysis (actual - target)
- RT score and recommendations

---

## timing-sync

Displays time synchronization status including NTP/PTP daemons, PTP hardware, and kernel time state.

```bash
# Basic status
$ timing-sync

# Detailed PTP hardware capabilities
$ timing-sync --ptp

# Detailed kernel time info
$ timing-sync --verbose

# All details
$ timing-sync --ptp --verbose

# JSON output
$ timing-sync --json
```

**Output includes:**

- Detected sync daemons (chrony, ntpd, systemd-timesyncd, linuxptp)
- PTP hardware devices with clock names
- (with `--ptp`) Detailed PTP capabilities: max adjustment, PPS, ext-ts, periodic outputs, pins
- (with `--ptp`) NIC-to-PTP clock bindings
- (with `--ptp`) Best clock recommendation for RT workloads
- Kernel time synchronization status
- Time offset and error estimates
- Assessment and recommendations

---

## See Also

- [Timing Domain API](../../../src/timing/README.md) - Library API reference
