# CLAUDE.md — context for Claude (and other coding agents) in `golfsim`

If you're an AI assistant landing in this repo, read this file first. It's the cross-session,
cross-machine handoff document — Claude/Cowork sessions don't sync between machines, so this
file (committed to git) is how the Mac-side and Windows-side sessions stay coherent.
**Read on start, update before stop, commit before exit** (see the session-end checklist below).

This file stays slim on purpose. The heavyweight reference material lives in `docs/` and is read
**on demand** — pull these in only when the task calls for it:

- **Linear** ([linear.app/golfsim](https://linear.app/golfsim)) — the prioritized work queue. Every ticket has goal, plan, done-when, dependencies, and files to touch. Each is also tagged `machine/windows` / `machine/mac` / `machine/either` so you can filter to what the current machine can actually do.
- `docs/worklog.md` — what's been done (summarized history). This is the "shipped features" reference. **All prior milestones live here, not above.**
- `docs/ue5-cookbook.md` — UE5/MCP/PCG pitfalls + recipes + Fab asset list (read when you hit an engine issue)
- `docs/pipeline-data-contract.md` — the `courses/<id>/` file-shape contract
- `docs/plan.md` — full project plan, MVP ladder, decisions
- `docs/event-protocol.md` — the event envelope every driver implements
- `docs/windows-setup.md` — Windows prerequisites + UE5 project creation + MCP wiring
- `README.md`, `pipeline/README.md` — human-facing orientation + pipeline quickstart

---

## Project in one paragraph

`golfsim` is an open-source, cross-platform golf simulator. The competitive thesis is three-fold: (1) AI-assisted course pipeline that ingests open LIDAR + OSM data so the community can produce courses 10x faster than existing community-course tools allow; (2) walking/treadmill integration via BLE FTMS that adds a fitness tier to sim-golf (sit-down play stays a first-class option); (3) genuinely cross-platform — Windows / Mac / Linux desktop with iPad as a future mobile tier, all from a single UE5 codebase.

## Architecture invariants — do not break these

1. **Monolithic per-platform binary.** Each platform ships as one executable containing the sim, the renderer, and the platform-appropriate hardware drivers. No services, no IPC for end users. Drivers go in via CoreBluetooth (Apple) / Windows.Devices.Bluetooth (Windows) / BlueZ (Linux). Multi-machine setups are developer-only.
2. **In-process normalized event bus.** All hardware drivers (walking sensor, launch monitor, HR strap, manual UI) publish typed events of the same shape to an in-process pub/sub. The sim subscribes. Multiplayer is the same envelope serialized over the network. The shape is the contract — see `docs/event-protocol.md`.
3. **Course pipeline is decoupled from the engine.** The Python pipeline in `pipeline/` runs anywhere; it produces UE5-import-ready PNGs in `courses/<id>/`. The engine ingests those files. Pipeline and engine never share runtime state. (File shapes: `docs/pipeline-data-contract.md`.)
4. **AGPL-3.0 + commercial dual-license.** The repo is AGPL-3.0 (`LICENSE`); closed-source/proprietary use needs a separate paid commercial license (`COMMERCIAL.md`). New code in the repo MUST be AGPL-3.0-compatible, and external contributions require a CLA so the project can keep offering commercial exceptions. Public-facing name: **GolfForge**.

## Where work happens (which machine does what)

| Machine | Owns |
|---|---|
| Mac mini M5 | Python pipeline development. All docs. Future BLE driver prototypes via `bleak`. Future Mac/iOS UE5 builds (Apple toolchain requires Mac). |
| Windows PC | UE5 development for Windows/Linux build targets. MCP-driven editor automation. C++ engine work. |
| Both | The git repo. Anything committed is visible to the other machine on next `git pull`. |

## Conventions

- **Git:** main branch only for now (single contributor). Conventional-style commit messages welcome but not enforced. Use `git lfs` — `.gitattributes` declares LFS rules for `*.uasset`, `*.umap`, `*.png` in `engine/` and `courses/`.
- **Python:** the pipeline uses stdlib + `requirements.txt`; run `./setup.sh` to create the venv (auto-detects `uv`, falls back to `python3 -m venv`). Engine-side helper scripts (`engine/scripts/*.py`) run via `execute_unreal_python` in the UE5 embedded interpreter and are **stdlib-only** — no system venv on Windows.
- **UE5:** project files live in `engine/`. Keep one `.uproject` there. Marketplace/Fab assets are gitignored & re-downloadable per machine — current asset list + Fab-plugin notes are in `docs/ue5-cookbook.md`.
- **Pipeline outputs:** committed assets in `courses/<id>/` are reference data, not throwaway artifacts. Shapes + how to consume each file: `docs/pipeline-data-contract.md`. `osm_raw.json` and `dem.tif` are gitignored intermediates.
- **No emojis** in files unless explicitly requested.
- **No new markdown files** unless they add structural value. Update existing docs rather than creating siblings.

## Current status

- **Latest:** **GOL-141 — Round Setup wizard: chrome + Course step** (2026-06-03, Windows). Fifth child of the GOL-137 UI-elevation epic. Replaced the old `UPreRoundPicker` combo-box modal with the full-screen GolfForge wizard: header brand + clickable stepper (Course→Format→Players) + close-X, footer live summary + Back/Continue, a `UWidgetSwitcher` whose Course step is built in full (3-up `UI/CourseCard` grid seeded from the new `Game/CourseRegistry`) and whose Format/Players steps are "Coming soon" stubs for GOL-142/143. Selecting the cooked course unlocks Continue → steps to Tee Off → `StartRound(CourseId, Normal)`. Difficulty dropped from the wizard (moves to the Format step); resume banner built but hidden behind `SetResumeVisible()`. `GolfRangeHUD` retyped to `URoundSetupWizard` (member + `bRoundSetupOpen` + Open/Ensure/Close trio); `PreRoundPicker.{h,cpp}` deleted. **Known gap → GOL-153 (Low):** the stepper's active state reads via **text only** (green/bold number + white/bold name) — pill backgrounds/badge wouldn't repaint (`SetStyle`/`SetBrush`/visibility-toggle all failed in the centered topbar `UHorizontalBox`, though identical brushes paint on cards/buttons; root cause unknown). Commit `4fad7de` (full-build + Live-Coding verified). **GOL-137: 5/12 children done (138/139/150/140/141) — history in `docs/worklog.md`.**
- **Active focus:** (1) **GOL-137 UI elevation** (High) — next child: **GOL-142** (round-setup **Step 2 Format** — holes/game/turn/hole-out; reuses the `USegmentedControl`/`UToggleSwitch` atoms + the wizard chrome just built), then **GOL-143** (Step 3 Players + Tee Off), then in-round HUD (GOL-144/145). Also queued: **GOL-151** (Lucide icon font), **GOL-152** (project/exe rename Golfsim→GolfForge — cross-machine), **GOL-153** (stepper pill repaint). (2) **v0.0.3-alpha release** — Mac cook (`machine/mac`) then tag + GitHub Release. (3) **GOL-49** — `engine/scripts/package.sh` bundling BuildCookRun + the post-cook `courses/` copy.
- History → `docs/worklog.md`. Next work → Linear ([linear.app/golfsim](https://linear.app/golfsim)). Engine gotchas → `docs/ue5-cookbook.md`.

## Session-end checklist

Before you stop a session, in order:

1. **Summarize** newly-completed work into `docs/worklog.md` (prepend a dated, tight entry — outcome + committed artifact, not blow-by-blow). Reference the Linear ticket ID (e.g., `GOL-5 R3 trees DONE`) so cross-links are easy.
2. **Update Linear:** mark shipped tickets Done, drop a brief outcome comment (numbers, what files landed). File new tickets for any follow-ups you discovered while working. Re-prioritize the top of the queue if your work changes what's next.
3. **Append** any new gotchas/recipes to `docs/ue5-cookbook.md`.
4. **Bump** the Current status section above — *replace* the "Latest" line with the new milestone (do **not** move the old one down to a "Prior:" entry; that history lives in `docs/worklog.md`). Update the Active-focus pointer to the next-priority Linear ticket IDs. Keep the section to ~3 bullets max.
5. **Update** `docs/plan.md` if an architectural decision changed.
6. **Commit + push**, and tell the user what state the repo is in so the other machine knows what it'll see on `git pull`.

The next session — possibly on the other machine — starts by reading this file, scans Linear for the top-priority ticket tagged for its machine, and picks up from there.
