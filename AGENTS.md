# AGENTS.md — context for Claude (and other coding agents)

If you are an AI assistant landing in this repo, read this file first. It is the cross-session handoff document — Cowork sessions don't sync between machines, so this file is how the Mac-side and Windows-side Claude sessions stay coherent. Read on start, update before stop, commit before exit.

---

## Project in one paragraph

`golfsim` is an open-source, cross-platform golf simulator. The competitive thesis is three-fold: (1) AI-assisted course pipeline that ingests open LIDAR + OSM data so the community can produce courses 10x faster than GSPro's CourseForge; (2) walking/treadmill integration via BLE FTMS that turns sim-golf into a Zwift-like fitness product; (3) genuinely cross-platform — Windows / Mac / Linux desktop with iPad as a future mobile tier, all from a single UE5 codebase.

## Architecture invariants — do not break these

1. **Monolithic per-platform binary.** Each platform ships as one executable containing the sim, the renderer, and the platform-appropriate hardware drivers. No services, no IPC for end users. Drivers go in via CoreBluetooth (Apple) / Windows.Devices.Bluetooth (Windows) / BlueZ (Linux). Multi-machine setups are developer-only.
2. **In-process normalized event bus.** All hardware drivers (walking sensor, launch monitor, HR strap, manual UI) publish typed events of the same shape to an in-process pub/sub. The sim subscribes. Multiplayer is the same envelope serialized over the network. The shape is the contract — see `docs/event-protocol.md`.
3. **Course pipeline is decoupled from the engine.** The Python pipeline in `pipeline/` runs anywhere; it produces UE5-import-ready PNGs in `courses/<id>/`. The engine ingests those files. Pipeline and engine never share runtime state.
4. **MIT licensed.** Keep contribution surface maximal. New code in the repo MUST be MIT-compatible.

## Where work happens (which machine does what)

| Machine | Owns |
|---|---|
| Mac mini M5 | Python pipeline development. All docs. Future BLE driver prototypes via `bleak`. Future Mac/iOS UE5 builds (Apple toolchain requires Mac). |
| Windows PC | UE5 development for Windows/Linux build targets. MCP-driven editor automation. C++ engine work. |
| Both | The git repo. Anything committed is visible to the other machine on next `git pull`. |

## Key documents (read these next if needed)

- `README.md` — repo orientation, getting started
- `docs/plan.md` — full project plan, MVP ladder, recent decisions
- `docs/event-protocol.md` — event envelope spec (the contract every driver implements)
- `docs/windows-setup.md` — Windows-side prerequisites + UE5 project creation + MCP wiring
- `pipeline/README.md` — pipeline-specific quickstart and backends

## Conventions

- **Git:** main branch only for now (single contributor). Conventional-style commit messages welcome but not enforced. Use `git lfs` — `.gitattributes` declares LFS rules for `*.uasset`, `*.umap`, `*.png` in `engine/` and `courses/`.
- **Python:** the pipeline uses stdlib + `requirements.txt`. Run `./setup.sh` to create the venv (auto-detects `uv`, falls back to `python3 -m venv`).
- **UE5:** project files live in `engine/`. Keep one `.uproject` there.
- **Pipeline outputs:** committed PNGs in `courses/<id>/` are reference assets, not artifacts. Each course folder should contain `heightmap.png`, `heightmap.json`, `splatmap.png`, `splatmap.json`, and optional `layer_*.png`. `osm_raw.json` and `dem.tif` are gitignored intermediates.
- **No emojis** in files unless explicitly requested.
- **No new markdown files** unless they add structural value. Update existing docs rather than creating siblings.

---

## Current state — update each session

> Each session should update the three sections below before exit. Older entries can be pruned once they stop being relevant.

### What's done

