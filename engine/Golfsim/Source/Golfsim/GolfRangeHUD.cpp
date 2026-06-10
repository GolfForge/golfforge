#include "GolfRangeHUD.h"

#include "GolfBallActor.h"
#include "GolfEnvironment.h"
#include "ManualShotDialog.h"              // manual-shot dialog (GOL-8)
#include "Range/GolfPinActor.h"            // range target pin (GOL-29)
#include "Session/ShotHistorySubsystem.h"  // session shot history (GOL-65)
#include "ShotHistoryPanel.h"
#include "PreviousSessionsList.h"
#include "UI/RoundSetupWizard.h"            // GOL-141: round-setup wizard over main menu
#include "UI/PracticeSetup.h"               // GOL-73: practice-drill picker over main menu
#include "Game/CourseRegistry.h"            // GOL-141: course-card seed list
#include "UI/RoundHud.h"                    // GOL-144: in-round glass top HUD
#include "UI/LeaveConfirmDialog.h"          // GOL-147: leave/quit confirmation modal
#include "ScorecardPanel.h"                 // GOL-120: end-of-round scorecard modal
#include "Kismet/GameplayStatics.h"        // OpenLevel for the scorecard's Back-to-Menu
#include "CheatSheetPanel.h"
#include "SwingMeterWidget.h"   // GOL-67
#include "Events/EventBusSubsystem.h"      // publish shot.taken / subscribe shot.outcome (GOL-7)
#include "Drivers/LaunchMonitorManager.h"  // active-driver status -> panel dot (GOL-11)
#include "Drivers/LaunchMonitorDriver.h"
#include "Sound/SoundBase.h"               // ball-strike one-shot SFX
#include "Physics/BallFlightTypes.h"       // FBallTrajectory (carried on the outcome event)
#include "Physics/RangeSurface.h"          // ClassifyRangeLie -> the integrator's surface provider (GOL-9)
#include "Round/RoundSubsystem.h"          // IsActive() guard for the range pin (GOL-117)
#include "Round/RoundState.h"              // GolfsimRound::IsWithinGimme (CTP putt-out hole-out, GOL-73)
#include "Practice/PracticeModeSubsystem.h"// GOL-73: CTP config/session/scoring + practice.shot_scored

#include "DrawDebugHelpers.h"              // GOL-73: ball->pin result line
#include "EngineUtils.h"
#include "Camera/CameraActor.h"     // follow camera (Camera dropdown: Tee / Follow)
#include "Camera/CameraComponent.h" // disable the follow cam's aspect-ratio constraint
#include "Components/InputComponent.h"
#include "SettingsMenu.h"
#include "MainMenu.h"                          // startup main menu (Range / Play Course / Exit)
#include "Framework/Application/SlateApplication.h"
#include "Engine/Canvas.h"          // UCanvas (SizeX/SizeY) for the DrawHUD resolution readout
#include "Engine/GameViewportClient.h"
#include "Sound/AmbientSound.h"     // GOL-166: gate placed ambient SFX to gameplay
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/IConsoleManager.h"    // GOL-167: golf.ShowFPS toggle for the HUD FPS readout

// GOL-167: on-HUD FPS counter for playtesting (sits by the resolution readout).
// Toggle live in the console: `golf.ShowFPS 0` / `golf.ShowFPS 1`.
static TAutoConsoleVariable<int32> CVarGolfShowFps(
	TEXT("golf.ShowFPS"), 1,
	TEXT("Show the on-HUD FPS counter (top-left). 1=on (default), 0=off."),
	ECVF_Default);

namespace
{
	struct FClubPreset { const TCHAR* Name; double SpeedMps; double LaunchDeg; double SpinRpm; };

	// Trackman PGA-Tour averages (ball speed mph -> m/s), hit straight; per-shot
	// dispersion is added at fire time so no two shots are identical. Putter is a
	// placeholder until a real putting model lands -- BallFlight produces a tiny
	// arc and GroundRoll handles the actual roll at the Fairway friction value.
	static const FClubPreset GBag[] = {
		{ TEXT("Driver"),         74.6, 10.9,  2686.0 },
		{ TEXT("3-Wood"),         70.6,  9.2,  3655.0 },
		{ TEXT("5-Wood"),         67.4,  9.4,  4350.0 },
		{ TEXT("4-Iron"),         64.4, 10.4,  4630.0 },
		{ TEXT("5-Iron"),         60.3, 11.9,  5280.0 },
		{ TEXT("6-Iron"),         57.7, 14.1,  6204.0 },
		{ TEXT("7-Iron"),         55.0, 16.3,  7097.0 },
		{ TEXT("8-Iron"),         52.6, 18.1,  7998.0 },
		{ TEXT("9-Iron"),         48.7, 20.4,  8647.0 },
		{ TEXT("Pitching Wedge"), 45.6, 24.2,  9304.0 },
		{ TEXT("Gap Wedge 50"),   43.0, 26.0,  9750.0 },
		{ TEXT("Sand Wedge 56"),  40.0, 28.0, 10500.0 },
		{ TEXT("Lob Wedge 60"),   36.0, 32.0, 11500.0 },
		{ TEXT("Putter"),          4.0,  1.0,   100.0 },   // placeholder; see comment above
	};
	static constexpr int32 GBagNum = UE_ARRAY_COUNT(GBag);

	// Aim turn speed while an arrow is held, deg/sec. Tunable.
	static constexpr float TurnRateDegPerSec = 75.0f;

