# GitHub infrastructure & contribution rules

Durable reference for how the project is run on GitHub: the board, its field /
option IDs, milestones, labels, the `gh` recipes, and the contribution rules.
Rolled out of `docs/SESSION_HANDOFF.md` (2026-06-29). Update in place when board
structure or IDs change.

---

## Contribution rules

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

## Board & repo

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
  `record-store-history` (#33), `event-log-history` (#34), `post-history` (#35),
  `oo-task-refactor-history` (#47), `snapshot-stream-history` (#46).
- **`gh` is fully set up** with scopes `repo`, `read:project`, `project`
  (issue writes + project board writes both work). Logged in as `galudino`.

## Project field IDs

For `gh project item-edit --field-id ...`:

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
