# CLAUDE.md — context for Claude (and other coding agents) in `golfsim`

If you're an AI assistant landing in this repo, read this file first. It's the cross-session,
cross-machine handoff document — Claude/Cowork sessions don't sync between machines, so this
file (committed to git) is how the Mac-side and Windows-side sessions stay coherent.
**Read on start, update before stop, commit before exit** (see the session-end checklist below).

This file stays slim on purpose. The heavyweight reference material lives in `docs/` and is read
**on demand** — pull these in only when the task calls for it:

- **Linear** ([linear.app/golfsim](https://linear.app/golfsim)) — the prioritized work queue. Every ticket has goal, plan, done-when, dependencies, and files to touch. Each is also tagged `machine/windows` / `machine/mac` / `machine/either` so you can filter to what the current machine can actually do.
- `docs/worklog.md` — what's been done (summarized history). This is the "shipped features" reference.
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
4. **AGPL-3.0 + commercial dual-license.** The repo is AGPL-3.0 (`LICENSE`); closed-source/proprietary use needs a separate paid commercial license (`COMMERCIAL.md`). New code in the repo MUST be AGPL-3.0-compatible, and external contributions require a CLA so the project can keep offering commercial exceptions. (Public-facing name: **GolfForge** — see Linear GOL-43/GOL-48; the repo/module rename is a separate task.)

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

- **Latest:** **GOL-9 ground roll + range follow camera DONE** (2026-05-28, Windows). Carry now becomes **total distance + final lie** via a pure-SI **analytical** ground-roll model (not Chaos): `Physics/GroundRoll.{h,cpp}` (`EGolfLie` + per-surface `FSurfaceRoll` table + `SimulateGroundRoll`) and `Physics/RangeSurface.{h,cpp}` (`ClassifyLie` mirroring `build_range_splatmap.py`), wired through a world-agnostic **`SurfaceProvider` seam** on the integrator (the range HUD registers one: launch-local SI → world → lie), plus a panel **Total (yd)** row. Trackman driver = carry 264 / total 280.5 yd; **24/24 tests** (6 new `Golfsim.GroundRoll.*`). The range also got a **Follow camera** (GOL-37: a chase `ACameraActor` via `SetViewTargetWithBlend` + right-click orbit; the ball now **launches from the floor**, not eye height; red landing-marker removed; no 16:9 letterbox). **Post-landing is a flat friction-slide for now (no real skips)** — follow-ups GOL-38 (multi-bounce), GOL-39 (physics refinement: coefficients/green spin-back/cross-surface/UDataAsset), GOL-40 (course lie from splatmap PNGs), GOL-41 (follow-cam UX), GOL-42 (range bunker/green). *Prior — **GOL-11 OpenFlight LIVE over Socket.IO; GOL-36 fixed** (2026-05-28): a pluggable LM framework under `Drivers/`: abstract `ULaunchMonitorDriver` + `ULaunchMonitorManager` (`UGameInstanceSubsystem`) that enumerates/selects the active driver (the seam a future settings UI + Square Omni/GOL-12 use), with `UOpenFlightDriver` the first concrete driver. OpenFlight is **Socket.IO (Engine.IO v4) over Flask-SocketIO**, so the driver speaks a minimal EIO4 layer on UE's `IWebSocket` (handshake `0`→`40`→ack, ping `2`/pong `3`, decode `42["shot",{shot,stats}]`). Real-schema parser: `club`, `launch_angle_vertical`/`_horizontal`, `spin_rpm`+`spin_axis_deg`→back/side split, launch-angle est-spin heuristic shown as `est` on the panel. HUD subscribes `shot.taken` so **any** producer (random/manual/LM) drives the panel; panel got a connection-status dot. Console: `golfsim.LMSelect/LMConnect/LMDisconnect/LMSimulate/LMTrigger`. AGPL-isolated (talk-over-socket only; `Drivers/README.md`). **GOL-36 was NOT an lws bug:** the "can't connect" was a Windows `localhost`→IPv6 `::1` mismatch vs the IPv4-only Flask dev server (binds `0.0.0.0`) — lws dialed a dead `::1` and surfaced a misleading `WSAENOTSOCK`/errno 10038; fixed by mapping `localhost`→`127.0.0.1` in `ResolveUrl`. Verified live (`golfsim.LMConnect` → mock shots → ball + panel; 17/17 tests). An experimental plain RFC 6455 `/feed` transport (simpler, no EIO4) is parked on branch `feature/openflight-raw-feed` pending a possible upstream PR; `main` ships Socket.IO so it works against every stock OpenFlight install. **Range UX landed with it:** a panel **Launch Monitor dropdown** (Off + drivers) connects/disconnects the active driver, a **Simulate Shot** button (shown when connected) round-trips `simulate_shot`, **Carry counts up** during ball flight, and our club selection **pushes to OpenFlight** (`set_club`) with naming normalized in the driver (our display names ↔ OpenFlight ids). Also fixed a one-shot panel-display lag by carrying the source shot's launch metrics on `FShotOutcomeEvent` (the integrator publishes the outcome before the HUD's own `shot.taken` ran). Earlier 2026-05-26: **GOL-8 manual-shot dialog DONE** — the first deliberate EventBus *producer*: a pure-C++ `UManualShotDialog : UUserWidget` (no WBP; `USpinBox` fields for ball speed/launch/backspin/sidespin/azimuth + a club dropdown + Fire). **M** toggles it on the range (auto-fire panel hides); Fire routes through the shared `AGolfRangeHUD::PublishShotTaken` (extracted from `FireRandom`) and publishes `FShotTakenEvent` with `Source="manual-shot-dialog"`, so it flies the entered shot (not random); picking a club autofills the spinners from the bag and the dialog shows the resolved carry/offline. Bottom-right anchored, fixed-width 1-decimal spinners (mostly stops the AutoSize panel jittering on slider drag; tiny residual drift accepted). Earlier 2026-05-26: **GOL-7 EventBus DONE** — the day-one in-process pub/sub (invariant #2): pure-C++ `FGolfEventBus` (typed channel per `EEventKind`, synchronous snapshot dispatch) wrapped by `UEventBusSubsystem : UGameInstanceSubsystem`; envelope + payload `USTRUCT`s in `Events/EventTypes.h` (`FGolfEvent` base + `FShotTakenEvent`/`FShotOutcomeEvent`, per `docs/event-protocol.md`). A built-in integrator subscriber turns `shot.taken` → `session.shot_outcome` via `GolfBallFlight::Simulate`; the range now **fires through the bus** (`AGolfRangeHUD::FireRandom` publishes, `OnShotOutcome` plays the ball + panel — no direct solver call), `golfsim.PublishTestShot` round-trips it, 4 new automation tests (11/11 pass headless). **Unblocks all 10 driver/sim tickets.** *Prior (2026-05-23, Windows): Practice Range R3 + R4 DONE.* **R3 trees:** range got its own PCG graph `/Game/PCG/PCG_TreeScatter_Range` (separate from BethPage's to dodge the shared-density footgun), driven by `scatter_range_trees.py` (persistent, GenerateOnLoad). `build_range_splatmap.py` reworked from 504 m perimeter ring → tight 400×70-yd open lane framed by a 22-yd tree wall (trees ±35 yd off center). Perf: 4K had tanked to ~45 FPS (GPU/geometry-bound; 3D fairway grass was ~half the cost), so grass **removed from the range** (grass-output node deleted from `M_PracticeRange`; `build_range_material.py` defaults `BUILD_GRASS=False`) and tree density dialed to **0.10 ppsm (~1,966 trees)**. Landed ~50 FPS / 16 ms GPU. `stat fps` + resolution readout added to the range HUD. **R4 environment selectors:** two Time/Sky dropdowns on the R2 panel drive a new `AGolfRangeEnvironment` director actor (canonical UE pattern), find-or-spawned by HUD in PIE. `ApplyEnvironment()` composes Time (Dawn/Morning/Noon/Dusk/Night) × Sky (Clear/Cloudy/Overcast) → DirectionalLight rotation/temperature/intensity + fog + SkyLight intensity + VolumetricCloud `Cloud_GlobalCoverage`/`Cloud_GlobalDensity` (runtime MID). Pure C++, zero umap/asset changes — UE5.7 Basic level already ships the cloud + RealTimeCapture SkyLight + SkyAtmosphere. `golfsim.SetTime`/`golfsim.SetSky` console commands added.
- **Backlog moved to Linear (2026-05-25):** `docs/backlog.md` is gone; the prioritized work queue is now [linear.app/golfsim](https://linear.app/golfsim). Tickets are self-contained (goal + plan + done-when + files + pitfalls) so an agent can pick one cold. Filter by `machine/windows` or `machine/mac` to see what fits the current machine.
- **Active focus:** **EventBus (GOL-7) + manual-shot dialog (GOL-8) shipped; LM framework + OpenFlight (GOL-11) LIVE over Socket.IO** (GOL-36 fixed — `localhost`→IPv6 mismatch, not an lws bug; works against stock OpenFlight). A real launch monitor now drives the sim. Tracks from here:
  - **Ship target (this week):** range + a course (or a couple of holes) playable to OpenFlight users now that the LM is live.
  - **Square Omni driver (Medium, Windows):** **GOL-12** — second concrete `ULaunchMonitorDriver` on the proven framework (subclass, normalize its shot, `RegisterDriver` one line). What the pluggable seam was built for.
  - **Raw `/feed` upstream (Low, optional):** branch `feature/openflight-raw-feed` holds a simpler plain-WS transport; revisit if the OpenFlight maintainer takes the PR. Not needed for shipping.
  - **Sim correctness (Medium, Windows):** **GOL-9 ground roll DONE** (analytical pure-SI; carry → total + `final_lie`; panel Total row). Refinement now lives in **GOL-38** (multi-bounce/skip — the post-landing model is a flat slide today) + **GOL-39** (coefficient tuning, green spin-back, cross-surface, UDataAsset); **GOL-40** generalizes lie detection to real courses via splatmap PNGs.
  - **Pipeline polish (High, Mac/either):** **GOL-33** — Bethpage's pipeline output has holes / missing tees / missing fairways. Audit + fix the OSM extraction before chasing more courses (GOL-19 second-course deferred until this lands). Windows can do the UE-side visual audit; the Python fix is Mac-side.
  - **Walking track (High, Mac):** **GOL-32** — order ESP32 + TCRT5000 parts (5-minute Amazon run). Unblocks **GOL-13** build → **GOL-14** FTMS Windows driver.
  - **Range polish (Low, Windows):** GOL-27 (R4 preset tuning), GOL-28 (yardage markers), GOL-29 (`SetPin` target), GOL-30 (Nanite backdrop), GOL-31 (static-Nanite trees) — all optional follow-ups from R3/R4.
  - **Course-quality arc (Medium, after GOL-33):** **GOL-34 bunker authoring tools** (raised lip + depression — sand traps that look like real bunkers), **GOL-35 fairway cut patterns** (stripes / criss-cross / diagonal mowing lines). Both blocked by GOL-33 — the polish pass establishes the corrected feature polygons these tools consume.
- History → `docs/worklog.md`. Next work → Linear. Engine gotchas → `docs/ue5-cookbook.md`.

## Session-end checklist

Before you stop a session, in order:

1. **Summarize** newly-completed work into `docs/worklog.md` (prepend a dated, tight entry — outcome + committed artifact, not blow-by-blow). Reference the Linear ticket ID (e.g., `GOL-5 R3 trees DONE`) so cross-links are easy.
2. **Update Linear:** mark shipped tickets Done, drop a brief outcome comment (numbers, what files landed). File new tickets for any follow-ups you discovered while working. Re-prioritize the top of the queue if your work changes what's next.
3. **Append** any new gotchas/recipes to `docs/ue5-cookbook.md`.
4. **Bump** the Current status section above (especially the "Latest" line and the active-focus pointer to specific Linear ticket IDs).
5. **Update** `docs/plan.md` if an architectural decision changed.
6. **Commit + push**, and tell the user what state the repo is in so the other machine knows what it'll see on `git pull`.

The next session — possibly on the other machine — starts by reading this file, scans Linear for the top-priority ticket tagged for its machine, and picks up from there.
