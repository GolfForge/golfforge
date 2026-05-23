# CLAUDE.md — context for Claude (and other coding agents) in `golfsim`

If you're an AI assistant landing in this repo, read this file first. It's the cross-session,
cross-machine handoff document — Claude/Cowork sessions don't sync between machines, so this
file (committed to git) is how the Mac-side and Windows-side sessions stay coherent.
**Read on start, update before stop, commit before exit** (see the session-end checklist below).

This file stays slim on purpose. The heavyweight reference material lives in `docs/` and is read
**on demand** — pull these in only when the task calls for it:

- `docs/worklog.md` — what's been done (summarized history)
- `docs/backlog.md` — what's next (the prioritized work queue)
- `docs/ue5-cookbook.md` — UE5/MCP/PCG pitfalls + recipes + Fab asset list (read when you hit an engine issue)
- `docs/pipeline-data-contract.md` — the `courses/<id>/` file-shape contract
- `docs/plan.md` — full project plan, MVP ladder, decisions
- `docs/event-protocol.md` — the event envelope every driver implements
- `docs/windows-setup.md` — Windows prerequisites + UE5 project creation + MCP wiring
- `README.md`, `pipeline/README.md` — human-facing orientation + pipeline quickstart

---

## Project in one paragraph

`golfsim` is an open-source, cross-platform golf simulator. The competitive thesis is three-fold: (1) AI-assisted course pipeline that ingests open LIDAR + OSM data so the community can produce courses 10x faster than GSPro's CourseForge; (2) walking/treadmill integration via BLE FTMS that turns sim-golf into a Zwift-like fitness product; (3) genuinely cross-platform — Windows / Mac / Linux desktop with iPad as a future mobile tier, all from a single UE5 codebase.

## Architecture invariants — do not break these

1. **Monolithic per-platform binary.** Each platform ships as one executable containing the sim, the renderer, and the platform-appropriate hardware drivers. No services, no IPC for end users. Drivers go in via CoreBluetooth (Apple) / Windows.Devices.Bluetooth (Windows) / BlueZ (Linux). Multi-machine setups are developer-only.
2. **In-process normalized event bus.** All hardware drivers (walking sensor, launch monitor, HR strap, manual UI) publish typed events of the same shape to an in-process pub/sub. The sim subscribes. Multiplayer is the same envelope serialized over the network. The shape is the contract — see `docs/event-protocol.md`.
3. **Course pipeline is decoupled from the engine.** The Python pipeline in `pipeline/` runs anywhere; it produces UE5-import-ready PNGs in `courses/<id>/`. The engine ingests those files. Pipeline and engine never share runtime state. (File shapes: `docs/pipeline-data-contract.md`.)
4. **MIT licensed.** Keep contribution surface maximal. New code in the repo MUST be MIT-compatible.

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

- **Latest:** Practice Range R2 DONE (2026-05-23, Windows) — UMG launch-monitor panel, the project's first UMG widget, pure-C++ `UGolfRangePanel` (no WBP): top-right metrics grid (yd/mph) refreshed per shot + `UComboBoxString` club dropdown synced with the 1-6 keys. Range now shows the cursor (GameAndUI) so the dropdown is clickable; Space still fires. Bundled a themed golf-tee **hardware cursor** on the range (transparent PNG in `Content/Slate/`, set via `SetHardwareCursor`). Built on R1 (tee-fixed arrow aim) + the spacebar shot tool.
- **Active focus:** practice-range polish, **R3 next** — perimeter trees (+ optional 3D grass) via the existing PCG path; then R4 time-of-day + weather selectors driven from the R2 panel. See `docs/backlog.md` for the full breakdown.
- History → `docs/worklog.md`. Next work → `docs/backlog.md`. Engine gotchas → `docs/ue5-cookbook.md`.

## Session-end checklist

Before you stop a session, in order:

1. **Summarize** newly-completed work into `docs/worklog.md` (prepend a dated, tight entry — outcome + committed artifact, not blow-by-blow).
2. **Update** `docs/backlog.md` — drop shipped items, re-prioritize what's next.
3. **Append** any new gotchas/recipes to `docs/ue5-cookbook.md`.
4. **Bump** the Current status section above.
5. **Update** `docs/plan.md` if an architectural decision changed.
6. **Commit + push**, and tell the user what state the repo is in so the other machine knows what it'll see on `git pull`.

The next session — possibly on the other machine — starts by reading this file and picks up from here.
