# Device Diagnostics Tools

**Location:** `tools/cpp/device/`
**Library:** [src/device/](../../../src/device/)
**Domain:** Device

Command-line tools for device diagnostics including serial ports, I2C, SPI, CAN, and GPIO.

---

## Tool Summary

| Tool             | Purpose                                 | Key Options                        |
| ---------------- | --------------------------------------- | ---------------------------------- |
| `device-info`    | Overview of all device buses            | `--json`                           |
| `device-serial`  | Detailed serial port inspection         | `--port`, `--config`, `--json`     |
| `device-i2c`     | I2C bus info with optional device scan  | `--bus`, `--scan`, `--json`        |
| `device-can`     | CAN interface status and statistics     | `--interface`, `--stats`, `--json` |
| `device-rtcheck` | Device permissions and RT configuration | `--verbose`, `--json`              |

---

## device-info

One-shot overview of all device buses and ports.

```bash
# Human-readable output
$ device-info

# JSON for scripting
$ device-info --json
```

**Output includes:**

- Serial ports (count, types, access status)
- I2C buses (count, adapter names, accessibility)
- SPI devices (count, bus/CS, speed)
- CAN interfaces (count, type, state, bitrate)
- GPIO chips (count, line counts, accessibility)

---

## device-serial

Detailed serial port inspection with termios and USB information.

```bash
# List all serial ports
$ device-serial

# Show termios configuration for all ports
$ device-serial --config

# Query specific port
$ device-serial --port ttyUSB0 --config

# JSON output
$ device-serial --json
```

**Output includes:**

- Port type (USB-serial, built-in UART, ACM)
- Access status (readable, writable)
- Driver name
- USB details (VID/PID, manufacturer, product, serial)
- RS485 configuration
- Termios settings (baud rate, 8N1 notation, flow control)

---

## device-i2c

I2C bus enumeration with optional device scanning.

```bash
# List all I2C buses
$ device-i2c

# Show specific bus details
$ device-i2c --bus 1

# Scan for devices on bus
$ device-i2c --bus 1 --scan

# JSON output
$ device-i2c --json
```

**Output includes:**

- Bus number and adapter name
- Accessibility status
- Functionality flags (I2C, SMBus, 10-bit, PEC)
- Discovered devices (with --scan)

---

## device-can

CAN interface status and statistics.

```bash
# List all CAN interfaces
$ device-can

# Show traffic statistics
$ device-can --stats

# Query specific interface
$ device-can --interface can0 --stats

# JSON output
$ device-can --json
```

**Output includes:**

- Interface type (physical, virtual, slcan)
- Link state (UP/DOWN, RUNNING)
- Bus state (ERROR_ACTIVE, ERROR_PASSIVE, BUS_OFF)
- Bitrate and sample point
- Controller mode flags (FD, loopback, listen-only)
- Error counters (TEC/REC)
- Traffic statistics (TX/RX frames and bytes)

---

## device-rtcheck

Device permissions and RT configuration validation.

```bash
# Run all checks
$ device-rtcheck

# Show passing devices too
$ device-rtcheck --verbose

# JSON for CI integration
$ device-rtcheck --json
```

**Checks performed:**

1. Serial port access (read/write permissions)
2. I2C bus access (device node permissions)
3. SPI device access (device node permissions)
4. SPI configuration validity
5. CAN interface state (UP, not BUS_OFF)
6. CAN bitrate configuration
7. GPIO chip access (character device permissions)

**Exit codes:** 0=all pass, 1=warnings, 2=failures

---

## See Also

- [Device Domain API](../../../src/device/README.md) - Library API reference
