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

- **Latest:** **GOL-150 — UI gradient material + apply (bento bg ambiance + rounded hover-wash); menu version/title** (2026-06-02, Windows). Third child of the GOL-137 UI-elevation epic. Slate has no native gradient brush, so `engine/scripts/build_ui_gradient_materials.py` builds two `MD_UI` translucent materials (`M_UIGradient{Linear,Radial}`, `/Game/UI/Materials/`) consumed as `UImage` brushes via new `GolfUITheme::MakeLinearGradient`/`MakeRadialGradient` (per-widget MID + flat fallback). Applied: subtle bento bg ambiance (two soft radials) + the tile hover-wash upgraded to a bottom-up gradient with a **per-tile-sized rounded-box SDF mask** (a `MaterialExpressionCustom` node) so it respects the card corners — the crisp card itself stays `FSlateRoundedBoxBrush`. Menu brand subtitle now reads `ProjectVersion` (`DefaultGame.ini` = `0.0.4-alpha`) instead of a hardcoded mock; `ProjectDisplayedTitle=GolfForge` for the packaged app title. Stopgaps for the `→`/`↵` glyphs missing from Barlow/JetBrains (real fix = GOL-151 Lucide icon font). Builds clean on UE 5.7; commit `7edacce`. **GOL-137: 3/12 children done (138 theme+fonts, 139 bento menu, 150 gradients) — history in `docs/worklog.md`.** Driven by the Claude Design handoff in `Build/handoff/` (spec + 8 renders + working web prototype + `RESOLVED_TOKENS.md`). New `Source/Golfsim/UI/GolfUITheme.{h,cpp}` — shared design tokens (palette sRGB-decoded to render-correct linear, radii 18/12/8, shadows, surface/border ramps), `FSlateFontInfo` getters, rounded glass/card brushes, reusable atoms (eyebrow/title/mono-number/kbd/status-dot/accent+ghost buttons with Normal/Hover/Pressed/Disabled styles) + `SetHoverLift`. Barlow Condensed / Barlow / JetBrains Mono imported as **embedded** Runtime `Font` assets under `Content/UI/Fonts/` (OFL-1.1, cross-machine/cook-safe; composite weights authored via `CompositeFont.import_text`). Main menu rebuilt as the GolfForge bento (`MainMenu.{h,cpp}` + new reusable `UI/MenuTile.{h,cpp}`): proportional `UCanvasPanel` anchors (responsive 1080/1440/2160 + DPI), keyboard 1-4/Enter/Esc nav, Practice disabled seam, Settings-over-menu, live clock + real player name. **Key UMG gotcha banked (cookbook): a `UButton`/content-sizing wrapper shrinks a card to its content even in a fill slot — tiles must root in a layout panel (`UOverlay`) with self-handled mouse.** Builds clean on UE 5.7; commits `b475993` (GOL-138) + `04e17fc` (GOL-139). **2/12 GOL-137 children done.**
- **Active focus:** (1) **GOL-137 UI elevation** (High) — next children: **GOL-140** (settings redesign + reusable segmented/slider/toggle controls) or **GOL-151** (Lucide icon font), then round-setup wizard (GOL-141/142/143) + in-round HUD (GOL-144/145). Also queued: **GOL-152** (project/exe rename Golfsim→GolfForge — cross-machine). (2) **v0.0.3-alpha release** — Mac cook (`machine/mac`) then tag + GitHub Release. (3) **GOL-49** — `engine/scripts/package.sh` bundling BuildCookRun + the post-cook `courses/` copy.
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
