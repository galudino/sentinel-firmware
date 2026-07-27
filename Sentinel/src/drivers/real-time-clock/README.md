# `drivers/real-time-clock/` — DS3231 RTC

Driver for the Maxim **DS3231** temperature-compensated I²C real-time clock — the
system's wall-clock time source (Unix seconds) and a spare temperature channel.

| | |
|---|---|
| **Key type** | `sentinel::ds3231<Transport>` (I²C) |
| **Files** | [`sentinel_ds3231.hpp`](sentinel_ds3231.hpp) · [`sentinel_ds3231.cpp`](sentinel_ds3231.cpp) |
| **Issues** | #5 (driver + SQW interrupt), #6 (BLE time-sync path) |

## API (selected)

```cpp
std::optional<uint32_t> unix_time() const noexcept;        // read wall clock
bool set_unix_time(uint32_t unix_seconds) noexcept;        // set wall clock
// + alarm flags, RTC temperature, square-wave (SQW) config
```

```cpp
sentinel::ds3231<cyhal_i2c_bus_transport> rtc(bus);
if (auto now = rtc.unix_time()) { /* seconds since epoch */ }
```

## Cadence & time-set

The RTC is read on a **1 Hz SQW-interrupt** cadence by
[`rtc_service`](../../task/) — the interrupt is the read trigger, keeping the
service off a polling loop. Setting the clock is a separate concern: the **BLE
time-sync write** goes through the DS3231 GATT service into `set_unix_time()`
(decision recorded in the RTC time-design note) — `rtc_service` itself stays
read-only, it never sets time.

## See also

- [`task/`](../../task/) — `rtc_service` (SQW-driven reads).
- [`bluetooth/`](../../bluetooth/) — `gatt::ds3231` (the R/W time characteristic).
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
