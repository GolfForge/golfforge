# Backlog — moved to Linear

The prioritized work queue moved to Linear on 2026-05-25.

→ **[linear.app/golfsim](https://linear.app/golfsim)**

## Why

`docs/backlog.md` was outgrowing its format — tickets got long, dependencies got implicit, and there was no good way for an agent to filter by which machine could pick up a given task. Linear gives us self-contained tickets with goal/plan/done-when/files/pitfalls, explicit blockers, and `machine/windows` / `machine/mac` / `machine/either` labels.

## Conventions in Linear

- **Labels:**
  - `area/*` — `area/pipeline`, `area/engine`, `area/range`, `area/launch-monitor`, `area/walking`, `area/hardware`, `area/architecture`, `area/cross-platform`, `area/docs`
  - `machine/*` — `machine/windows`, `machine/mac`, `machine/either`. Pick a ticket whose label matches where you're sitting.
  - **Kind:** `Feature`, `Improvement`, `Bug` (Linear defaults).
- **Priority:** Urgent (1) is reserved for architectural invariants and broken-state stuff. High (2) is the next 1-2 things to actually do. Medium (3) and Low (4) are the queue.
- **Blockers:** every ticket states its dependencies explicitly. Linear renders these as graph edges.
- **Done flow:** when a ticket lands, drop a short outcome comment (numbers, files landed) and mark Done. Add a dated entry to `docs/worklog.md` referencing the ticket ID.

## What lives where now

- **What's been built:** `docs/worklog.md` (dated, summarized, "shipped features" reference).
- **What's next:** Linear.
- **Architectural decisions / project plan:** `docs/plan.md`.
- **Engine pitfalls / recipes:** `docs/ue5-cookbook.md`.
- **Pipeline → engine file shapes:** `docs/pipeline-data-contract.md`.

## For agents

Read `CLAUDE.md` first. It points here, points at Linear, and tells you which machine you're on. Then filter Linear by `machine/<your-machine>` and `priority: Urgent or High` to see what's actionable right now.
