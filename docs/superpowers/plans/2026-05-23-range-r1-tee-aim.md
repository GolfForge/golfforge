# Range R1: Tee-Fixed Aim + Arrow-Key Aiming — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the Practice Range, plant the player on the tee box and let the Left/Right arrow keys turn the view + aim together, with WASD walking and mouse-look disabled; shots fire along the aim.

**Architecture:** All changes live in the range-only `AGolfRangeHUD` (`GolfRangeHUD.h/.cpp`) — the HUD set as `BP_FirstPersonGameMode`'s HUDClass, which only the range uses. We do **not** edit `IMC_Default` or the FP character Blueprint (those are shared with `BethPageBlack`, where walking must stay). Movement + mouse-look are neutralized range-only via the PlayerController's `SetIgnoreMoveInput(true)` / `SetIgnoreLookInput(true)` flags. Arrow keys are bound on the HUD's existing legacy `InputComponent` (same mechanism as the 1-6/Space binds); a per-frame `Tick` integrates yaw into the **control rotation** (the FP camera follows it via `bUsePawnControlRotation`). `FireRandom` reads the **control rotation** for the shot heading so view and shot are always consistent.

**Tech Stack:** UE5.7 C++ (gameplay/HUD/input), the existing `GolfBallFlight` solver + `AGolfBallActor` (untouched), MCP bridge + PIE for verification.

**Spec source:** the approved R1 item in `docs/backlog.md` ("Practice Range polish §R1").

---

## Domain note: how this is verified

