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

**Last updated:** 2026-07-27 (session: **#65 + #57 DONE, promoted `develop →
main`**). #65 — Bosch C-driver adapter moved out of the transports into the bme280
driver (In Review, bench AC pending). #57 — per-directory README fan-out across
the whole `src/` tree, **COMPLETE + CLOSED**. Earlier: **#53 FULLY DONE + CLOSED**
— Doxygen backfill (0 warnings) + end-of-Phase-I formatting/convention sweep;
docs.yml CI added; #58–#64 filed. #56 + #49 on-bench signed off + closed;
#6/#45/#55 GATT, #38, #51 merged. **NEXT: on-bench BME280 re-verify for #65 (AC
#4); OTA DFU validation (#63) is the Phase I finale.**

**#57 (this session — merged `develop`, tag `57-per-directory-readmes-history`;
CLOSED):** every meaningful `src/` subdirectory now has a `README.md` fanning out
from the `src/README.md` keystone — 22 pages (app, resource, drivers + 7 driver
subdirs, transport + 2, task, storage, diagnostics, telemetry, bluetooth, logging,
utilities, test, testbench). Uniform shape: purpose → key types/entry points →
usage snippet → cross-links. All reachable from `src/README.md` (no orphans); 286
relative links verified. Snippets checked against real APIs (e.g.
`create_orchestrator`, `record_store` ctor, `jedec_id()`/`is_known_jedec`,
`device_snapshot::populate(out)`, `current_firmware_version`). Vendored/generated
dirs left undocumented by design.

**#65 (this session — merged `develop`, tag `65-bosch-adapter-history`; issue
closed):** the `byte_transport` CRTP base + all three CYHAL transports carried
static `bosch_read`/`bosch_write`/`bosch_delay` (Bosch Sensortec function-pointer
ABI) — an inverted dependency (reusable transport depending on a sensor-specific
vendor lib). Moved the **entire** adapter into `bme280<Transport>` as private
statics that cast `intf_ptr` back to `Transport*` and branch the register framing
on the compile-time `is_i2c`/`is_spi` tag over the transport's generic
`read`/`write`/`write_read`/`delay_us`. Transports are now vendor-agnostic
byte-movers — **`grep bosch` under `transport/` is empty**. Wire framing preserved
byte-for-byte on the bme280 I²C bus-transport path (net −201 LOC: one adapter
replaces four duplicated copies). **Builds:** testbench release + firmware release
both link + OTA-sign clean. **On-bench BME280 re-verification PENDING** (AC #4 —
this changes the bytes on the wire: chip-id 0x60, soft-reset, settings round-trip,
a plausible T/H/P sample).

**#53 sweep (this session, merged `5cdb265`, tag `53-format-sweep-history`):**
`clang-format -i` across `Sentinel/src`; reviewer rules — `RecordT`→`RecordType`,
classes defined inside `namespace sentinel {}` (no out-of-line qualified defs),
for-loop postfix / statement prefix, and **record-store reads C-style out-param
→ `std::optional<T>`** (`record_store`/`ram_record_store`/`system_event_log`/
`snapshot_persistence`/`gatt_paged` read chain; `read_range` kept its
caller-array buffer form). Verified: testbench debug + firmware release
(OTA-signed) link; `doxygen Doxyfile` = 0 warnings. Delivered partly via
subagent fan-out (test call sites + namespace-nested), parent-fixed the bme280
alias-ordering + missed `run_boot_sequence` reads that the build caught.

**New issues filed this session:** #60 (CI: build + artifacts), #61 (CI:
clang-format + doxygen-0-warnings gates — the *enforcement* so docs/style never
regress, replacing per-phase cleanup), #62 (enable GitHub Pages — needs repo
public, gated on #53/#57), #63 (**OTA DFU validation vs a known-good Infineon
host** — Phase I finale; client DFU is a separate `sentinel-client` concern),
#64 (evaluate `std::expected`/value-or-error error handling — C++23, so a
`__cplusplus`-gated `result<T,E>` alias for the RPi5/nRF5340 ports).

**Docs are decoupled by design:** Doxygen = code-only API reference (`INPUT =
Sentinel/src`); the narrative Markdown (#57 READMEs, `docs/architecture/`)
renders on GitHub. Doxygen HTML is gitignored + published by CI (never committed
to source). See [[reference_doxygen]].

**THIS SESSION (2026-07-27):**
- **#53 Doxygen backfill — DONE (merged `90c42dc`, tag
  `53-doxygen-backfill-history`).** `doxygen Doxyfile` emits **0 warnings**
  tree-wide (was 1,248). Every entity in `Sentinel/src` documented; Doxyfile is
  now a **code-only** build (`INPUT = Sentinel/src`; narrative Markdown renders
  on GitHub per #57; `__attribute__(x)` stripped; vendored `bosch/`/
  `app_bt_utils`/`cy_ota_config`/`*driver_file_template*` excluded). Delivered
  by a 6-way subagent fan-out + parent mop-up. Comments only; both builds link.
  **⚠️ Doxygen verify:** run `doxygen Doxyfile` **as-is** (HTML on) — the
  `GENERATE_HTML=NO` shortcut hits a 1.11.0 false-positive on member
  `\param`/`\return`. #53 stays **In Progress** for its *other* half: the
  end-of-Phase-I `clang-format -i` + reviewer-rule sweep (`RecordT`→
  `RecordType`, namespace-nested defs, prefix/postfix, ref-for-mutation).
- **#57 — `Sentinel/src/README.md` keystone merged (`d7ba68e`).** Module map +
  4 Mermaid diagrams. Remaining: per-directory README fan-out (In Progress).
- **#58 (User button SW2/P0_4 driver) + #59 (on-demand diagnostics service =
  extended POST, button + GATT triggered, non-destructive) — filed, Phase II
  Backlog.** The exhaustive destructive testbench stays a separate app
  (decision #15); #59 is the safe, live-unit "more involved POST".

**⚠️ RECURRED 2026-07-27 (post-#65 bench):** the marginal SPI-flash connection is
back — W25Q128 3/5 (`erase_program_read` "post-erase not blank, last_err=0";
`security_register_round_trip` byte mismatch) + record_store 0/7 (garbage
head/tail from the init scan; read `-4`). **Not a code regression:** #65 never
touched the flash path (W25Q128/record_store run over `cyhal_spi_bus_transport`,
untouched; the direct `cyhal_spi_transport` #65 edited is instantiated nowhere),
and **BME280 passed 4/4 on the wire — #65 AC #4 SIGNED OFF**. Physical signature
confirmed: (a) RAM-backed `system_event_log` **8/8** = record_store logic is fine,
only the flash instance fails; (b) corruption **moves every run** (security byte 0
then byte 11 = single-bit flip 0x67→0x77; record_store tail 15/2/128); (c) SPI
HAL returns success (`last_err=0`) but data is wrong; (d) short transactions pass
(JEDEC/SR1/power-down), long/multi-byte ones corrupt. **Now fails on both reset
AND reflash** → not a power-up/ready sequencing gap → degraded physical contact.
**RESOLVED same day → 47/47, zero code change:** moved the SPI **VCC/GND jumpers to
different sockets on the same power rail** — the prior sockets were **worn** (loose
grip). Root cause confirmed = breadboard socket contact, not the rail/leads/
decoupling. (So #65 AC #4 stands signed off; the flash suite is green again.)

**⚠️ BENCH RELIABILITY WATCH — marginal SPI (flash) connection.** The testbench
first came up with **5 failures** (W25Q128 `erase_program_read` + 4 flash-backed
`record_store` tests: `read -4`/`count=0`), then — with **zero code change**,
just re-seating SPI VCC/GND and reflashing — went 5-fail → 1-fail → **47/47**,
and has stayed 47/47 across the **three runs since (two reflashes + one bare
reset, no reflash)**. The failing test *moved* between runs and it was
**SPI-only** (I²C sensors never flaked). The clean bare-reset run makes a
firmware power-up/ready sequencing gap unlikely (it re-ran full bring-up + suite
without re-flashing). At the configured
**100 kHz** SPI clock there is no signal-margin/opcode explanation → it is a
**marginal physical contact** (loose jumper / breadboard / GND-VCC), not a
firmware bug (record_store logic is separately proven by the RAM-backed
`system_event_log` suite, 8/8 every run). **Currently seated well, NOT proven
permanently fixed.** Before trusting OTA DFU (which writes flash), harden the
flash wiring (short/solid leads, 100 nF decoupling at the module, solid GND) and
get repeatable 47/47 across several *cold power-cycles* (not just debugger
reflashes). Discriminator: if failures return only on cold boot (never on
reflash), suspect a flash power-up/ready sequencing gap in bring-up instead.

**THIS SESSION (2026-07-10, autonomous):**
- **#56 (MERGED to `develop`, squash `8ea30be`, tag
  `56-w25q128-write-completion-history`; board → In Review).** w25q128
  erase/program could report false success when the op was a silent no-op —
  completion was inferred from BUSY-clear alone, which can't tell "finished"
  from "never started". New `wait_until_write_complete()` requires **both** BUSY
  and WEL (SR1 bit 1) to clear; a completed op auto-clears WEL (JEDEC), so a
  never-started op leaves WEL latched and now times out honestly. Rewired
  `page_program` / `chip_erase` / `erase_with_address`. **On-bench pending:**
  AC #2 (deterministic `erase_program_read` across many runs) + AC #3 (a
  WEL-injection regression test — not feasible off-bench today; the suites drive
  the real device, no fake SPI transport exists).
- **#49 (MERGED to `develop`, squash `c5b625d`, tag
  `49-record-store-fast-scan-history`; board → In Review).**
  `record_store::initialize()` was an unconditional O(capacity) min/max scan
  (~8.7 s/region, ~17 s boot). Now classifies the region from **slot 0**: valid
  seq 0 → never wrapped → binary-search the valid/empty boundary
  (O(log capacity)); empty → `region_is_blank()` per-sector head probe
  (O(sector_count)) distinguishes a truly blank region (fast, the ~17 s case)
  from a power-loss-mid-recycle-of-sector-0 transient (falls through to the full
  scan so no records are lost); else wrapped → the unchanged authoritative
  `initialize_full_scan()`. No on-flash format change. New testbench
  `recycle_transient_recovery` guards the transient. **Builds validated locally
  (release firmware OTA image signs + fits; release + debug testbench link).**
  **On-bench pending:** run the record_store suite (now 7 tests → expect 7/7)
  and measure the empty-region boot scan (<1 s).
- **#53 (branch `53-doxyfile-doc-infra`, NOT merged — review; board → In
  Progress).** Added a curated repo-root `Doxyfile` (`OUTPUT_DIRECTORY =
  docs/doxygen`, gitignored; `EXTRACT_ALL=NO` + `WARN_IF_UNDOCUMENTED` so the
  coverage gap shows; MathJax, no dot). Verified `doxygen Doxyfile` runs clean
  (1.11.0, exit 0). The `clang-format -i` + reviewer-rule sweep + doc backfill
  stay the **end-of-Phase-I** task.
- **#57 (branch `57-src-readme-docs`, NOT merged — review; board → In
  Progress).** Added `Sentinel/src/README.md` — the module-map TOC + the four
  core Mermaid diagrams (CRTP transport, bus-arbiter tasks, two-lane snapshot,
  boot orchestrator). Per-directory module READMEs fan out from this anchor
  (remainder of #57, left so the pattern can be reviewed first).

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
  1. **On-bench sign-off of #56 + #49** (both merged to `develop`, board In
     Review). #56: `erase_program_read` deterministic across many runs.
     #49: record_store suite 7/7 + empty-region boot-scan <1 s measurement.
  2. **Review + merge the #53 + #57 docs branches** (`53-doxyfile-doc-infra`,
     `57-src-readme-docs`) — then continue #57's per-directory README fan-out.
  3. **End-of-Phase-I `clang-format` sweep (#53)** + **OTA DFU validation with
     `sentinel-client`** — the Phase I finale.

---

## Next issues (post #56/#49 merge)

1. **On-bench sign-off — #56 + #49** (both merged to `develop`, In Review).
   Flash a testbench build to the GD25Q128/CYBLE-416045 and run:
   - **#56:** `erase_program_read` deterministically across many consecutive
     runs (was intermittent). Optionally add a WEL-injection regression test if
     a fake SPI transport is introduced (AC #3).
   - **#49:** the record_store suite — 7 tests now incl.
     `recycle_transient_recovery` — expect 7/7; and confirm the empty-region
     boot scan is well under 1 s (was ~8.7 s/region).
2. **#53 — Doxyfile done (branch, review);** the `clang-format -i` +
   reviewer-rule sweep (`RecordT`→`RecordType`, namespace-nested defs,
   prefix/postfix, ref-for-mutation) + Doxygen backfill remain the
   **end-of-Phase-I** task (deferred so it doesn't churn in-flight branches).
3. **#57 — `src/README.md` anchor done (branch, review);** fan out the
   per-directory module READMEs from it incrementally.
4. **OTA DFU validation with `sentinel-client`** — the Phase I finale.

Durable as-built detail lives in `docs/architecture/decisions.md` and the closed
issues #6 / #45 / #55.