- Repo scaffolded with pipeline + docs + license + LFS rules.
- Python data pipeline (`build_heightmap.py`, `build_splatmap.py`) working end-to-end with mirror fallback for Overpass and an empty-cache guard.
- Pebble Beach test course: full pipeline output, committed for reference.
- **Bethpage Black: complete.** Heightmap (22.5–65.5m, 43m range, 8.42% UE5 Z scale) and splatmap (38 fairways, 25 greens, 83 bunkers, 66 tees across the five-course state park) both fetched and committed. **Ready for Milestone 0 import.**
- `pipeline/setup.sh` for one-shot venv setup.
- `docs/windows-setup.md` checklist for the Windows machine.
- Remote set: `origin = git@github.com:pucho/golfsim.git` (now `https://github.com/pucho/golfsim.git` on the Windows clone).
- **Windows bring-up complete (2026-05-14).** VS2022 (MSVC 14.51 / VS2026), Git LFS 3.7.1, Python 3.12.10, Node 24.15.0, UE 5.7.4 (at `D:\ue57\UE_5.7`), Claude Desktop all installed and verified.
- **`.gitignore` patched for nested UE project layout.** UE5 creates the project under `engine/<ProjectName>/`, so build-artifact rules use `engine/**/*` patterns. Also excludes `engine/UnrealClaudeMCP-upstream/` and `engine/**/Plugins/UnrealClaudeMCP/` so we don't vendor upstream MIT code.
- **UE5.7.4 C++ project created at `engine/Golfsim/Golfsim.uproject`.** Blank C++ template, no starter content, Maximum Quality desktop target. First compile + `TestActor` sanity-check class both succeeded. ModelingToolsEditorMode plugin enabled by default.
- **NAJEMWEHBE/UnrealClaudeMCP plugin installed.** Upstream cloned to `engine/UnrealClaudeMCP-upstream/` (v0.9.1, 2026-05-08), plugin folder copied to `engine/Golfsim/Plugins/UnrealClaudeMCP/`. Builds clean. Registers **96 tools total** (71 native C++ handlers including `inspect_landscape`, `import_texture`, `execute_unreal_python`, `spawn_actor`; 25 synthetic bridge-side tools for bulk ops). TCP server listening on `127.0.0.1:18888`.
- **Claude Desktop MCP config written** to `%APPDATA%\Claude\claude_desktop_config.json` pointing at `engine/UnrealClaudeMCP-upstream/bridge/unreal_claude_mcp_bridge.py` via the Windows `py` launcher. Bridge has no extra Python deps (stdlib only).
- **MCP smoke test passed (2026-05-14).** From Claude Code: spawned `SmokeCube` (StaticMeshActor with `/Engine/BasicShapes/Cube.Cube`) at `(0,0,200)`, confirmed via `get_actors_in_level`, captured viewport screenshot. Bridge round-trip works for spawn/inspect/property-set/screenshot.
- **Milestone 0 complete (2026-05-14, on second attempt).** Bethpage Black heightmap imported as a Landscape in `/Game/Maps/BethPageBlack.umap` — a non-WP **Basic** level, 21.5MB inline .umap, no `__ExternalActors__` tree. Z scale 8.4167% (43.09m elev range; loaded_bounds Z = ±2154 cm = ±21.5m, matches expected math), components=63x63 quads x 1 subsection x 32x32 grid = 1024 components total. Material `M_GolfsimGreen` (`/Game/Materials/M_GolfsimGreen`, simple lit Constant3Vector → BaseColor) authored via Python and assigned at import. `DropSphere` (StaticMeshActor with engine sphere mesh, Movable mobility, simulate-physics on, BlockAll collision profile) sits at `(0, 0, 3000)` ready for the user to hit Simulate. The first attempt saved as a derivative of the Open World Template — see "Known pitfalls" below for why that crashes PIE.
- **Milestone 0.5 complete (2026-05-14).** Splatmap-driven layer painting on the Bethpage Black landscape. Pipeline (`build_splatmap.py`) extended to write per-channel `splat_fairway/green/bunker/rough.png` alongside the RGBA `splatmap.png`. Mac-side Claude generated the 4 singles and committed (Windows lacks the pipeline venv per [[feedback-pipeline-runs-on-mac]]). New material `/Game/Materials/M_GolfsimCourse` authored via Python with 4 chained `MaterialExpressionLandscapeLayerWeight` nodes (Fairway → Green → Bunker → Rough → BaseColor) feeding flat `Constant3Vector` colors per layer. 4 `LandscapeLayerInfoObject` assets created at `/Game/Landscape/Layers/LII_<Name>.uasset` via the Paint UI (Python factory unbound — manual creation). User imported the 4 single-channel PNGs through Manage > Import (Layers array, heightmap field unchecked). Visual confirmation: distinct rough / green / bunker regions clearly visible on the landscape; `M_GolfsimGreen` retained as fallback baseline.
- **Milestone 0.7 partial complete (2026-05-14): cart paths + tees added; trees + water deferred.** `M_GolfsimCourse` extended in place via Python with 2 more chained `LandscapeLayerWeight` nodes (CartPath = light grey, Tee = bright manicured green). 6 layers now: Fairway → Green → Bunker → Rough → CartPath → Tee → BaseColor (12 expressions total). 2 new LayerInfos `LII_CartPath` + `LII_Tee` created via Paint UI (set to Legacy Weight Blending). User imported `layer_cart_path.png` + `layer_tee.png` via Manage > Import. Cart paths render thin / sparse — OSM gives them as line geometries, rasterized at ~70 cm/pixel they're 1 pixel wide and read poorly. Trees (`layer_trees.png`) and water (`layer_water.png`) deliberately deferred — they want 3D foliage instances and a water-body actor (UE Water plugin) respectively, not flat color layers.

