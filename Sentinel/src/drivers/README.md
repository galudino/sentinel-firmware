# `drivers/` — chip-named device drivers

One subdirectory per physical part. Each driver is **pure logic parameterised over
a [transport](../transport/)** — it contains no HAL calls, so the same driver runs
on the PSoC 6 today and on another platform later by swapping the template
argument. Drivers return `std::optional<T>` for value-producing reads and `bool`
for actions.

| Subdirectory | Part | Key type | Bus |
|---|---|---|---|
| [`flash-memory/`](flash-memory/) | Winbond W25Q128JV 128 Mbit SPI NOR | `w25q128<Transport>` | SPI |
| [`real-time-clock/`](real-time-clock/) | Maxim DS3231 RTC | `ds3231<Transport>` | I²C |
| [`temperature-humidity-pressure/`](temperature-humidity-pressure/) | Bosch BME280 | `bme280<Transport>` | I²C or SPI |
| [`die-temperature/`](die-temperature/) | PSoC 6 on-die temp (SAR ADC) | `psoc6_die_temperature` | on-chip |
| [`led/`](led/) | PWM-driven LED | `led_pwm<PWM>` | PWM |
| [`tachometer/`](tachometer/) | Tachometer input + RPM windowing | `tachometer_input`, `tach_rpm_windowed` | GPIO/TCPWM |
| [`cyhal/`](cyhal/) | PSoC 6-specific driver bindings | `tachometer_psoc6` | — |

## Conventions

- **Chip-named** — the directory and class say what part it is, on both this repo
  and the [`sentinel-client`](../../../docs/architecture/decisions.md).
- **Transport-parameterised** — `template <typename Transport> class w25q128`,
  compile-time tag detection for I²C vs SPI where a part supports both (BME280).
- **Optional/bool API + `last_error()`** — the happy path is compact; the raw
  vendor error code stays recoverable for diagnostics.
- New drivers start from [`../driver_file_template/`](../driver_file_template/).

```cpp
sentinel::bme280_i2c<cyhal_i2c_bus_transport> sensor(bus, 0x76);
if (auto id = sensor.read_chip_id()) { /* expect 0x60 */ }
```

## See also

- [`transport/`](../transport/) — the façade every driver speaks to.
- [`task/`](../task/) — the service tasks that drive these on a cadence.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md).