	// Place the ball at the player pawn (the tee), facing its heading with pitch/roll
	// flattened. Reuses one ball, repositioning per shot. Mirrors GolfsimConsole.cpp.
	AGolfBallActor* GetOrSpawnBallAt(UWorld* World, const FVector& Loc, const FRotator& Rot)
	{
		for (TActorIterator<AGolfBallActor> It(World); It; ++It)
		{
			It->SetActorLocationAndRotation(Loc, Rot);
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGolfBallActor>(AGolfBallActor::StaticClass(), Loc, Rot, Params);
	}

	// Follow-cam framing (Unreal units = cm). Tunable; the chase sits behind + above the ball along the
	// captured launch direction and always looks at the ball, so it stays centered even when lagging.
	static constexpr float FollowBackUU = 900.f;     // behind the ball
	static constexpr float FollowUpUU = 500.f;       // above the ball
	static constexpr float FollowInterpSpeed = 4.f;  // position smoothing (VInterpTo)
	static constexpr float FollowViewBlend = 0.25f;  // SetViewTargetWithBlend time, s

	// Ball-center launch height above the traced ground -- single source of truth lives on
	// AGolfBallActor (GOL-110). Aliased here for the file-scope launch trace code below.
	static constexpr float BallRestHeightUU = AGolfBallActor::BallRestHeightUU;

	// Ideal chase pose for a ball at BallPos, given the (flattened) downrange direction.
	void ComputeFollowPose(const FVector& BallPos, const FVector& DownrangeDir, FVector& OutLoc, FRotator& OutRot)
	{
		OutLoc = BallPos - DownrangeDir * FollowBackUU + FVector::UpVector * FollowUpUU;
		OutRot = (BallPos - OutLoc).Rotation();
	}

	// Find the range's environment director, or spawn one if absent. Logic-only + PIE-only, so
	// nothing is persisted into the umap. Mirrors GetOrSpawnBallAt / GolfsimConsole's helpers.
	AGolfEnvironment* GetOrSpawnRangeEnv(UWorld* World)
	{
		for (TActorIterator<AGolfEnvironment> It(World); It; ++It)
		{
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGolfEnvironment>(AGolfEnvironment::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

AGolfRangeHUD::AGolfRangeHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AGolfRangeHUD::BeginPlay()
{
	Super::BeginPlay();
	EnsureInputBound();
}

void AGolfRangeHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEventBusSubsystem* EBus = EventBusWeak.Get())
	{
		EBus->Unsubscribe(OutcomeSub);   // weak-captured anyway, but don't leave a dead entry
		EBus->Unsubscribe(RoundCompleteSub);   // GOL-120
		EBus->SurfaceProvider = nullptr; // the subsystem outlives this HUD; drop the lie source (GOL-9)
	}
	OutcomeSub = FGolfEventSubscription{};
	RoundCompleteSub = FGolfEventSubscription{};
	EventBusWeak = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AGolfRangeHUD::UpdateAmbientPlayback()
{
	// GOL-166: the placed AAmbientSound beds/bird zones auto-activate on level
	// load, but the main menu / course-select / settings all sit on top of a
	// live level -- so without gating you'd hear ambience under the menu. Gate
	// it to actual gameplay: audible iff no menu/modal is up (!InputGated()).
	// Cached once (the ambient actors live in the persistent level); rebuilt per
	// HUD BeginPlay, i.e. per level. Both maps use this HUD.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bAmbientCached)
	{
		bAmbientCached = true;
		bAmbientAudible = false;
		for (TActorIterator<AAmbientSound> It(World); It; ++It)
		{
			if (UAudioComponent* AC = It->GetAudioComponent())
			{
				AC->bAutoActivate = false;
				AC->Stop();   // silent until gameplay starts (kills any auto-activate)
				AmbientComponents.Add(AC);
			}
		}
	}

	const bool bDesired = !InputGated();
	if (bDesired == bAmbientAudible)
	{
		return;
	}
	bAmbientAudible = bDesired;
	for (const TObjectPtr<UAudioComponent>& AC : AmbientComponents)
	{
		if (!AC)
		{
			continue;
		}
		if (bDesired)
		{
			AC->FadeIn(1.0f);          // ramp up over 1s when entering gameplay
		}
		else
		{
			AC->FadeOut(1.0f, 0.0f);   // ramp down + stop when a menu/modal opens
		}
	}
}

void AGolfRangeHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateFollowCam(DeltaSeconds);
	UpdateInRoundHud();   // GOL-144: drive the glass round panel + hole map; toggle vs the legacy panel
	UpdateAmbientPlayback();   // GOL-166: birds play only in-game, never under the menu / course select

	// GOL-29: keep retrying the pin placement until the actor exists. EnsureInputBound calls
	// ApplyPinDistance once at panel mount, but on the very first ticks the pawn may not be
	// possessed yet -- ApplyPinDistance early-returns and the pin actor never spawns. Tick
	// re-fires until Pin becomes a valid actor; thereafter this is a no-op every frame.
	// GOL-117: while a round is active, URoundPinSubsystem owns the pin -- don't fight it.
	if (Panel && !Pin.IsValid() && !RoundIsActive()
		&& GetOwningPlayerController() && GetOwningPlayerController()->GetPawn())
	{
		ApplyPinDistance(CurrentPinYd);
	}

	// GOL-67: drive the swing-meter bars while in Game mode and a swing is in progress.
	// Idle state is a no-op (bars hold their last values, which we zeroed on each shot resolution).
	if (CurrentInputMode == EInputMode::Game && SwingState.State != GolfsimKeyboardSwing::EState::Idle)
	{
		GolfsimKeyboardSwing::Tick(SwingState, SwingConfig, DeltaSeconds);
		if (SwingMeter)
		{
			SwingMeter->SetMeters(SwingState.Power, SwingState.Accuracy);
		}
	}

	// Count the Carry readout up while the ball is in the air, then snap to the exact final once it
	// lands (or if the trajectory was invalid, the first tick just sets the final immediately).
	if (bCarryAnimating && Panel)
	{
		AGolfBallActor* Ball = AnimBall.Get();
		if (Ball && Ball->IsPlaying())
		{
			// Live downrange grows from ~0 to carry in flight, then on past carry during the rollout.
			// Carry freezes at its landing value; Total keeps counting up through the roll.
			const double LiveYd = Ball->GetCurrentCarryMeters() * 1.0936132983;   // m -> yd
			Panel->UpdateMetrics(AnimClub, AnimSpeedMph, AnimLaunchDeg, AnimSpinRpm,
				FMath::Min(LiveYd, AnimTargetCarryYd), FMath::Min(LiveYd, AnimTargetTotalYd),
				AnimOfflineYd, bAnimSpinEstimated);
		}
		else
		{
			Panel->UpdateMetrics(AnimClub, AnimSpeedMph, AnimLaunchDeg, AnimSpinRpm,
				AnimTargetCarryYd, AnimTargetTotalYd, AnimOfflineYd, bAnimSpinEstimated);   // landed + rolled: exact finals
			bCarryAnimating = false;

			// GOL-73: the ball has settled -- if this was a CTP shot, score it now (the settled world
			// position handles aim + side-offset pins; the launch-frame outcome can't).
			if (bCtpScorePending)
			{
				bCtpScorePending = false;
				OnCtpShotSettled(Ball);
			}
		}
	}

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

void AGolfRangeHUD::EnsureInputBound()
{
	if (bInputBound)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;   // PlayerOwner not ready yet; retried from DrawHUD
	}
	EnableInput(PC);
	if (!bControlsLocked)
	{
		PC->SetIgnoreMoveInput(true);   // planted on the tee
		PC->SetIgnoreLookInput(true);   // arrows aim instead of the mouse
		// Free the cursor so the player can click the panel's club dropdown. GameAndUI keeps
		// the legacy key binds (1-6 / Space / arrows) live while routing clicks to the UI.
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
		// Themed golf-tee hardware cursor (range-only). Loads Content/Slate/golf_tee_cursor.png;
		// hotspot (normalized) sits on the sharp tip. Override Default + Hand so it shows over the UI too.
		//
		// MAC GATING (see GOL-159): UE5.7's Mac NSCursor path doesn't accept this single
		// 64x64 RGBA PNG cleanly — it renders as multiple small ghost cursors in neon
		// violet on Apple Silicon. Until we ship a proper multi-resolution Mac cursor
		// (or replace with a Slate software-cursor widget for cross-platform parity),
		// Mac users get the system arrow.
#if !PLATFORM_MAC
		UWorld* World = GetWorld();
		if (UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr)
		{
			const FName CursorPath(TEXT("Slate/golf_tee_cursor"));
			const FVector2D HotSpot(0.063, 0.047);
			GVC->SetHardwareCursor(EMouseCursor::Default, CursorPath, HotSpot);
			GVC->SetHardwareCursor(EMouseCursor::Hand, CursorPath, HotSpot);
		}
#endif
		// Always-on FPS overlay so range perf is visible at a glance in PIE and
		// fullscreen (range-only, like the rest of this lock block; runs once).
		// Swap to "stat unit" for the GPU/draw ms breakdown when diagnosing.
		PC->ConsoleCommand(TEXT("stat fps"));
		bControlsLocked = true;
	}
	if (!Panel)
	{
		Panel = CreateWidget<UGolfRangePanel>(PC, UGolfRangePanel::StaticClass());
		if (Panel)
		{
			// Weak capture: the panel must never call into a destroyed HUD (e.g. during PIE teardown).
			TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
			Panel->OnClubChosen = [WeakThis](int32 Idx)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->SelectClub(Idx);
					HUD->ReturnFocusToGame();   // GOL-123: don't let the combo swallow Space
				}
			};
			// Feed the dropdown from the bag so the names can't drift from the firing presets.
			TArray<FString> ClubNames;
			ClubNames.Reserve(GBagNum);
			for (const FClubPreset& P : GBag)
			{
				ClubNames.Add(P.Name);
			}
			Panel->SetClubOptions(ClubNames);
			Panel->AddToViewport();
			Panel->SetSelectedClubIndex(ActiveClub);
			Panel->UpdateMetrics(GBag[ActiveClub].Name, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);   // "-" until first shot

			// Camera dropdown: Tee (fixed pawn view) / Follow (chase cam). Defaults to Tee so the
			// current view is unchanged unless the user opts in.
			Panel->SetCameraOptions({ TEXT("Tee"), TEXT("Follow") });
			Panel->SetSelectedCameraIndex(0);
			Panel->OnCameraChosen = [WeakThis](int32 Idx)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->SetCameraMode(Idx);
					HUD->ReturnFocusToGame();
				}
			};

			// GOL-145: the telemetry readout's primary button (Swing / Sim shot). Routed by mode in
			// TriggerPrimaryAction (Game -> swing meter; a connected LM -> RequestSimulatedShot).
			Panel->OnPrimaryAction = [WeakThis]()
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->TriggerPrimaryAction(); }
			};

			// GOL-147: top-left Menu button opens the settings menu (same as Esc / the in-round Menu).
			Panel->OnMenu = [WeakThis]()
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ToggleSettingsMenu(); }
			};

			// Pin distance (GOL-29). Always start at 150 yd; persistence caused more confusion than
			// it was worth (race + test-pollution corner cases all set the spinner to 0 on next load).
			// The user can drag the spinner to whatever they want during the session. The Tick loop
			// re-fires ApplyPinDistance until the pawn is ready and the actor actually spawns.
			CurrentPinYd = 150.0;
			Panel->OnPinChanged = [WeakThis](double Y)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->ApplyPinDistance(Y);
					HUD->ReturnFocusToGame();
				}
			};
			Panel->OnPuttModeChanged = [WeakThis](bool b)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->SetPuttMode(b);
					HUD->ReturnFocusToGame();
				}
			};
			Panel->SetPinValue(CurrentPinYd);
			ApplyPinDistance(CurrentPinYd);

			// GOL-73: CTP settings cluster lives on the range readout but is hidden until the player
			// enters Closest-to-Pin via the Practice menu (no mode dropdown on the range -- the range is
			// plain free-fire). Defaults mirror FCtpConfig (50-250 yd, no side, no putt-out).
			Panel->SetCtpConfigValues(50.0, 250.0, false, false, 10.0);
			Panel->OnEndPractice = [WeakThis]()
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->SetPracticeMode(GolfsimPractice::EPracticeMode::Free);
					HUD->ReturnFocusToGame();
				}
			};
			Panel->OnCtpConfigChanged = [WeakThis](double Mn, double Mx, bool bSide, bool bPutt, double Within)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->ApplyCtpConfig(Mn, Mx, bSide, bPutt, Within);
					HUD->ReturnFocusToGame();
				}
			};

			// Environment director: find-or-spawn the range's time-of-day/weather actor and wire the
			// Time + Sky dropdowns to it. Names come from the director (single source of truth, like
			// the club bag); weak capture so a dropdown can never call into a destroyed actor.
			if (UWorld* World = GetWorld())
			{
				if (AGolfEnvironment* Env = GetOrSpawnRangeEnv(World))
				{
					TWeakObjectPtr<AGolfEnvironment> WeakEnv(Env);
					Panel->OnTimeChosen = [WeakEnv, WeakThis](int32 Idx)
					{
						if (AGolfEnvironment* E = WeakEnv.Get()) { E->SetTime(Idx); }
						if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ReturnFocusToGame(); }
					};
					Panel->OnSkyChosen = [WeakEnv, WeakThis](int32 Idx)
					{
						if (AGolfEnvironment* E = WeakEnv.Get()) { E->SetSky(Idx); }
						if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ReturnFocusToGame(); }
					};
					Panel->SetTimeOptions(Env->GetTimePresetNames());
					Panel->SetSkyOptions(Env->GetSkyPresetNames());
					Panel->SetSelectedTimeIndex(Env->GetTimeIndex());
					Panel->SetSelectedSkyIndex(Env->GetSkyIndex());
				}
			}
		}
	}
	// Subscribe once to the bus: shot.outcome plays the ball + refreshes the panel (it carries the
	// source shot's launch metrics, so no separate shot.taken stash is needed). Weak capture so a
	// late dispatch during PIE teardown can never call into a destroyed HUD.
	if (!OutcomeSub.IsValid())
	{
		if (UEventBusSubsystem* EBus = UEventBusSubsystem::Get(this))
		{
			TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
			OutcomeSub = EBus->Subscribe(EEventKind::ShotOutcome,
				[WeakThis](const FGolfEvent& Event)
				{
					if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OnShotOutcome(Event); }
				});
			// GOL-120: round.complete -> auto-open the scorecard. Reads par from URoundSubsystem
			// state (Schedule survives until next StartRound). Event itself carries PerHoleStrokes.
			RoundCompleteSub = EBus->Subscribe(EEventKind::RoundComplete,
				[WeakThis](const FGolfEvent& Event)
				{
					AGolfRangeHUD* HUD = WeakThis.Get();
					if (!HUD || Event.Kind != EEventKind::RoundComplete) { return; }
					const FRoundCompleteEvent& RC = static_cast<const FRoundCompleteEvent&>(Event);
					TArray<int32> Pars;
					if (const URoundSubsystem* Sub = URoundSubsystem::Get(HUD))
					{
						const GolfsimRound::FRoundState& S = Sub->GetState();
						Pars.Reserve(S.Schedule.Num());
						for (const GolfsimRound::FHoleSpec& H : S.Schedule) { Pars.Add(H.Par); }
					}
					HUD->OpenScorecardForState(Pars, RC.PerHoleStrokes);
				});
			EventBusWeak = EBus;   // cache for a reliable Unsubscribe in EndPlay

			// Ground-roll lie source (GOL-9): map a launch-local SI landing position to the painted
			// surface. Mirrors AGolfBallActor::SampleToWorld (tee + flattened aim), then world cm -> m;
			// the range landscape is centered at the world origin, so world meters == the splatmap's
			// frame. Read live -- the integrator runs synchronously during a fire, so the pawn + aim are
			// this shot's. Weak-captured: a stale call after teardown returns Unknown (-> no roll).
			EBus->SurfaceProvider = [WeakThis](const FVector& LandingLocalSIm) -> EGolfLie
			{
				AGolfRangeHUD* HUD = WeakThis.Get();
				APlayerController* PC = HUD ? HUD->GetOwningPlayerController() : nullptr;
				APawn* Pawn = PC ? PC->GetPawn() : nullptr;
				if (!Pawn) { return EGolfLie::Unknown; }
				FRotator Aim = PC->GetControlRotation();
				Aim.Pitch = 0.f;
				Aim.Roll = 0.f;
				const FVector WorldCm = Pawn->GetActorLocation() + Aim.RotateVector(LandingLocalSIm * 100.0);
				return GolfRangeSurface::ClassifyLie(WorldCm.X / 100.0, WorldCm.Y / 100.0);
			};

			// GOL-145: launch-monitor §6 gating. The LM dropdown's status drives the input mode (a
			// connected device -> Simulation: the device owns the shot stream; "Simulated (no device)"/
			// disconnected -> Game: keyboard swing) plus the status pill + primary-button label.
			if (ULaunchMonitorManager* LM = ULaunchMonitorManager::Get(this))
			{
				// Async connect/disconnect re-derives the gating state (so a failed connect falls back
				// to Game mode + amber pill). Resolve the device name fresh each fire.
				LM->OnActiveStatusChanged = [WeakThis](ELaunchMonitorStatus Status, const FString& Detail)
				{
					AGolfRangeHUD* HUD = WeakThis.Get();
					if (!HUD) { return; }
					ULaunchMonitorManager* M = ULaunchMonitorManager::Get(HUD);
					ULaunchMonitorDriver* D = M ? M->GetActiveDriver() : nullptr;
					const FString Name = D ? D->GetDisplayName().ToString() : FString(TEXT("Simulated (no device)"));
					HUD->ApplyLaunchMonitorState(Status, Name);
					// On connect, sync the device to our current club (re-picking the same dropdown
					// entry wouldn't fire OnClubChosen, so push it here too).
					if (Status == ELaunchMonitorStatus::Online && D)
					{
						D->SetSelectedClub(GBag[FMath::Clamp(HUD->ActiveClub, 0, GBagNum - 1)].Name);
					}
				};

				// Ball-ready indicator (GOL-186): the active driver reports "armed / take your shot".
				LM->OnActiveReadyChanged = [WeakThis](bool bReady)
				{
					if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ApplyLaunchMonitorReady(bReady); }
				};

				// Dropdown options: "Simulated (no device)" (index 0 = keyboard/game) + each available
				// driver (today just OpenFlight; Square Omni / Blue Tees Rainmaker appear automatically
				// once their drivers register). Picking a driver connects it; index 0 drops to keyboard.
				const TArray<FLaunchMonitorDriverInfo> Drivers = LM->GetAvailableDrivers();
				ULaunchMonitorDriver* Active = LM->GetActiveDriver();
				const bool bActiveConnected = Active && Active->IsConnected();
				TArray<FString> LMNames;
				LMNames.Reserve(Drivers.Num() + 1);
				LMNames.Add(TEXT("Simulated (no device)"));
				int32 SelectedLM = 0;
				for (int32 i = 0; i < Drivers.Num(); ++i)
				{
					LMNames.Add(Drivers[i].DisplayName.ToString());
					if (bActiveConnected && Drivers[i].Id == LM->GetActiveDriverId())
					{
						SelectedLM = i + 1;
					}
				}
				Panel->SetLaunchMonitorOptions(LMNames);
				Panel->SetSelectedLaunchMonitorIndex(SelectedLM);
				// Default = Simulated: clear any configured-but-idle driver so a late callback can't
				// repaint the pill as that driver's "Offline".
				if (SelectedLM == 0) { LM->SetActiveDriver(FString(), /*bConnectNow=*/false); }

				Panel->OnLaunchMonitorChosen = [WeakThis](int32 Idx)
				{
					AGolfRangeHUD* HUD = WeakThis.Get();
					if (!HUD) { return; }
					ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(HUD);
					if (!Mgr) { HUD->ReturnFocusToGame(); return; }
					if (Idx <= 0)
					{
						// "Simulated (no device)": drop the driver entirely (clear the active id).
						Mgr->SetActiveDriver(FString(), /*bConnectNow=*/false);
						HUD->ApplyLaunchMonitorState(ELaunchMonitorStatus::Sim, TEXT("Simulated (no device)"));
					}
					else
					{
						const TArray<FLaunchMonitorDriverInfo> Avail = Mgr->GetAvailableDrivers();
						if (Avail.IsValidIndex(Idx - 1))
						{
							Mgr->SetActiveDriver(Avail[Idx - 1].Id, /*bConnectNow=*/true);
							// Connect is async; show Pairing now -- OnActiveStatusChanged refines it to
							// Online/Off when the driver reports back.
							const ELaunchMonitorStatus Now = Mgr->GetActiveStatus();
							HUD->ApplyLaunchMonitorState(
								Now == ELaunchMonitorStatus::Online ? Now : ELaunchMonitorStatus::Pairing,
								Avail[Idx - 1].DisplayName.ToString());
						}
					}
					HUD->ReturnFocusToGame();
				};

				// Apply the initial gating from the current selection (default = Simulated -> Game mode).
				ApplyLaunchMonitorState(
					bActiveConnected ? ELaunchMonitorStatus::Online : ELaunchMonitorStatus::Sim,
					(bActiveConnected && Active) ? Active->GetDisplayName().ToString()
												 : FString(TEXT("Simulated (no device)")));
			}
		}
	}
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::Q,        IE_Pressed, this, &AGolfRangeHUD::PrevClub);
	InputComponent->BindKey(EKeys::E,        IE_Pressed, this, &AGolfRangeHUD::NextClub);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AGolfRangeHUD::OnSpaceForCurrentMode);
	InputComponent->BindKey(EKeys::Left,     IE_Pressed,  this, &AGolfRangeHUD::TurnLeftPressed);
	InputComponent->BindKey(EKeys::Left,     IE_Released, this, &AGolfRangeHUD::TurnLeftReleased);
	InputComponent->BindKey(EKeys::Right,    IE_Pressed,  this, &AGolfRangeHUD::TurnRightPressed);
	InputComponent->BindKey(EKeys::Right,    IE_Released, this, &AGolfRangeHUD::TurnRightReleased);
	InputComponent->BindKey(EKeys::M,        IE_Pressed,  this, &AGolfRangeHUD::ToggleManualDialog);
	InputComponent->BindKey(EKeys::C,        IE_Pressed,  this, &AGolfRangeHUD::ToggleCameraMode);       // GOL-73 camera flip
	InputComponent->BindKey(EKeys::H,        IE_Pressed,  this, &AGolfRangeHUD::ToggleHistoryFromKey);   // GOL-65
	// Settings/credits menu. Escape works in packaged builds + PIE (the editor's Escape-stops-play
	// only applies when the viewport hasn't captured focus); golfsim.Credits also opens it.
	InputComponent->BindKey(EKeys::Escape,   IE_Pressed,  this, &AGolfRangeHUD::ToggleSettingsMenu);
	// Tab: dev cheat sheet listing every key binding. Replaced by a real keybindings UI later.
	InputComponent->BindKey(EKeys::Tab,      IE_Pressed,  this, &AGolfRangeHUD::ToggleCheatSheet);
	// Z: alternate to Esc -- opens Settings in-game (which carries the Main Menu + Quit shortcuts).
	// In-game never jumps straight to the main menu; it always routes through Settings (GOL-140).
	InputComponent->BindKey(EKeys::Z,        IE_Pressed,  this, &AGolfRangeHUD::ToggleSettingsMenu);
	// Follow-cam orbit: right mouse + drag to circle the resting ball (Follow mode).
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AGolfRangeHUD::OrbitPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AGolfRangeHUD::OrbitReleased);
	InputComponent->BindAxisKey(EKeys::MouseX, this, &AGolfRangeHUD::OnOrbitYaw);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &AGolfRangeHUD::OnOrbitPitch);
	// GOL-121: skip the menu greet when the HUD is spawning on the auto-loaded course map mid
	// round-start. Otherwise the menu pops up over Black 1's first tee and the player has to
	// click Range just to dismiss it.
	if (URoundSubsystem* Round = URoundSubsystem::Get(this); !Round || !Round->IsPendingStart())
	{
		ShowMainMenu();   // greet on the already-loaded range; gameplay stays gated until "Range"
	}
	bInputBound = true;
}