This is gameplay/input integration, not pure logic, so there are **no new unit tests** — the pure solver already has its automation tests and is untouched here. R1 is verified the way ball-flight was smoke-tested: build with the editor closed, then confirm behavior in PIE via the MCP bridge (`get_log_lines`) plus the user reading their focused viewport (per the cookbook, the viewport screenshot tool and Python FPS reads are unreliable when the editor is backgrounded — the user's focused viewport is the trustworthy visual check).

## Worktree note

Do **not** use a git worktree for this. The UE5 project, the editor session, and the MCP bridge are all tied to this working tree; a worktree would duplicate the engine dir and break the editor/MCP wiring. Work in the main checkout on `main` (repo convention: single contributor, main-only).

## File structure

- **Modify:** `engine/Golfsim/Source/Golfsim/GolfRangeHUD.h` — add a constructor, `Tick`, arrow-key handler shims, turn-state flags, a controls-locked flag.
- **Modify:** `engine/Golfsim/Source/Golfsim/GolfRangeHUD.cpp` — enable tick, lock move/look, bind arrows, integrate yaw in `Tick`, read control rotation in `FireRandom`, update the HUD hint + log.

No other files change. No new UCLASS (but adding members/methods to a UCLASS changes layout, so this needs a full editor-closed build — Live Coding can't be relied on for it).

---

## Task 1: Tee-fixed arrow-key aiming in `AGolfRangeHUD`

**Files:**
- Modify: `engine/Golfsim/Source/Golfsim/GolfRangeHUD.h`
- Modify: `engine/Golfsim/Source/Golfsim/GolfRangeHUD.cpp`

- [ ] **Step 1: Header — add constructor, Tick, arrow handlers, and state**

In `GolfRangeHUD.h`, replace the `public:`/`private:` body so the class reads:

```cpp
UCLASS()
class GOLFSIM_API AGolfRangeHUD : public AHUD
{
	GENERATED_BODY()

public:
	AGolfRangeHUD();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

private:
	void EnsureInputBound();
	void SelectClub(int32 Index);
	void FireRandom();

	// BindKey needs parameterless members; thin shims onto SelectClub(i).
	void SelectClub0() { SelectClub(0); }
	void SelectClub1() { SelectClub(1); }
	void SelectClub2() { SelectClub(2); }
	void SelectClub3() { SelectClub(3); }
	void SelectClub4() { SelectClub(4); }
	void SelectClub5() { SelectClub(5); }

	// Arrow-key aim: press/release toggle a held flag; Tick integrates the yaw.
	void TurnLeftPressed()   { bTurnLeft = true; }
	void TurnLeftReleased()  { bTurnLeft = false; }
	void TurnRightPressed()  { bTurnRight = true; }
	void TurnRightReleased() { bTurnRight = false; }

	int32 ActiveClub = 0;
	FString LastShotText;
	bool bInputBound = false;
	bool bControlsLocked = false;   // move + mouse-look ignored once (range-only)
	bool bTurnLeft = false;
	bool bTurnRight = false;
};
```

- [ ] **Step 2: Cpp — add a constructor that enables ticking**

In `GolfRangeHUD.cpp`, immediately after the `}` that closes the anonymous `namespace { ... }` block (before `void AGolfRangeHUD::BeginPlay()`), add:

```cpp
AGolfRangeHUD::AGolfRangeHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}
```

Also add a tunable turn-rate constant inside the existing anonymous namespace (next to `GBag`):

```cpp
	// Aim turn speed while an arrow is held, deg/sec. Tunable.
	static constexpr float TurnRateDegPerSec = 75.0f;
```

- [ ] **Step 3: Cpp — lock move/look and bind the arrow keys in `EnsureInputBound`**

In `EnsureInputBound`, right after `EnableInput(PC);`, insert the controls lock:

```cpp
	EnableInput(PC);
	if (!bControlsLocked)
	{
		PC->SetIgnoreMoveInput(true);   // planted on the tee
		PC->SetIgnoreLookInput(true);   // arrows aim instead of the mouse
		bControlsLocked = true;
	}
```

Then, just before `bInputBound = true;`, add the arrow binds alongside the existing club/space binds:

```cpp
	InputComponent->BindKey(EKeys::Left,  IE_Pressed,  this, &AGolfRangeHUD::TurnLeftPressed);
	InputComponent->BindKey(EKeys::Left,  IE_Released, this, &AGolfRangeHUD::TurnLeftReleased);
	InputComponent->BindKey(EKeys::Right, IE_Pressed,  this, &AGolfRangeHUD::TurnRightPressed);
	InputComponent->BindKey(EKeys::Right, IE_Released, this, &AGolfRangeHUD::TurnRightReleased);
	bInputBound = true;
```

- [ ] **Step 4: Cpp — implement `Tick` to integrate yaw into the control rotation**

Add this definition (e.g. after `BeginPlay`):

```cpp
void AGolfRangeHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Dir = (bTurnRight ? 1.0f : 0.0f) - (bTurnLeft ? 1.0f : 0.0f);
	if (Dir == 0.0f)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	FRotator CR = PC->GetControlRotation();
	CR.Yaw += TurnRateDegPerSec * DeltaSeconds * Dir;   // camera follows (bUsePawnControlRotation)
	PC->SetControlRotation(CR);
}
```

- [ ] **Step 5: Cpp — `FireRandom` reads the control rotation (view = shot)**

In `FireRandom`, replace the current location/rotation block:

```cpp
	FVector Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	if (APawn* Pawn = PC->GetPawn())
	{
		Loc = Pawn->GetActorLocation();
		Rot = Pawn->GetActorRotation();
		Rot.Pitch = 0.f;
		Rot.Roll = 0.f;
	}
```

with:

```cpp
	FRotator Rot = PC->GetControlRotation();   // aim = where the camera points
	Rot.Pitch = 0.f;
	Rot.Roll = 0.f;
	FVector Loc = FVector::ZeroVector;
	if (APawn* Pawn = PC->GetPawn())
	{
		Loc = Pawn->GetActorLocation();        // launch from the tee
	}
```

Then fold the aim heading into the existing shot log (replace the `UE_LOG` at the end of `FireRandom`):

```cpp
	UE_LOG(LogTemp, Display,
		TEXT("golfsim range: %s aim=%.1fdeg carry=%.1fm apex=%.1fm lateral=%.1fm"),
		C.Name, Rot.Yaw, T.CarryM, T.ApexM, T.LateralOffsetM);
```

- [ ] **Step 6: Cpp — update the on-screen control hint**

In `DrawHUD`, change the `L2` line so the player sees the aim keys:

```cpp
	const FString L2 = TEXT("[1-6] club   [<- ->] aim   [Space] hit");
```

- [ ] **Step 7: Build with the editor CLOSED**

Close the UE editor (this changes the `AGolfRangeHUD` class layout — Live Coding can't be trusted for it). Then run:

```
<UE_ROOT>\Engine\Build\BatchFiles\Build.bat GolfsimEditor Win64 Development -Project=<repo>\engine\Golfsim\Golfsim.uproject -WaitMutex -FromMsBuild
```

Expected: `Build succeeded` and a nonzero `UnrealEditor-Golfsim.dll` rebuilt. If it fails to compile, fix the error before continuing (do not reopen the editor on a broken build).

- [ ] **Step 8: Verify in PIE via MCP + the focused viewport**

Reopen the editor (it loads `PracticeRange.umap`, the default map). Then, through the MCP bridge:

1. Start PIE: `pie_control` (play). Have the user click into the PIE viewport so it has focus.
2. **Movement locked:** user presses W/A/S/D → the player does not move off the tee; moves the mouse → the view does not turn. (User confirms visually.)
3. **Arrow aim:** user holds Left arrow → the view rotates left; Right arrow → right.
4. **Aim = shot:** user presses `3` (5-Iron), aims roughly straight downrange, presses Space. Read the log:
   `get_log_lines(category_filter='LogTemp', count=20)` → expect a line `golfsim range: 5-Iron aim=~0deg carry=.. apex=.. lateral=..` and the yellow tracer goes downrange (+X).
   Then user holds Left ~1s (aim ≈ -45°), Space → log shows `aim=~-45deg` and the tracer flies left of center; hold Right past center (aim ≈ +30°), Space → `aim=~+30deg`, tracer flies right.

Expected: WASD/mouse do nothing; arrows turn the view; each shot's logged `aim` matches the facing and the ball visibly flies that way. This is the pass condition for R1.

- [ ] **Step 9: Commit (user-driven per repo convention)**

Per this repo's workflow, the user runs git. Hand them these two commands:

```
git add engine/Golfsim/Source/Golfsim/GolfRangeHUD.h engine/Golfsim/Source/Golfsim/GolfRangeHUD.cpp
```
```
git commit -m "feat(range): tee-fixed arrow-key aiming" -m "Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

(If the docs from the prior session are still uncommitted, the user may prefer to commit those first; this commit only stages the two HUD files.)

---

## After R1 ships

Update `docs/worklog.md` (prepend a dated R1 entry), tick R1 off `docs/backlog.md`, append any input/HUD gotcha discovered to `docs/ue5-cookbook.md`, and bump `CLAUDE.md`'s Current status. Next item is R2 (UMG metrics/club panel).

---

## Self-review

**Spec coverage (R1 backlog item):**
- "Player stays planted on the tee box" → Step 3 `SetIgnoreMoveInput(true)`. ✓
- "Left/Right arrows yaw the view + aim together" → Steps 1/3/4 (binds + `Tick` control-rotation yaw; camera follows). ✓
- "Space fires along that heading" → Step 5 (`FireRandom` reads control rotation). ✓
- "Disable WASD movement and mouse-look" → Step 3 (`SetIgnoreMoveInput`/`SetIgnoreLookInput`). ✓
- "reuses the HUD's existing legacy `InputComponent` binding" → Step 3 (BindKey, same as 1-6/Space). ✓
- Done-when ("arrows turn where you face, Space fires that way, can't walk off") → Step 8 verification. ✓

**Placeholder scan:** none — every code step shows full code; build/verify give exact commands + expected output.

**Type/name consistency:** `bTurnLeft`/`bTurnRight`/`bControlsLocked` declared in the header (Step 1) and used in Steps 3-4; handler shims `TurnLeftPressed/Released`, `TurnRightPressed/Released` declared (Step 1) and bound (Step 3); `TurnRateDegPerSec` defined in the anon namespace (Step 2) and used in `Tick` (Step 4); `Tick` declared (Step 1) and defined (Step 4). Consistent.

**Risk flagged:** if the FP camera did *not* track control rotation, `SetControlRotation` wouldn't move the view — but mouse-look currently turns the range camera, which proves the camera uses pawn control rotation, so this holds. Step 8 catches it regardless (if the view doesn't turn, debug before committing).
