# `test/` — testbench suites (one per subsystem)

Run-to-completion test suites, one per subsystem. Each suite drives the **real**
component (decision #15) — real drivers, real buses, real flash — not a mock; the
only doubles are where a component *is* a test double (e.g.
[`ram_record_store`](../storage/) standing in for the flash store). Suites are run
bottom-up by the [`test_orchestrator`](../testbench/).

| | |
|---|---|
| **Entry point** | `test::<subsystem>::run_all()` → `test_result` tally |
| **Result type** | [`sentinel_test_result.hpp`](sentinel_test_result.hpp) |

## Suites

| Suite | Covers |
|---|---|
| [`sentinel_test_w25q128`](sentinel_test_w25q128.cpp) | W25Q128 erase/program/read on the real SPI NOR |
| [`sentinel_test_record_store`](sentinel_test_record_store.cpp) | Flash-backed circular store (incl. fast-scan recovery, #49) |
| [`sentinel_test_system_event_log`](sentinel_test_system_event_log.cpp) | Typed event log (RAM-backed store) |
| [`sentinel_test_bme280`](sentinel_test_bme280.cpp) | BME280 chip-id, reset, settings round-trip, sample |
| [`sentinel_test_ds3231`](sentinel_test_ds3231.cpp) | DS3231 time round-trip, temperature |
| [`sentinel_test_die_temperature`](sentinel_test_die_temperature.cpp) | PSoC 6 SAR die-temperature |
| [`sentinel_test_post`](sentinel_test_post.cpp) | POST (fake-driven subsystem probes) |
| [`sentinel_test_device_snapshot`](sentinel_test_device_snapshot.cpp) | `device_snapshot` populate/pack |
| [`sentinel_test_snapshot_persistence`](sentinel_test_snapshot_persistence.cpp) | Lane-1 persistence task over a scratch store |
| [`sentinel_test_snapshot_stream`](sentinel_test_snapshot_stream.cpp) | Lane-2 live stream task |
| [`sentinel_test_driver_file_template`](sentinel_test_driver_file_template.cpp) | The new-driver template |

Each suite exposes `run_all()`; the orchestrator runs every suite to completion
and tallies pass/fail over serial.

## See also

- [`testbench/`](../testbench/) — the orchestrator that runs these.
- [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) — #15 (tests drive real components).
- [`../README.md`](../README.md) — source-tree map. Build/flash: [`docs/architecture/hardware-bench.md`](../../../docs/architecture/hardware-bench.md).