### What's next (priority order)

1. **Pipeline (Mac-side, owned per [[feedback-pipeline-runs-on-mac]]): investigate + fix `layer_cart_path.png` for Bethpage Black.** When painted onto the landscape in M0.7, cart paths rendered both sparse AND visually wrong — the user reported "huge white areas that don't resemble a cart path." Two distinct problems may be in play:
   - **Symptom A — width too small**: OSM `highway=path` (and `golf=cartpath`) features come as polylines. Rasterized at ~70 cm/pixel they're 1 pixel wide and read as faint dashes. Fix: buffer the geometry by ~2-3 m before drawing in `pipeline/build_splatmap.py`'s `rasterize_layer` call for the cart_path extra.
   - **Symptom B — wrong shapes entirely**: The "huge white areas" suggest something else is being captured into the cart_path channel — possibly the `highway=path` Overpass query (line 95 of `build_splatmap.py`) is matching pedestrian footpaths around the park, or polygon features tagged with `highway=path` are being flood-filled. **Action for Mac-side agent**: open `courses/bethpage-black/layer_cart_path.png` in any viewer, compare against `courses/bethpage-black/osm_raw.json` (if cached) to see which OSM elements ended up in this channel. Also worth checking: does Bethpage's OSM data even tag cart paths as `golf=cartpath`, or is the data thin enough that we're falling back to `highway=path` and pulling in non-cart features?
   - **Alternative architecture**: cart paths might be better as **spline-mesh geometry** (UE5 `LandscapeSplineActor` with a path mesh) than as a weight-paint layer. Splines preserve sharp edges + width regardless of landscape resolution, and UE has a Landscape Splines system that paints + raises the terrain along the spline. Pipeline output would change from a raster mask to a polyline list (GeoJSON or similar), and the UE-side import would use `LandscapeSplineActor.add_segment` (if Python-bound) or a manual spline draw. Decide between (a) fix rasterization with dilation, or (b) emit `cart_paths.geojson` and switch to splines, based on what the Mac agent finds in the symptom-B investigation.
