# Session Handoff

This file captures rolling context for AI assistants (Claude, etc.) and human
collaborators who join the project partway through. Update it at the end of
any session that introduced new architectural decisions, new GitHub
infrastructure, or non-obvious constraints. Keep it under ~350 lines — rotate
old context into commit messages, issue bodies, or `docs/architecture/*.md`
when it grows too long.

**Last updated:** 2026-06-29 (session: #47). **Merged to `main` this session:**
**#47 OO task refactor** — squash `1dc481d` on `develop`, merge `0528923` on
`main`, tag `oo-task-refactor-history`; #47 closed, board → Done. All four
procedural tasks (battery/debug/rtc/bme280 services) are now Meyers-singleton
classes (`X::instance().…`) mirroring the bus arbiters; no file-static task
state remains (ISRs recover the instance via the HAL callback arg; bus
transports moved to `run()` locals). Both app + testbench link clean under
`-Werror`. (Prior session merged #35 POST + #36 snapshot struct + #37 BME280
sample task via `b9fee41`; #33 record store + #34 event log already on `main`.)
**Decisions in play (unchanged):** #13 (boot orchestrator over a shared
`sentinel::resource` device context), #14 (two-lane snapshot model), #15
(testbench tests REAL components), #16 (all FreeRTOS tasks are OO/class style —
**now fully realized by #47**).
**Created this session: #48 — testbench serial orchestrator** (standalone,
sequenced *before* #38). Make the testbench run bottom-up + serially (inits →
bus tasks → per-driver prelim tests → service/event-log/snapshot/POST tests →
THEN start continuous readers) so the serial log reads top-to-bottom as a
diagnostic. Testbench twin of the #38 boot orchestrator (decision #13); needs
the 7 test modules turned into run-to-completion `run()` calls invoked by one
high-priority one-shot orchestrator task (post-scheduler — I/O tests can't run
pre-scheduler). #38 then inherits the proven pattern.
**NEXT: #46 (live snapshot stream, lane 2) → #48 (testbench serial orchestrator)
→ #38 (boot orchestrator + device context + lane-1 persistence) → #6 (GATT).**

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
- **What's working today (merged to `main`):** all three Phase I drivers
  (#1, #5, #14, #15), both bus arbiters (#27, #28), BLE stack init (#29), debug
  stream (#25), cross-platform transport CRTP base (#26), full BSP / build / OTA
  infrastructure (#30), **#33 — flash-backed circular record store** (+ shared
  W25Q128 device mutex; all 6 ACs pass on the GD25Q128), **#34 — System Event
  Log**, **#35 — POST** (fake-driven suite; hardware ACs owned by #38),
  **#37 — BME280 sample service task + cache**, and **#36 — `device_snapshot`
  struct + `populate()`** (80-byte packed, cache-backed; off-bench suite passes).
- **What's working today (merged to `main`):** add **#47 — all FreeRTOS tasks
  are OO/class singletons** (battery/debug/rtc/bme280 services + the bus
  arbiters); `X::instance().task_create()` / accessors. Style is now locked for
  #46/#38's new tasks.
- **What's next (open, dependency-ordered):**
  1. **#46** — Live snapshot stream task (lane 2, ~100 ms BLE) ← **NEXT**
  2. **#48** — testbench serial orchestrator (bottom-up run-to-completion test
     sequence; pioneers #38's one-shot-orchestrator pattern)
  3. **#38** — boot orchestrator + shared device context + periodic snapshot
     persistence (lane 1, ~5 min flash); also wires #34/#35 boot-path + carries
     POST's on-bench hardware ACs. Inherits #48's orchestrator pattern.
  4. **#6** — BLE GATT services Phase I (wires producer notify-sinks → characteristics; assigns UUIDs) — *On Hold/Blocked until #46+#38*

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
11. **System Event Log is store-templated, clock-injected, RAM-testable (#34,
   AS BUILT).** `system_event_log<Store>` is duck-typed on its record store so
   one body runs over the flash `record_store` (app) and a `ram_record_store`
   test double (tests) with no vtable — same pattern as the drivers. Time comes
   in as a `uint32_t(*)()` callback ("unix secs, or 0"), NOT a `ds3231&`, so the
   log doesn't depend on the RTC driver and tests get deterministic timestamps.
   The full `sentinel::firmware_version` carries a baked version *string* and is
   far larger than 4 bytes, so records embed a compact 4-byte
   `firmware_version_compact` (`build` = low 8 bits). Recording is non-blocking
   (staging queue + drain task); cross-task reads need no lock (single writer,
   atomic aligned head). `ram_record_store` mirrors the flash slot layout
   (status + sequence + payload) over a **caller-owned** buffer so a fresh store
   re-scanning the same bytes faithfully emulates a warm reboot.
12. **POST is probe / aggregate / record, all duck-typed (#35, AS BUILT).**
   `sentinel::diagnostics::post` (`src/diagnostics/sentinel_post.hpp`, header-
   only) splits into three testable layers: one templated `probe_*()` per
   subsystem (duck-typed on the driver — `read_chip_id`, `oscillator_stop_flag`,
   `jedec_id`+`is_known_jedec`, store `initialize`/head/tail/capacity), a pure
   `post::summary` accumulator (`all_passed` / `failure_count` / 16-slot
   `invalid`-sentinel-terminated `results`), and `record_results(log, summary)`
   that emits to a duck-typed event log. The testbench drives the probes with
   tiny **fake driver doubles** so every `post_result` code is exercised
   deterministically off-bench (the physical ACs — pull a pin, swap the flash,
   drain the RTC battery — are manual on-bench). `post_subsystem` /
   `post_result` enums are an append-only wire contract (shared with #6 + iOS
   client). **Deviation from the issue sketch:** the record-store probe is
   read-only (no throwaway test record → no log pollution); the SPI+flash+store
   write path is instead validated by `record_results` writing POST's own real
   result records. On all-pass → one `post_passed`; per failure → one
   `post_subsystem_failed`. If the *record store itself* failed POST, event-log
   writes are skipped (futile) and the BLE debug stream (#25) is the only sink.
   **App boot-wiring deferred → see decision #13:** POST is NOT wired from a
   pre-scheduler `main()` as this issue's wording implies. The probes do their
   I/O through the bus-arbiter tasks, which only pump once the scheduler runs,
   so POST is wired from a high-priority one-shot boot-orchestrator task over a
   shared device context, landing with #38.
13. **Boot orchestration = ONE high-priority one-shot task over a shared device
   context (PLANNED; lands with #38).** Two coupled structural moves that
   collapse POST wiring + event-log wiring + snapshot persistence into a single
   readable boot sequence on the production I/O path:
   - **Shared device context.** The sensor/storage drivers (`bme280`, `ds3231`,
     `w25q128`), the flash record stores (event-log + snapshot `record_store`),
     and the `system_event_log` become application-scoped singletons owned by
     `sentinel::resource` and borrowed by reference — exactly how the cyhal SCB
     bus handles + `flash_device_mutex` already live there. Stops every task
     `new`-ing up its own driver. Motivation: (a) the BME280 ctor reads factory
     calibration into per-instance `calib_data` (`sentinel_bme280.hpp`), so N
     instances = N redundant calibration reads + N cached copies; (b) consumers
     multiply fast (POST, #37 telemetry, #38 snapshot, ~6 #6 GATT services all
     read the same sensors); (c) it completes the resource-layer shared-device
     pattern the flash mutex (decision #4) already started. This was always the
     intended shape — drivers reachable like the SCB representations.
   - **Boot-orchestrator task.** A single highest-priority one-shot FreeRTOS
     task created in `create_tasks()` that runs FIRST then self-deletes:
     `post::run(ctx…)` → `post::record_results(ctx.event_log, summary)` →
     `event_log.run_boot_sequence()` → spawn the service tasks. All boot-time
     sequencing in one function, in dependency order.
   - **Why a task, not pre-scheduler (deviation from #35's wording).** Every
     probe's I/O goes through the bus-ARBITER tasks, which block on
     `xQueueReceive(…, portMAX_DELAY)` and only pump after `vTaskStartScheduler()`.
     A literally-pre-scheduler POST can't complete one I²C/SPI transaction. The
     orchestrator honors the issue's *intent* (POST before the app does real
     work; results before service tasks spawn) while using the SAME production
     arbiter path POST validates — no parallel direct-bus code to rot. POST is
     also the event log's first writer (per the spec), so the two wire together
     here, not separately.
   - **Sequencing.** Gated on the flash map: #36 locks the event-log + snapshot
     region offsets/sizes (decision #11 left them provisional for exactly this);
     the orchestrator + device context + POST/event-log/snapshot wiring then land
     together in #38. Wiring POST alone before the map is final = throwaway
     region numbers or a half-built context #38 reopens.
14. **Device Snapshot is a TWO-LANE model over one shared `populate()` (planned).**
   `populate_snapshot()` (#36) is the single primitive; two independent consumers
   call it at their own cadence and route to their own sink — they do NOT share a
   refresh loop (different cadence AND different gating):
   - **Lane 1 — always-on persistence (#38).** A task records a snapshot to the
     flash record store every ~5 min for the device's whole operational life,
     connected or not. Read back via the same paged protocol as the System Event
     Log (`SnapshotHistory` service). Mirrors #34 one-for-one.
   - **Lane 2 — on-demand live stream (#46, CREATED this session).** A dedicated
     normally-idle task (option a — NOT a timer inside #6) that the
     `SnapshotStream` enable characteristic (#6) wakes. While enabled + connected
     it loops `populate()` → BLE notify at ~100 ms, then returns to idle.
     Producer/GATT split: the task produces + calls a notify sink; #6 owns the
     characteristic + the actual `wiced_bt_gatt` notify. The live-stream task had
     NO issue before this session — it was hand-waved "forthcoming, under #6";
     #46 closes that gap.
   - **`populate()` reads CACHES, not fresh bus I/O.** BME280 from #37's ~1 Hz
     sample cache, time from rtc_service, store counts from head/tail, BLE state,
     uptime — all cheap. This is what lets lane 2 stream at 100 ms with zero
     I²C/SPI contention, and serves lane 1 uniformly. Consequence: most fields
     refresh at ~1 Hz, so a fixed 100 ms stream sends some near-duplicate frames;
     an event-driven "notify-on-change" variant is a later refinement.
   - **Corrected build order: #37 → #36 → {#46 stream, #38 persistence} → #6.**
     Because `populate()` reads #37's cache, **#37 lands before #36** (a swap from
     the old #36-first sketch). The two lanes then consume `populate()`; #6 wires
     the producer notify-sinks to characteristics + assigns the 128-bit UUIDs.
15. **The testbench validates REAL planned components — no test-specific
   doubles for on-bench ACs.** Fake driver doubles (e.g. the #35 POST suite's
   `fake_bme280`/`fake_store`) are for **off-bench LOGIC** only — they prove a
   pure algorithm forwards the right bytes deterministically on a host. A
   component's **on-bench acceptance criteria** are validated by running it
   through its **real** wiring / consumers, never a throwaway harness built just
   to exercise it. Consequences, applied this session:
   - **#35 POST hardware ACs are owned by #38.** A true on-bench POST must run
     the real BME280/DS3231/W25Q128 through the real boot orchestrator + shared
     device context — which *is* #38. So all six physical ACs (`all_pass_path`,
     `bme280_disconnect`, `w25q128_unknown_jedec`, `oscillator_stop`,
     `degraded_operation`, `timing < 100 ms`) are validated under #38, not via a
     temporary POST harness. (#35 stays closed on off-bench-green; #38 carries
     the bench sign-off.)
   - **#36 `populate()` ACs** (`populate_default`/`populate_partial`) validate
     via the real consumers (#46 stream / #38 persistence calling it on-bench),
     not a populate-runner harness.
   - **#37 ACs** split: `task_create_success` / `bus_coexistence` /
     `bme280_disconnect_resilience` are observable now from the running service
     task; `sample_period_change` / `subscriber_notify` / `mutex_correctness`
     validate when their real consumers (#6 GATT characteristic, #46 stream)
     exercise the API.
   - **Board pattern:** an issue that cannot proceed until a dependency is
     complete goes to **On Hold/Blocked** (this session: #6 ← #46+#38; #45 ← #6).
     Keep board status in sync with reality (this session also corrected #34,
     which was stale at On Hold/Blocked despite being closed → Done).
16. **All FreeRTOS tasks are OO (class) style.** Tasks under `Sentinel/src/task/`
   had drifted into two styles: OO classes (the bus arbiters `i2c_bus`/`spi_bus`,
   #27/#28) and procedural namespaces-of-free-functions with the task's state in
   `.cpp` **file-static globals** (`battery_service`, `debug_stream`,
   `rtc_service`, and the `bme280_service` written this session). Standardize on
   OO: a class per task, state (caches, mutexes, notify queues, handles) as
   **private members**, `task_create()` + accessors as members, the loop as a
   private `run()` behind a static trampoline — mirroring the bus arbiters and
   the class sketches the issues already use (#37/#38/#46). **No runtime cost**
   (singleton-with-members vs namespace-with-statics → identical static storage);
   pure encapsulation/consistency. #47 does the conversion; **every new task is
   written OO from the start.** (`bme280_service` was written procedurally only
   to match `rtc_service` — a mistake; the right move when conventions conflict
   is to flag it, not silently pick one.)

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

## This/next session — the snapshot cluster (#37 → #36 → #46/#38 → #6)

`develop` and `main` are both synced with origin through the **#35 POST merge**;
tags `record-store-history`, `event-log-history`, `post-history` are pushed.
#35 is closed, board → Done. The snapshot cluster was **planned as a unit** this
session (decision #14 + #46 created) so session boundaries can't drop the
live-stream task. Build in dependency order:

1. **#37 — BME280 sample task + cache ← STARTING NOW.** Production replacement
   for the testbench `continuous_read` loop: sample BME280 at ~1 Hz over the I²C
   arbiter, cache `latest()` behind a mutex, expose a notify-queue/subscribe API.
   No BLE dependency — testable standalone. This is the cache `populate()` reads.
2. **#36 — `device_snapshot` + `populate()`.** Struct (no deps) + `populate()`
   that aggregates **from caches** (BME280 ← #37, time ← rtc_service, store
   head/tail counts, BLE state, uptime). Coordinate the snapshot flash region
   offset/size with the event-log region so they don't overlap (decision #11
   left `kEventLogRegion*` provisional at ~512 KiB pending exactly this — **#36
   locks the full flash map**).
3. **#46 (lane 2 stream) + #38 (lane 1 persistence + boot orchestrator + device
   context).** See decisions #13 + #14. #38 is the big one (stands up the real
   app boot path + the always-on persistence); #46 is the idle-until-enabled
   100 ms live stream.
4. **#6 — GATT.** Wires producer notify-sinks → characteristics, assigns the
   128-bit UUIDs. Client #9 mirrors 1:1.

### Two deferred items still owed (carried from #34 + #35) → land via the boot orchestrator in #38

Both #34 (event-log) and #35 (POST) shipped with **app boot-wiring deferred** —
the testbenches validate the logic over RAM/fake doubles, but nothing calls them
from the real boot path yet. Per **decision #13**, both land together inside the
single high-priority one-shot **boot-orchestrator task** (over the shared
`sentinel::resource` device context), sequenced for **#38** once #36 locks the
flash map — NOT pre-scheduler in `main()`, and NOT gated on #6:

1. **POST** — `post::run(ctx.bme, ctx.rtc, ctx.flash, ctx.event_store,
   ble_stack_ok, gatt_db_ok)` then `post::record_results(ctx.event_log, summary)`.
   The BLE-status args come from `stack_initialize()` + GATT-db registration.
2. **Event-log task** — clock wrapper over `rtc_service::last_unix_time`, the app
   event-log `record_store` over the now-final region constants, the drain task,
   and `event_log.run_boot_sequence()` (the orchestrator calls this right after
   POST records, since POST is the log's first writer).

**On-bench POST hardware ACs remain manual** (pull the BME280 SDA pin, swap in an
unknown-JEDEC flash, drain the DS3231 battery, scope the < 100 ms timing).

### How #34 actually shipped (for #35's consumers)

The System Event Log is `sentinel::diagnostics::system_event_log<Store>` in
`src/diagnostics/sentinel_system_event_log.hpp`, store-templated and duck-typed
(flash `record_store` in the app, `ram_record_store` in tests; singleton is
`system_event_log<Store>::instance()`). Records are typed-variant **36-byte**
structs in `sentinel_system_event.hpp` (8-byte header + 28-byte body; every
typed view `static_assert`s to 36). Key facts for callers:

- `record_*()` are **non-blocking**: stamp a timestamp (via an injected
  `uint32_t(*)()` clock), `xQueueSend` a 36-byte staging record (return `false`
  if the depth-16 queue is full), and a drain task / `drain_pending()` does the
  `store.append()`. Keep SPI off the caller's path.
- Boot continuity: `run_boot_sequence()` synthesizes `shutdown_unexpected` (at
  the prior record's timestamp) when the last session didn't end clean, then
  appends `boot_complete` with a recovered `boot_count`.
- Cross-task reads need **no lock** (single writer commits payload-then-head;
  aligned 32-bit head read is atomic on CM4).
- App-side wiring is still pending (thin, "follows later"): pass a clock wrapper
  over `rtc_service::last_unix_time`, an app `record_store` over the provisional
  `kEventLogRegionOffsetBytes` / `kEventLogRegionSizeBytes` (~512 KiB; finalise
  alongside the #38 snapshot region so they don't overlap), then
  `task_create()`. BLE GATT retrieval is **#6**.
- **Per-type structs MUST stay 36 bytes** — adding a field eats reserved
  padding; the `static_assert`s enforce it. Treat the field layouts + enum
  values as an append-only wire contract shared with the iOS client.

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
