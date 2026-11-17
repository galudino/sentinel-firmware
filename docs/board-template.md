# GitHub Projects board — reusable template

A project-agnostic kanban spec, factored out of the `sentinel` board so it can be
dropped into future projects. It mirrors GitHub's default Projects template
(`Backlog → Ready → In progress → In review → Done`) with a `On Hold/Blocked`
side state added, so collaborators read it correctly out of the box.

## Status column model

Left-to-right flow. `On Hold/Blocked` sits right after `In Progress` because that
is where work most often stalls; it is a **side state** you drop into and return
from, not a linear step.

| Order | Status | Color | One-line description |
|------:|--------|-------|----------------------|
| 1 | **Backlog** | gray | Full specification/scope is pending. |
| 2 | **Ready** | yellow | Specced, scoped, ready to start. |
| 3 | **In Progress** | blue | Actively being worked on. |
| 4 | **On Hold/Blocked** | red | Paused or blocked on external dependency. |
| 5 | **In Review** | purple | Awaiting review / merge. |
| 6 | **Done** | green | Merged; all acceptance criteria met. Ship status tracked via Milestone. |

### What each column means

- **Backlog** — new or not-yet-refined items: default assignee only, scope not
  fully fleshed out. The "not ready" bucket. (Standard pairing: `Backlog` =
  unrefined, `Ready` = refined-and-pullable — exactly GitHub's default usage, so
  the name is kept.)
- **Ready** — specced, scoped, assigned, pullable. The "ready to go" backlog.
  Entry trigger is **specced + scoped + assigned** — do *not* gate on due dates
  unless the project actually uses them.
- **In Progress** — actively worked, has a working branch.
- **On Hold/Blocked** — would otherwise be `Ready`/`In Progress` but waits on
  another issue or external dependency. Returns to `In Progress` once unblocked.
- **In Review** — fully implemented; awaiting verification of acceptance criteria
  (and code review, where there is a reviewer). On a solo project this is
  **self-verification / acceptance testing**, not peer review.
- **Done** — merged with all acceptance criteria met. **"Done" ≠ "Shipped":** an
  item can be merged weeks before a tagged release, so track *shipped* via
  Milestones/Releases, not this column.

## Transition rules

```
Backlog ──► Ready ──► In Progress ──► In Review ──► Done
                          ▲   │            │
                          │   └──► On Hold/Blocked ◄──┘
                          └────────────┘
```

- `In Progress` → `On Hold/Blocked` when blocked; back to `In Progress` once the
  blocking item resolves.
- `In Review` → `On Hold/Blocked` if integration testing surfaces a blocker;
  → `Done` when testing passes and all AC are met.
- **Reopened items** take one of two paths — no dedicated "Reopened" column:
  - **Regression / unmet AC** → reopen the issue into `In Progress`. The issue's
    own reopened state + timeline already records the bounce-back; a separate
    column would duplicate that and fragment the flow.
  - **Genuinely new scope** → file a new follow-up issue instead.

## Companion fields (optional)

The `sentinel` board also carries these single-selects; include whichever fit:

- **Priority** — `P0`, `P1`, `P2`
- **Size** — `XS`, `S`, `M`, `L`, `XL`
- **Phase** — project-specific milestone grouping (e.g. `Phase I`–`Phase IV`)

Plus GitHub's built-ins: Milestone (use for **ship/release** tracking), Labels,
Parent issue / Sub-issues, Linked PRs.

## Recreating the Status field in a new project

The option set is replaced atomically. When editing an **existing** field, pass
every option **with its `id`** to preserve item assignments; omit `id` only when
creating brand-new options. For a **fresh** project, create the single-select
with the options in order:

```bash
# PROJECT_NUMBER / OWNER for the target project
FIELD=$(gh project field-list "$PROJECT_NUMBER" --owner "$OWNER" --format json \
  | python3 -c 'import sys,json;print(next(f["id"] for f in json.load(sys.stdin)["fields"] if f["name"]=="Status"))')

gh api graphql -f query='
mutation($field:ID!){
  updateProjectV2Field(input:{
    fieldId:$field,
    singleSelectOptions:[
      {name:"Backlog",         color:GRAY,   description:"Full specification/scope is pending."},
      {name:"Ready",           color:YELLOW, description:"Specced, scoped, ready to start."},
      {name:"In Progress",     color:BLUE,   description:"Actively being worked on."},
      {name:"On Hold/Blocked", color:RED,    description:"Paused or blocked on external dependency."},
      {name:"In Review",       color:PURPLE, description:"Awaiting review / merge."},
      {name:"Done",            color:GREEN,  description:"Merged; all acceptance criteria met. Ship status tracked via Milestone."}
    ]
  }){ projectV2Field{ ... on ProjectV2SingleSelectField{ options{ id name } } } }
}' -F field="$FIELD"
```

> When reordering/rewording an **existing** board, add `id:"<optionId>"` to each
> entry (fetch via the `options{ id name color description }` query) so issue
> assignments survive the update.