2. **Milestone 0.8: trees as foliage scatter.** `layer_trees.png` mask drives placement of UE Foliage instances. Pick a placeholder tree static mesh (Engine starter content has Quixel basics, or grab a free Megascans tree). Use UE's Foliage tool's "Paint Foliage Where Texture Mask Is Above Threshold" or PCG with a sample-texture node. Density target ~1 tree per 4-9 m² in painted regions.
3. **Milestone 0.9: water as body actors.** `layer_water.png` mask outlines water hazards. UE Water plugin (`UWaterBodyLake`) needs spline-defined edges, so we'd vectorize the mask (cv2.findContours equivalent) into closed polylines, then either spawn one lake per polygon or use a flat `WaterBodyOcean`-style large plane clipped by the mask. First pass: simple translucent-blue plane at z=0 covering all water regions, no Water plugin yet.
4. **Material polish.** M_GolfsimCourse uses flat colors (M0.5/0.7 milestones were layer plumbing). Upgrade to tiled textures + normal maps per layer for course realism. Keep the layer-blend topology, swap the `Constant3Vector` inputs for `TextureSample` chains.
5. **Ball flight aerodynamics solver (C++).** Drag + gravity + Magnus lift. Tune against Trackman reference numbers. Probably a week of careful work.
6. **ESP32 walking-pad driver (parallel hardware track).** Order LilyGo TTGO T-Display + TCRT5000 sensors. Optical-sensor mode with belt marks via 3D-printed jig.

### Recent decisions worth remembering

- **MCP choice (revised 2026-05-14):** started with **NAJEMWEHBE/UnrealClaudeMCP** (free, MIT, 96 tools, v0.9.1) instead of paid flopperam. Reasoning: keeps OSS contribution surface clean, has `inspect_landscape` + `import_texture` + `execute_unreal_python` which covers Milestone 0 and most of 0.5. If we hit a wall (most likely on PCG foliage or complex splatmap painting), escalate to flopperam $15/mo trial or StraySpark $89.99 lifetime. StraySpark wasn't on the radar when plan was first written but is the heaviest hitter — 359 tools, lifetime license, source-control aware, closed-loop PIE testing — worth the spend if NAJEMWEHBE starts feeling thin.
- **M0 landscape import path (2026-05-14):** primary all-Python path via `unreal.LandscapeEditorObject.import_landscape_data()` is dead in UE5.7 — `LandscapeEditorObject`, `LandscapeProxy.Import`, `LandscapeSubsystem`, and `WorldPartitionEditorSubsystem` are all unbound in the Python API. Used the manual fallback: human clicks Landscape mode → Manage tab → New Landscape → Import from File. Python automation handles everything else (material creation via `MaterialEditingLibrary`, WP cell loading via `WorldPartitionBlueprintLibrary.load_actors`, sphere spawn + physics setup, screenshots). For repeatable course imports later we'll either (a) write a stdlib-only PNG decoder + the Texture2D → RenderTarget → `landscape_import_heightmap_from_render_target` chain, or (b) escalate to a paid MCP that wraps the editor's import flow.
- **Third-party MCP plugin is NOT vendored.** Upstream cloned per-machine to `engine/UnrealClaudeMCP-upstream/` (gitignored). The plugin folder is copied (not symlinked, for Windows compatibility) into `engine/Golfsim/Plugins/UnrealClaudeMCP/` (also gitignored). New contributors clone + copy themselves.
- **Claude Code is the surface for engine work; Cowork stays for repo/docs work.** `.mcp.json.example` at repo root is the template; each machine copies it to `.mcp.json` (gitignored). Cowork mode deliberately doesn't load arbitrary MCPs from `claude_desktop_config.json` — its plugin/MCP set is curated. So driving UE5 from Cowork isn't possible; switch to Claude Code (CLI) for that.
- **First course:** Bethpage Black. 1.4km × 1.4km bbox `-73.4540,40.7423,-73.4374,40.7549`. Will incidentally pick up adjacent Bethpage State Park courses; fine.
- **Walking pad sensor approach:** non-invasive optical (TCRT5000) reading painted marks on the belt. 3D-printed bracket and ruler-jig for mark placement. Single sensor; quadrature direction-sensing not needed.
- **Compressed walking mode** (~3:1 game-to-pad meters) is the default; strict 1:1 and cart modes are opt-in.

### Known pitfalls / pending issues