void AGolfRangeHUD::PrevClub()
{
	if (InputGated()) { return; }
	SelectClub((ActiveClub - 1 + GBagNum) % GBagNum);
}

void AGolfRangeHUD::NextClub()
{
	if (InputGated()) { return; }
	SelectClub((ActiveClub + 1) % GBagNum);
}

void AGolfRangeHUD::SelectClub(int32 Index)
{
	if (InputGated()) { return; }
	ApplyClubSelection(Index);
}

void AGolfRangeHUD::ApplyClubSelection(int32 Index)
{
	ActiveClub = FMath::Clamp(Index, 0, GBagNum - 1);
	UE_LOG(LogTemp, Display, TEXT("golfsim range: club -> %s"), GBag[ActiveClub].Name);
	if (Panel)
	{
		Panel->SetSelectedClubIndex(ActiveClub);   // keep the dropdown in step with 1-6 keys (guarded)
		Panel->SetMetricClubName(GBag[ActiveClub].Name);   // GOL-123: also refresh the "Club: X" readout
	}
	// Push our selection to the connected launch monitor so it uses our club (OpenFlight set_club ->
	// mock distributions / hardware estimates). Harmless no-op when nothing is connected.
	if (ULaunchMonitorManager* LM = ULaunchMonitorManager::Get(this))
	{
		if (ULaunchMonitorDriver* D = LM->GetActiveDriver())
		{
			D->SetSelectedClub(GBag[ActiveClub].Name);
		}
	}
}

void AGolfRangeHUD::RefreshActiveCamera()
{
	// Re-invoke the current mode -- SetCameraMode handles SetViewTargetWithBlend either to the
	// pawn (Tee) or to a re-framed follow-cam around the active ball (Follow). Dropdown stays
	// in sync since the active index doesn't change.
	SetCameraMode(bFollowCam ? 1 : 0);
}

void AGolfRangeHUD::ReturnFocusToGame()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

bool AGolfRangeHUD::SelectDriverIfNeeded()
{
	// Driver is bag index 0 by convention; ApplyClubSelection clamps if the bag is ever empty.
	if (ActiveClub == 0) { return false; }
	ApplyClubSelection(0);
	return true;
}

bool AGolfRangeHUD::SelectPutterIfAvailable()
{
	// Walk the bag for the "Putter" entry. The bag is small (14 clubs); a linear scan is fine.
	int32 PutterIdx = INDEX_NONE;
	for (int32 i = 0; i < GBagNum; ++i)
	{
		if (FCString::Stricmp(GBag[i].Name, TEXT("Putter")) == 0)
		{
			PutterIdx = i;
			break;
		}
	}
	if (PutterIdx == INDEX_NONE) { return false; }
	if (ActiveClub == PutterIdx) { return false; }   // already on putter; no-op
	// Bypass the input gate -- this fires from a shot-outcome subscriber, not user input. Modals
	// being open shouldn't stop a context-driven club swap.
	ApplyClubSelection(PutterIdx);
	return true;
}

void AGolfRangeHUD::FireRandom()
{
	if (InputGated()) { return; }
	const FClubPreset& C = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
	// Club-typical values + per-shot dispersion (so no two shots are identical).
	const double BallMps = C.SpeedMps  * FMath::FRandRange(0.97, 1.03);
	const double Launch  = C.LaunchDeg + FMath::FRandRange(-1.5, 1.5);
	const double Back    = C.SpinRpm   * FMath::FRandRange(0.92, 1.08);
	const double Az      = FMath::FRandRange(-2.5, 2.5);      // face/path spread
	const double Side    = FMath::FRandRange(-700.0, 700.0);  // curve
	PublishShotTaken(BallMps, Launch, Az, Back, Side, C.Name, TEXT("range-fire"));
}

void AGolfRangeHUD::FireManualShot(const FManualShotValues& Values)
{
	// Display units -> SI at the boundary: only ball speed converts; angles stay degrees, spin rpm.
	const double BallMps = Values.BallSpeedMph * 0.44704;   // mph -> m/s
	PublishShotTaken(BallMps, Values.LaunchDeg, Values.AzimuthDeg, Values.BackspinRpm,
		Values.SidespinRpm, Values.Club, TEXT("manual-shot-dialog"));
}

void AGolfRangeHUD::PublishShotTaken(double BallMps, double LaunchDeg, double AzDeg, double BackRpm,
	double SideRpm, const FString& Club, const FString& Source)
{
	// Build the shot.taken envelope and publish it. The integrator subscriber runs the solver and
	// publishes shot.outcome (GOL-7: the HUD fires through the bus, not the solver), carrying this
	// shot's launch metrics; OnShotOutcome reads them + the ball launches from the live tee + aim --
	// so this just builds + publishes.
	FShotTakenEvent Shot;
	Shot.Source         = Source;
	Shot.PlayerId       = GolfsimEvents::LocalPlayerId();
	Shot.Club           = Club;
	Shot.Lie            = TEXT("tee");
	Shot.BallSpeedMps   = BallMps;
	Shot.LaunchAngleDeg = LaunchDeg;
	Shot.AzimuthDeg     = AzDeg;
	Shot.BackspinRpm    = BackRpm;
	Shot.SidespinRpm    = SideRpm;

	UE_LOG(LogTemp, Display, TEXT("golfsim range: %s -> shot.taken published (%s)"), *Club, *Source);

	if (UEventBusSubsystem* EBus = UEventBusSubsystem::Get(this))
	{
		EBus->Publish(Shot);   // synchronous: integrator simulates + publishes outcome -> OnShotOutcome
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim range: no EventBus subsystem; shot dropped"));
	}
}

void AGolfRangeHUD::ToggleManualDialog()
{
	if (bMenuOpen) { return; }   // main menu owns the screen until dismissed
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	if (!ManualDialog)
	{
		ManualDialog = CreateWidget<UManualShotDialog>(PC, UManualShotDialog::StaticClass());
		if (!ManualDialog)
		{
			return;
		}

		// Fire routes back here so the launch transform + panel bookkeeping stay in one place.
		TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
		ManualDialog->OnFire = [WeakThis](const FManualShotValues& V)
		{
			if (AGolfRangeHUD* HUD = WeakThis.Get())
			{
				HUD->FireManualShot(V);
			}
		};
		// Picking a club seeds the spinners from that club's bag preset (autofill); straight shot.
		TWeakObjectPtr<UManualShotDialog> WeakDlg(ManualDialog);
		ManualDialog->OnClubChosen = [WeakDlg](int32 Idx)
		{
			if (UManualShotDialog* D = WeakDlg.Get())
			{
				const FClubPreset& P = GBag[FMath::Clamp(Idx, 0, GBagNum - 1)];
				D->SetFields(P.SpeedMps * 2.2369362921, P.LaunchDeg, P.SpinRpm, 0.0, 0.0);
			}
		};

		// Club options from the bag (same single source as the auto-fire panel).
		TArray<FString> ClubNames;
		ClubNames.Reserve(GBagNum);
		for (const FClubPreset& P : GBag)
		{
			ClubNames.Add(P.Name);
		}
		ManualDialog->SetClubOptions(ClubNames);
		ManualDialog->AddToViewport(10);   // above the auto-fire panel
		ManualDialog->SetSelectedClubIndex(ActiveClub);
		// Open on the active club's realistic values rather than zeros.
		const FClubPreset& A = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
		ManualDialog->SetFields(A.SpeedMps * 2.2369362921, A.LaunchDeg, A.SpinRpm, 0.0, 0.0);
	}

	bManualOpen = !bManualOpen;
	ManualDialog->SetVisibility(bManualOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (Panel)
	{
		// Hide the auto-fire panel while the dialog is open; restore it (children stay clickable) when closed.
		Panel->SetVisibility(bManualOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void AGolfRangeHUD::EnsureSettingsMenu()
{
	if (SettingsMenu)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	SettingsMenu = CreateWidget<USettingsMenu>(PC, USettingsMenu::StaticClass());
	if (!SettingsMenu)
	{
		return;
	}
	SettingsMenu->SetResolutionOptions(GolfDisplay::SupportedResolutions());
	SettingsMenu->SetUpscalerOptions(GolfDisplay::AvailableUpscalerIndices());
	SettingsMenu->SetCreditsText(GolfDisplay::CreditsText());
	SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	SettingsMenu->OnApplyDisplay = [WeakThis](const FGolfDisplaySettings& S)
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ApplyDisplaySettings(S); }
	};
	SettingsMenu->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseSettings(); }
	};
	// GOL-125 / GOL-147: Main Menu button -> mode-aware leave confirm (range copy on the range), then
	// abandon round + LoadMap PracticeRange + show main menu on confirm.
	SettingsMenu->OnMainMenu = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->RequestLeaveToMainMenu(); }
	};
	SettingsMenu->AddToViewport(40);   // top modal: above the range panel, manual dialog, AND the main menu (30)
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::ToggleSettingsMenu()
{
	// Esc/Tab in-range. If settings is already open (even over the menu), close it; otherwise only open
	// when the menu isn't up -- the bento's Settings tile uses OpenSettingsOverMenu for that path.
	if (bSettingsOpen) { CloseSettings(); return; }
	if (bMenuOpen) { return; }
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	bSettingsOpen = true;
	SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());   // reseed in case values changed elsewhere
	SettingsMenu->SetActionButtonsVisible(true);            // in-range/mid-round: keep Main Menu + Quit
	SettingsMenu->SetVisibility(ESlateVisibility::Visible);
}

