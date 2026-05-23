# Backlog — what's next (priority order)

> The prioritized work queue. When an item ships, summarize it into `docs/worklog.md` and drop it from here. Engine gotchas/recipes live in `docs/ue5-cookbook.md`.

## 1. Practice Range polish (active)

The flat walkable+shootable range is the default map (minimal level DONE 2026-05-23, see worklog). This pass turns it into a proper launch-monitor range. **Decisions (2026-05-23):** UMG panel for the UI; essentials metrics in yd/mph; arrows yaw camera+aim with no walking/mouse-look; environment selectors after the core. Build order R1 → R2 → R3 → R4 — **R1+R2 done, R3 next.**

**R1 — Tee-fixed aim + arrow-key aiming** — ✓ **DONE 2026-05-23** (see worklog; recipe in cookbook). Player planted on the tee; Left/Right arrows yaw view+aim; WASD/mouse-look off; shot follows the aim. Plan: `docs/superpowers/plans/2026-05-23-range-r1-tee-aim.md`.

**R2 — UMG range panel: metrics grid + club dropdown** — ✓ **DONE 2026-05-23** (see worklog; UMG-in-C++ recipe in cookbook). Pure-C++ `UGolfRangePanel : UUserWidget` (no WBP), top-right grid (Club · Ball Speed mph · Launch deg · Spin rpm · Carry yd · Offline yd) refreshed per shot, `UComboBoxString` club dropdown synced with the 1-6 keys; `AGolfRangeHUD` owns firing/bag and drives the widget. Range now shows the mouse cursor + `FInputModeGameAndUI` so the dropdown is clickable; Space still fires (focus returned to the viewport after a pick).

**R3 — Trees on the sides (+ optional 3D grass)** *(range map; PCG path already built).* Retarget `scatter_full_landscape.py`'s `LEVEL_HINT` + volume bounds to the ~504 m range (≈ ±25200 cm) and run its generate→report 2-call. The painted perimeter Trees band already gates placement (`build_pcg_treescatter.py` filters Trees>0.3) — no graph change. Optionally fold in `build_landscape_grass.py` (LGT_FairwayGrass already bound on `M_PracticeRange`) + USER "Build Grass Maps" near the fairway for 3D grass in the same pass. *Done when:* a perimeter tree line frames the range and FPS holds.

**R4 — Environment selectors: time-of-day + weather** *(after core).* Add a VolumetricCloud to the range level. A small runtime env controller applies presets: **Time** (Dawn / Morning / Noon / Dusk / Night → DirectionalLight pitch/yaw/temperature/intensity + fog + SkyLight recapture) and **Sky** (Clear / Cloudy / Overcast → VolumetricCloud coverage/density + sun/ambient). Two dropdowns added to the R2 panel drive it. Reuses the knob philosophy of `build_range_lighting.py`, presets centralized in one place. *Done when:* switching Time/Sky in the panel changes lighting + clouds live in PIE.

**Later / optional** *(kept, lower priority):* yardage markers (50–300 yd `ATextRenderActor`s along +X from the tee, ×91.44 cm/yd) · `golfsim.SetPin <yards>` + `AGolfPinActor` target green/flag (C++ editor-closed; add `SetPinCmd` to `GolfsimConsole.cpp` mirroring the `FAutoConsoleCommandWithWorldAndArgs` + `GetOrSpawnBall` pattern) · USER-supplied far-end Nanite backdrop. Real LM driver stays the v0.3 milestone; shots fired manually with `golfsim.FireShot` / Space for now.

## 2. Ball-flight follow-ups (sim correctness)

**Ball flight DONE (2026-05-22, see worklog).** Next pieces, in order: **(a) Ground interaction + roll (Chaos)** — turn the solver's carry/landing (position, speed, descent angle, lie) into `total_m` + `final_lie` via bounce/roll on the landing surface; the other half of `session.shot_outcome`. (Also unlocks a Total-distance metric for the range grid.) **(b) In-process EventBus** (`docs/event-protocol.md`) — the day-one architecture invariant still isn't built: stand up the `EventBus` subsystem, a `manual-shot-dialog` driver that publishes `shot.taken`, and a subscriber that runs `GolfBallFlight::Simulate` and emits `session.shot_outcome`. `FShotInput` already mirrors `shot.taken`, so this is mostly wiring + the pub/sub. **(c) Blueprint / live-tuning layer (additive, anytime):** expose the solver for real-time tuning without recompiling — promote `FShotInput` / `FAeroCoefficients` / `FBallTrajectory` to `USTRUCT(BlueprintType)`, add a `UGolfBallFlightLibrary : UBlueprintFunctionLibrary` with `BlueprintCallable` `Simulate` / `TraceFromResolved`, and an `EditAnywhere FAeroCoefficients` on `AGolfBallActor` (or a `UDataAsset`) so Cd/Cl knobs are tweakable in Details. Optional UMG slider panel to dial the model against more reference data. Keep the pure-C++ integrator untouched — thin wrapper/reflection layer only.

## 3. M0.8.5 tree-scatter polish (BethPage, optional)

Scale-up is validated (29,070 Silver Birch @ 100 FPS / 4.3 GB on BethPage); punted at the stop-at-gate decision, pick up anytime: (a) tune `DENSITY_PPSM` / `TREES_THRESHOLD` in `build_pcg_treescatter.py` for a denser forest (0.02 was the gate value, likely sparse); (b) add **Baltic Pine** (`Tree_Baltic_Pine`, on disk — SkeletalMesh+ProceduralVegetationPreset format) as a 2nd species; (c) save `BethPageBlack.umap` with a non-transient full volume so the forest persists. Re-measure perf after density changes (watch `stat streaming`; `r.Streaming.PoolSize` is a fixed 1000 MB default). PCG Python API reference → `docs/ue5-cookbook.md`.

## 4. ESP32 walking-pad driver (parallel hardware track)

Order LilyGo TTGO T-Display + TCRT5000 sensors. Optical-sensor mode with belt marks via 3D-printed jig.
