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

- **Latest:** **v0.0.5-alpha macOS arm64 cook shipped (GOL-190)** (2026-06-08, Mac). Cooked the Mac build (`BuildCookRun` green in 2m51s) and uploaded `GolfForge-macos-arm64-v0.0.5-alpha.zip` (1.22 GB) to the `v0.0.5-alpha` GitHub Release next to the Windows zip. Post-steps: dylib fixup (`libtbb.12`+`libtbbmalloc.2` into `Contents/MacOS/`, GOL-71), course staging (`courses/golfforge-demo-black/` → `Contents/UE/courses/`), `Golfsim.app`→`GolfForge.app`, `xattr -cr`; smoke test passed (played a hole + range). **Cook-blocker found:** the GOL-160 facelift added Fab deps absent from this Mac clone — two cook crashes fixed by fetching **7 packs** (4 Megascans 3D rocks `Massive_Tundra_Rock_Formation_wcrmbiar`/`_wd4icipcb`/`Nordic_Forest_Cliff_Large_xibldbu`/`Tundra_Mossy_Boulder_vivveardw` **crash** if missing; surfaces `Cut_Grass_sfenffsa`/`Mossy_Forest_Floor_vfylbge` checkerboard; `Tree_Baltic_Pine`); cookbook Fresh-clone recipe now lists all 7. Both v0.0.5-alpha desktop binaries (Windows + Mac) are now published.
- **Active focus:** **LM connectors (GOL-97 epic):** GOL-178 driver done → **GOL-181** validate the community connectors against the listener (gate before public claim — **only Square Omni/squaregolf is validated; springbok (MLM2PRO/Mevo+) is NOT — its full release expects the GSPro APIv1 connect window, needs research; README promotes Square Omni only, do not re-add springbok — its dropdown entry is now commented out in `LaunchMonitorManager`**); **GOL-180** native-connector epic for auth-gated devices (R50/Launch Pro/GCQuad). **GOL-189** DLSS Frame Generation toggle shipped but **not generating frames yet** (test cooked exe / vsync / Reflex-runtime). **alpha-3 GOL-160 vibe pass:** flag (GOL-165) + materials (GOL-163) + atmosphere (GOL-161) + water (GOL-164) + tree species (GOL-167) + ambient SFX (GOL-166) done; props (GOL-168) range markers + on-turf numbers + criss-cross mow stripes done, **GOL-28 done**. Remaining: **GOL-168** remainder (Fab backdrop/benches/clubhouse — blocked on user meshes) + **GOL-175** water caustics; optional GOL-163 **P4** per-region noise. **Deferred:** **GOL-31** static-Nanite tree variant + **GOL-173** tree sway; sound polish + wind-coupling (Polish epic); **GOL-171**/**GOL-172** course layer-data audit + pipeline overlay (mac); **GOL-170** 3D grass; **GOL-169** night; **GOL-27** range R4. Siblings: **GOL-93** ball physics, **GOL-94** course authoring. Other: GOL-152 rename, GOL-153/154, GOL-158/159 (beta).
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
