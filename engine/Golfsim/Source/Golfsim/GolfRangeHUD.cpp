#include "GolfRangeHUD.h"

#include "GolfBallActor.h"
#include "GolfRangeEnvironment.h"
#include "ManualShotDialog.h"              // manual-shot dialog (GOL-8)
#include "Range/GolfPinActor.h"            // range target pin (GOL-29)
#include "Session/ShotHistorySubsystem.h"  // session shot history (GOL-65)
#include "ShotHistoryPanel.h"
#include "PreviousSessionsList.h"
#include "CheatSheetPanel.h"
#include "SwingMeterWidget.h"   // GOL-67
#include "Events/EventBusSubsystem.h"      // publish shot.taken / subscribe shot.outcome (GOL-7)
#include "Drivers/LaunchMonitorManager.h"  // active-driver status -> panel dot (GOL-11)
#include "Drivers/LaunchMonitorDriver.h"
#include "Physics/BallFlightTypes.h"       // FBallTrajectory (carried on the outcome event)
#include "Physics/RangeSurface.h"          // ClassifyRangeLie -> the integrator's surface provider (GOL-9)

#include "EngineUtils.h"
#include "Camera/CameraActor.h"     // follow camera (Camera dropdown: Tee / Follow)
#include "Camera/CameraComponent.h" // disable the follow cam's aspect-ratio constraint
#include "Components/InputComponent.h"
#include "SettingsMenu.h"
#include "MainMenu.h"                          // startup main menu (Range / Play Course / Exit)
#include "Framework/Application/SlateApplication.h"
#include "Engine/Canvas.h"          // UCanvas (SizeX/SizeY) for the DrawHUD resolution readout
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/ICursor.h"

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
	AGolfRangeEnvironment* GetOrSpawnRangeEnv(UWorld* World)
	{
		for (TActorIterator<AGolfRangeEnvironment> It(World); It; ++It)
		{
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGolfRangeEnvironment>(AGolfRangeEnvironment::StaticClass(),
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
		EBus->SurfaceProvider = nullptr; // the subsystem outlives this HUD; drop the lie source (GOL-9)
	}
	OutcomeSub = FGolfEventSubscription{};
	EventBusWeak = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AGolfRangeHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateFollowCam(DeltaSeconds);

	// GOL-29: keep retrying the pin placement until the actor exists. EnsureInputBound calls
	// ApplyPinDistance once at panel mount, but on the very first ticks the pawn may not be
	// possessed yet -- ApplyPinDistance early-returns and the pin actor never spawns. Tick
	// re-fires until Pin becomes a valid actor; thereafter this is a no-op every frame.
	if (Panel && !Pin.IsValid() && GetOwningPlayerController() && GetOwningPlayerController()->GetPawn())
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
		UWorld* World = GetWorld();
		if (UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr)
		{
			const FName CursorPath(TEXT("Slate/golf_tee_cursor"));
			const FVector2D HotSpot(0.063, 0.047);
			GVC->SetHardwareCursor(EMouseCursor::Default, CursorPath, HotSpot);
			GVC->SetHardwareCursor(EMouseCursor::Hand, CursorPath, HotSpot);
		}
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
				if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->SetCameraMode(Idx); }
			};

			// GOL-67: Mode dropdown. Default = Game (lower barrier; no LM hardware required). The
			// HUD's SetInputMode flips the swing-meter visibility AND hides the LM dropdown so a
			// keyboard player isn't shown launch-monitor options that aren't relevant.
			Panel->SetModeOptions({ TEXT("Game"), TEXT("Simulation") });
			Panel->SetSelectedModeIndex((int32)CurrentInputMode);
			Panel->OnModeChosen = [WeakThis](int32 Idx)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get())
				{
					HUD->SetInputMode(Idx == 0 ? EInputMode::Game : EInputMode::Simulation);
				}
			};

			// Pin distance (GOL-29). Always start at 150 yd; persistence caused more confusion than
			// it was worth (race + test-pollution corner cases all set the spinner to 0 on next load).
			// The user can drag the spinner to whatever they want during the session. The Tick loop
			// re-fires ApplyPinDistance until the pawn is ready and the actor actually spawns.
			CurrentPinYd = 150.0;
			Panel->OnPinChanged = [WeakThis](double Y)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ApplyPinDistance(Y); }
			};
			Panel->OnPuttModeChanged = [WeakThis](bool b)
			{
				if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->SetPuttMode(b); }
			};
			Panel->SetPinValue(CurrentPinYd);
			ApplyPinDistance(CurrentPinYd);

			// Environment director: find-or-spawn the range's time-of-day/weather actor and wire the
			// Time + Sky dropdowns to it. Names come from the director (single source of truth, like
			// the club bag); weak capture so a dropdown can never call into a destroyed actor.
			if (UWorld* World = GetWorld())
			{
				if (AGolfRangeEnvironment* Env = GetOrSpawnRangeEnv(World))
				{
					TWeakObjectPtr<AGolfRangeEnvironment> WeakEnv(Env);
					Panel->OnTimeChosen = [WeakEnv](int32 Idx)
					{
						if (AGolfRangeEnvironment* E = WeakEnv.Get()) { E->SetTime(Idx); }
					};
					Panel->OnSkyChosen = [WeakEnv](int32 Idx)
					{
						if (AGolfRangeEnvironment* E = WeakEnv.Get()) { E->SetSky(Idx); }
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

			// Launch-monitor connection status -> panel dot (the active driver). Weak-captured panel
			// so a status change after PIE teardown is a no-op.
			if (ULaunchMonitorManager* LM = ULaunchMonitorManager::Get(this))
			{
				TWeakObjectPtr<UGolfRangePanel> WeakPanel(Panel);
				LM->OnActiveStatusChanged = [WeakPanel, WeakThis](bool bConnected, const FString& Detail)
				{
					if (UGolfRangePanel* P = WeakPanel.Get()) { P->SetConnectionStatus(bConnected, Detail); }
						// On connect, sync the device to our current club (re-picking the same dropdown
						// entry wouldn't fire OnClubChosen, so push it here too).
						if (bConnected)
						{
							if (AGolfRangeHUD* HUD = WeakThis.Get())
							{
								if (ULaunchMonitorManager* M = ULaunchMonitorManager::Get(HUD))
								{
									if (ULaunchMonitorDriver* D = M->GetActiveDriver())
									{
										D->SetSelectedClub(GBag[FMath::Clamp(HUD->ActiveClub, 0, GBagNum - 1)].Name);
									}
								}
							}
						}
				};
				if (Panel)
				{
					// Launch-monitor dropdown: "Off" + each available driver (today just OpenFlight;
					// Square Omni etc. appear automatically once registered). Picking a driver connects
					// it; "Off" disconnects. Defaults to "Off" unless one is already connected.
					const TArray<FLaunchMonitorDriverInfo> Drivers = LM->GetAvailableDrivers();
					ULaunchMonitorDriver* Active = LM->GetActiveDriver();
					const bool bActiveConnected = Active && Active->IsConnected();
					TArray<FString> LMNames;
					LMNames.Reserve(Drivers.Num() + 1);
					LMNames.Add(TEXT("Off"));
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

					Panel->OnLaunchMonitorChosen = [WeakThis](int32 Idx)
					{
						AGolfRangeHUD* HUD = WeakThis.Get();
						if (!HUD) { return; }
						ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(HUD);
						if (!Mgr) { return; }
						if (Idx <= 0)
						{
							Mgr->DisconnectActive();   // "Off"
							return;
						}
						const TArray<FLaunchMonitorDriverInfo> Avail = Mgr->GetAvailableDrivers();
						if (Avail.IsValidIndex(Idx - 1))
						{
							Mgr->SetActiveDriver(Avail[Idx - 1].Id, /*bConnectNow=*/true);
						}
					};

					// "Simulate Shot" button (shown by the panel only while connected) -> ask the active
					// driver to emit a shot (OpenFlight mock mode round-trips it back over the socket).
					Panel->OnSimulateShot = [WeakThis]()
					{
						AGolfRangeHUD* HUD = WeakThis.Get();
						if (!HUD) { return; }
						ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(HUD);
						if (!Mgr) { return; }
						if (ULaunchMonitorDriver* D = Mgr->GetActiveDriver())
						{
							D->RequestSimulatedShot();
						}
					};

					// Only override the panel's default ("No launch monitor", gray) when something is
					// actually connected -- so a disconnected startup reads consistently with "Off".
					if (bActiveConnected && Active)
					{
						Panel->SetConnectionStatus(true, FString::Printf(TEXT("%s: connected"),
							*Active->GetDisplayName().ToString()));
					}

					// GOL-67: apply the initial Mode visibility (default = Game -> hide LM controls,
					// mount the swing meter). Must run AFTER the LM combo + Simulate button mount
					// so the visibility flip lands on populated widgets.
					SetInputMode(CurrentInputMode);
				}
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
	InputComponent->BindKey(EKeys::H,        IE_Pressed,  this, &AGolfRangeHUD::ToggleHistoryFromKey);   // GOL-65
	// Settings/credits menu. Escape works in packaged builds + PIE (the editor's Escape-stops-play
	// only applies when the viewport hasn't captured focus); golfsim.Credits also opens it.
	InputComponent->BindKey(EKeys::Escape,   IE_Pressed,  this, &AGolfRangeHUD::ToggleSettingsMenu);
	// Tab: dev cheat sheet listing every key binding. Replaced by a real keybindings UI later.
	InputComponent->BindKey(EKeys::Tab,      IE_Pressed,  this, &AGolfRangeHUD::ToggleCheatSheet);
#if WITH_EDITOR
	// Z: re-open the main menu mid-session. Editor / PIE only -- so a dev can reach "Previous
	// Sessions" or quit without re-launching the editor. Compiled out of cooked builds.
	InputComponent->BindKey(EKeys::Z,        IE_Pressed,  this, &AGolfRangeHUD::ToggleMainMenuDev);
#endif
	// Follow-cam orbit: right mouse + drag to circle the resting ball (Follow mode).
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AGolfRangeHUD::OrbitPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AGolfRangeHUD::OrbitReleased);
	InputComponent->BindAxisKey(EKeys::MouseX, this, &AGolfRangeHUD::OnOrbitYaw);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &AGolfRangeHUD::OnOrbitPitch);
	ShowMainMenu();   // greet on the already-loaded range; gameplay stays gated until "Range"
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
	ActiveClub = FMath::Clamp(Index, 0, GBagNum - 1);
	UE_LOG(LogTemp, Display, TEXT("golfsim range: club -> %s"), GBag[ActiveClub].Name);
	if (Panel)
	{
		Panel->SetSelectedClubIndex(ActiveClub);   // keep the dropdown in step with 1-6 keys (guarded)
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
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->ToggleSettingsMenu(); }
	};
	SettingsMenu->AddToViewport(20);   // above the range panel + manual dialog
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
}

