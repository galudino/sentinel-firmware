# `bluetooth/` — BLE stack context + GATT accessor layer

The Bluetooth LE surface: the stack/connection context and a thin **accessor
layer** over the generated GATT database. GATT services are **chip-named** (they
mirror the drivers), and this layer is the single seam between the generated
`app_*` / `HDLC_*` symbols and the rest of the firmware.

| | |
|---|---|
| **Key types** | `ble_context`, `ble_gatt`, and the per-service `gatt::*` accessors |
| **Stack/context** | [`sentinel_ble_context.hpp`](sentinel_ble_context.hpp) / [`.cpp`](sentinel_ble_context.cpp) · [`sentinel_ble_gatt.hpp`](sentinel_ble_gatt.hpp) / [`.cpp`](sentinel_ble_gatt.cpp) |
| **Issues** | #6 (GATT catalog), #45 (DIS + Platform ID), #55 (CPU temperature char) |

## GATT accessor layer

The GATT DB itself is defined in `src/design.cybt` and regenerated into
`GeneratedSource/` (gitignored). Nothing outside this folder touches those raw
generated globals — the `gatt::*` accessors wrap them as `inline` + `noexcept`
accessor/mutator pairs, with the notify-sender in the same layer:

| Accessor namespace | File | Service |
|---|---|---|
| `gatt::bme280` | [`sentinel_gatt_bme280.hpp`](sentinel_gatt_bme280.hpp) | BME280 ambient sample (packed `bme280_sample`) |
| `gatt::ds3231` | [`sentinel_gatt_ds3231.hpp`](sentinel_gatt_ds3231.hpp) | DS3231 time (R/W — BLE time-sync), RTC temp, alarms |
| `gatt::system` | [`sentinel_gatt_system.hpp`](sentinel_gatt_system.hpp) | System service: CPU temperature, clear-store, bootloader |
| `gatt::snapshot_stream` | [`sentinel_gatt_snapshot_stream.hpp`](sentinel_gatt_snapshot_stream.hpp) | Live snapshot stream (lane 2) + enable char |
| `gatt::paged` | [`sentinel_gatt_paged.hpp`](sentinel_gatt_paged.hpp) | Paged reads: Snapshot History + System Event Log |
| `gatt::dis` | [`sentinel_gatt_dis.hpp`](sentinel_gatt_dis.hpp) | Device Information Service (0x180A) + Platform ID |
| `gatt::battery` | [`sentinel_gatt_battery.hpp`](sentinel_gatt_battery.hpp) | Battery Service |
| `gatt::debug` | [`sentinel_gatt_debug.hpp`](sentinel_gatt_debug.hpp) | Debug output stream |

The accessors are `inline`+`noexcept` (never `constexpr` — they read extern
GATT-DB globals). Producers (the [service tasks](../task/)) call the mutator +
notify sender; async work triggered from a BLE callback (clear store, request
bootloader) is deferred to [`ble_maintenance_task`](../task/) off the BT callback.

## See also

- [`task/`](../task/) — the producers + `ble_maintenance_task`.
- [`telemetry/`](../telemetry/) · [`diagnostics/`](../diagnostics/) — snapshot stream + event-log sources.
- `app_bt_utils.*` — vendored BT helper (excluded from Doxygen).
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md).