void AGolfRangeHUD::OpenSettingsOverMenu()
{
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	bSettingsOpen = true;
	SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());
	SettingsMenu->SetActionButtonsVisible(false);   // from the main menu: Main Menu + Quit are redundant
	SettingsMenu->SetVisibility(ESlateVisibility::Visible);
	SettingsMenu->SetKeyboardFocus();   // settings owns keys while open; CloseSettings hands focus back to the menu
}

void AGolfRangeHUD::CloseSettings()
{
	if (!SettingsMenu)
	{
		return;
	}
	bSettingsOpen = false;
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
	if (bMenuOpen && MainMenu)
	{
		MainMenu->SetKeyboardFocus();   // settings was opened over the bento -> return focus to it
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();   // hand keys back to gameplay
	}
}

void AGolfRangeHUD::ApplyDisplaySettings(const FGolfDisplaySettings& S)
{
	GolfDisplay::Apply(S);
	UE_LOG(LogTemp, Display, TEXT("golfsim settings: applied %dx%d windowMode=%d quality=%d screen%%=%.0f"),
		S.Resolution.X, S.Resolution.Y, S.WindowModeIndex, S.QualityLevel, S.ScreenPercentage);
}

namespace
{
	// GOL-29: pin sits down the corridor centerline. World +X downrange from the tee, Y=0 always.
	// Corridor max length = LANE_LEN_YD = 400 yd (RangeSurface.cpp), so this also bounds the
	// resolved placement so a 400+ yd request doesn't bury the disc in the tree wall.
	constexpr double YdPerMeter = 1.0936132983;
	constexpr double CmPerYd = 91.44;
	constexpr double PinMaxYd = 400.0;

	// Where to drop the pawn when entering putt mode: a few yards behind the flag on the green.
	constexpr double PuttStandoffYd = 3.0;   // ~9 ft -- a "lag putt" starting distance
}

bool AGolfRangeHUD::RoundIsActive() const
{
	if (const URoundSubsystem* Sub = URoundSubsystem::Get(this))
	{
		return Sub->IsActive();
	}
	return false;
}

void AGolfRangeHUD::EnsureRoundHud()
{
	if (RoundHud) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) { return; }
	RoundHud = CreateWidget<URoundHud>(PC, URoundHud::StaticClass());
	if (!RoundHud) { return; }

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	RoundHud->OnMenu = [WeakThis]()
	{
		// GOL-147: Menu opens the settings menu (same as Esc). Leaving the round is a step deeper --
		// Settings -> "Main Menu" -> the mode-aware leave confirm.
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ToggleSettingsMenu(); }
	};
	RoundHud->AddToViewport(20);   // above the legacy panel (0) + swing meter (15), below modals (30+)
	RoundHud->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::EnsureLeaveDialog()
{
	if (LeaveDialog) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) { return; }
	LeaveDialog = CreateWidget<ULeaveConfirmDialog>(PC, ULeaveConfirmDialog::StaticClass());
	if (!LeaveDialog) { return; }

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	LeaveDialog->OnConfirm = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseLeaveDialog(); HUD->ReturnToMainMenu(); }
	};
	LeaveDialog->OnCancel = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseLeaveDialog(); }
	};
	LeaveDialog->AddToViewport(45);   // above the settings modal (40); main menu (30)
	LeaveDialog->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::RequestLeaveToMainMenu()
{
	if (bLeaveConfirmOpen) { return; }
	// The range path is reached from inside Settings -> drop Settings so the confirm sits over the HUD
	// and a Cancel returns to the range (not back into Settings).
	if (bSettingsOpen) { CloseSettings(); }

	EnsureLeaveDialog();
	if (!LeaveDialog) { return; }

	// Mode-aware copy: an active round is "course" (progress auto-saved); otherwise the range.
	ELeaveMode Mode = ELeaveMode::Range;
	int32 HoleNum = 0;
	if (const URoundSubsystem* Sub = URoundSubsystem::Get(this))
	{
		if (Sub->IsActive())
		{
			Mode = ELeaveMode::Course;
			const GolfsimRound::FRoundState& S = Sub->GetState();
			if (S.Schedule.IsValidIndex(S.HoleIndex)) { HoleNum = S.Schedule[S.HoleIndex].Ref; }
		}
	}
	LeaveDialog->Configure(Mode, HoleNum);

	bLeaveConfirmOpen = true;
	LeaveDialog->SetVisibility(ESlateVisibility::Visible);
	LeaveDialog->SetKeyboardFocus();   // owns Esc / Enter while open
}

void AGolfRangeHUD::CloseLeaveDialog()
{
	if (!LeaveDialog) { return; }
	bLeaveConfirmOpen = false;
	LeaveDialog->SetVisibility(ESlateVisibility::Collapsed);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();   // hand keys back to gameplay
	}
}

void AGolfRangeHUD::UpdateInRoundHud()
{
	EnsureRoundHud();
	if (!RoundHud) { return; }

	const bool bRound = RoundIsActive();

	// GOL-145: the bottom HUD (telemetry readout + control bar) shows in both the range and a round
	// (RoundHud's top panels layer on top in-round). Only the manual-shot dialog hides it. The pin/putt
	// dev cluster is range-only -- hide it during a round so the in-round readout stays clean.
	if (Panel)
	{
		Panel->SetVisibility(bManualOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		Panel->SetRangeControlsVisible(!bRound);
		Panel->SetMenuButtonVisible(!bRound && !bMenuOpen);   // GOL-147: range-only; RoundHud provides the in-round Menu, the main menu hides it
	}

	const URoundSubsystem* Round = URoundSubsystem::Get(this);
	if (!bRound || InputGated() || !Round)
	{
		RoundHud->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const GolfsimRound::FRoundState& S = Round->GetState();
	if (!S.Schedule.IsValidIndex(S.HoleIndex))
	{
		RoundHud->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const GolfsimRound::FHoleSpec& Hole = S.Schedule[S.HoleIndex];

	FRoundHudData D;
	D.PlayerName = GolfDisplay::ReadPlayerName();
	D.Handicap   = GolfDisplay::ReadHandicap();

	// score vs par over completed holes (+ how many holes are through).
	int32 Total = 0, ParDone = 0;
	for (int32 i = 0; i < S.PerHoleStrokes.Num(); ++i)
	{
		Total += S.PerHoleStrokes[i];
		if (S.Schedule.IsValidIndex(i)) { ParDone += S.Schedule[i].Par; }
	}
	D.ScoreVsPar = Total - ParDone;
	D.HolesThru  = S.PerHoleStrokes.Num();
	D.HoleNum    = Hole.Ref;
	D.Par        = Hole.Par;
	D.HoleYds    = FMath::RoundToInt(FVector::Dist(Hole.TeeWorldLoc, Hole.GreenWorldLoc) / CmPerYd);
	D.Shot       = S.StrokesThisHole + 1;

	// to-pin: live pawn -> pin (XY), in yards.
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			const FVector P = Pawn->GetActorLocation();
			const FVector2D Delta(Hole.PinWorldLoc.X - P.X, Hole.PinWorldLoc.Y - P.Y);
			D.ToPinYd = FMath::RoundToInt(Delta.Size() / CmPerYd);
		}
	}

	// conditions: real sky + time from the env director if one exists on this map (find-only, no spawn
	// -- spawning on the course map would re-light it). Wind + temp stay seams ("--") for GOL-154.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGolfEnvironment> It(World); It; ++It)
		{
			const AGolfEnvironment* Env = *It;
			const TArray<FString> SkyNames = Env->GetSkyPresetNames();
			const TArray<FString> TimeNames = Env->GetTimePresetNames();
			if (SkyNames.IsValidIndex(Env->GetSkyIndex()))   { D.SkyName = SkyNames[Env->GetSkyIndex()]; }
			if (TimeNames.IsValidIndex(Env->GetTimeIndex())) { D.TimeName = TimeNames[Env->GetTimeIndex()]; }
			break;
		}
	}

	RoundHud->SetData(D);
	RoundHud->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void AGolfRangeHUD::ApplyPinDistance(double Yards)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (RoundIsActive())
	{
		// GOL-117: a round owns the pin via URoundPinSubsystem; the spinner / console SetPin
		// would fight it. Skip; the spinner can change again after round.complete.
		UE_LOG(LogTemp, Display, TEXT("AGolfRangeHUD::ApplyPinDistance: round active -- ignoring (pin owned by URoundPinSubsystem)"));
		return;
	}
	const double ClampedYd = FMath::Clamp(Yards, 0.0, PinMaxYd);
	CurrentPinYd = ClampedYd;

	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;   // no pawn yet -- EnsureInputBound retries on the next DrawHUD tick
	}

	// Tee origin: the pawn's X is the tee X (lane runs along world +X). Y forced to 0 so the pin
	// sits on the corridor centerline regardless of where the pawn has wandered to. While in putt
	// mode the pawn has been teleported onto the green, so use the cached tee location instead --
	// otherwise dragging the spinner would re-anchor the pin to the green and walk it away.
	// Source ground Z by tracing down from above the requested XY so the disc sits flush on the
	// visible turf (bTraceComplex=true; same gotcha as the launch trace -- landscape simple-
	// collision is a coarser mip a few cm above the heightfield).
	const FVector TeeLoc = bTeeCached ? TeeOriginalLoc : Pawn->GetActorLocation();
	const double PinWorldXcm = TeeLoc.X + ClampedYd * CmPerYd;
	// GOL-73: CTP pins may sit off the centerline. Clamp the lateral offset to the lane's +/-35 yd tree
	// wall; free-play keeps CurrentPinSideYd = 0, so the pin stays on Y = 0 exactly as before.
	const double SideYd = FMath::Clamp(CurrentPinSideYd, -35.0, 35.0);
	const double PinWorldYcm = SideYd * CmPerYd;
	const FVector ProbeStart(PinWorldXcm, PinWorldYcm, TeeLoc.Z + 5000.0);
	const FVector ProbeEnd(PinWorldXcm, PinWorldYcm, TeeLoc.Z - 5000.0);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GolfsimPinFloorTrace), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(Pawn);
	if (AGolfPinActor* Existing = Pin.Get())
	{
		Params.AddIgnoredActor(Existing);   // never let the disc eat its own placement trace
	}

	double GroundZ = TeeLoc.Z;   // fall back to tee Z if the trace misses (shouldn't happen on the range)
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ProbeStart, ProbeEnd, ECC_WorldStatic, Params))
	{
		GroundZ = Hit.ImpactPoint.Z;
	}

	// Find-or-spawn the pin and slide it into place. Actor origin = disc bottom, so Z = GroundZ.
	AGolfPinActor* PinActor = Pin.Get();
	if (!PinActor)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PinActor = World->SpawnActor<AGolfPinActor>(AGolfPinActor::StaticClass(),
			FVector(PinWorldXcm, PinWorldYcm, GroundZ), FRotator::ZeroRotator, Sp);
		Pin = PinActor;
	}
	else
	{
		PinActor->SetActorLocationAndRotation(FVector(PinWorldXcm, PinWorldYcm, GroundZ), FRotator::ZeroRotator);
	}

	if (Panel)
	{
		// SetPinValue is suppress-guarded, so this is safe even when ApplyPinDistance was
		// triggered by the spinner itself (loop guard inside the panel).
		Panel->SetPinValue(ClampedYd);
		Panel->SetPinActualReadout(ClampedYd);
	}
	// Persistence intentionally dropped -- always start at 150 next session. The Read/WritePin
	// helpers still exist (still unit-tested) for the future "remember last pin" toggle.

	// If we're in putt mode, re-teleport the pawn onto the new green so the toggle tracks the pin.
	if (bPuttMode)
	{
		SetPuttMode(true);
	}
}