void AGolfRangeHUD::ToggleSettingsMenu()
{
	if (bMenuOpen) { return; }   // settings is reachable from in-range, not over the main menu
	EnsureSettingsMenu();
	if (!SettingsMenu)
	{
		return;
	}
	bSettingsOpen = !bSettingsOpen;
	SettingsMenu->SetVisibility(bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bSettingsOpen)
	{
		SettingsMenu->SetCurrent(GolfDisplay::ReadCurrent());   // reseed in case values changed elsewhere
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

void AGolfRangeHUD::ApplyPinDistance(double Yards)
{
	UWorld* World = GetWorld();
	if (!World)
	{
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
	const FVector ProbeStart(PinWorldXcm, 0.0, TeeLoc.Z + 5000.0);
	const FVector ProbeEnd(PinWorldXcm, 0.0, TeeLoc.Z - 5000.0);

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
			FVector(PinWorldXcm, 0.0, GroundZ), FRotator::ZeroRotator, Sp);
		Pin = PinActor;
	}
	else
	{
		PinActor->SetActorLocationAndRotation(FVector(PinWorldXcm, 0.0, GroundZ), FRotator::ZeroRotator);
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

#if WITH_EDITOR
void AGolfRangeHUD::ToggleMainMenuDev()
{
	// Defensive: don't pop the menu over another modal -- those have their own close paths.
	if (bSettingsOpen || bHistoryOpen || bSessionsListOpen || bCheatOpen) { return; }
	if (bMenuOpen)
	{
		DismissMainMenu();
	}
	else
	{
		ShowMainMenu();   // recomputes "Previous Sessions" enabled state based on on-disk sessions
	}
}
#endif

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
	// at a dangling locked-power state.
	SwingState = GolfsimKeyboardSwing::FState{};

	if (Panel)
	{
		Panel->SetSelectedModeIndex(bGame ? 0 : 1);   // re-entrancy guarded inside
		Panel->SetLMControlsVisible(!bGame);
	}

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
			SwingMeter->SetMeters(0.0, 0.0);
			SwingMeter->SetHintText(TEXT("Press Space to start your swing"));
			SwingMeter->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else if (SwingMeter)
	{
		SwingMeter->SetVisibility(ESlateVisibility::Collapsed);
	}
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
	if (CurrentInputMode == EInputMode::Simulation)
	{
		FireRandom();
		return;
	}

	// Game mode: step the swing state machine. The HUD owns the active-club lookup; the namespace
	// just maps Power% + Accuracy% + club -> shot fields.
	const FClubPreset& C = GBag[FMath::Clamp(ActiveClub, 0, GBagNum - 1)];
	const GolfsimKeyboardSwing::FClubPreset SwingClub = ToSwingClub(C);
	GolfsimKeyboardSwing::FResolution Res;
	const bool bResolved = GolfsimKeyboardSwing::OnSpace(SwingState, SwingConfig, SwingClub, Res);

	// Update the hint text on every press so the player knows what the next press will do.
	if (SwingMeter)
	{
		switch (SwingState.State)
		{
			case GolfsimKeyboardSwing::EState::Power:
				SwingMeter->SetHintText(TEXT("Press Space to lock POWER"));
				break;
			case GolfsimKeyboardSwing::EState::Accuracy:
				SwingMeter->SetHintText(TEXT("Press Space to lock ACCURACY"));
				break;
			default:
				SwingMeter->SetHintText(TEXT("Press Space to start your swing"));
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
			SwingMeter->SetMeters(0.0, 0.0);
			SwingMeter->SetHintText(TEXT("Whiff! Press Space to try again"));
		}
		return;
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
	SettingsMenu->ShowSection(1);
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
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->DismissMainMenu(); }
	};
	// GOL-65: "Previous Sessions" opens the session-picker list over the main menu. Picking a
	// session opens the full history table; closing the table returns to the list; closing the
	// list returns to the main menu underneath. H key in-range opens current-session only.
	MainMenu->OnPreviousSessions = [WeakThis]()
	{
		if (AGolfRangeHUD* HUD = WeakThis.Get()) { HUD->OpenPreviousSessionsList(); }
	};
	MainMenu->AddToViewport(30);   // above the panel, manual dialog, and settings modal
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

		// GOL-67: persistent bottom-left discoverability hint so first-time users find Tab.
		// Hidden while a modal is up to keep the panel/menu the only focus surface.
		if (!InputGated())
		{
			const FString Hint(TEXT("Tab: Key bindings"));
			DrawText(Hint, FLinearColor(0.85f, 0.78f, 0.30f), 20.0f, (float)Canvas->SizeY - 30.0f);
		}
	}
}
