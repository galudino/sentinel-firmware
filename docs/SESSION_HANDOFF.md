# Session Handoff

This file captures rolling context for AI assistants (Claude, etc.) and human
collaborators who join the project partway through. Update it at the end of
any session that introduced new architectural decisions, new GitHub
infrastructure, or non-obvious constraints. Keep it under ~350 lines — rotate
old context into commit messages, issue bodies, or `docs/architecture/*.md`
when it grows too long.

**Last updated:** 2026-06-28 (#34 System Event Log IMPLEMENTED on branch
`34-diagnostics-system-event-log` — typed-variant 36-byte records, RAM-backed
record store test double, store-templated log with a non-blocking staging
queue + drain task + boot-sequence synthesis; all 8 ACs validated in the
testbench against the RAM store, and the full `sentinel-testbench` ELF builds +
links clean. NOT yet squash-merged/tagged/pushed — pending review. #33 record
store remains on `develop`, still not merged to `main`.)

---

## Working style — sessions

**One Claude Code session per issue** is the intended cadence. A session may
span more than one issue when two issues are genuinely intertwined (e.g. a
view + the state machine that drives it), but the default is one-issue-one-
session. This keeps each session's context tight and avoids the "session got
too big and started forgetting things" failure mode.

The durable record lives in **git history + GitHub issues/project boards +
this file + MEMORY.md** — not in any single conversation. Starting a fresh
session loses *narrative* (the running "why" thread) but never loses *work*.
To re-acquire context at the start of a new session, the opening prompt should
be roughly:

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
- **What's working today (closed/merged to `develop`):** all three Phase I
  drivers (#1, #5, #14, #15), both bus arbiters (#27, #28), BLE stack init
  (#29), debug stream (#25), cross-platform transport CRTP base (#26), full BSP
  / build / OTA infrastructure (#30), and **#33 — flash-backed circular record
  store** (+ a shared W25Q128 device mutex; all 6 ACs pass on the GD25Q128).
- **What's next (open, ordered by dependency):**
  1. **#34** — System Event Log (design LOCKED, see below) ← START HERE
  2. **#35** — POST (consumes #33 + emits event-log records via #34)
  3. **#36** — Device Snapshot struct + populate
  4. **#37** — Telemetry sample task + **#38** — Periodic snapshot persistence
  5. **#6** — BLE GATT services Phase I (consumes all the above)

---

## Architectural decisions (cumulative)

1. **System Event Log uses TYPED-VARIANT 36-byte records, not uniform 8-byte
   records.** Common 8-byte header (timestamp + event_type + reserved), 28-byte
   per-type body with `static_assert(sizeof(...) == 36)` on every typed view.
   Phase I events span a wide richness range; 3-byte payload is too limiting.
   ~7k records / 256 KiB is still ample. (#34)
2. **ONE event log, not two.** Periodic structured state is a *separate*
   dedicated log (#38) of `device_snapshot` records. Discrete events vs periodic
   state are separated by purpose, not by record format.
3. **#33 (record store) is the shared foundation — AS BUILT.** Both #34 (events)
   and #38 (snapshots) consume `sentinel::record_store<RecordT, Transport>`
   (`Sentinel/src/storage/sentinel_record_store.hpp`) on top of the W25Q128.
   Header-only template, duck-typed like the drivers. Key as-built facts that
   differ from the original #33 sketch:
   - **Signature is `<RecordT, Transport>`, capacity is a runtime ctor arg**
     (`record_store(flash&, region_offset_bytes, region_size_bytes)`), not a
     `CapacityRecords` template param.
   - **Each slot carries a 4-byte monotonic `sequence`** (8-byte header:
     status + 3 reserved + sequence) so head/tail recover correctly *after
     wrap* — status bytes alone can't tell newest from oldest once every slot
     is `0xA5`.
   - **Slots are power-of-two sized** (`next_pow2(8 + sizeof(RecordT))`), so a
     slot never straddles a 256 B page / 4 KiB sector and index→address is one
     multiply. Consequence: a 36-byte record → 64-byte slot (≈44% overhead);
     size flash regions accordingly (see #34 plan).
   - `append()` is a two-phase write (payload, then status commit) → power-loss
     safe. `append_uncommitted_for_test()` exists to exercise the torn-write
     path. `initialize()` scans to recover head/tail; `erase_all()` resets.
4. **W25Q128 access is serialized by a shared recursive device mutex.** The SPI
   bus arbiter only serializes individual transactions, but a flash write is a
   multi-transaction logical op (write-enable → program → poll BUSY) and the
   chip's Write Enable Latch is global state that auto-clears on completion.
   Two concurrent writers clobbered each other's WEL (observed on-bench as
   spurious test failures); a read during another task's in-flight erase
   returns garbage. Fix: `sentinel::resource::flash_device_mutex` (recursive,
   created in `peripheral_initialize`), passed to each `w25q128` instance,
   taken around **every** logical op. This is what the #34 event-log task and
   #38 snapshot task (both flash writers) require to coexist. The driver's
   mutex param defaults to `nullptr` (no locking) for single-task use.
5. **#6 (BLE GATT services Phase I) is parented under #4, not #24.** GATT
   surface area is cross-platform application scope, not Infineon-specific
   platform implementation.
6. **SCB1, not SCB2, for the SPI bus on Infineon.** SCB2 hangs at boot on this
   BSP — reproducible against the stock Infineon Hello-World template; fault
   lands in FreeRTOS `xQueueSemaphoreTake` during `cybsp_init`. Detail + the
   debugging arc captured in issue #1. SCB1 + P10[0..3] is the workaround.
7. **W25Q128 driver accepts known clones via accept-list.** Winbond, GigaDevice,
   XTX, Boya, ZBIT. Memory-type `0x40` and capacity `0x18` are stable across the
   ecosystem; only the manufacturer byte varies. The physical part on the bench
   is a **GigaDevice GD25Q128** (`0xC8 0x40 0x18`).
8. **GATT services are CHIP-NAMED, not function-named, for sensors.** The
   Phase I GATT contract (#6) exposes a `BME280` service and a `DS3231` service
   as distinct services with distinct UUIDs — NOT a single generic "Sensor"
   service. Reason: TMP117 vs DS18B20 (both temperature) need disambiguation;
   chip-specific quirks differ; firmware + client mirror each other 1:1. The
   sentinel-client side uses the same convention (`BME280Service`,
   `DS3231Service`). Aggregate/cross-cutting services keep **descriptive**
   function names: `System`, `SnapshotStream`, `SnapshotHistory`,
   `SystemEventLog`, `OTA`, `DebugStream`. (Naming locked 2026-06-09; the old
   `Debug` / `Telemetry History` names are retired.)
9. **One-shot-sample rule for sensor GATT characteristics.** When a sensor IC +
   driver return all fields in a single read, the GATT layer exposes them as
   **one packed-struct characteristic**, not one per field. BME280's `read()`
   yields T+H+P at once → a single `bme280_sample` characteristic (`int16` 0.01 °C,
   `uint16` 0.01 %RH, `uint32` Pa = 8 B), NOT three. Split only when registers are
   genuinely independent (DS3231 Unix-time / temp / alarm-flags). #6 owns the
   contract + UUIDs; client #9 mirrors it 1:1.
10. **Device identity = standard DIS + machine-stable `platform_id` enum (#45).**
   The standard Device Information Service (`0x180A`) carries display metadata
   (Manufacturer Name, Model Number, HW Rev, PnP ID — Phase I). The `System`
   service carries a `uint8` `platform_id` enum (`cyble_416045`/`rpi5`/`nrf5340`)
   that the client + cloud branch on — **never** the Model Number string (same
   lesson as manifest-over-filename for firmware versions). `vendor_id` is
   *derived* via `vendor_of(platform_id)`, not its own characteristic. These
   `uint8` values are a permanent **wire contract** (BLE + cloud-manifest keys):
   explicit, append-only, never reused. One canonical token set
   (`cyble-416045`/`rpi5`/`nrf5340`) spans firmware `platform:` labels ↔ GATT
   `platform_id` ↔ cloud manifest `target`.

---

## Project / contribution rules

- **No outside-project references in any public artifact.** Issues, READMEs,
  commit messages, code comments, project board — none of them mention any
  outside project that this code is not derived from. Outside projects are
  private development context only. Enforced unilaterally; flag any drift.
- **Branch workflow:** git-flow style. Branch off `develop`, incremental
  commits, tag pre-squash tip (`<feature-name>-history`), squash-merge into
  `develop`, merge `develop` into `main` with explicit merge commit (`--no-ff`).
- **No `Co-Authored-By` trailer on commits.** No "Generated with Claude Code"
  PR marketing either.
- **Issue body templates:**
  - *Completed* — for closed issues; preserve original body above `---`, append
    a `## Completed` section with Start / End / Tag / commits / files / tests.
  - *Forward-spec* — for backlog issues; Overview → Datasheet & references →
    Hardware integration → Driver design → Acceptance criteria → Implementation
    notes → Related issues.
- **Issue Project fields to populate:** Status, Priority (P0/P1/P2),
  Size (XS/S/M/L/XL), Start date, Target date, Milestone.

---

## GitHub infrastructure

- **Repository:** [github.com/galudino/sentinel-firmware](https://github.com/galudino/sentinel-firmware)
- **Project board:** `galudino/projects/2`, named **`sentinel`** — project ID
  `PVT_kwHOAbZCmc4BYDDx`. **MERGED board** holding BOTH `sentinel-firmware` and
  `sentinel-client` issues (the old client board `projects/4` was deleted
  2026-06-08). Per-repo focus = saved Views filtered by `repository:`. Repo is a
  built-in board axis; Status is stage-only.
- **Milestones (repo):** Phase I (MVP on Infineon CYBLE-416045), Phase II (driver
  expansion + refinements), Phase III (Raspberry Pi 5 / POSIX), Phase IV (Nordic
  nRF5340). Each phase also has a `Main firmware application: Phase N` epic issue
  (the `milestone`-labelled roadmap nodes #4/#42/#43/#44) under root epic #41.
- **Tags:** annotated pre-squash history tags: `migration-history`,
  `ds3231-driver-history`, `i2c-bus-task-history`, `flash-memory-history`,
  `record-store-history` (#33)
- **`gh` is fully set up** with scopes `repo`, `read:project`, `project`
  (issue writes + project board writes both work). Logged in as `galudino`.
- **Project field IDs** (for `gh project item-edit --field-id ...`):

  | Field | Type | ID |
  |---|---|---|
  | Status | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlPE` |
  | Phase | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhU_3FM` |
  | Priority | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlU8` |
  | Size | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlVA` |
  | Start date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVI` |
  | Target date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVM` |
  | Estimate | text | `PVTF_lAHOAbZCmc4BYDDxzhTLlVE` |

- **Status option IDs (stage-only, collapsed 2026-06-08):** Backlog `18fe16c9`
  (was "Platform Backlog"; Driver/General Backlog deleted), Ready `61e4505c`,
  In Progress `47fc9ee4`, In Review `df73e18b`, On Hold/Blocked `06bd8365`,
  Done `98236657`.
- **Phase option IDs:** Phase I `eb79509f`, Phase II `2bd5ada7`,
  Phase III `f009f762`, Phase IV `a59b2206`.
- **Priority option IDs:** P0 `79628723`, P1 `0a877460`, P2 `da944a9c`.
- **Size option IDs:** XS `6c6483d2`, S `f784b110`, M `7515a9f1`,
  L `817d0097`, XL `db339eb2`.
- **Labels:** namespaced — `platform:`, `interface:`, `stack:`, `subsystem:`,
  plus `driver`/`transport`, `milestone` (phase-epic marker), `project config:
  vscode`. The `subsystem:` set (ble/dfu/event-log/snapshot/debug) is spelled
  identically on the client repo.

---

## Useful `gh` incantations

```sh
# List Phase I open work
gh issue list --milestone "Phase I (MVP on Infineon CYBLE-416045)" --state open

# View an issue's sub-issue tree
gh api graphql -f query='
  query($num:Int!){ repository(owner:"galudino",name:"sentinel-firmware"){
    issue(number:$num){ subIssues(first:30){ totalCount nodes{number state title} } } } }' \
  -F num=4

# Add a sub-issue (parent <- child); needs node IDs, not numbers
gh api graphql -f query='
  mutation($p:ID!,$c:ID!){ addSubIssue(input:{issueId:$p,subIssueId:$c}){ subIssue{number} } }' \
  -F p=<parent-node-id> -F c=<child-node-id>

# Set a Project field
gh project item-edit --project-id PVT_kwHOAbZCmc4BYDDx --id <item-id> \
  --field-id <field-id> --single-select-option-id <option-id>   # or --date / --text

# Gotcha: project item-edit single-select needs OPTION ID, not the name.
# Gotcha: jq escapes inside gh --jq fail in some shells; pipe to python3 instead.
# Gotcha: bash assoc arrays don't persist across separate Bash tool calls; do
#         multi-step gh work in ONE call, or source a /tmp/*.sh of IDs each call.
```

---

## Where things live in the repo

```
sentinel-firmware/
├── README.md                       # Project overview (start here)
├── docs/SESSION_HANDOFF.md         # ← you are here
├── mtb_shared/                     # ModusToolbox shared deps (gitignored)
└── Sentinel/                       # ModusToolbox project root
    ├── README.md                   # Build / flash instructions
    ├── src/
    │   ├── app/                    # main.cpp + Device Configurator output
    │   ├── testbench/              # alternate entry for testbench builds
    │   ├── bluetooth/              # AIROC BLE stack wrappers + GATT
    │   ├── drivers/                # Device drivers (CRTP-templated)
    │   ├── transport/
    │   │   ├── platform_agnostic/  # CRTP byte_transport base + tags
    │   │   └── cyhal/              # CYHAL-specific concrete transports
    │   ├── task/                   # FreeRTOS tasks (bus arbiters, services)
    │   ├── resource/               # Peripheral resource singletons
    │   ├── logging/                # Ring buffer + debug stream helpers
    │   └── utilities/              # Small headers (span, ring_buffer, etc.)
    ├── bsps/                       # ModusToolbox board support packages
    ├── configs/                    # FreeRTOS + MCUBoot + signing config
    └── third-party/                # MCUBoot
```

## Hardware bench setup (current)

- **Board:** CYBLE-416045-EVAL, KitProg3 over USB (serial 115200 8N1).
- **I²C bus:** SCB6 @ 100 kHz — P6[5] SDA, P6[4] SCL. Devices: BME280 (0x76),
  DS3231 (0x68). DS3231 SQW on P6[3] (falling-edge 1 Hz → rtc_service).
- **SPI bus:** SCB1 @ 100 kHz — P10[0] MOSI (A5), P10[1] MISO (A4),
  P10[2] SCLK (A2), P10[3] SS0 (A3). Device: GD25Q128 flash on SS0.
  **W25Q128 HOLD# (pin 7) and WP# (pin 3) must be tied to VCC** — floating
  them was the cause of the "all 0x00 reads" debugging session.

---

## Next session — recommended starting point (#34, design LOCKED)

`develop` is **1 commit ahead of `origin/develop`** (the #33 squash `41abac9`)
and the `record-store-history` tag is local-only — **both still need pushing**
(held this session by request). Also note #33 is on `develop` but **not yet
merged to `main`**; do that `--no-ff` merge when convenient.

1. Read this file + the body of issue #34.
2. `git checkout -b 34-diagnostics-system-event-log develop`
3. Implement per the locked design below. Scope = the `system_event_log`
   component + typed records + queue/drain task, validated **in testbench**
   against a RAM-backed store. BLE GATT retrieval is **#6**; POST emission is
   **#35**; both are out of scope for #34.

### Locked design (decided 2026-06-28)

Target split (per the established convention): #34 is a record-store *consumer*,
but its 8 ACs are all testbench-shaped, so build + validate in **testbench**
with the RAM store. App `main.cpp` boot-wiring is thin and follows later.

**Files**
- `src/diagnostics/sentinel_system_event.hpp` — `system_event` enum (closed,
  ranges per issue), 8-byte `system_event_record_header`, 36-byte
  `system_event_record`, and the per-type typed views, each with
  `static_assert(sizeof(...) == 36)`.
- `src/diagnostics/sentinel_system_event_log.hpp` — the log class + FreeRTOS
  staging queue + drain task.
- `src/storage/sentinel_ram_record_store.hpp` — RAM-backed store exposing the
  **same duck-typed API** as `record_store` (initialize/append/read/count/
  head_index/tail_index/capacity/erase_all + `append_uncommitted_for_test`),
  backed by a fixed array. Lets tests avoid flash wear.
- `src/test/sentinel_test_system_event_log.{hpp,cpp}` + testbench wiring
  (mirror the `record_store` test module; one-shot suite → `vTaskDelete(nullptr)`
  at the end — do NOT fall off the task fn, it freezes the scheduler).

**Three approved deviations from the issue's API sketch**
1. **Template the log on the store type:** `system_event_log<Store>` (duck-typed,
   no vtable), so the same code runs over the flash `record_store` (app) or
   `ram_record_store` (tests). Singleton accessor becomes
   `system_event_log<Store>::instance()`.
2. **Inject a time-provider callback** (`uint32_t (*now_unix)()`), NOT a
   `ds3231&`. The log only needs "unix time, or 0 if unavailable"; a callback
   decouples it from the RTC driver and makes timestamps deterministic in tests
   (needed for `unexpected_shutdown_synthesis`). App passes a thin wrapper over
   rtc_service; tests pass a controllable lambda.
3. **Size the flash region ~512 KiB (8,192 records), not 256 KiB.** The #33
   power-of-two slotting makes a 36-byte record a 64-byte slot, so 256 KiB only
   holds 4,096. 512 KiB on a 16 MiB chip is free. (Tests use the RAM store, so
   this only matters for the app-side region constant.)

**Behavior to implement**
- `record_*()` enqueue a 36-byte staging record (`xQueueSend`, 0 timeout →
  non-blocking; return false if full). The drain task `xQueueReceive`-blocks
  and `store.append()`s — keeps SPI off the caller's path.
- `count()/read()/read_range()` delegate to the store. Cross-task read is safe:
  single writer (the task) writes flash-then-`m_head`, and aligned 32-bit
  reads are atomic on CM4, so a reader sees either the old or a fully-committed
  head — no torn record. (Document this; don't add a lock for reads.)
- Boot sequence in the task before the drain loop: read the most-recent record;
  if it is NOT `shutdown_clean`, synthesize a `shutdown_unexpected` at that
  record's timestamp; then append `boot_complete` (fw version + boot_count,
  where boot_count = prior `boot_lifecycle_record.boot_count` + 1, else 1).
- Per-type structs MUST stay 36 bytes; adding a field eats reserved padding.

5. When complete: tag `event-log-history`, squash-merge into `develop`, merge
   `develop` into `main` with `--no-ff`. Update this file. Push.
```
git push origin main develop <new-history-tag>
```

---

## Suggestions for the project's MEMORY.md

These lines (if not already present) carry the most useful context across
sessions and complement this handoff doc:

```
- gh is fully configured (repo + read:project + project scopes). ONE merged
  board `sentinel` (galudino/projects/2, ID PVT_kwHOAbZCmc4BYDDx) holds BOTH
  repos' issues; the old client board projects/4 was deleted. Phase is a
  single-select field (I–IV); per-repo focus via saved Views.
- Companion repo sentinel-client (SwiftUI iOS/iPadOS/macOS) mirrors firmware
  Phase I over BLE. Uses AsyncBluetooth SPM lib; chip-named GATT service
  protocols; see its docs/SESSION_HANDOFF.md.
- GATT services are chip-named (BME280Service, DS3231Service), not a generic
  "Sensor" service; aggregate services are descriptive (System, SnapshotStream,
  SnapshotHistory, SystemEventLog, OTA, DebugStream). Firmware #6 owns the
  contract; client #9 mirrors 1:1. One-shot-sample sensors = one packed-struct
  characteristic (BME280).
- Default working cadence: one Claude Code session per GitHub issue.
```