- **Mac Cowork sandbox can't `unlink` files in `.git/`.** Symptom: `.git/index.lock` accumulates. Fix from host Mac terminal: `rm -f .git/index.lock`.
- **Mac Cowork sandbox is Linux, not macOS.** Don't create `.venv` from the Cowork bash — it'll have Linux-ELF binaries that the host Mac can't execute. Use `./setup.sh` from the host Mac terminal.
- **Windows Cowork sandbox can't `git clone` into the workspace mount.** Same family as the Mac note: writing into `.git/config` of a new clone fails with `Operation not permitted`. Do clones from PowerShell on the Windows side.
- **Sandbox `git status` shows LFS-tracked PNGs as "modified" on every fresh session.** The Linux Cowork sandbox doesn't ship `git-lfs`, so it can't run the smudge/clean filter and sees pointer-vs-binary diffs. Ignore in the sandbox; on Windows with LFS installed, `git status` is clean.
- **`.gitattributes` has a latent ordering bug.** `*  text=auto eol=lf` catch-all comes AFTER the `*.png ... -text` LFS lines, and git applies the LAST matching attribute, so `text=auto` wins on PNGs. Not breaking anything (LFS still works, content survives), but worth fixing: move the catch-all to the top of the file, or scope it to text extensions.
- **MSVC 14.51 (VS2026) is "not a preferred version" per UE5.7.4.** UE wants 14.44. Soft warning during build; the project compiled fine. If we hit weird linker errors on plugin builds later, this is the first suspect.
- **Cowork mode runs inside Claude Desktop.** Quitting Claude Desktop (e.g., to reload the MCP config) ends the active Cowork session. Always update AGENTS.md and commit before restarting Claude Desktop so the next session picks up cleanly.
- **Overpass mirrors are flaky.** Script now retries across 4 mirrors. If all fail, just try again later — usually a few hours fixes it. (Observed: main `overpass-api.de` was 504ing all evening on May 13; `kumi.systems` recovered by morning.)
- **Argparse + negative longitudes:** use `--bbox-wgs84=-73.45,...` (equals sign), not `--bbox-wgs84 -73.45,...` (space), or argparse swallows the leading minus.
- **TestActor.cpp/h** were created during toolchain sanity check and aren't deleted yet. Harmless test scaffold (~600 bytes); delete via Content Browser → C++ Classes → Golfsim → TestActor → right-click → Delete next session, or leave.
- **`execute_unreal_python` does not return stdout via the bridge.** Tool result always shows `output: None`; actual `unreal.log()` output lands in the editor's Output Log. Use `get_log_lines(category_filter='LogPython', count=N)` to read it. For success/failure feedback, prefer `unreal.log(...)` followed by an explicit `get_log_lines` call rather than relying on the tool's return.
- **WorldPartition cells don't auto-load when the level opens via `save_current_level_as`.** Symptom: `inspect_landscape` reports `loaded_bounds = 0` and 0 streaming proxies even though the landscape exists. Fix: `unreal.WorldPartitionBlueprintLibrary.get_actor_descs()` → filter by `native_class.get_name()` containing 'Landscape' → `WPBL.load_actors([guid, ...])`. Loaded 256 of 320 proxies on first call; the rest load by camera proximity.
- **`MaterialEditingLibrary.connect_material_property` returns False silently** when the named output pin doesn't exist on the source expression. For `MaterialExpressionConstant3Vector`, the correct output name is `""` (empty string), not `"RGB"`. Same library has no `get_material_expressions`, and `Material.expressions` is protected from `get_editor_property`. Workaround: nuke + re-create via `MaterialFactoryNew` if a stale wire prevents reconnection.
- **StaticMeshActor mobility defaults to Static.** `set_simulate_physics(True)` silently does nothing on a Static component — no warning, no error. Always call `smc.set_mobility(unreal.ComponentMobility.MOVABLE)` before enabling physics, then `set_collision_profile_name('BlockAll')` to actually collide with the landscape.
- **Open World Template default landscape leaves WP HLOD orphans.** Deleting the `Landscape` actor via `delete_actor` removes only the parent; 64 `LandscapeStreamingProxy` actors and 64 `WorldPartitionHLOD` actors remain. Bulk-delete via `EditorActorSubsystem.destroy_actor()` in a Python loop. For new courses, **always** start from a Basic empty level instead of Open World Template (next bullet for why).
- **"Save Level As" off the unsaved Open World Template creates a level INSTANCE, not a copy. PIE/Simulate then crashes with `Assertion failed: Actor->GetLevel() == DestLevel`.** First M0 attempt saved Untitled_1 (Open World Template) as `/Game/Maps/BethPageBlack`. The new landscape's streaming proxies got created with packages under `/Engine/__ExternalActors__/Maps/Templates/OpenWorld/...` (the template's path) instead of under our map's path. When PIE tries to instantiate the level, the level paths don't match → fatal assertion. **Fix**: import courses into a brand-new **File → New Level → Basic** (not Open World, not Empty Open World — both have WP). Basic levels in this project don't enable World Partition; the entire landscape lives inline in the .umap, no external actors at all.
- **`unreal.AutomationLibrary.take_high_res_screenshot` and `HighResShot` console command silently no-op when UE editor is backgrounded.** Symptom: dispatch returns success, but no PNG ever lands on disk. UE pauses viewport rendering when the editor window doesn't have focus (resource-saving). For programmatic screenshots that work without focus, use the bridge's `get_viewport_screenshot` (synchronous, returns inline base64; decode the saved tool-result via `ConvertFrom-Json` + `[Convert]::FromBase64String`). It returns the last-rendered frame, which may itself be stale if the viewport hasn't redrawn since the last user interaction — moving the camera via `set_camera_transform` doesn't always trigger a re-render either.
- **MCP edits are in editor-memory only until something saves.** `spawn_actor`, `set_actor_property`, `delete_actor`, and any `execute_unreal_python` mutation dirty UObjects but do not write `.umap` / `.uasset` files. Symptom: next UE session opens to a "wait, where did my sphere/lighting/material go?" state. After any meaningful MCP edit chain, call `save_dirty_assets` (the bridge tool wraps UE's "Save All"). Cheap and idempotent; just always call it at the end of an MCP-driven sequence before the user closes UE or before you check `git status` expecting to see new files.
- **`LandscapeLayerInfoObject` is half-bound in 5.7 Python: createable but barely configurable.** `LandscapeLayerInfoObjectFactory` is unbound (no `AssetTools.create_asset` path). `unreal.new_object` works but creates the object in `/Engine/Transient` — useless without a real package. Even when you wire up a real package, only `hardness` and `phys_material` are settable; `layer_name` is read-only and `bNoWeightBlend` (the weight-blend toggle) is not exposed at all. Path: have the user create the 4 LayerInfos via Landscape > Paint mode's `+` button (UE auto-sets `LayerName` to match the material's `LandscapeLayerWeight` parameter), then double-click each LII asset and switch from "Non Weight Blended" to **Legacy Weight Blending** in the Details panel. Python is fine for the material side; the LayerInfo side is a manual handoff.
- **UE5.7 Landscape Paint UI doesn't auto-populate Target Layers from a freshly-assigned material.** Symptom: you `set_actor_property` `LandscapeMaterial` to a new layer-blend material, switch to Paint tab, see "There are currently no target layers assigned to this landscape." `Landscape.force_layers_full_update()` and `get_target_layer_names()` don't help (former is for Edit Layers, latter returns []). Fix: small "Add From Material(s)" button in the Target Layers section toolbar of the Paint panel — one click and the layers appear. Not Python-driveable that we found.
- **UE5.7 Landscape > Manage > Import dialog's "Layers" array is the right place to bulk-import per-layer weight PNGs.** Right-click on a layer in the Paint panel doesn't have an Import option in 5.7 (UI moved). In Manage tab > Import: uncheck "Heightmap File" (so existing elevation isn't overwritten), expand the "Layers" array, set the path on each layer row to the matching `splat_<name>.png`, click Import. Single round-trip for all layers.

---

## Session-end checklist for the agent

Before you stop a Cowork session, do these in order:

1. **Update** the "Current state" section above (especially "What's done" and "What's next").
2. **Update** `docs/plan.md` if any architectural decision changed.
3. **Commit** with a useful message describing what changed this session.
4. **Push** to `origin/main`.
5. **Mention to the user** what state you're leaving the repo in so they know what the other machine will see when it pulls.

The next Claude session — possibly on the other machine — will start by reading this file and pick up exactly where you left off.