void AGolfRangeHUD::SetPuttMode(bool bEnabled)
{
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}

	if (bEnabled)
	{
		// Cache the tee pose + club the first time we go on; re-entering putt mode at a new pin
		// distance shouldn't overwrite the original tee (we only restore back to that).
		if (!bTeeCached)
		{
			TeeOriginalLoc = Pawn->GetActorLocation();
			TeeOriginalRot = PC->GetControlRotation();
			TeeOriginalClub = ActiveClub;
			bTeeCached = true;
		}

		AGolfPinActor* PinActor = Pin.Get();
		if (!PinActor)
		{
			return;   // no pin yet -- ApplyPinDistance hasn't run; bail quietly
		}

		// Stand a few yards behind the flag (lower X), face the flag (Yaw 0 = +X). The range corridor
		// is flat, so the pawn's capsule-center Z carries over from the tee unchanged -- no per-pawn
		// ground trace needed at v1. If GOL-110-style undulation lands on the range later, swap this
		// for a downward trace + add a cached pawn-to-floor offset.
		const FVector PinLoc = PinActor->GetActorLocation();
		const double StandoffCm = PuttStandoffYd * CmPerYd;
		const FVector PuttLoc(PinLoc.X - StandoffCm, 0.0, TeeOriginalLoc.Z);
		Pawn->SetActorLocation(PuttLoc);
		PC->SetControlRotation(FRotator::ZeroRotator);   // Yaw 0 -> face +X (toward the flag)

		// Select the Putter (last preset in the bag). SelectClub clamps so a huge index lands on the
		// last entry; the bag layout (driver -> putter) is enforced at the top of this .cpp.
		SelectClub(0x7FFFFFFF);

		// Snap camera back to Tee view so the chase cam doesn't fly off above a green-distance shot.
		if (Panel)
		{
			Panel->SetSelectedCameraIndex(0);
		}
		SetCameraMode(0);

		bPuttMode = true;
		if (Panel)
		{
			Panel->SetPuttMode(true);
		}
	}
	else
	{
		bPuttMode = false;
		if (bTeeCached)
		{
			Pawn->SetActorLocation(TeeOriginalLoc);
			PC->SetControlRotation(TeeOriginalRot);
			SelectClub(TeeOriginalClub);
			bTeeCached = false;
		}
		if (Panel)
		{
			Panel->SetPuttMode(false);
		}
	}
}

// --- GOL-73: closest-to-pin practice mode ------------------------------------------------------

void AGolfRangeHUD::SetPracticeMode(GolfsimPractice::EPracticeMode Mode)
{
	using namespace GolfsimPractice;

	if (RoundIsActive())
	{
		// CTP is a range drill; a round owns the pin + flow. Ignore mode switches mid-round.
		return;
	}
	if (Mode == CtpMode)
	{
		return;
	}

	UPracticeModeSubsystem* Sub = UPracticeModeSubsystem::Get(this);
	UShotHistorySubsystem* History = UShotHistorySubsystem::Get(this);

	// Always clear any in-flight CTP transients on a mode change.
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(CtpRespawnTimer); }
	bCtpAwaitingRespawn = false;
	bCtpScorePending = false;
	if (bCtpPutting) { EndCtpPuttSequence(); }   // restore the tee pose if we were mid-putt-out

	CtpMode = Mode;

	if (Mode == EPracticeMode::ClosestToPin)
	{
		if (Sub)
		{
			// Seed the session from the subsystem's current config (panel defaults, or a prior console set).
			Sub->StartCtpSession(Sub->GetConfig());
		}
		if (History) { History->SetCurrentMode(TEXT("ctp")); }
		if (Panel)
		{
			Panel->SetCtpControlsVisible(true);
			Panel->SetRangeControlsVisible(false);   // the free-play pin spinner is CTP's job now
		}
		RefreshCtpScoreboard();
		SpawnNextCtpPin();   // first pin
	}
	else
	{
		if (Sub) { Sub->EndSession(); }
		if (History) { History->SetCurrentMode(TEXT("free")); }
		if (Panel)
		{
			Panel->SetCtpControlsVisible(false);
			Panel->SetRangeControlsVisible(true);
		}
		// Restore the free-play pin: back on the centerline at the spinner's distance.
		CurrentPinSideYd = 0.0;
		ApplyPinDistance(CurrentPinYd);
	}
}

void AGolfRangeHUD::ApplyCtpConfig(double MinYd, double MaxYd, bool bSideOffset, bool bPuttOut, double WithinYd)
{
	using namespace GolfsimPractice;
	UPracticeModeSubsystem* Sub = UPracticeModeSubsystem::Get(this);
	if (!Sub) { return; }

	// Convert the panel's yards into the subsystem's SI config. Keep the rest of the config (gimme).
	FCtpConfig Cfg = Sub->GetConfig();
	Cfg.MinM        = FMath::Min(MinYd, MaxYd) * MetersPerYard;
	Cfg.MaxM        = FMath::Max(MinYd, MaxYd) * MetersPerYard;
	Cfg.bSideOffset = bSideOffset;
	Cfg.bPuttOut    = bPuttOut;
	Cfg.PuttWithinM = WithinYd * MetersPerYard;
	Sub->SetConfig(Cfg);

	// Reflect the (possibly reordered) min/max back to the panel so the spinboxes don't drift.
	if (Panel)
	{
		Panel->SetCtpConfigValues(Cfg.MinM / MetersPerYard, Cfg.MaxM / MetersPerYard,
			bSideOffset, bPuttOut, WithinYd);
	}
}

void AGolfRangeHUD::SpawnNextCtpPin()
{
	using namespace GolfsimPractice;
	UPracticeModeSubsystem* Sub = UPracticeModeSubsystem::Get(this);
	if (!Sub) { return; }

	const FCtpPin NextPin = Sub->NextPin();
	CurrentPinYd     = NextPin.DistanceM   / MetersPerYard;
	CurrentPinSideYd = NextPin.SideOffsetM / MetersPerYard;
	ApplyPinDistance(CurrentPinYd);   // uses CurrentPinSideYd for the lateral placement
	if (Panel) { Panel->SetCtpPinInfo(CurrentPinYd, CurrentPinSideYd); }   // tell the player the target
	bCtpAwaitingRespawn = false;
}

void AGolfRangeHUD::OnCtpShotSettled(AGolfBallActor* Ball)
{
	using namespace GolfsimPractice;
	AGolfPinActor* PinActor = Pin.Get();
	if (!Ball || !PinActor)
	{
		return;
	}
	UPracticeModeSubsystem* Sub = UPracticeModeSubsystem::Get(this);
	if (!Sub) { return; }

	const FVector BallWorld = Ball->GetActorLocation();
	const FVector PinWorld  = PinActor->GetActorLocation();
	const double DistM = ScoreDistanceM(BallWorld, PinWorld);

	// Result line: ball-rest -> pin, held ~2 s so it's readable before the next pin spawns.
	if (UWorld* World = GetWorld())
	{
		DrawDebugLine(World, BallWorld, PinWorld, FColor(111, 226, 118), /*persistent=*/false,
			/*life=*/2.0f, /*depthPriority=*/0, /*thickness=*/3.0f);
	}

	const FCtpConfig& Cfg = Sub->GetConfig();

	if (bCtpPutting)
	{
		// Playing it out -- putts are practice only, never scored. The CTP score is the approach's
		// distance from the pin (already recorded below when the approach settled). Putting out just
		// lets the player finish the hole for realism, then the next pin spawns.
		if (GolfsimRound::IsWithinGimme(BallWorld, PinWorld, Cfg.GimmeRadiusFt))
		{
			EndCtpPuttSequence();
			StartCtpRespawnTimer();
		}
		else
		{
			TeleportPawnForPutt(BallWorld, PinWorld);   // putt again from the new lie
		}
		return;
	}

	// Approach shot -- THIS is the closest-to-pin score (how close the approach finished to the pin).
	// Always recorded by distance; putt-out, if on, is a separate unscored "play it out" sequence.
	Sub->RecordCarry(DistM);
	RefreshCtpScoreboard();
	if (Cfg.bPuttOut && DistM <= Cfg.PuttWithinM)
	{
		TeleportPawnForPutt(BallWorld, PinWorld);   // close enough to putt -> play it out (not scored)
		bCtpPutting = true;
	}
	else
	{
		StartCtpRespawnTimer();
	}
}

void AGolfRangeHUD::TeleportPawnForPutt(const FVector& BallWorld, const FVector& PinWorld)
{
	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}

	// Cache the tee pose once so EndCtpPuttSequence can restore it (shared with SetPuttMode's members;
	// the manual putt checkbox is hidden in CTP, so there's no contention).
	if (!bTeeCached)
	{
		TeeOriginalLoc = Pawn->GetActorLocation();
		TeeOriginalRot = PC->GetControlRotation();
		TeeOriginalClub = ActiveClub;
		bTeeCached = true;
	}

	// Stand a short standoff behind the ball along the ball->pin line, facing the pin. Flat range, so
	// the pawn's capsule Z carries over from the tee (same simplification as SetPuttMode).
	FVector ToPin = PinWorld - BallWorld;
	ToPin.Z = 0.0;
	const FVector Dir = ToPin.IsNearlyZero() ? FVector::ForwardVector : ToPin.GetSafeNormal();
	constexpr double PuttStandoffCm = 1.5 * 91.44;   // ~1.5 yd behind the lie
	const FVector Stand = BallWorld - Dir * PuttStandoffCm;
	Pawn->SetActorLocation(FVector(Stand.X, Stand.Y, TeeOriginalLoc.Z));
	PC->SetControlRotation(Dir.Rotation());

	SelectPutterIfAvailable();
}

void AGolfRangeHUD::EndCtpPuttSequence()
{
	bCtpPutting = false;

	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (PC && Pawn && bTeeCached)
	{
		Pawn->SetActorLocation(TeeOriginalLoc);
		PC->SetControlRotation(TeeOriginalRot);
		SelectClub(TeeOriginalClub);
		bTeeCached = false;
	}
}

void AGolfRangeHUD::StartCtpRespawnTimer()
{
	bCtpAwaitingRespawn = true;   // gate fires until the next pin appears
	if (UWorld* World = GetWorld())
	{
		// WeakLambda auto-invalidates if the HUD is torn down before the 2 s fires.
		World->GetTimerManager().SetTimer(CtpRespawnTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { SpawnNextCtpPin(); }),
			2.0f, /*loop=*/false);
	}
}

void AGolfRangeHUD::RefreshCtpScoreboard()
{
	using namespace GolfsimPractice;
	UPracticeModeSubsystem* Sub = UPracticeModeSubsystem::Get(this);
	if (!Sub || !Panel) { return; }

	const FCtpSession& S = Sub->GetSession();
	const int32 Count = AttemptCount(S);

	// CTP score is always distance-from-pin (yards). Putt-out, if on, is unscored play, so it never
	// turns these into stroke counts.
	auto YdStr = [](double M) { return FString::Printf(TEXT("%.1f yd"), M / MetersPerYard); };
	FString ThisStr = TEXT("-"), BestStr = TEXT("-"), AvgStr = TEXT("-");
	if (Count > 0)
	{
		ThisStr = YdStr(LastDistanceM(S));
		BestStr = YdStr(BestDistanceM(S));
		AvgStr  = YdStr(AvgDistanceM(S));
	}
	Panel->SetCtpScore(ThisStr, BestStr, AvgStr, Count);
}

// --- GOL-65: shot-history table + previous-sessions list ---------------------------------------

