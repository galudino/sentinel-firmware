# `telemetry/` — `device_snapshot`

The canonical aggregate of live device state: one **packed struct** that captures
everything worth recording or streaming at an instant — sensor readings, storage
counts, BLE/link state, POST status, CPU die temp.

| | |
|---|---|
| **Key type** | `sentinel::telemetry::device_snapshot` (80-byte `__attribute__((packed))`) |
| **Files** | [`sentinel_device_snapshot.hpp`](sentinel_device_snapshot.hpp) · [`sentinel_device_snapshot.cpp`](sentinel_device_snapshot.cpp) |
| **Issues** | #36 (struct + `populate()`), #14 (two-lane model) |

## `populate()`

`populate()` fills a snapshot from the current system state — it reads the cached
sensor samples ([`bme280_service`](../task/), [`rtc_service`](../task/)), the die
temperature, storage counts, `ble_connected`, RSSI/TX-power, and POST status. It
only *reads* caches; it never touches a bus itself.

```cpp
sentinel::telemetry::device_snapshot snap{};
sentinel::telemetry::device_snapshot::populate(snap);   // fill from live state
```

## Two lanes consume it

The same populated snapshot feeds two independent lanes (decision #14):

- **Lane 1 — persistence:** [`snapshot_persistence_task`](../task/) appends it to a
  flash [`record_store`](../storage/) (~5 min) for history.
- **Lane 2 — live stream:** [`snapshot_stream_task`](../task/) sends it over the
  Snapshot Stream GATT characteristic (~100 ms) while a subscriber is enabled.

See [**Two-lane snapshot model**](../README.md#two-lane-snapshot-model).

## See also

- [`task/`](../task/) — the two lane tasks · [`storage/`](../storage/) — lane-1 backing store · [`bluetooth/`](../bluetooth/) — lane-2 GATT char.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) (#14).
