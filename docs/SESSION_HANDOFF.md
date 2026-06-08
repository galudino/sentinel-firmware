# Session Handoff

This file captures rolling context for AI assistants (Claude, etc.) and human
collaborators who join the project partway through. Update it at the end of
any session that introduced new architectural decisions, new GitHub
infrastructure, or non-obvious constraints. Keep it under ~350 lines — rotate
old context into commit messages, issue bodies, or `docs/architecture/*.md`
when it grows too long.

**Last updated:** 2026-06-08 (end of Phase I planning + Driver Backlog
flesh-out + companion client-repo scaffolding session)

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
  separate repo + project board; see its own `docs/SESSION_HANDOFF.md`.
- **What ships in Phase I:** BME280 + DS3231 + W25Q128 telemetry over BLE GATT,
  flash-backed System Event Log, flash-backed periodic Device Snapshots, live
  Device Snapshot stream, OTA DFU via MCUBoot.
- **What's working today (closed):** all three Phase I drivers (#1, #5, #14,
  #15), both bus arbiters (#27, #28), BLE stack init (#29), debug stream (#25),
  cross-platform transport CRTP base (#26), full BSP / build / OTA
  infrastructure (#30).
- **What's next (open, ordered by dependency):**
  1. **#33** — Flash-backed circular record store (foundation) ← START HERE
  2. **#34** — System Event Log + **#35** — POST (both consume #33)
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
3. **#33 (record store) is the shared foundation.** Both #34 (events) and #38
   (snapshots) consume `record_store<RecordT, Capacity>` on top of the W25Q128.
   Template params let each consumer pick its own record size.
4. **#6 (BLE GATT services Phase I) is parented under #4, not #24.** GATT
   surface area is cross-platform application scope, not Infineon-specific
   platform implementation.
5. **SCB1, not SCB2, for the SPI bus on Infineon.** SCB2 hangs at boot on this
   BSP — reproducible against the stock Infineon Hello-World template; fault
   lands in FreeRTOS `xQueueSemaphoreTake` during `cybsp_init`. Detail + the
   debugging arc captured in issue #1. SCB1 + P10[0..3] is the workaround.
6. **W25Q128 driver accepts known clones via accept-list.** Winbond, GigaDevice,
   XTX, Boya, ZBIT. Memory-type `0x40` and capacity `0x18` are stable across the
   ecosystem; only the manufacturer byte varies. The physical part on the bench
   is a **GigaDevice GD25Q128** (`0xC8 0x40 0x18`).
7. **GATT services are CHIP-NAMED, not function-named, for sensors.** The
   Phase I GATT contract (#6) exposes a `BME280` service and a `DS3231` service
   as distinct services with distinct UUIDs — NOT a single generic "Sensor"
   service. Reason: TMP117 vs DS18B20 (both temperature) need disambiguation;
   chip-specific quirks differ; firmware + client mirror each other 1:1. The
   sentinel-client side uses the same convention (`BME280Service`,
   `DS3231Service`). Aggregate/cross-cutting services keep function names
   (`LiveTelemetry`, `SystemEventLog`, `OTA`, `Debug`).

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
- **Project board:** `galudino/projects/2` — project ID `PVT_kwHOAbZCmc4BYDDx`
- **Milestones:** Phase I (M1), Phase II (M2), Phase III (M3)
- **Tags:** annotated pre-squash history tags: `migration-history`,
  `ds3231-driver-history`, `i2c-bus-task-history`, `flash-memory-history`
- **`gh` is fully set up** with scopes `repo`, `read:project`, `project`
  (issue writes + project board writes both work). Logged in as `galudino`.
- **Project field IDs** (for `gh project item-edit --field-id ...`):

  | Field | Type | ID |
  |---|---|---|
  | Status | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlPE` |
  | Priority | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlU8` |
  | Size | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlVA` |
  | Start date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVI` |
  | Target date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVM` |
  | Estimate | text | `PVTF_lAHOAbZCmc4BYDDxzhTLlVE` |

- **Status single-select option IDs:** Platform Backlog `18fe16c9`,
  Driver Backlog `d238a41a`, General Backlog `f75ad846`, Ready `61e4505c`,
  In progress `47fc9ee4`, In review `df73e18b`, On Hold/Blocked `06bd8365`,
  Done `98236657`.
- **Priority option IDs:** P0 `79628723`, P1 `0a877460`, P2 `da944a9c`.
- **Size option IDs:** XS `6c6483d2`, S `f784b110`, M `7515a9f1`,
  L `817d0097`, XL `db339eb2`.

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

## Next session — recommended starting point (#33)

1. Read this file + the body of issue #33.
2. `git checkout -b 33-flash-record-store develop`
3. Implement `record_store<RecordT, CapacityRecords>` on top of the W25Q128
   driver (`Sentinel/src/drivers/flash-memory/sentinel_w25q128.hpp`). Circular
   append + indexed read; 1-byte status byte per record for power-loss safety;
   lazy sector erase. See #33 body for the full spec.
4. Wire a smoke test into the testbench; run against the GD25Q128 on the bench.
5. When complete: tag `record-store-history`, squash-merge into `develop`,
   merge `develop` into `main` with `--no-ff`. Update this file. Push.
```
git push origin main develop <new-history-tag>
```

---

## Suggestions for the project's MEMORY.md

These lines (if not already present) carry the most useful context across
sessions and complement this handoff doc:

```
- gh is fully configured (repo + read:project + project scopes). Firmware
  project board ID PVT_kwHOAbZCmc4BYDDx (galudino/projects/2); client board
  PVT_kwHOAbZCmc4BZ6kp (galudino/projects/4). Phase I/II/III milestones in both.
- Companion repo sentinel-client (SwiftUI iOS/iPadOS/macOS) mirrors firmware
  Phase I over BLE. Uses AsyncBluetooth SPM lib; chip-named GATT service
  protocols; see its docs/SESSION_HANDOFF.md.
- GATT services are chip-named (BME280Service, DS3231Service), not a generic
  "Sensor" service. Firmware #6 and the client mirror this 1:1.
- Default working cadence: one Claude Code session per GitHub issue.
```
