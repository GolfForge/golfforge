# Settings + Credits menu — design

**Date:** 2026-05-29 · **Tickets:** GOL-52 (graphics settings), GOL-59 (in-app credits/attributions). DLSS/FSR upscaler = GOL-53 (out of scope; the Display section leaves a slot for it).

## Context

GolfForge has no menu/settings surface today — the app boots straight into the Practice Range and
all UI is the range HUD (`AGolfRangeHUD`) plus corner widgets (`UGolfRangePanel`, `UManualShotDialog`).
Two needs converge on one new surface: (1) user-facing graphics settings, persisted (the user asked
for resolution control; today there is none and no `UGameUserSettings` usage), and (2) an in-app
credits/attributions screen, which is a **compliance requirement** — ODbL wants the OpenStreetMap
attribution shown to users of the rendered courses, and the Unreal Engine EULA requires an Unreal
credit in shipped products. Building both on one menu is efficient and avoids a second surface.

## Goals / non-goals

**Goals**
- A centered modal menu, Esc-toggled, hosting **Display** settings and **Credits**.
- Display: resolution, window mode, quality preset, screen-percentage — applied and **persisted** across runs via `UGameUserSettings`.
- Credits: the required attributions (OSM/ODbL, Unreal, USGS/SRTM/OpenTopography, GolfForge license), consistent with `ATTRIBUTION.md`.

**Non-goals (now)**
- DLSS/FSR/XeSS upscalers (GOL-53) — the Display section is laid out to accept an upscaler dropdown later.
- A pre-game main menu / a global menu owner subsystem — see the reuse seam below.
- Audio/input/gameplay settings.

## Surface & invocation

- `USettingsMenu` opens **centered**, with a dimmed/translucent backdrop over the range.
- **Esc** toggles it (bound in `AGolfRangeHUD::EnsureInputBound`, mirroring the M-dialog).
- While open it **swallows gameplay keys** (Space / 1-6 / arrows) so edits don't fire shots; the mouse cursor is already active in the range (the existing panel has clickable dropdowns), so no input-mode change is required beyond gating gameplay keys.
- Closing (Esc or **Close**) returns focus to the game viewport (reuse the existing `ReturnFocusToGameViewport()` idiom).

## Components

Each is a focused pure-C++ `UUserWidget`, tree built in `NativeOnInitialized()` (no WBP), a **dumb view** exposing setters + `TFunction` callbacks — logic lives in the owner. This mirrors `UGolfRangePanel` / `UManualShotDialog` exactly.

- **`USettingsMenu`** — the overlay shell + a 2-entry nav (**Display** | **Credits**) that swaps the content area. Owns the two section sub-trees (built by `BuildDisplaySection()` / `BuildCreditsSection()` helpers). If credits grows, `UCreditsView` can be split out as its own reusable widget — not now (YAGNI).
  - **Display section** widgets: resolution `UComboBoxString`, window-mode `UComboBoxString` (Windowed / Borderless / Fullscreen), quality `UComboBoxString` (Low/Med/High/Epic), screen-% `USlider` (+ a value label), **Apply** `UButton`, **Close** `UButton`.
    - Callbacks out: `OnApplyDisplay(FGolfDisplaySettings)` and `OnClose` (TFunctions set by the HUD). Setters in: `SetResolutionOptions(TArray<FString>)`, `SetCurrent(FGolfDisplaySettings)` to seed controls from current values on open.
  - **Credits section**: a `UScrollBox` + `UTextBlock`(s) of static attribution text; `SetCreditsText(FString)`.
- **`FGolfDisplaySettings`** — a plain C++ struct (not reflected; passed via TFunction) carrying resolution (`FIntPoint`), window mode (`EWindowMode::Type`), quality level (`int32` 0-3), screen-% (`float`). The view↔owner contract.

## Settings backing & data flow

Stock engine **`UGameUserSettings`** (`GEngine->GetGameUserSettings()`) — no subclass needed yet:
- Resolution → `SetScreenResolution(FIntPoint)`; window mode → `SetFullscreenMode(EWindowMode::Type)`; then `ApplyResolutionSettings(false)`.
- Quality preset → `SetOverallScalabilityLevel(0..3)`.
- Screen-% → resolution-scale / `r.ScreenPercentage` via the resolution-quality scalability value.
- Persist → `ApplySettings(false)` + `SaveSettings()` (writes `GameUserSettings.ini`, reloaded on next launch).
- Supported resolutions for the dropdown → `UKismetSystemLibrary::GetSupportedFullscreenResolutions` (fall back to a sane fixed list if empty).

Flow: HUD opens menu → seeds controls from current `UGameUserSettings` (`SetCurrent`) → user edits → **Apply** fires `OnApplyDisplay` → HUD writes the values into `UGameUserSettings` + applies + saves. Credits is static text (no state).

Add a `UGolfUserSettings : UGameUserSettings` subclass **only** when we need a key the stock class lacks (e.g. the GOL-53 upscaler mode) — flagged, not built.

## Ownership & reuse seam

`AGolfRangeHUD` creates the menu in `BeginPlay` (Transient `TObjectPtr<USettingsMenu>`), toggles it on Esc, and implements the apply/close callbacks — same pattern as `ManualDialog`. The `USettingsMenu` widget itself is **range-agnostic** (knows nothing about golf/range), so a future course or pre-game main menu reuses it via a more global owner (PlayerController / GameInstance subsystem). We do **not** build that global owner now.

## Console commands

Following `GolfsimConsole.cpp`: `golfsim.SetResolution <W>x<H>`, `golfsim.SetQuality <0-3>`, `golfsim.Credits` (open the menu to the Credits section). Thin wrappers over the same `UGameUserSettings` apply path / menu toggle.

## Credits content (kept in sync with `ATTRIBUTION.md`)

- "© OpenStreetMap contributors — course data under ODbL" (+ the openstreetmap.org/copyright reference)
- "Made with Unreal® Engine — © Epic Games, Inc."
- "Elevation: USGS 3DEP / SRTM (public domain), via OpenTopography"
- "GolfForge — AGPL-3.0 + commercial (see LICENSE / COMMERCIAL.md)"

## Files

- **New:** `engine/Golfsim/Source/Golfsim/SettingsMenu.{h,cpp}` (`USettingsMenu` + `FGolfDisplaySettings`).
- **Modify:** `GolfRangeHUD.{h,cpp}` — own/create/toggle the menu, Esc bind in `EnsureInputBound`, gate gameplay keys while open, implement apply/close callbacks + the `UGameUserSettings` writes.
- **Modify:** `GolfsimConsole.cpp` — the three console commands.
- No new `Build.cs` deps (Engine/UMG/Slate already linked; `UGameUserSettings` is in Engine).

## Testing & verification

- UI follows the existing convention (Panel/ManualDialog have **no** automation tests) → **PIE-verified by the user**; I'll provide a checklist (open/close, each control changes the viewport, settings **persist across a restart**, gameplay keys gated while open, Credits readable).
- Keep any pure helper (e.g. parsing `WxH` for the console command, building the resolution list) trivially unit-testable if it's free; not a gate.
- The existing engine automation suite (**24/24**) must stay green after the HUD/console changes.

## Out of scope / follow-ups

- GOL-53: DLSS/FSR/XeSS upscaler dropdown (slots into the Display section; may motivate `UGolfUserSettings`).
- Global menu owner + pre-game main menu (when courses land).
