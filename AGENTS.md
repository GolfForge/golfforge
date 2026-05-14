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

### What's next (priority order)

1. **MCP smoke test.** Fully quit Claude Desktop (system tray → Quit), relaunch, open a new conversation, ask Claude to list MCP tools — should see `execute_unreal_python`, `spawn_actor`, `inspect_landscape`, etc. Then: *"Spawn a cube actor at (0, 0, 200) in my UE editor and label it SmokeCube."* Watch the World Outliner. Cube appearing = bring-up gate passed.
2. **Milestone 0.** Import `courses/bethpage-black/heightmap.png` into UE5 as a Landscape (Z scale 8.42% from `heightmap.json`). Apply a single default material (no splatmap-driven layers yet). Spawn a physics sphere. Watch it roll. That's the milestone. With the MCP wired, this is mostly: ask Claude to use `import_texture` + `execute_unreal_python` to create the Landscape from the PNG, then `spawn_actor` for the sphere.
3. **Milestone 0.5.** Add splatmap-driven material layers using `courses/bethpage-black/splatmap.png` (RGBA = fairway/green/bunker/rough). Verify the course visibly separates into surface types.
4. **Ball flight aerodynamics solver (C++).** Drag + gravity + Magnus lift. Tune against Trackman reference numbers. Probably a week of careful work.
5. **ESP32 walking-pad driver (parallel hardware track).** Order LilyGo TTGO T-Display + TCRT5000 sensors. Optical-sensor mode with belt marks via 3D-printed jig.

### Recent decisions worth remembering

- **MCP choice (revised 2026-05-14):** started with **NAJEMWEHBE/UnrealClaudeMCP** (free, MIT, 96 tools, v0.9.1) instead of paid flopperam. Reasoning: keeps OSS contribution surface clean, has `inspect_landscape` + `import_texture` + `execute_unreal_python` which covers Milestone 0 and most of 0.5. If we hit a wall (most likely on PCG foliage or complex splatmap painting), escalate to flopperam $15/mo trial or StraySpark $89.99 lifetime. StraySpark wasn't on the radar when plan was first written but is the heaviest hitter — 359 tools, lifetime license, source-control aware, closed-loop PIE testing — worth the spend if NAJEMWEHBE starts feeling thin.
- **Third-party MCP plugin is NOT vendored.** Upstream cloned per-machine to `engine/UnrealClaudeMCP-upstream/` (gitignored). The plugin folder is copied (not symlinked, for Windows compatibility) into `engine/Golfsim/Plugins/UnrealClaudeMCP/` (also gitignored). New contributors clone + copy themselves.
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

---

## Session-end checklist for the agent

Before you stop a Cowork session, do these in order:

1. **Update** the "Current state" section above (especially "What's done" and "What's next").
2. **Update** `docs/plan.md` if any architectural decision changed.
3. **Commit** with a useful message describing what changed this session.
4. **Push** to `origin/main`.
5. **Mention to the user** what state you're leaving the repo in so they know what the other machine will see when it pulls.

The next Claude session — possibly on the other machine — will start by reading this file and pick up exactly where you left off.
