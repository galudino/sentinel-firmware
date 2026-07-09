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

**Last updated:** 2026-07-08 (session: **#6/#45 GATT services**, in progress on
branch `6-ble-gatt-services-phase-i`). Earlier: #38, #51 merged (below). **NEXT:
finish #6 — see "#6 — in progress" below.**

**#6/#45 (in progress, branch `6-ble-gatt-services-phase-i`, NOT yet merged):**
the Phase I GATT catalog. **Landed + building clean (Release + testbench):**
- Complete GATT database in `src/design.cybt` (regen via `bt-configurator-cli`
  — output dir is relative to the config's parent, so use `-o GeneratedSource`,
  not `-o src/GeneratedSource`; `GeneratedSource/` is gitignored + rebuilt).
  8 custom services + DIS; CUD on every custom char, CCCD only on notifiers.
- **`platform_id`/`vendor_id` enums + `vendor_of()`** (`sentinel_platform_id.hpp`,
  append-only wire contract, #45).
- **GATT accessor layer** (`src/bluetooth/sentinel_gatt_*.hpp`): the single seam
  over generated `app_*`/`HDLC_*` symbols — `inline`+`noexcept` (never
  `constexpr`), accessor/mutator (no raw-ref), notify sender in the layer.
- **#45 done**: DIS populated (Manufacturer via `vendor_of`, Model/HW static,
  FW/Serial mirrored) + System identity (Serial R/W, FW Version, Platform ID).
- BME280 Ambient Sample (R/Notify, published from the bme280_service cache hook)
  + DS3231 Unix Time (R/W — write sets the RTC via `ctx.rtc.set_unix_time()`,
  the BLE time-sync path, **not** rtc_service) / RTC Temperature (R/Notify) /
  Alarm Flags. Live Snapshot Stream sink attached; enable char drives start/stop.
- Paged **Snapshot History** + **System Event Log** reads (Record Count / Index /
  Record Block) over `resource::context()`'s stores, refreshed in
  `gatt::paged::before_read` at the top of the read handler (relative cursor →
  `tail_index()+cursor`; block = `floor(MAX_LEN/rec_size)` records).
- **Async maintenance task** (`ble_maintenance_task`, `xTaskNotify` bits): Clear
  Store (both stores; `erase_all` is multi-second) + Request Bootloader Mode
  (deferred `NVIC_SystemReset`) — kept off the BT callback.
- **Real `gatt_db_ok`**: `ble_context` stores the `wiced_bt_gatt_db_init` result;
  the orchestrator reads it live at POST (BTM_ENABLED has run by then).
- **`device_snapshot` fully populated**: BLE link metrics via
  `ble_context::refresh_link_metrics()` (async, ~1 Hz throttled RSSI/TX-power
  cached); **CPU die temp** via the new `drivers::psoc6_die_temperature` (SAR ADC
  DieTemp channel + SFLASH dual-slope conversion; cache+throttle+mutex). No zero
  fields left.
- UUIDs assigned + recorded in issue #6 body + client #9 mirrors 1:1. Issue #6
  UUID table + #45 acceptance boxes updated (kept as official docs).

**#6 remaining (next session) — all firmware wired; only bench validation left:**
- **On-bench validation** (nRF Connect + serial): enumerate all services, DIS
  reads, notifications, paged walk, Unix Time write sets the RTC, Clear/Bootloader.
- **Die-temp on-bench validation** tracked in **#55** (SFLASH calibration accuracy
  + SAR 8-bit divider 6 conflict check).
- Latent quirk noted: `firmware_version::build()` truncates to 8 bits; use
  `array()[3]` for the full 16-bit build (done in `gatt::system`).
- `bt-configurator-cli -o` is relative to the config's parent dir (`src/`), so
  regenerate with `-o GeneratedSource` (not `-o src/GeneratedSource`).

