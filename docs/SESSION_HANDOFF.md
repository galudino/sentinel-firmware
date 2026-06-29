# Session Handoff

Rolling status for AI assistants (Claude, etc.) and human collaborators who join
partway through: **what just merged, what's in play, what's next.** Durable
reference lives in [`docs/architecture/`](architecture/) — see the pointers
below. Update this file at the end of any session; keep it lean (~150 lines) by
pushing decisions / infra / as-built detail into the architecture docs rather
than letting them accumulate here.

## Durable reference (read these for the "why")

- [`architecture/decisions.md`](architecture/decisions.md) — **cumulative
  architectural decisions (#1–#16…)**, referenced by number across the codebase.
- [`architecture/github-infrastructure.md`](architecture/github-infrastructure.md)
  — project board ID + all field/option IDs, milestones, labels, `gh` recipes,
  and the **contribution rules** (branch workflow, commit/issue conventions).
- [`architecture/hardware-bench.md`](architecture/hardware-bench.md) — board,
  bus pinouts, wiring gotchas, local build/flash quick reference.
- [`architecture/repo-layout.md`](architecture/repo-layout.md) — source tree map.

---

**Last updated:** 2026-06-29 (session: #46). **Merged to `main` this session:**
**#46 live snapshot stream task (lane 2)** — squash `5bfccc9` on `develop`,
merge `61b7d1d` on `main`, tag `snapshot-stream-history`; #46 closed, board →
Done. New `sentinel::task::snapshot_stream_task` (OO/class singleton, decision
#16): normally idle (blocks on `ulTaskNotifyTake`, zero CPU), woken by `start()`
to loop `populate_snapshot()` → notify sink at ~100 ms while
`streaming() && central_connected()`, auto-stops on `stop()` or disconnect.
Producer/GATT split — `set_notify_sink(notify_fn)` for #6's `wiced_bt_gatt`
notify; connection via a `connected_fn` predicate (default
`ble_context_object.connected()`, overridable for off-bench tests). Off-bench
behavioral suite covers all 5 ACs; on-bench BLE-central AC owned by #6. (This
session also rotated the durable sections of this handoff into
[`docs/architecture/`](architecture/).) **Deviation from #46's sketch:** class
is in `sentinel::task`, not the sketch's `sentinel::app` — every task lives there
(decision #16).

**Decisions in play:** #13 (boot orchestrator over a shared `sentinel::resource`
device context), #14 (two-lane snapshot model — **lane 2 now realized by #46**),
#15 (testbench tests REAL components), #16 (all FreeRTOS tasks are OO/class
style). Full text in [`architecture/decisions.md`](architecture/decisions.md).

**NEXT: #48 (testbench serial orchestrator) → #38 (boot orchestrator + device
context + lane-1 persistence) → #6 (GATT).**

---

## Working style — sessions

**One Claude Code session per issue** is the intended cadence. A session may
span more than one issue when two issues are genuinely intertwined (e.g. a
view + the state machine that drives it), but the default is one-issue-one-
session. This keeps each session's context tight and avoids the "session got
too big and started forgetting things" failure mode.

The durable record lives in **git history + GitHub issues/project boards +
this file + `docs/architecture/` + MEMORY.md** — not in any single conversation.
Starting a fresh session loses *narrative* (the running "why" thread) but never
loses *work*. To re-acquire context at the start of a new session, the opening
prompt should be roughly:

> "Working on issue #N in sentinel-firmware. Read `docs/SESSION_HANDOFF.md`
> and the body of issue #N, list the dependencies, propose the implementation,
> then proceed."

Cross-device: `claude remote-control` (or `/remote-control` in a session)
mirrors a session to the Claude iOS app / browser via QR code while code keeps
running locally. `/config` → "Enable Remote Control for all sessions" makes it
default.

---

## Project status — one-screen summary

- **Codebase:** modern C++17 firmware targeting the Infineon CYBLE-416045-EVAL
  development kit (PSoC 6 BLE), built with ModusToolbox 3.8 + GCC ARM 11.3 +
  MCUBoot for OTA.
- **Active phase:** Phase I (MVP on Infineon CYBLE-416045)
- **Companion repo:** `sentinel-client` (SwiftUI iOS/iPadOS/macOS client) —
  separate repo, but **shares ONE merged project board** with firmware (the
  `sentinel` board, `galudino/projects/2`); see its own `docs/SESSION_HANDOFF.md`.