void AGolfRangeHUD::EnsureHistoryPanel()
{
	if (HistoryPanel)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	HistoryPanel = CreateWidget<UShotHistoryPanel>(PC, UShotHistoryPanel::StaticClass());
	if (!HistoryPanel)
	{
		return;
	}
	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	HistoryPanel->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseHistoryPanel(); }
	};

	// Live append: only repaint while we're viewing the current session. Past-session snapshots are
	// loaded once per pick and stay frozen.
	if (UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(this))
	{
		Sub->OnEntriesChanged.AddWeakLambda(this, [this]()
		{
			if (HistoryPanel && bHistoryOpen && !bHistoryFromList)
			{
				if (UShotHistorySubsystem* S = UShotHistorySubsystem::Get(this))
				{
					HistoryPanel->SetSession(TEXT("Current session"), S->GetEntries());
				}
			}
		});
	}

	HistoryPanel->AddToViewport(35);   // above the previous-sessions list (which sits at 32 over the main menu)
	HistoryPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::EnsureSessionsList()
{
	if (SessionsList)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	SessionsList = CreateWidget<UPreviousSessionsList>(PC, UPreviousSessionsList::StaticClass());
	if (!SessionsList)
	{
		return;
	}
	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	SessionsList->OnSessionPicked = [WeakThis](const FString& Id)
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenHistoryForSession(Id, /*bFromList=*/true); }
	};
	SessionsList->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseSessionsList(); }
	};
	SessionsList->AddToViewport(32);   // above the main menu (30), below the history panel (35)
	SessionsList->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::ToggleHistoryPanel()
{
	// In-range H key: defensive guard against opening over the settings modal.
	if (bSettingsOpen) { return; }
	if (bHistoryOpen)
	{
		CloseHistoryPanel();
		return;
	}
	EnsureHistoryPanel();
	if (!HistoryPanel) { return; }
	UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(this);
	HistoryPanel->SetSession(TEXT("Current session"), Sub ? Sub->GetEntries() : TArray<FShotHistoryEntry>{});
	bHistoryOpen = true;
	bHistoryFromList = false;
	HistoryPanel->SetVisibility(ESlateVisibility::Visible);
}

void AGolfRangeHUD::OpenPreviousSessionsList()
{
	EnsureSessionsList();
	if (!SessionsList) { return; }

	TArray<FPreviousSessionInfo> RowList;
	if (UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(this))
	{
		const TArray<FString> Ids = Sub->ListPastSessionIds();
		RowList.Reserve(Ids.Num());
		for (const FString& Id : Ids)
		{
			FPreviousSessionInfo R;
			R.SessionId = Id;
			// Cheap shot-count via parse. Acceptable for the small N of past sessions; revisit if/when
			// session counts get huge.
			const int32 Count = Sub->LoadSession(Id).Num();
			R.DisplayLabel = FString::Printf(TEXT("%s   (%d shot%s)"),
				*Id, Count, Count == 1 ? TEXT("") : TEXT("s"));
			RowList.Add(MoveTemp(R));
		}
	}
	SessionsList->SetSessions(RowList);
	bSessionsListOpen = true;
	SessionsList->SetVisibility(ESlateVisibility::Visible);
}

void AGolfRangeHUD::OpenHistoryForSession(const FString& SessionId, bool bFromList)
{
	EnsureHistoryPanel();
	if (!HistoryPanel) { return; }
	UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(this);
	const TArray<FShotHistoryEntry> Entries = Sub ? Sub->LoadSession(SessionId) : TArray<FShotHistoryEntry>{};
	HistoryPanel->SetSession(SessionId, Entries);
	bHistoryOpen = true;
	bHistoryFromList = bFromList;
	HistoryPanel->SetVisibility(ESlateVisibility::Visible);
	// Hide the list underneath while the panel's up; closing the panel will re-show it (or the menu).
	if (bFromList && SessionsList)
	{
		SessionsList->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AGolfRangeHUD::CloseHistoryPanel()
{
	if (!HistoryPanel || !bHistoryOpen) { return; }
	bHistoryOpen = false;
	HistoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (bHistoryFromList)
	{
		// Return to the list. The main menu is still mounted underneath the list.
		if (SessionsList && bSessionsListOpen)
		{
			SessionsList->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// Defensive: list got dropped somehow -- rebuild it from the main menu path.
			OpenPreviousSessionsList();
		}
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	bHistoryFromList = false;
}

void AGolfRangeHUD::CloseSessionsList()
{
	if (!SessionsList || !bSessionsListOpen) { return; }
	bSessionsListOpen = false;
	SessionsList->SetVisibility(ESlateVisibility::Collapsed);
	// Main menu is still mounted underneath -- nothing to do; control returns to it automatically.
}

void AGolfRangeHUD::EnsureRoundSetup()
{
	if (RoundSetup) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) { return; }
	RoundSetup = CreateWidget<URoundSetupWizard>(PC, URoundSetupWizard::StaticClass());
	if (!RoundSetup) { return; }

	// Seed the Course step from the lightweight registry: the cooked course (selectable) + disabled
	// placeholders. Names stay trademark-safe per GOL-20.
	RoundSetup->SetCourses(GolfCourseRegistry::All());

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	RoundSetup->OnTeeOff = [WeakThis](const FString& CourseId, const FRoundConfig& Config)
	{
		AGolfRangeHUD* HUD = WeakThis.Get();
		if (!HUD) { return; }
		// GOL-143: persist the single player's name + handicap (the scorecard + menu chip read these).
		if (Config.Players.Num() > 0)
		{
			const FRoundPlayer& P = Config.Players[0];
			if (!P.Name.IsEmpty()) { GolfDisplay::WritePlayerName(P.Name); }
			GolfDisplay::WriteHandicap(P.Handicap);
		}
		// Close both modals before kicking off the round so the load-map transition starts clean.
		HUD->CloseRoundSetup();
		HUD->DismissMainMenu();
		if (URoundSubsystem* Sub = URoundSubsystem::Get(HUD))
		{
			// Difficulty stays Normal (scoring moves to GOL-69); Config carries the live Holes subset.
			Sub->StartRound(CourseId, EGolfDifficulty::Normal, Config);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("RoundSetup: no URoundSubsystem; round not started"));
		}
	};
	RoundSetup->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->CloseRoundSetup(); }
	};

	RoundSetup->AddToViewport(35);   // above the main menu (30) + sessions list (32)
	RoundSetup->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::OpenRoundSetup()
{
	EnsureRoundSetup();
	if (!RoundSetup) { return; }
	RoundSetup->ResetToFirstStep();   // reopen always starts at the Course step with a clean selection
	bRoundSetupOpen = true;
	RoundSetup->SetVisibility(ESlateVisibility::Visible);
	RoundSetup->SetKeyboardFocus();   // wizard owns Enter/Esc while open; CloseRoundSetup hands focus back
}

void AGolfRangeHUD::CloseRoundSetup()
{
	if (!RoundSetup || !bRoundSetupOpen) { return; }
	bRoundSetupOpen = false;
	RoundSetup->SetVisibility(ESlateVisibility::Collapsed);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

// --- GOL-73: practice-drill picker (mirrors the round-setup trio above) -------------------------

void AGolfRangeHUD::EnsurePracticeSetup()
{
	if (PracticeSetup) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) { return; }

	PracticeSetup = CreateWidget<UPracticeSetup>(PC, UPracticeSetup::StaticClass());
	if (!PracticeSetup) { return; }

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	PracticeSetup->OnStartCtp = [WeakThis]()
	{
		AGolfRangeHUD* HUD = WeakThis.Get();
		if (!HUD) { return; }
		HUD->ClosePracticeSetup();
		HUD->DismissMainMenu();
		HUD->SetPracticeMode(GolfsimPractice::EPracticeMode::ClosestToPin);
	};
	PracticeSetup->OnClose = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ClosePracticeSetup(); }
	};

	PracticeSetup->AddToViewport(35);   // above the main menu (30), same layer as the round wizard
	PracticeSetup->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::OpenPracticeSetup()
{
	EnsurePracticeSetup();
	if (!PracticeSetup) { return; }
	PracticeSetup->ResetSelection();
	bPracticeSetupOpen = true;
	PracticeSetup->SetVisibility(ESlateVisibility::Visible);
	PracticeSetup->SetKeyboardFocus();   // owns Enter/Esc while open; ClosePracticeSetup hands focus back
}

void AGolfRangeHUD::ClosePracticeSetup()
{
	if (!PracticeSetup || !bPracticeSetupOpen) { return; }
	bPracticeSetupOpen = false;
	PracticeSetup->SetVisibility(ESlateVisibility::Collapsed);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void AGolfRangeHUD::EnsureScorecardPanel()
{
	if (Scorecard) { return; }
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) { return; }
	Scorecard = CreateWidget<UScorecardPanel>(PC, UScorecardPanel::StaticClass());
	if (!Scorecard) { return; }

	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	Scorecard->OnBackToMenu = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ReturnToMainMenu(); }
	};

	Scorecard->AddToViewport(36);   // above sessions list (32), picker (35), main menu (30)
	Scorecard->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::OpenScorecardForState(const TArray<int32>& Pars, const TArray<int32>& Strokes)
{
	EnsureScorecardPanel();
	if (!Scorecard) { return; }
	Scorecard->SetScorecard(GolfDisplay::ReadPlayerName(), Pars, Strokes);
	bScorecardOpen = true;
	Scorecard->SetVisibility(ESlateVisibility::Visible);
	int32 TotalStrokes = 0;
	for (int32 S : Strokes) { TotalStrokes += S; }
	UE_LOG(LogTemp, Display, TEXT("AGolfRangeHUD: scorecard opened (%d holes, %d total strokes)"),
		Strokes.Num(), TotalStrokes);
}

void AGolfRangeHUD::CloseScorecardPanel()
{
	if (!Scorecard || !bScorecardOpen) { return; }
	bScorecardOpen = false;
	Scorecard->SetVisibility(ESlateVisibility::Collapsed);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void AGolfRangeHUD::ReturnToMainMenu()
{
	// Close any open modal so the next world's HUD doesn't inherit stale state.
	CloseScorecardPanel();
	if (bSettingsOpen) { ToggleSettingsMenu(); }
	CloseRoundSetup();
	// Defensive: round may or may not be active. AbandonRound is a no-op when !bActive.
	if (URoundSubsystem* Sub = URoundSubsystem::Get(this))
	{
		Sub->AbandonRound();
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("PracticeRange")));
}

// --- GOL-67: Game / Simulation mode + swing meter ----------------------------------------------

namespace
{
	// Translate the file-scope FClubPreset row to the swing component's club preset (same fields).
	// Lives here so the swing component header stays UE-free + testable without dragging GBag in.
	GolfsimKeyboardSwing::FClubPreset ToSwingClub(const FClubPreset& C)
	{
		GolfsimKeyboardSwing::FClubPreset Out;
		Out.Name = C.Name;
		Out.NominalSpeedMps = C.SpeedMps;
		Out.LaunchDeg = C.LaunchDeg;
		Out.SpinRpm = C.SpinRpm;
		return Out;
	}
}