**#38 (merged):** boot orchestrator + shared device context + lane-1 snapshot
persistence. Both configs build clean under `-Werror -Wall -Wextra -pedantic-errors`
(`TESTBENCH=1` and `TESTBENCH=0` both reach `Linking … .elf`; only the benign
imgtool/`click` signing step fails). New pieces:
- **Shared device context** (`src/resource/sentinel_device_context.hpp`) —
  `resource::context()` Meyers singleton (decision **#17**, amends #13): holds
  the shared `bme`/`rtc`/`flash` drivers + `event_store`/`snapshot_store`,
  first-constructed inside the orchestrator (post-scheduler). `initialize_stores()`
  scans both flash regions, binds the event log, sets `context_ready()`.
  `rtc_service`/`bme280_service` now **borrow** `ctx.rtc`/`ctx.bme`.
- **Production boot orchestrator** (`src/app/sentinel_boot_orchestrator.cpp`) —
  one-shot highest-prio task (twin of #48): build context → `initialize_stores`
  → `post::run` (real drivers) → cache first-fail status → `record_results`
  (enqueue) → start event-log drain task (runs boot sequence, then drains POST
  records) → start service tasks (rtc, bme280, snapshot-persistence, snapshot-
  stream, battery) → self-delete. `main()` no longer hard-asserts on BLE-stack
  failure (POST records it; degraded boot, decision #12).
- **Lane-1 persistence** (`src/task/sentinel_task_snapshot_persistence.cpp`) —
  OO singleton, ~5 min cadence, `populate→append`, `capture_now`/`count`/`read`/
  `read_range`/`erase_all`, `snapshot_persisted` heartbeat every N captures,
  first capture = boot anchor. New testbench suite drives the **real** task over
  a scratch store (decision #15); wired as the 9th orchestrator group.
- **Flash map finalized:** event log `[0x100000..0x180000)`, snapshots
  `[0x180000..0x280000)`; `populate_snapshot` now sources storage counts +
  `ble_connected` + POST status. New events `snapshot_persisted` (0x45) /
  `pre_fault_snapshot_captured` (0x46) + `snapshot_event_record`.

**ON-BENCH BRING-UP (this session, CYBLE-416045 + GD25Q128):** happy-path boot
now validated end-to-end — POST passes (per-probe serial feedback added:
`post bme280 PASS` … `post ble_stack PASS`), event log + snapshot history scan +
init, all service tasks start, RTC/BME280 continuous reads + snapshot persistence
run. **Two on-bench bugs found + fixed:**
- **Boot hung in `resource::context()`** — first C++ function-local `static`
  constructed *post-scheduler* dead-locked in `__cxa_guard_acquire` (gthread path
  unwired in this newlib/wiced port). Fix: **`-fno-threadsafe-statics`** in
  `CXXFLAGS` (decision **#18**; safe because every singleton is first-touched
  from the single orchestrator task). Also hardened the I²C transport `exchange()`
  to fail-fast on a null response queue.
- **Boot scanned the event-log region twice** (~8.7 s each) — POST's
  `probe_record_store` re-`initialize()`d an already-scanned store. Fixed: it now
  trusts an `initialized()` store. Boot flash-scan is still ~17 s (two O(capacity)
  region scans) → optimization filed as **#49**; unified portable logging facade
  filed as **#50**.
- **Testbench: only rtc_service ran, not bme280_service** — with the guard gone
  (decision #18), the two reader tasks raced to first-construct `context()`. Fixed
  (`d2a02fb`): the test orchestrator builds `context()` once up front before
  starting the readers (same single-first-touch the app already did). **Confirmed
  fixed on-bench.**

**Also this session (not #38-blocking):**
- **Hoisted `resource::system_initialize()`** (`sentinel_resource.cpp`) out of the
  two near-identical `main.cpp`/`testbench.cpp` bodies; banner uses
  `APP_NAME_STRING` (stringized). POST reports measured duration for the <100 ms
  AC. POST prints per-probe `post <subsystem> <PASS|fail_*>`.
- **`docs/acceptance/post-hardware-acceptance-checklist.md`** — the on-bench
  fault-injection checklist for #35's six ACs (this is #38's sign-off record).
- **Build via `Sentinel/scripts/build-sentinel-{firmware,testbench}-debug.sh`**
  (venv → signing works; no more `click` error). **BLE needs the Release config**
  for `sentinel-firmware`. See [[reference_local_firmware_build]].
- **#51 DONE (merged 2026-07-01):** unified entry points + de-duplicated BT/OTA
  config. One shared `src/main.cpp` calls `sentinel::create_orchestrator()` (in
  `sentinel_orchestrator_entry.hpp`), defined per-target in each orchestrator TU;
  `TESTBENCH` `CY_IGNORE`s the other dir so the linker resolves it with no
  `#ifdef`. Single canonical `src/design.cybt` + `src/cy_ota_config.h` (regen'd
  `GeneratedSource/` at `src/`, gitignored; canonical OTA config carries the
  correct `CYBLE-416045-EVAL` board name). Orchestrator files renamed to match
  their class: `sentinel_boot_orchestrator.*` / `sentinel_test_orchestrator.*`.
  Both targets link + boot on-bench (firmware boot sequence; testbench test
  tally). Also fixed a pre-existing bug: `build-sentinel-testbench-release.sh`
  had `testbench_mode=0` (built the app under the testbench name) → set to `1`.
- **#52 DONE (merged 2026-07-01):** the `TESTBENCH`-toggle footgun surfaced during
  #51. mtbninja aggregates every `build/**/local/*.o` into the link regardless of
  `CY_BUILD_LOCATION`, so coexisting firmware+testbench trees cross-link (~2000
  dup-symbol errors; pre-#51 it *silently* emitted a mislabeled binary). Per-target
  build dirs (the first idea) are impossible — mtbninja scans them all (verified
  shared/sibling/unique-parent layouts). Fix: a Makefile parse-time guard cleans
  `./build` when `TESTBENCH` changes from the last build (`build/.last_testbench`),
  gated on build/program goals. Same-target rebuilds stay incremental; only the
  firmware↔testbench switch pays a one-time rebuild. Scripts inherit it (all call
  `make build|program`).

**#38 SIGNED OFF (2026-07-01):** all six on-bench POST hardware ACs resolved —
[`docs/acceptance/post-hardware-acceptance-checklist.md`](acceptance/post-hardware-acceptance-checklist.md)
carries the evidence. **AC 1/2/4/5/6 PASS on-bench** (nominal all-pass +
`done in 2 ms`; bme280 `fail_no_ack` + degraded boot; ds3231 `fail_self_test` →
self-clears across two boots); **AC 3 documented bench-infeasible** (no swappable
non-accept-listed SPI-NOR part; logic covered off-bench by #35 `fake_flash`,
decision #15). Squash-merged → `develop`, #38 closed, board → Done.

**Decisions in play:** #13 (boot orchestrator over a shared `sentinel::resource`
device context — **realized by #38**), #14 (two-lane snapshot model — **both
lanes now built**: lane 2 #46, lane 1 #38), #15 (testbench tests REAL
components), #16 (all FreeRTOS tasks OO/class), **#17 (device context = post-
scheduler Meyers singleton, amends #13)**. Full text in
[`architecture/decisions.md`](architecture/decisions.md).

**NEXT: #6 (BLE GATT services Phase I) — its deps #38 + #46 are now done.**

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
  tasks are OO/class singletons**, **#46 — live snapshot stream task (lane 2)**
  (`snapshot_stream_task`, idle-until-enabled, ~100 ms; off-bench suite passes,
  on-bench BLE-central AC owned by #6), **#48 — testbench serial bottom-up
  test orchestrator** (`test_orchestrator`; every suite is run-to-completion
  `run_all() → tally`, fixture-owned bus transports), and **#38 — boot
  orchestrator + shared device context + lane-1 snapshot persistence**
  (`resource::context()`, `app::boot_orchestrator`, `snapshot_persistence_task`;
  decision #17; both builds clean; **merged to `develop` 2026-07-01, on-bench
  POST hardware ACs signed off — AC 1/2/4/5/6 PASS, AC 3 bench-infeasible**).
- **What's next (open, dependency-ordered):**
  1. **#6** — BLE GATT services Phase I (wires producer notify-sinks →
     characteristics, incl. attaching `snapshot_stream_task`'s notify sink +
     driving `start()`/`stop()` from the `SnapshotStream` enable char, exposing
     `snapshot_persistence_task` history reads, threading the real `gatt_db_ok`
     into the orchestrator's POST, and publishing BLE tx-power/RSSI + CPU temp
     into `populate_snapshot`; assigns UUIDs). Deps #38 + #46 now done. ← **NEXT**

---

## #6 — BLE GATT services Phase I (next)

With #38 merged, the producer side is complete and #6 is pure GATT wiring
(decision #8 chip-named services, #9 one-shot-sample rule):

- Attach `snapshot_stream_task::set_notify_sink` + drive `start()`/`stop()` from
  the `SnapshotStream` enable characteristic (lane 2).
- Expose `snapshot_persistence_task::read`/`read_range`/`count` as the
  `SnapshotHistory` paged-read service (lane 1), mirroring the `SystemEventLog`
  retrieval shape.
- Thread the **real GATT-DB-registration result** into
  `boot_orchestrator::task_create(ble_stack_ok, gatt_db_ok)` (Phase I currently
  passes the stack-init result for both).
- Publish BLE tx-power / peer RSSI (and the on-die CPU temp) so the remaining
  zero `device_snapshot` fields fill in.
- Assign the 128-bit service/characteristic UUIDs; client #9 mirrors 1:1.

The as-built API detail for the System Event Log, POST, and the shared device
context / boot orchestrator lives in decisions #11/#12/#13/#17 of
[`architecture/decisions.md`](architecture/decisions.md).