- **What ships in Phase I:** BME280 + DS3231 + W25Q128 telemetry over BLE GATT,
  flash-backed System Event Log, flash-backed periodic Device Snapshots, live
  Device Snapshot stream, OTA DFU via MCUBoot.
- **What's working today (merged to `main`):** all three Phase I drivers
  (#1, #5, #14, #15), both bus arbiters (#27, #28), BLE stack init (#29), debug
  stream (#25), cross-platform transport CRTP base (#26), full BSP / build / OTA
  infrastructure (#30), **#33 — flash-backed circular record store** (+ shared
  W25Q128 device mutex; all 6 ACs pass on the GD25Q128), **#34 — System Event
  Log**, **#35 — POST** (fake-driven suite; hardware ACs owned by #38),
  **#37 — BME280 sample service task + cache**, **#36 — `device_snapshot`
  struct + `populate()`** (80-byte packed, cache-backed), **#47 — all FreeRTOS
  tasks are OO/class singletons**, and **#46 — live snapshot stream task
  (lane 2)** (`snapshot_stream_task`, idle-until-enabled, ~100 ms; off-bench
  suite passes, on-bench BLE-central AC owned by #6).
- **What's next (open, dependency-ordered):**
  1. **#48** — testbench serial orchestrator (bottom-up run-to-completion test
     sequence; pioneers #38's one-shot-orchestrator pattern) ← **NEXT**
  2. **#38** — boot orchestrator + shared device context + periodic snapshot
     persistence (lane 1, ~5 min flash); also wires #34/#35 boot-path + carries
     POST's on-bench hardware ACs. Inherits #48's orchestrator pattern.
  3. **#6** — BLE GATT services Phase I (wires producer notify-sinks →
     characteristics, incl. attaching `snapshot_stream_task`'s notify sink +
     driving `start()`/`stop()` from the `SnapshotStream` enable char; assigns
     UUIDs) — *On Hold/Blocked until #38* (its lane-2 dep #46 is now done).

---

## #48 — testbench serial orchestrator (immediate next)

Standalone, sequenced *before* #38. Make the testbench run bottom-up + serially
(inits → bus tasks → per-driver prelim tests → service/event-log/snapshot/POST
tests → THEN start continuous readers) so the serial log reads top-to-bottom as
a diagnostic. Testbench twin of the #38 boot orchestrator (decision #13); needs
the test modules turned into run-to-completion `run()` calls invoked by one
high-priority one-shot orchestrator task (post-scheduler — I/O tests can't run
pre-scheduler). #38 then inherits the proven pattern.

## #38 — boot orchestrator + device context + lane-1 persistence

The big one: stands up the real app boot path (decision #13) + the always-on
~5 min flash persistence (lane 1, decision #14), and carries the **two deferred
boot-wiring items** owed from #34 + #35. Both #34 (event-log) and #35 (POST)
shipped with app boot-wiring deferred — the testbenches validate the logic over
RAM/fake doubles, but nothing calls them from the real boot path yet. Per
decision #13 they land together inside the single high-priority one-shot
**boot-orchestrator task** (over the shared `sentinel::resource` device context),
once #36's flash map is final — NOT pre-scheduler in `main()`, and NOT gated on
#6:

1. **POST** — `post::run(ctx.bme, ctx.rtc, ctx.flash, ctx.event_store,
   ble_stack_ok, gatt_db_ok)` then `post::record_results(ctx.event_log, summary)`.
   The BLE-status args come from `stack_initialize()` + GATT-db registration.
2. **Event-log task** — clock wrapper over `rtc_service::last_unix_time`, the app
   event-log `record_store` over the final region constants, the drain task, and
   `event_log.run_boot_sequence()` (the orchestrator calls this right after POST
   records, since POST is the log's first writer).

Also still owed by #36 / wired here: `device_snapshot`'s storage counts, BLE
state, CPU temp, and POST status fields are currently 0 — they source from the
shared device context + `ble_context` cache here / in #6.

**On-bench POST hardware ACs remain manual** (pull the BME280 SDA pin, swap in an
unknown-JEDEC flash, drain the DS3231 battery, scope the < 100 ms timing).

The as-built API detail for the System Event Log (record format, non-blocking
`record_*()`, `run_boot_sequence()`) and POST live in decisions #11/#12 of
[`architecture/decisions.md`](architecture/decisions.md).