void AGolfRangeHUD::SetInputMode(EInputMode NewMode)
{
	CurrentInputMode = NewMode;
	const bool bGame = (CurrentInputMode == EInputMode::Game);

	// Mode swap resets any in-flight swing -- otherwise toggling mid-swing would leave the bars
	// at a dangling locked-power state. (GOL-145: mode is now derived from the LM status; the control
	// bar's pill + button label are set by ApplyLaunchMonitorState, not here.)
	SwingState = GolfsimKeyboardSwing::FState{};

	if (bGame)
	{
		// Lazy-create the swing meter the first time we enter Game mode.
		if (!SwingMeter)
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				SwingMeter = CreateWidget<USwingMeterWidget>(PC, USwingMeterWidget::StaticClass());
				if (SwingMeter)
				{
					SwingMeter->SetSweetSpot(SwingConfig.SweetSpotLow, SwingConfig.SweetSpotHigh);
					SwingMeter->AddToViewport(15);   // above the range panel, below modals
				}
			}
		}
		if (SwingMeter)
		{
			SwingMeter->ResetMeter();   // dashes, marker home, neutral colors, default prompt
			SwingMeter->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else if (SwingMeter)
	{
		SwingMeter->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AGolfRangeHUD::ApplyLaunchMonitorState(ELaunchMonitorStatus Status, const FString& Name)
{
	// §6: a connected device owns the shot stream (Simulation mode); everything else is keyboard Game
	// mode. SetInputMode flips the swing meter visibility AND resets any in-progress swing.
	SetInputMode(Status == ELaunchMonitorStatus::Online ? EInputMode::Simulation : EInputMode::Game);
	if (Panel)
	{
		Panel->SetLaunchMonitorStatus(Status, Name);
		Panel->SetPrimaryActionLabel(Status == ELaunchMonitorStatus::Online ? TEXT("Sim shot") : TEXT("Swing"));
		// Ball-ready only makes sense for a live device; clear it whenever we're not Online (GOL-186).
		if (Status != ELaunchMonitorStatus::Online) { Panel->SetLaunchMonitorReady(false); }
	}
}

void AGolfRangeHUD::ApplyLaunchMonitorReady(bool bReady)
{
	if (Panel) { Panel->SetLaunchMonitorReady(bReady); }
}

void AGolfRangeHUD::TriggerPrimaryAction()
{
	// The Swing / Sim-shot button is the click-equivalent of Space: OnSpaceForCurrentMode routes by
	// mode (Game -> advance the swing meter; Simulation -> ask the active LM to emit) with the same
	// modal + ball-in-flight guards.
	OnSpaceForCurrentMode();
}

void AGolfRangeHUD::SetSwingDifficulty(EGolfDifficulty D)
{
	ActiveDifficulty = D;
	SwingConfig.Profile = GolfsimKeyboardSwing::FSwingDifficultyProfile::For(D);
	// SweetSpot stays constant across profiles (band thresholds, not tuning knobs), so the meter
	// widget's overlay needs no refresh. If a future profile shifts SweetSpotLow/High, mirror to
	// SwingMeter->SetSweetSpot here.
}

void AGolfRangeHUD::OnSpaceForCurrentMode()
{
	if (InputGated()) { return; }
	// GOL-73: swallow fires during the ~2 s read-the-result gap after a CTP attempt, until the next
	// pin spawns. Putt-out fires are NOT gated (bCtpAwaitingRespawn is false mid-putt-sequence).
	if (bCtpAwaitingRespawn) { return; }
	// GOL-120: no turbo-firing -- block Space while a ball is mid-flight. Without this you can
	// rapid-fire the swing meter and the visualizer's previous trajectory gets cut short by the
	// next shot's PlayTrajectory. Also implicitly waits for URoundTeeUpSubsystem's between-shot
	// teleport (which fires on settle) so the next swing actually launches from the new rest spot.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGolfBallActor> It(World); It; ++It)
		{
			if (It->IsPlaying()) { return; }
			break;   // one ball
		}
	}
	if (CurrentInputMode == EInputMode::Simulation)
	{
		// GOL-145 §6: a connected LM owns the shot stream -> ask it to emit (OpenFlight mock mode
		// round-trips it back). Falls back to a random shot only if no driver is active (e.g. the dev
		// console forced Simulation without a device).
		if (ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(this))
		{
			if (ULaunchMonitorDriver* D = Mgr->GetActiveDriver()) { D->RequestSimulatedShot(); return; }
		}
		FireRandom();
		return;
	}

	// Game mode: step the swing state machine. The HUD owns the active-club lookup; the namespace
	// just maps Power% + Accuracy% + club -> shot fields.
	const FClubPreset& C = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
	const GolfsimKeyboardSwing::FClubPreset SwingClub = ToSwingClub(C);
	GolfsimKeyboardSwing::FResolution Res;
	const bool bResolved = GolfsimKeyboardSwing::OnSpace(SwingState, SwingConfig, SwingClub, Res);

	// Drive the meter feedback for the *new* state so the player knows what the next press does.
	// OnSpace has already advanced SwingState.State: press 1 -> Power, press 2 -> Accuracy,
	// press 3 -> Idle (resolved, handled below after the bResolved check).
	if (SwingMeter)
	{
		switch (SwingState.State)
		{
			case GolfsimKeyboardSwing::EState::Power:
				SwingMeter->ResetMeter();   // press 1: clear any prior result, start fresh
				SwingMeter->SetHintText(TEXT("to set your power"));
				break;
			case GolfsimKeyboardSwing::EState::Accuracy:
				SwingMeter->OnPowerLocked();   // press 2: power locked, accuracy phase begins
				SwingMeter->SetHintText(TEXT("— stop in the green"));
				break;
			default:
				break;
		}
	}

	if (!bResolved) { return; }   // press 1 or 2 -- no shot yet

	if (Res.bWhiffed)
	{
		// Whiff: no shot, just a log line for now. Sound/visual feedback is a polish follow-up.
		UE_LOG(LogTemp, Display, TEXT("golfsim Game: WHIFF (power=%.2f, accuracy=%.2f)"),
			SwingState.Power, SwingState.Accuracy);
		if (SwingMeter)
		{
			SwingMeter->ResetMeter();
			SwingMeter->SetHintText(TEXT("Whiff — try again"));
		}
		return;
	}

	// Resolved (press 3): show the verdict on the meter, then publish the shot to the physics sim.
	// The "Pure / Push / Pull" read is qualitative (which side of the zone the marker stopped) -- the
	// real offline yardage comes from physics and lands in the launch-monitor readout.
	if (SwingMeter)
	{
		const double Mid = 0.5 * (SwingConfig.SweetSpotLow + SwingConfig.SweetSpotHigh);
		const double HalfZone = 0.5 * (SwingConfig.SweetSpotHigh - SwingConfig.SweetSpotLow);
		const bool bInZone = SwingState.Accuracy >= SwingConfig.SweetSpotLow
		                  && SwingState.Accuracy <= SwingConfig.SweetSpotHigh;
		SwingMeter->OnAccuracyResult(bInZone, SwingState.Accuracy);

		const bool bRight = SwingState.Accuracy > Mid;
		const double Over = FMath::Abs(SwingState.Accuracy - Mid) - HalfZone;   // >0 -> outside the zone
		FString Prompt;
		if (bInZone)            { Prompt = TEXT("Striped it — dead straight"); }
		else if (Over <= 0.06)  { Prompt = bRight ? TEXT("Slight fade right") : TEXT("Slight draw left"); }
		else                    { Prompt = bRight ? TEXT("Pushed right") : TEXT("Pushed left"); }
		SwingMeter->SetHintText(Prompt);
	}

	UE_LOG(LogTemp, Display,
		TEXT("golfsim Game: %s power=%.2f accuracy=%.2f -> speed=%.1f m/s az=%.1f side=%.0f rpm"),
		C.Name, SwingState.Power, SwingState.Accuracy,
		Res.BallSpeedMps, Res.AzimuthDeg, Res.SidespinRpm);

	PublishShotTaken(Res.BallSpeedMps, Res.LaunchAngleDeg, Res.AzimuthDeg,
		Res.BackspinRpm, Res.SidespinRpm, FString(C.Name), TEXT("keyboard-swing"));
}

void AGolfRangeHUD::ToggleCheatSheet()
{
	// Don't pop the cheat sheet over a settings modal -- it's a peek, not a stack of modals.
	if (bSettingsOpen) { return; }

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	if (!CheatSheet)
	{
		CheatSheet = CreateWidget<UCheatSheetPanel>(PC, UCheatSheetPanel::StaticClass());
		if (!CheatSheet) { return; }
		TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
		CheatSheet->OnClose = [WeakThis]()
		{
			if (AGolfRangeHUD* HUD = WeakThis.Get())
			{
				HUD->bCheatOpen = false;
				if (HUD->CheatSheet) { HUD->CheatSheet->SetVisibility(ESlateVisibility::Collapsed); }
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().SetAllUserFocusToGameViewport();
				}
			}
		};
		CheatSheet->AddToViewport(36);   // above the history panel + previous-sessions list
		CheatSheet->SetVisibility(ESlateVisibility::Collapsed);
	}

	bCheatOpen = !bCheatOpen;
	CheatSheet->SetVisibility(bCheatOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bCheatOpen && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void AGolfRangeHUD::OpenCreditsSection()
{
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	if (!bSettingsOpen)
	{
		ToggleSettingsMenu();
	}
	SettingsMenu->ShowSection(4);   // GOL-140: Credits is now the 5th rail section (0=Graphics..4=Credits)
}

void AGolfRangeHUD::EnsureMainMenu()
{
	if (MainMenu)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	MainMenu = CreateWidget<UMainMenu>(PC, UMainMenu::StaticClass());
	if (!MainMenu)
	{
		return;
	}
	TWeakObjectPtr<AGolfRangeHUD> WeakThis(this);
	MainMenu->OnPlayRange = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get())
		{
			// GOL-73: "Range" is plain free-fire -- leave any active CTP drill first.
			HUD->SetPracticeMode(GolfsimPractice::EPracticeMode::Free);
			HUD->DismissMainMenu();
		}
	};
	// GOL-65: "Previous Sessions" opens the session-picker list over the main menu. Picking a
	// session opens the full history table; closing the table returns to the list; closing the
	// list returns to the main menu underneath. H key in-range opens current-session only.
	MainMenu->OnPreviousSessions = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenPreviousSessionsList(); }
	};
	// GOL-141: Play Course opens the round-setup wizard over the main menu. Tee Off starts the
	// round via URoundSubsystem; auto-load (GOL-117) handles the level transition.
	MainMenu->OnPlayCourse = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenRoundSetup(); }
	};
	// GOL-73: the Practice tile opens the drill picker over the menu.
	MainMenu->OnPlayPractice = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenPracticeSetup(); }
	};
	// GOL-139: the bento Settings tile opens settings above the menu; the player chip shows the real name.
	MainMenu->OnSettings = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenSettingsOverMenu(); }
	};
	MainMenu->SetPlayerName(GolfDisplay::ReadPlayerName());
	MainMenu->AddToViewport(30);   // above the panel + manual dialog; the settings modal sits above it (40)
	MainMenu->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::ShowMainMenu()
{
	EnsureMainMenu();
	if (!MainMenu)
	{
		return;
	}
	// Greys "Previous Sessions" when no past sessions exist on disk. Recomputed at show time so
	// quitting + restarting reflects the just-written previous session.
	if (UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(this))
	{
		MainMenu->SetPreviousSessionsCount(Sub->ListPastSessionIds().Num());
	}
	bMenuOpen = true;
	MainMenu->SetVisibility(ESlateVisibility::Visible);
	MainMenu->SetKeyboardFocus();   // GOL-139: route 1-4 / Enter / Esc to the menu's NativeOnKeyDown
}

void AGolfRangeHUD::DismissMainMenu()
{
	if (!MainMenu)
	{
		return;
	}
	bMenuOpen = false;
	MainMenu->SetVisibility(ESlateVisibility::Collapsed);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();   // hand keys back to the range
	}
}

void AGolfRangeHUD::OnShotOutcome(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotOutcome)
	{
		return;
	}
	const FShotOutcomeEvent& Out = static_cast<const FShotOutcomeEvent&>(Event);

	// Launch the ball from the live tee + current aim, so any producer's shot flies from the player.
	AGolfBallActor* Ball = nullptr;
	if (UWorld* World = GetWorld())
	{
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		if (APlayerController* PC = GetOwningPlayerController())
		{
			Rot = PC->GetControlRotation();
			Rot.Pitch = 0.f;
			Rot.Roll = 0.f;
			FollowDownrangeDir = Rot.Vector();   // capture the aim now so a curving ball doesn't swing the chase
			if (APawn* Pawn = PC->GetPawn())
			{
				Loc = Pawn->GetActorLocation();
				// Launch from the floor (the tee surface), not the pawn's elevated capsule center, so the
				// ball flies, lands, and rolls on the ground instead of floating at eye height. Trace down
				// against world static (the landscape); the ball has collision disabled so it can't self-hit.
				// Use bTraceComplex=true: the landscape's simple-collision representation is a coarser mip
				// that can sit a few cm above the visual heightfield, which made the ball appear to bounce/
				// roll a few cm above the visible turf. Same lesson the water script learned (cookbook).
				FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GolfRangeFloorTrace));
				TraceParams.bTraceComplex = true;
				TraceParams.AddIgnoredActor(Pawn);   // skip the pawn capsule even if its channel changes
				FHitResult Ground;
				if (World->LineTraceSingleByChannel(Ground, Loc, Loc - FVector(0.f, 0.f, 100000.f),
					ECC_WorldStatic, TraceParams))
				{
					Loc.Z = Ground.ImpactPoint.Z + BallRestHeightUU;
				}
			}
		}
		Ball = GetOrSpawnBallAt(World, Loc, Rot);
		if (Ball)
		{
			Ball->PlayTrajectory(Out.Trajectory);
		}

		// Ball-strike SFX: one-shot at the tee as the ball launches (CC0 SW_BallStrike). Lazy-loaded
		// once; plays for every shot source (keyboard / LM / manual) since they all land here.
		if (!StrikeSound)
		{
			StrikeSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/BallStrike/SW_BallStrike.SW_BallStrike"));
		}
		if (StrikeSound)
		{
			UGameplayStatics::PlaySoundAtLocation(World, StrikeSound, Loc);
		}
	}

	const double CarryYd   = Out.CarryM         * 1.0936132983;   // m -> yd
	const double TotalYd   = Out.TotalM         * 1.0936132983;   // carry + ground roll (GOL-9)
	const double OfflineYd = Out.LateralOffsetM * 1.0936132983;   // signed; + = right (at the rest position)

	// The outcome carries this shot's source metrics, so they're the current shot's -- no one-shot lag.
	// OpenFlight may report no club -> show "-".
	const FString ClubName = Out.Club.IsEmpty() ? FString(TEXT("-")) : Out.Club;
	const double SpeedMph   = Out.BallSpeedMps * 2.2369362921;    // m/s -> mph
	const double LaunchDeg  = Out.LaunchAngleDeg;
	const double SpinRpm    = Out.BackspinRpm;

	// Carry counts up as the ball flies (Tick reads the ball's live downrange), then snaps to CarryYd
	// on landing. The other metrics are known now, so show them immediately.
	AnimBall           = Ball;
	AnimClub           = ClubName;
	AnimSpeedMph       = SpeedMph;
	AnimLaunchDeg      = LaunchDeg;
	AnimSpinRpm        = SpinRpm;
	bAnimSpinEstimated = Out.bSpinEstimated;
	AnimOfflineYd      = OfflineYd;
	AnimTargetCarryYd  = CarryYd;
	AnimTargetTotalYd  = TotalYd;
	bCarryAnimating    = true;

	// GOL-73: in CTP every shot (approach or putt) scores once it settles; Tick resolves it. Never
	// during a round -- a round owns the pin via URoundPinSubsystem, not the range CTP pin.
	if (IsCtpActive() && !RoundIsActive())
	{
		bCtpScorePending = true;
	}

	// Follow cam: a new shot re-chases from the tee (even if we were parked on the last ball). Snap the
	// cam to the tee framing first so the view blends from there, not in a long pan across the range.
	if (bFollowCam && Ball && Ball->IsPlaying())
	{
		if (APlayerController* PC = GetOwningPlayerController())
		{
			if (ACameraActor* Cam = GetOrSpawnFollowCam())
			{
				FVector CamLoc;
				FRotator CamRot;
				ComputeFollowPose(Ball->GetActorLocation(), FollowDownrangeDir, CamLoc, CamRot);
				Cam->SetActorLocationAndRotation(CamLoc, CamRot);
				PC->SetViewTargetWithBlend(Cam, FollowViewBlend);
			}
		}
		bFollowChasing = true;
		bFollowParked = false;
		FollowIdleSeconds = 0.f;   // new shot -> restart the idle-return countdown
	}

	if (Panel)
	{
		// First frame: static metrics now, carry + total starting at the ball's live downrange (~0 at
		// launch). If the trajectory was invalid (not playing), show the exact finals immediately.
		const double StartCarryYd = (Ball && Ball->IsPlaying())
			? Ball->GetCurrentCarryMeters() * 1.0936132983 : CarryYd;
		const double StartTotalYd = (Ball && Ball->IsPlaying()) ? StartCarryYd : TotalYd;
		Panel->UpdateMetrics(ClubName, SpeedMph, LaunchDeg, SpinRpm,
			StartCarryYd, StartTotalYd, OfflineYd, Out.bSpinEstimated);
	}
	if (ManualDialog && bManualOpen)
	{
		ManualDialog->SetResult(CarryYd, OfflineYd);   // live "dial in a shot" feedback (final)
	}
	UE_LOG(LogTemp, Display,
		TEXT("golfsim range: shot.outcome carry=%.1fm lateral=%.1fm -> ball played, panel updated"),
		Out.CarryM, Out.LateralOffsetM);
}

