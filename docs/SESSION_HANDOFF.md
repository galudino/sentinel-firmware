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

**Last updated:** 2026-07-10 (session: **#6 / #45 / #55 GATT services — MERGED +
closed**). Earlier: #38, #51 merged. **NEXT: #49 + #56 (may be tackled
autonomously), then #53; new docs roadmap #57.**

**#6 / #45 / #55 (MERGED to `develop` 2026-07-10, squash `f573cb1`, history tag
`6-ble-gatt-services-phase-i-history`; issues closed).** Phase I GATT catalog +
DIS/Platform ID + PSoC 6 die-temperature, validated on-bench (nRF Connect +
testbench 46/46). As-built + durable notes:
- **GATT DB** in `src/design.cybt`, regen via `bt-configurator-cli -c src/design.cybt
  -o GeneratedSource` — the `-o` path is relative to the config's parent (`src/`),
  so NOT `-o src/GeneratedSource`; `GeneratedSource/` is gitignored + rebuilt.
  8 custom services + DIS + Battery; CUD on every custom char, CCCD only on
  notifiers. UUIDs in the #6 body (client #9 mirrors 1:1).
- **Accessor layer** (`src/bluetooth/sentinel_gatt_*.hpp`) = the single seam over
  generated `app_*`/`HDLC_*`: `inline`+`noexcept` (never `constexpr` — they read
  extern GATT-DB globals), accessor/mutator (no raw-ref), notify sender in-layer.
- **Producers wired**: BME280 Ambient Sample; DS3231 Unix Time (R/W — the write is
  the BLE time-sync path via `ctx.rtc.set_unix_time()`, **not** rtc_service, per
  `[[project_rtc_time_design]]`) / RTC Temp / Alarm Flags; Snapshot Stream (sink +
  enable char); paged Snapshot History + System Event Log; async
  `ble_maintenance_task` for Clear Store + Request Bootloader (off the BT callback);
  real `gatt_db_ok` into POST.
- **#55 die-temp**: `drivers::psoc6_die_temperature` (SAR DieTemp channel, 1.2 V BGR,
  32× avg, SFLASH dual-slope conversion; cache+throttle+mutex; SAR on free 8-bit
  divider 6). Surfaced via System **CPU Temperature** char (R/Notify),
  `cpu_die_temp_service` (heartbeat-gated log like the peers, `SENTINEL_TESTBENCH`
  gate), and a real-SAR testbench suite. ~34 °C on-bench, correct vs BME280/DS3231.
- **`device_snapshot`** fully populated (RSSI/TX-power async-cached + CPU die temp).
- Bugs fixed en route: `firmware_version::c_str()` formatted `0.0.0.1` as `0.0.01.`
  (now guarded by static_assert); `unused()` is const-ref variadic (accepts
  non-copyable). Note `firmware_version::build()` still truncates to 8 bits — use
  `array()[3]` for the full 16-bit build.
- On-bench GATT checklist:
  [`acceptance/gatt-nrf-connect-checklist.md`](acceptance/gatt-nrf-connect-checklist.md).

**NEXT (dependency-ordered):**
1. **#49** — `record_store::initialize()` is O(capacity): slow boot flash scan
   (~17 s, two region scans). Optimize the scan.
2. **#56** — w25q128 erase/program can report false success when WEL doesn't latch
   (verify WEL / that BUSY asserted before trusting BUSY-clear). Filed with diagnosis.
3. **#53** — formatting / house style / Doxygen — address after #49 + #56.
   **#57** — per-directory README docs across the tree (roadmap; complements #53).

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

**NEXT: #49 (slow boot flash scan) + #56 (w25q128 WEL false-success) — may be
tackled autonomously — then #53 (formatting / Doxygen). #57 (per-dir READMEs) is
a new docs roadmap item. #6 / #45 / #55 merged to `develop` + closed.**

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
- **Also merged to `develop`:** **#6 / #45 / #55** — Phase I BLE GATT services,
  Device Information Service + Platform ID, and the PSoC 6 die-temperature
  driver/service (squash `f573cb1`, tag `6-ble-gatt-services-phase-i-history`).
- **What's next (open, dependency-ordered):**
  1. **#49** — `record_store::initialize()` is O(capacity); slow boot flash scan
     (~17 s across two region scans). ← **NEXT**
  2. **#56** — w25q128 erase/program can falsely report success when WEL doesn't
     latch (verify WEL / BUSY-asserted before trusting BUSY-clear).
  3. **#53** — formatting / house style / Doxygen (after #49 + #56).
     **#57** — per-directory README docs (roadmap; complements #53).

---

## Next issues (post #6/#45/#55 merge)

1. **#49 — slow boot flash scan.** `record_store::initialize()` is O(capacity):
   two region scans (~8.7 s each; ~17 s total) at boot. Optimize the scan (e.g.
   binary-search the head/tail rather than a full linear sweep, or a stored
   head/tail hint). Touches `src/storage/sentinel_record_store.hpp`.
2. **#56 — w25q128 false-success erase/program.** After `write_enable`, the driver
   trusts `wait_until_ready` (BUSY-clear) as completion — but if the WEL latch
   didn't stick, the op is silently ignored, BUSY never asserts, and it returns a
   false `true`. Fix: verify WEL set (or that BUSY asserted) before trusting the
   clear. `src/drivers/flash-memory/sentinel_w25q128.hpp`; surfaced intermittently
   by `sentinel_test_w25q128.cpp::erase_program_read`.
3. **#53 — formatting / house style / Doxygen** (address after #49 + #56).
4. **#57 — per-directory README docs** (roadmap; narrative module docs, GitHub-
   rendered; complements #53's Doxygen API reference).

Durable as-built detail lives in `docs/architecture/decisions.md` and the closed
issues #6 / #45 / #55.
