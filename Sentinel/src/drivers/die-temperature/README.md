# `drivers/die-temperature/` — PSoC 6 on-die temperature

Reads the PSoC 6's **internal die temperature** from the SAR ADC's DieTemp sensor
— no external part. Surfaced over BLE as the System *CPU Temperature*
characteristic and useful as a coarse board-health signal.

| | |
|---|---|
| **Key type** | `sentinel::drivers::psoc6_die_temperature` |
| **Files** | [`sentinel_psoc6_die_temperature.hpp`](sentinel_psoc6_die_temperature.hpp) · [`sentinel_psoc6_die_temperature.cpp`](sentinel_psoc6_die_temperature.cpp) |
| **Issue** | #55 |

## How it works

SAR DieTemp channel with the 1.2 V band-gap reference, 32× averaging, and the
SFLASH dual-slope conversion. The reading is cached, throttled, and
mutex-guarded; the SAR runs on a free 8-bit divider so it doesn't disturb other
ADC users. ~34 °C on-bench, cross-checked against the BME280 / DS3231 ambient
readings.

```cpp
sentinel::drivers::psoc6_die_temperature die_temp;
die_temp.initialize();
die_temp.refresh();                       // throttled SAR read into the cache
int16_t centi_c{};
if (die_temp.cached_centi_c(centi_c)) { /* centi_c in 0.01 °C */ }
```

## See also

- [`task/`](../../task/) — `cpu_die_temp_service` (heartbeat-gated periodic read).
- [`bluetooth/`](../../bluetooth/) — `gatt::system` (CPU Temperature characteristic).
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