ACameraActor* AGolfRangeHUD::GetOrSpawnFollowCam()
{
	if (ACameraActor* Existing = FollowCam.Get())
	{
		return Existing;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Cam = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Cam)
	{
		if (UCameraComponent* CamComp = Cam->GetCameraComponent())
		{
			CamComp->SetConstraintAspectRatio(false);   // fill the viewport (no 16:9 letterbox)
		}
	}
	FollowCam = Cam;
	return Cam;
}

void AGolfRangeHUD::SetCameraMode(int32 Index)
{
	bFollowCam = (Index == 1);
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	if (!bFollowCam)
	{
		// Tee: blend back to the pawn's view (the current fixed-tee camera).
		bFollowChasing = false;
		bFollowParked = false;
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, FollowViewBlend);
		}
		return;
	}

	// Follow: frame the existing ball now (chase if mid-flight, park if resting); else wait for a shot.
	AGolfBallActor* Ball = AnimBall.Get();
	if (!Ball)
	{
		return;
	}
	if (ACameraActor* Cam = GetOrSpawnFollowCam())
	{
		FVector CamLoc;
		FRotator CamRot;
		ComputeFollowPose(Ball->GetActorLocation(), FollowDownrangeDir, CamLoc, CamRot);
		Cam->SetActorLocationAndRotation(CamLoc, CamRot);
		PC->SetViewTargetWithBlend(Cam, FollowViewBlend);
	}
	bFollowChasing = Ball->IsPlaying();
	bFollowParked = !Ball->IsPlaying();
}

void AGolfRangeHUD::ToggleCameraMode()
{
	if (InputGated()) { return; }
	const int32 Next = bFollowCam ? 0 : 1;   // 0 = Tee, 1 = Follow
	if (Panel) { Panel->SetSelectedCameraIndex(Next); }
	SetCameraMode(Next);
}

void AGolfRangeHUD::OrbitPressed()
{
	if (!bFollowCam)
	{
		return;   // orbit only applies to the follow cam
	}
	ACameraActor* Cam = FollowCam.Get();
	AGolfBallActor* Ball = AnimBall.Get();
	if (!Cam || !Ball)
	{
		return;
	}
	// Seed the orbit from the camera's current offset so the drag starts where we're already looking.
	const FVector Offset = Cam->GetActorLocation() - Ball->GetActorLocation();
	OrbitDistUU = FMath::Max((float)Offset.Size(), 100.f);
	OrbitYawDeg = (float)FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X));
	OrbitPitchDeg = (float)FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Offset.Z / OrbitDistUU, -1.0, 1.0)));
	PendingOrbitDX = 0.f;
	PendingOrbitDY = 0.f;
	bOrbiting = true;
}

void AGolfRangeHUD::OrbitReleased()
{
	bOrbiting = false;
	PendingOrbitDX = 0.f;
	PendingOrbitDY = 0.f;
}

void AGolfRangeHUD::OnOrbitYaw(float Value)
{
	if (bOrbiting) { PendingOrbitDX += Value; }
}

void AGolfRangeHUD::OnOrbitPitch(float Value)
{
	if (bOrbiting) { PendingOrbitDY += Value; }
}

void AGolfRangeHUD::UpdateFollowCam(float DeltaSeconds)
{
	if (!bFollowCam)
	{
		return;   // Tee mode -> the pawn is the view target
	}
	ACameraActor* Cam = FollowCam.Get();
	AGolfBallActor* Ball = AnimBall.Get();

	// Orbit (right-mouse drag) takes over when active, around the current ball -- works while parked or
	// mid-flight. Overrides the chase framing for this frame.
	if (bOrbiting && Cam && Ball)
	{
		FollowIdleSeconds = 0.f;   // the player is actively framing -> don't auto-return to the tee
		constexpr float OrbitSens = 0.4f;   // deg per mouse unit
		OrbitYawDeg += PendingOrbitDX * OrbitSens;
		OrbitPitchDeg = FMath::Clamp(OrbitPitchDeg - PendingOrbitDY * OrbitSens, 3.f, 85.f);
		PendingOrbitDX = 0.f;
		PendingOrbitDY = 0.f;

		const float YawR = FMath::DegreesToRadians(OrbitYawDeg);
		const float PitchR = FMath::DegreesToRadians(OrbitPitchDeg);
		const float Horiz = OrbitDistUU * FMath::Cos(PitchR);
		const FVector BallPos = Ball->GetActorLocation();
		const FVector CamPos = BallPos + FVector(Horiz * FMath::Cos(YawR), Horiz * FMath::Sin(YawR),
			OrbitDistUU * FMath::Sin(PitchR));
		Cam->SetActorLocationAndRotation(CamPos, (BallPos - CamPos).Rotation());
		return;
	}

	if (!bFollowChasing)
	{
		// Parked on a settled ball and not orbiting: after FollowIdleReturnSeconds, re-frame the follow
		// cam back onto the tee so the player is set for the next shot (the ball often rests far downrange
		// -- e.g. a long CTP target). We do NOT switch the camera *mode* (Tee/Follow) -- auto-flipping the
		// camera type is jarring; the player owns that choice (C key / dropdown). Just reset the framing.
		if (bFollowParked)
		{
			FollowIdleSeconds += DeltaSeconds;
			if (FollowIdleSeconds >= FollowIdleReturnSeconds)
			{
				FollowIdleSeconds = 0.f;
				bFollowParked = false;   // reframed -> stop counting until the next shot parks
				if (Cam)
				{
					APlayerController* PC = GetOwningPlayerController();
					APawn* Pawn = PC ? PC->GetPawn() : nullptr;
					if (Pawn)
					{
						FRotator Aim = PC->GetControlRotation();
						Aim.Pitch = 0.f; Aim.Roll = 0.f;
						FVector TeeLoc;
						FRotator TeeRot;
						ComputeFollowPose(Pawn->GetActorLocation(), Aim.Vector(), TeeLoc, TeeRot);
						Cam->SetActorLocationAndRotation(TeeLoc, TeeRot);
					}
				}
			}
		}
		return;   // parked / idle and not orbiting -> leave the camera where it is
	}
	if (!Cam || !Ball)
	{
		bFollowChasing = false;
		return;
	}

	const FVector BallPos = Ball->GetActorLocation();
	if (!Ball->IsPlaying())
	{
		// Came to rest: frame the final position and park (frozen until the next shot or a switch to Tee).
		FVector CamLoc;
		FRotator CamRot;
		ComputeFollowPose(BallPos, FollowDownrangeDir, CamLoc, CamRot);
		Cam->SetActorLocationAndRotation(CamLoc, CamRot);
		bFollowChasing = false;
		bFollowParked = true;
		return;
	}

	// Chase: smooth the position toward the ideal pose, but always look straight at the ball so it stays
	// centered even when the cam lags during fast flight.
	FVector DesiredLoc;
	FRotator UnusedRot;
	ComputeFollowPose(BallPos, FollowDownrangeDir, DesiredLoc, UnusedRot);
	const FVector NewLoc = FMath::VInterpTo(Cam->GetActorLocation(), DesiredLoc, DeltaSeconds, FollowInterpSpeed);
	Cam->SetActorLocationAndRotation(NewLoc, (BallPos - NewLoc).Rotation());
}

void AGolfRangeHUD::DrawHUD()
{
	Super::DrawHUD();
	EnsureInputBound();   // still the retry that binds input + creates the panel once the PC exists
	// The UMG panel draws club + metrics. We only add a tiny render-resolution
	// readout (top-left) to sit alongside the stat fps overlay, so fullscreen /
	// 4K perf is easy to gauge. Canvas->SizeX/Y is the actual viewport draw size.
	if (Canvas)
	{
		const FString Res = FString::Printf(TEXT("Res: %d x %d"),
			(int32)Canvas->SizeX, (int32)Canvas->SizeY);
		DrawText(Res, FLinearColor(1.0f, 0.92f, 0.35f), 20.0f, 20.0f);

		// GOL-167: on-HUD FPS counter (top-left, under the resolution) so perf is
		// glanceable while playtesting heavy scenes (e.g. the mixed-forest scatter).
		// EMA-smoothed off the frame delta so the number is steady; color-coded
		// green/amber/red against the 30 FPS floor. Toggle with `golf.ShowFPS`.
		if (CVarGolfShowFps.GetValueOnGameThread() != 0)
		{
			const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
			if (Dt > 0.0f)
			{
				const float Inst = 1.0f / Dt;
				SmoothedFps = (SmoothedFps <= 0.0f) ? Inst : FMath::Lerp(SmoothedFps, Inst, 0.1f);
			}
			const int32 Fps = FMath::RoundToInt(SmoothedFps);
			const float Ms  = (SmoothedFps > 0.0f) ? 1000.0f / SmoothedFps : 0.0f;
			const FLinearColor FpsColor =
				(SmoothedFps >= 55.0f) ? FLinearColor(0.35f, 1.0f, 0.35f) :      // green: smooth
				(SmoothedFps >= 30.0f) ? FLinearColor(1.0f, 0.85f, 0.30f) :      // amber: above floor
				                         FLinearColor(1.0f, 0.35f, 0.30f);       // red: below 30 gate
			DrawText(FString::Printf(TEXT("FPS: %d  (%.1f ms)"), Fps, Ms),
				FpsColor, 20.0f, 40.0f);
		}

		// GOL-67: persistent bottom-left discoverability hint so first-time users find Tab.
		// Hidden while a modal is up to keep the panel/menu the only focus surface.
		if (!InputGated())
		{
			const FString Hint(TEXT("Tab: Key bindings"));
			DrawText(Hint, FLinearColor(0.85f, 0.78f, 0.30f), 20.0f, (float)Canvas->SizeY - 30.0f);
		}

		// GOL-144: the live round readout is now the glass URoundHud (round panel + hole map),
		// driven from Tick -> UpdateInRoundHud(); the old canvas text was removed.
	}
}
