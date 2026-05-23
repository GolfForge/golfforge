# Worklog — what's been done (summarized)

> Dated milestone summaries, newest on top. The durable outcome + the committed artifact, not the blow-by-blow — process detail lives in git history, `docs/ue5-cookbook.md`, and the scripts themselves.

## 2026-05-23 — Practice Range + range shot tool (Windows)

- **Practice Range level** at `/Game/Maps/PracticeRange.umap` (Basic level), now the **default map** (`DefaultEngine.ini` `EditorStartupMap`/`GameDefaultMap`). Flat 504 m walkable launch-monitor test range: centered 400×100 yd fairway strip, tee box at the −X end, rough fill, perimeter tree band. Three reusable scripts: `build_range_splatmap.py` (stdlib-only PNG writer → 4 mutually-exclusive masks), `build_range_material.py` (`M_PracticeRange`, fork of the course material — same Fab surfaces + KBG tint + Fairway grass output), `build_range_lighting.py` (sun/fog/skylight). PlayerStart at the tee facing +X, `BP_FirstPersonGameMode` so WASD walks. Verified in PIE: driver shot carries ~241 m down the fairway with tracer + landing marker. (Manual editor steps, 5.7 Python-unbound: new Basic level, flat New Landscape 8×8 @ section 63 / scale 100, Paint reusing existing LIIs, Manage>Import the 4 splats.)
- **Range spacebar shot tool**: `AGolfRangeHUD` (set as the GameMode HUDClass). In PIE, number keys 1-6 select a club from a preset bag (Trackman PGA-Tour averages), Space hits a randomized shot (per-shot dispersion on speed/launch/spin/azimuth), selected club + last carry render bottom-right. Reuses the C++ solver + `AGolfBallActor`. Removed the Space→IA_Jump mapping so hitting doesn't jump.
- **Range R1 — tee-fixed arrow-key aiming** (backlog §R1): on the range the player is planted on the tee and Left/Right arrows yaw the view + aim. All in `GolfRangeHUD.h/.cpp`: a constructor enables `Tick`, which integrates yaw into the control rotation (the FP camera follows); `EnsureInputBound` calls `SetIgnoreMoveInput`/`SetIgnoreLookInput` on the controller (range-only — only this HUD does it, so BethPage walking is untouched) and binds Left/Right press+release on the legacy `InputComponent`; `FireRandom` now reads `PC->GetControlRotation()` for the shot heading so view = shot. No shared-asset (IMC/BP) edits. Verified in PIE: WASD + mouse-look dead, aim swept −34.9°→+27.8° across shots, carries correct per club.

## 2026-05-22 — Ball-flight aerodynamics solver, C++ (Windows)

Spin-dependent drag + gravity + Magnus lift + spin decay; RK4 @ 1 ms; pure-SI, UE-agnostic core. New files under `engine/Golfsim/Source/Golfsim/`: `Physics/BallFlightTypes.h`, `Physics/AeroCoefficients.h`, `Physics/BallFlightSolver.{h,cpp}` (`GolfBallFlight::Simulate` + `TraceFromResolved`), `GolfBallActor.{h,cpp}` (Toptracer-style growing-trail visualizer), `GolfsimConsole.cpp` (`golfsim.FireShot` / `golfsim.TraceShot`), `Tests/BallFlightTests.cpp` (7 automation tests). `Build.cs` got `PublicIncludePaths.Add(ModuleDirectory)` for path-qualified `Physics/...` includes. Validated headless against Trackman PGA-Tour averages (Driver/3w/5i/7i/9i/PW): carry within ~6%, apex ~7%, descent ~5°. **Flight-only** (carry, not post-bounce total — bounce/roll is deferred Chaos work). All coefficients are exposed knobs in `FAeroCoefficients`. `FShotInput` mirrors the `shot.taken` payload so the EventBus slots in later. Build with editor CLOSED (new classes); run tests headless with `-nullrhi`.

## 2026-05-17 — Material polish: all 7 landscape surfaces textured (Windows)

`M_GolfsimCourse` rebuilt from flat `Constant3Vector` placeholders to tiled Megascans PBR (albedo+normal+roughness) on every painted layer, via reusable scripts `build_course_material.py` + `build_landscape_grass.py`. Surface→layer map: Fairway/Green/Tee=Lawn_Grass, Rough=Uncut_Grass, Bunker=Bright_Desert_Sand, CartPath=Concrete_Floor, Trees=Clover_Patches (all Fab Megascans). 3D fairway grass via `LGT_FairwayGrass` = 5 flowerless Kentucky Bluegrass variants (PC3D Nanite pack), camera-driven with tight near-cull (~35 m) + ~10k density for perf. Base↔grass colour cohesion via a Desaturate→Multiply recolor; the `tint` RGB in `build_course_material.py` is a live knob. Backup kept as `M_GolfsimCourse_PreGrassBackup`. (Hard-won material-authoring fixes → `docs/ue5-cookbook.md`.)

