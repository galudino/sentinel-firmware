# `diagnostics/` — POST + System Event Log

Two boot-and-runtime health facilities: the **Power-On Self-Test** that vets the
hardware at boot, and the flash-backed **System Event Log** that records what
happened, as typed records.

| | |
|---|---|
| **Key types** | `diagnostics::post`, `diagnostics::system_event_log<Store>`, `diagnostics::system_event` |
| **Files** | [`sentinel_post.hpp`](sentinel_post.hpp) · [`sentinel_system_event.hpp`](sentinel_system_event.hpp) · [`sentinel_system_event_log.hpp`](sentinel_system_event_log.hpp) |
| **Issues** | #35 (POST), #34 (System Event Log) |

## POST — `post::run(...)`

Runs at boot against the **real** drivers (decision #15), probing each subsystem
and returning a `summary` of per-subsystem `post_result`s. The [boot
orchestrator](../app/) caches the first failure; a BLE-stack failure degrades boot
rather than asserting (decision #12).

```cpp
const auto summary = sentinel::diagnostics::post::run(bme, rtc, flash,
                                                      /* ...other subsystems */);
// summary carries a post_subsystem_result per post_subsystem
```

## System Event Log — `system_event_log<Store>`

A typed event recorder layered over a [`record_store`](../storage/). Each event is
a fixed 36-byte record with a typed variant body — boot lifecycle, firmware
update, BLE connection, fault, mode change, POST result, snapshot event:

```cpp
sentinel::diagnostics::system_event_log<event_store_t> log;
log.initialize(store, now_unix_fn);
log.record_boot_complete(fw_version, /* ... */);  // typed helpers append the right record
```

`system_event` is the event-type enum; the per-type record structs
(`boot_lifecycle_record`, `fault_record`, `post_result_record`,
`snapshot_event_record`, …) live in [`sentinel_system_event.hpp`](sentinel_system_event.hpp).

## See also

- [`storage/`](../storage/) — the `record_store` the log persists to.
- [`app/`](../app/) — runs POST and drains the log at boot · [`bluetooth/`](../bluetooth/) — paged GATT read of the log.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) (#12, #15).
