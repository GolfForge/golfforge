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

- **Latest:** **GOL-157 v0.0.4-alpha macOS arm64 SHIPPED — first Mac build with course play** (2026-06-04, Mac). `GolfForge-macos-arm64.zip` attached to the existing `v0.0.4-alpha` GitHub Release alongside the Win64 zip. Cook time 2m 13s warm-cache on M4 base. Two Mac-portability bugs caught + patched during the cook: (a) **ScorecardPanel.cpp Carbon `Cell` typedef clash** introduced by GOL-148 — renamed local helper to `MakeCell` (3 sites); Windows builds were unaffected because Carbon isn't included on Win64. **Cookbook lesson:** prefix generic UI helper names (`MakeCell`, `MakeRow`, `MakePanel`) — Apple framework typedefs use single-word names. (b) **GolfRangeHUD.cpp themed golf-tee cursor renders as violet NSCursor ghosts on Mac** — gated behind `#if !PLATFORM_MAC` for v0.0.4 (Mac users see OS arrow, cosmetic only). **GOL-159 filed** for the proper cross-platform Slate-software-cursor widget. **Resolved Mac courses staging path:** `<App>.app/Contents/UE/courses/<id>/` (CoursePaths candidate base #2). **GOL-158 filed** for the long-term refactor that drops the runtime `courses/` dependency (bake `hole.geojson` + `heightmap.json` into UCourseManifest UDataAsset, ~1 week beta-time work). Smoke-tested clean on M4: menu → range → Play Course → wizard → Tee Off → Hole 1 with glass HUD. **GOL-157 DONE.**
- **Prior:** **v0.0.4-alpha — Windows build + release prep** (2026-06-03, Windows). First package carrying the whole GOL-137 UI epic + GOL-151 Lucide icons. Win64 `BuildCookRun` (Development, both maps from `MapsToCook`, 1m16s) → post-cook `courses/golfforge-demo-black/` copy → launcher renamed `Golfsim.exe`→`GolfForge.exe` (cosmetic; real rename is GOL-152) → user smoke-tested menu/range/course → `GolfForge-windows-x64-v0.0.4-alpha.zip`. **GOL-148 staging verified** (all UI fonts incl. `Lucide` cooked). README + 4K screenshots refreshed (`mainmenu`/`roundsetup`/`coursehud` + candidates; ~2.7 MB set, PNG for UI / JPEG q85 for scenes). **Windows published** (tag `v0.0.4-alpha`, pre-release); **macOS arm64 follows on the same Release** → **GOL-157**. History → `docs/worklog.md`.
- **Active focus (alpha-3 next):** v0.0.4-alpha shipped cross-platform; per the 2026-06-02 restructure, alpha-3 = **course + range vibe pass + practice modes + ball physics polish + single-player on real courses**. User explicitly wants the next session focused on **materials/atmosphere/life** for both the course and the range (visual fidelity, not perf — perf stays deferred to beta per GOL-100). Specific tickets in the queue: (1) **GOL-94 first-party course authoring polish** (P3) — bunker depressions + fairway mowing patterns; (2) **GOL-93 ball physics + surface interaction** (P2) — multi-bounce/skip, coefficient tuning, green spin-back; (3) **GOL-73/74/75 practice modes** (P2) — closest-to-pin, islands, putting; (4) **GOL-115–122 GOL-112 single-player round flow children**. **Windows-side follow-ups from the v0.0.4 cook:** GOL-152 (rename Golfsim→GolfForge full module rename), GOL-153 (stepper pill repaint), GOL-154 (live weather → conditions wind/temp), GOL-158 (UCourseManifest refactor — beta), GOL-159 (cross-platform Slate cursor — beta), plus dev/secondary panel reskins + reduced-motion a11y.
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
