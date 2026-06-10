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
- `docs/launch-monitors.md` — user-facing guide: connect an LM via its GSPro Open Connect connector (dev internals live in `engine/Golfsim/Source/Golfsim/Drivers/README.md`)
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
| Mac mini M5 | Apple-toolchain-only work: Mac/iOS UE5 builds + cooks (require Mac), future BLE driver prototypes via `bleak`. Historically the primary spot for the Python pipeline + docs, but neither is Mac-only. |
| Windows PC | UE5 development for Windows/Linux build targets. MCP-driven editor automation. C++ engine work. |
| Both | The git repo (commit → visible on the other machine's next `git pull`). **The Python pipeline + docs are platform-agnostic — edit on whichever machine you're at** (the pipeline runs end-to-end on Windows: venv via `uv`, Overpass/OpenTopo, PIL/numpy, the test suite). Only real cross-machine rule: commit so the other side pulls it, and don't leave two divergent uncommitted copies of the same file. |

## Conventions

- **Git:** main branch only for now (single contributor). Conventional-style commit messages welcome but not enforced. Use `git lfs` — `.gitattributes` declares LFS rules for `*.uasset`, `*.umap`, `*.png` in `engine/` and `courses/`.
- **Python:** the pipeline uses stdlib + `requirements.txt`; run `./setup.sh` to create the venv (auto-detects `uv`, falls back to `python3 -m venv`). Engine-side helper scripts (`engine/scripts/*.py`) run via `execute_unreal_python` in the UE5 embedded interpreter and are **stdlib-only** — no system venv on Windows.
- **UE5:** project files live in `engine/`. Keep one `.uproject` there. Marketplace/Fab assets are gitignored & re-downloadable per machine — current asset list + Fab-plugin notes are in `docs/ue5-cookbook.md`.
- **Pipeline outputs:** committed assets in `courses/<id>/` are reference data, not throwaway artifacts. Shapes + how to consume each file: `docs/pipeline-data-contract.md`. `osm_raw.json` and `dem.tif` are gitignored intermediates.
- **No emojis** in files unless explicitly requested.
- **No new markdown files** unless they add structural value. Update existing docs rather than creating siblings.

## Current status

- **Latest:** **GOL-75 green break + round surface classification DONE (2026-06-10, Windows).** Putts (and any rolling ball) now **break along the fall line**: `GroundRoll.cpp` roll phase carries a 2D velocity and adds the slope's downhill gravity each step (`RollSlopeGain·g·Nz·|(Nx,Ny)|`; `RollSlopeGain=(5/7)·0.5`, a tunable knob), so the heading curves and speed modulates (uphill dies short, downhill runs). **Flat normal reduces EXACTLY to the old roll** (equivalence test 1e-6). The real unlock was fixing the **round `SurfaceProvider`**: the range HUD had classified course-world landings via the origin-centered range lane → everything read `rough` (putts died in ~1 yd once friction depended on the lie). It now classifies the **course splat sampler** (`UCourseSurfaceSubsystem::ClassifyAt`) at the tee+aim→world position when loaded (range still uses the lane), fixing **all** round shots; the HUD normal source 5-tap-averages for smooth break. Putt coefficients: stimp on green/fairway/tee, moderate **fringe friction** (0.25) on rough, full grab on bunker. New `Golfsim.GroundRoll.RollFollowsFallLine` test; suite **90/90**. Commits `831e823`+`4c9dced`. Break magnitude provisional (LM-gated, GOL-195).
- **Active focus — alpha-2 gameplay foundation.** The alpha-3 visual vibe pass is done; now make it *play* like a course. **(1) Bunkers — GOL-34 DONE. (2) Ball physics — GOL-93:** GOL-38 multi-bounce + GOL-39 cross-surface/spin-back + GOL-196 terrain-aware bounce DONE; remaining is **GOL-195** (per-surface coefficient calibration vs Square Omni LM data + promote `FSurfaceRoll` to a tunable UDataAsset/UDeveloperSettings) — **LM-gated, deferred**. **(3) Practice mode — GOL-73 closest-to-pin DONE** (carry + flat-green putt-out). **(4) Pin positions — GOL-191/192 DONE** (drop the course disc + Static/Random/Tournament pins from `green.geojson`; gimme-ring polish). **(5) Green break — GOL-75 DONE** (fall-line roll from the heightmap normal + round surface classification; putting plays on real course greens now). Break magnitude is provisional (`RollSlopeGain`, LM-gated GOL-195). Next practice ticket: islands **GOL-74**. **Parallel — UI cleanup:** tracer UI (GOL-107), reskin secondary panels onto `GolfUITheme` (GOL-155), round-setup pills (GOL-153).
- **Background / when LM hardware lands (GOL-97):** GOL-178 GSPro Open Connect driver done; **GOL-181** community-connector validation (Low, gated on the connectors + incoming **Square Omni / Blue Tees Rainmaker**) — **only Square Omni/squaregolf is validated; do NOT re-add springbok (MLM2PRO/Mevo+), its dropdown entry stays commented out in `LaunchMonitorManager` and the README promotes Square Omni only**; **GOL-180** native-connector research spikes (R50/Uneekor/GCQuad).
- **Wrapping up / deferred:** alpha-3 (GOL-160) core done — remaining **GOL-168** props (Fab backdrop/benches/clubhouse, blocked on user meshes) + **GOL-175** caustics; deferred GOL-31/173/170/169/27/30 + **GOL-172** (mac). Loose ends: **GOL-189** DLSS FrameGen (toggle in, no frames yet), **GOL-152** rename. Beta epics: GOL-86 distribution, GOL-133 hw matrix, GOL-95 Mac parity, GOL-96 walking/FTMS (GOL-32 parts).
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