## 2026-05-16 — M0.9 water (Windows + Mac)

Flat translucent water meshes — NOT the UE Water plugin (not scriptable for rendering in 5.7; see cookbook). Reusable `engine/scripts/build_water_actors.py`: one `DynamicMeshActor` per `water.geojson` Feature via GeometryScript, translucent `M_GolfsimWater`, water Z = median shoreline height (line-traced), collision generated (BlockAll). 8 `Water_*` actors on BethPageBlack, no plugin/restart, idempotent. Pipeline side (Mac): water vectorized to `water.geojson` and made mesh-only (`skip_raster` flag); painted water layer dropped after diagnosing the "big blob" as material-blend feathering, not a data mismatch. Georeference verified exact.

## 2026-05-15/16 — Trees via PCG (Windows)

- **M0.8**: painted Trees landscape layer added to `M_GolfsimCourse` (7 layers total). Legacy Foliage abandoned for Megaplant trees — see "the Megaplant saga" in the cookbook.
- **M0.8.5**: trees via PCG, end-to-end. `/Game/PCG/PCG_TreeScatter` 6-node graph built by reusable `engine/scripts/build_pcg_treescatter.py` (GetLandscape → SurfaceSampler → AttributeFiltering Trees>0.3 → TransformPoints → AddAttribute → SkinnedMeshSpawner). `engine/scripts/scatter_full_landscape.py` orchestrates a full-extent volume + async generate/report. Validated: **29,070 Silver Birch @ ~100 FPS / 4.3 GB GPU** full-course on BethPageBlack. Grey-trees root cause fixed: enable the `ProceduralVegetationEditor` plugin (now committed in `Golfsim.uproject`). Measure-only gate — umap NOT saved (volume transient); density tuning + 2nd species deferred (→ backlog).

## 2026-05-15 — First-person walk mode (Windows)

UE First Person feature pack migrated in (`/Game/FirstPerson/`, `/Game/Characters/`, `/Game/Input/`); `BP_FirstPersonGameMode` set as BethPageBlack's Game Mode Override, PlayerStart on the course. Press Play → WASD walks the 43 m relief at eye level. This is the in-editor course-testing harness.

## 2026-05-14/15 — Course import M0 → M0.7 (Windows)

- **M0**: Bethpage Black heightmap imported as a Landscape in `/Game/Maps/BethPageBlack.umap` (Basic level — NOT Open World, which crashes PIE; see cookbook). Z scale 8.42%, 1024 components. `DropSphere` physics test actor at `(0,0,3000)`.
- **M0.5**: splatmap-driven layer painting. `M_GolfsimCourse` with 4 chained `LandscapeLayerWeight` nodes (fairway/green/bunker/rough), 4 `LandscapeLayerInfoObject`s (created via Paint UI — Python factory unbound). Pipeline writes per-channel `splat_*.png` (generated Mac-side).
- **M0.7**: cart paths + tees added (grew to 7 layers). Cart-path flood-fill bug fixed Mac-side (open linestrings were being polygon-filled; rasterizer now dispatches on `geom: line|polygon`). Trees + water deferred at this point.

LayerInfo creation + Manage>Import are manual editor steps (5.7 Python-unbound). Landscape-import path itself is a manual fallback — see cookbook.

## 2026-05-14 — Windows bring-up + MCP (Windows)

VS2022 (MSVC 14.51/VS2026), Git LFS 3.7.1, Python 3.12.10, Node 24.15.0, UE 5.7.4 (`D:\ue57\UE_5.7`), Claude Desktop installed + verified. UE5.7.4 C++ project created at `engine/Golfsim/Golfsim.uproject` (Basic C++ template, no starter content). **NAJEMWEHBE/UnrealClaudeMCP** plugin installed — cloned to `engine/UnrealClaudeMCP-upstream/`, copied into `engine/Golfsim/Plugins/UnrealClaudeMCP/` (both gitignored; **not vendored** — fresh clones clone+copy themselves). 96 tools, TCP server on `127.0.0.1:18888`. Claude Desktop MCP config written (stdlib-only bridge). Smoke test passed (spawn/inspect/property-set/screenshot round-trip). `.gitignore` patched for the nested `engine/<Project>/` layout.

## Earlier (Mac) — repo + pipeline scaffold

Repo scaffolded with pipeline + docs + MIT license + LFS rules. Python data pipeline (`build_heightmap.py`, `build_splatmap.py`) working end-to-end with Overpass mirror fallback + empty-cache guard. Pebble Beach + **Bethpage Black** full pipeline output committed for reference (Bethpage: 22.5–65.5 m elev / 43 m range / 8.42% UE5 Z scale; 38 fairways / 25 greens / 83 bunkers / 66 tees). `pipeline/setup.sh` one-shot venv. `pipeline/tests/test_build_splatmap.py` pytest suite (28 tests, 0.2 s). Remote `origin = github.com/pucho/golfsim`.
