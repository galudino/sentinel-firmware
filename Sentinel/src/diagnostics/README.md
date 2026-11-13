# `diagnostics/` — POST + System Event Log

Two boot-and-runtime health facilities: the **Power-On Self-Test** that vets the
hardware at boot, and the flash-backed **System Event Log** that records what
happened, as typed records.

| | |
|---|---|
| **Key types** | `diagnostics::post`, `diagnostics::system_event_log<Store>`, `diagnostics::system_event` |
| **Files** | [`sentinel_post.hpp`](sentinel_post.hpp) · [`sentinel_system_event.hpp`](sentinel_system_event.hpp) · [`sentinel_system_event_log.hpp`](sentinel_system_event_log.hpp) |
| **Issues** | #35 (POST), #34 (System Event Log), #69 (device readiness), #80 (event coverage), #81/#86/#71 (firmware-update outcomes), #94 (RTC OSF) |

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

The DS3231 probe **reports** the oscillator-stop flag (`fail_self_test`) but
never clears it (#94): only a real BLE time-set may, so the fault stays visible
on every boot until the clock is actually fixed.

`sentinel_post.hpp` also defines `device_readiness` (`booting` → `post_running` →
`ready` / `degraded`), published by the boot orchestrator on the System service's
Device Readiness characteristic (#69) so a central connecting mid-boot can gate
its UI instead of reading stale/zero sensor values.

## System Event Log — `system_event_log<Store>`

A typed event recorder layered over a [`record_store`](../storage/). Each event is
a fixed 36-byte record with a typed variant body — boot lifecycle, firmware
update, BLE connection, fault, mode change, POST result, snapshot event:

```cpp
sentinel::diagnostics::system_event_log<event_store_t> log;
log.initialize(store, now_unix_fn);
log.record_boot_complete(fw_version, /* ... */);  // typed helpers append the right record
```

The log is transport-agnostic: `set_appended_hook(fn)` binds a callback fired
after each queued record is persisted — the app uses it to push the BLE Record
Count (#75).

### Firmware-update outcomes

`resolve_pending_update(download_finished, from, running)` turns a pending-update
marker found at boot into its event: a different running version →
`firmware_update_completed`; the same version → `firmware_reverted` if the
download had finished (MCUBoot rejected it), else `firmware_update_failed` with
reason `ota_fail_reason::interrupted` (a reset cut the download, #71). Versions
carry the full 16-bit build via the records' `*_build_hi` bytes (#86).
`ota_fail_reason` (aborted 1 · signal lost 2 · verify 3 · start 4 · interrupted
5) is the `mcuboot_result` code of a `firmware_update_failed` record.

`system_event` is the event-type enum; the per-type record structs
(`boot_lifecycle_record`, `fault_record`, `post_result_record`,
`snapshot_event_record`, …) live in [`sentinel_system_event.hpp`](sentinel_system_event.hpp).

## See also

- [`storage/`](../storage/) — the `record_store` the log persists to.
- [`app/`](../app/) — runs POST and drains the log at boot · [`bluetooth/`](../bluetooth/) — paged GATT read of the log.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) (#12, #15).
