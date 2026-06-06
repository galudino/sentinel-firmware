# Session Handoff

This file captures rolling context for AI assistants (Claude, etc.) and human
collaborators who join the project partway through. Update it at the end of
any session that introduced new architectural decisions, new GitHub
infrastructure, or non-obvious constraints. Keep it under ~300 lines — rotate
old context into commit messages, issue bodies, or `docs/architecture/*.md`
when it grows too long.

**Last updated:** 2026-06-06 (end of Phase I planning + Driver Backlog
flesh-out session)

---

## Project status — one-screen summary

- **Codebase:** modern C++17 firmware targeting the Infineon CYBLE-416045-EVAL
  development kit (PSoC 6 BLE), built with ModusToolbox 3.8 + GCC ARM 11.3 +
  MCUBoot for OTA.
- **Active phase:** Phase I (MVP on Infineon CYBLE-416045)
- **Phase progress:** I = 53 % (10 / 19 issues closed), II = 0 / 15, III = 0 / 2
- **What ships in Phase I:** BME280 + DS3231 + W25Q128 telemetry over BLE GATT,
  flash-backed System Event Log, flash-backed periodic Device Snapshots, live
  Device Snapshot stream, OTA DFU via MCUBoot.
- **What's working today (closed):** all three Phase I drivers (#1, #5, #14,
  #15), both bus arbiters (#27, #28), BLE stack init (#29), debug stream (#25),
  cross-platform transport CRTP base (#26), full BSP / build / OTA
  infrastructure (#30).
- **What's next (open, ordered by dependency):**
  1. **#33** — Flash-backed circular record store (foundation)
  2. **#34** — System Event Log + **#35** — POST (both consume #33)
  3. **#36** — Device Snapshot struct + populate
  4. **#37** — Telemetry sample task + **#38** — Periodic snapshot persistence
  5. **#6** — BLE GATT services Phase I (consumes all the above)

---

## Architectural decisions recorded this session

1. **System Event Log uses TYPED-VARIANT 36-byte records, not uniform 8-byte
   records.** Common 8-byte header (timestamp + event_type + reserved), 28-byte
   per-type body with `static_assert(sizeof(...) == 36)` on every typed view.
   Reasoning: Phase I events span a wide richness range; 3-byte payload is too
   limiting. Record density ~7k records / 256 KiB is still ample.
2. **ONE event log, not two.** Periodic structured state is handled by a
   *separate* dedicated log (#38) with `device_snapshot` records. Discrete
   events vs periodic state are separated by purpose, not by record format.
3. **#33 (record store) is the shared foundation.** Both #34 (events) and #38
   (snapshots) consume `record_store<RecordT, Capacity>` on top of the W25Q128.
   Template params let each consumer pick its own record size.
4. **#6 (BLE GATT services Phase I) was re-parented from #24 to #4.** GATT
   surface area is cross-platform application scope, not Infineon-specific
   platform implementation.
5. **SCB1, not SCB2, for the SPI bus on Infineon.** SCB2 hangs at boot on this
   BSP — reproducible against the stock Infineon Hello-World template. Detail
   captured in issue #1.
6. **W25Q128 driver accepts known clones via accept-list.** Winbond, GigaDevice,
   XTX, Boya, ZBIT. Memory-type `0x40` and capacity `0x18` are stable across the
   ecosystem; only the manufacturer byte varies.

---

## Project / contribution rules

- **No outside-project references in any public artifact.** Issues, READMEs,
  commit messages, code comments, project board — none of them mention any
  outside project that this code is not derived from. This is enforced
  unilaterally; flag any drift.
- **Branch workflow:** git-flow style. Branch off `develop`, incremental
  commits, tag pre-squash tip (`<feature-name>-history`), squash-merge into
  `develop`, merge `develop` into `main` with explicit merge commit (`--no-ff`).
- **No `Co-Authored-By` trailer on commits.**
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
- **Project board:** `galudino/projects/2` — project ID
  `PVT_kwHOAbZCmc4BYDDx`
- **Milestones:** Phase I (M1), Phase II (M2), Phase III (M3)
- **Tags:** annotated pre-squash history tags: `migration-history`,
  `ds3231-driver-history`, `i2c-bus-task-history`, `flash-memory-history`
- **`gh` auth scopes required:** `repo`, `read:project`, `project` (for issue
  writes + project board writes)
- **Project field IDs** (for `gh project item-edit --field-id ...`):

  | Field | Type | ID |
  |---|---|---|
  | Status | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlPE` |
  | Priority | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlU8` |
  | Size | single-select | `PVTSSF_lAHOAbZCmc4BYDDxzhTLlVA` |
  | Start date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVI` |
  | Target date | date | `PVTF_lAHOAbZCmc4BYDDxzhTLlVM` |
  | Estimate | text | `PVTF_lAHOAbZCmc4BYDDxzhTLlVE` |

---

## Useful `gh` incantations

```sh
# List Phase I open work
gh issue list --milestone "Phase I (MVP on Infineon CYBLE-416045)" --state open

# View an issue's sub-issue tree
gh api graphql -f query='
  query($num:Int!){
    repository(owner:"galudino",name:"sentinel-firmware"){
      issue(number:$num){
        subIssues(first:30){
          totalCount
          nodes{number state title}
        }
      }
    }
  }' -F num=4

# Add a sub-issue (parent ← child)
gh api graphql -f query='
  mutation($p:ID!,$c:ID!){
    addSubIssue(input:{issueId:$p,subIssueId:$c}){
      subIssue{number title}
    }
  }' -F p=<parent-node-id> -F c=<child-node-id>

# Set a Project field
gh project item-edit \
  --project-id PVT_kwHOAbZCmc4BYDDx \
  --id <item-id> \
  --field-id <field-id> \
  --single-select-option-id <option-id>      # or --date YYYY-MM-DD, or --text "..."
```

---

## Where things live in the repo

```
sentinel-firmware/
├── README.md                       # Project overview (start here)
├── LICENSE
├── docs/
│   └── SESSION_HANDOFF.md          # ← you are here
├── mtb_shared/                     # ModusToolbox shared dependencies (gitignored)
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
    │   ├── task/                   # FreeRTOS tasks
    │   ├── resource/               # Peripheral resource singletons
    │   ├── logging/                # Ring buffer + debug stream helpers
    │   └── utilities/              # Small headers (span, ring_buffer, etc.)
    ├── bsps/                       # ModusToolbox board support packages
    ├── configs/                    # FreeRTOS + MCUBoot + signing config
    └── third-party/                # MCUBoot
```

---

## Next session — recommended starting point

1. Read this file.
2. `gh issue list --milestone "Phase I (MVP on Infineon CYBLE-416045)" --state open`
   to see current Phase I work.
3. Pick the next dependency-correct work item. Most likely **#33** (record
   store) — it's the foundation for #34 (event log) and #38 (snapshot
   persistence), and nothing in Phase I depends on something else first.
4. Branch from `develop`: `git checkout -b <issue-number>-<short-slug> develop`.
5. Implement, commit incrementally, run testbench against real hardware.
6. When complete: tag the pre-squash tip, squash-merge into `develop`, merge
   `develop` into `main` with `--no-ff`. Update this file before the
   session ends.

---

## Suggestions for the project's MEMORY.md

If using Claude Code's auto-memory, the following lines would carry the most
useful context forward across sessions and complement this handoff doc:

```
- Public sentinel artifacts (issues, READMEs, commits, code comments,
  project board) MUST NOT reference any outside project the code is not
  derived from. Outside projects are private development context only.
- gh is set up with repo + read:project + project scopes. Project ID is
  PVT_kwHOAbZCmc4BYDDx (galudino/projects/2). Three Phase milestones exist:
  Phase I (M1), Phase II (M2), Phase III (M3).
- Two issue body templates are established: "Completed" for closed retros,
  forward-spec for Backlog drivers. See docs/SESSION_HANDOFF.md for the shape.
- Branch workflow: git-flow style, no Co-Authored-By trailer.
```
