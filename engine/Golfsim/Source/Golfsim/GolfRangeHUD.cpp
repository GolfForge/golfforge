#include "GolfRangeHUD.h"

#include "GolfBallActor.h"
#include "GolfRangeEnvironment.h"
#include "ManualShotDialog.h"              // manual-shot dialog (GOL-8)
#include "Events/EventBusSubsystem.h"      // publish shot.taken / subscribe shot.outcome (GOL-7)
#include "Drivers/LaunchMonitorManager.h"  // active-driver status -> panel dot (GOL-11)
#include "Drivers/LaunchMonitorDriver.h"
#include "Physics/BallFlightTypes.h"       // FBallTrajectory (carried on the outcome event)

#include "EngineUtils.h"
#include "Components/InputComponent.h"
#include "Engine/Canvas.h"          // UCanvas (SizeX/SizeY) for the DrawHUD resolution readout
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/ICursor.h"

namespace
{
	struct FClubPreset { const TCHAR* Name; double SpeedMps; double LaunchDeg; double SpinRpm; };

	// Trackman PGA-Tour averages (ball speed mph -> m/s), hit straight; per-shot
	// dispersion is added at fire time so no two shots are identical.
	static const FClubPreset GBag[] = {
		{ TEXT("Driver"),         74.6, 10.9, 2686.0 },
		{ TEXT("3-Wood"),         70.6,  9.2, 3655.0 },
		{ TEXT("5-Iron"),         60.3, 11.9, 5280.0 },
		{ TEXT("7-Iron"),         55.0, 16.3, 7097.0 },
		{ TEXT("9-Iron"),         48.7, 20.4, 8647.0 },
		{ TEXT("Pitching Wedge"), 45.6, 24.2, 9304.0 },
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
	}
	OutcomeSub = FGolfEventSubscription{};
	EventBusWeak = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AGolfRangeHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Count the Carry readout up while the ball is in the air, then snap to the exact final once it
	// lands (or if the trajectory was invalid, the first tick just sets the final immediately).
	if (bCarryAnimating && Panel)
	{
		AGolfBallActor* Ball = AnimBall.Get();
		if (Ball && Ball->IsPlaying())
		{
			const double LiveCarryYd = Ball->GetCurrentCarryMeters() * 1.0936132983;   // m -> yd
			Panel->UpdateMetrics(AnimClub, AnimSpeedMph, AnimLaunchDeg, AnimSpinRpm,
				LiveCarryYd, AnimOfflineYd, bAnimSpinEstimated);
		}
		else
		{
			Panel->UpdateMetrics(AnimClub, AnimSpeedMph, AnimLaunchDeg, AnimSpinRpm,
				AnimTargetCarryYd, AnimOfflineYd, bAnimSpinEstimated);   // landed: exact final
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
			Panel->UpdateMetrics(GBag[ActiveClub].Name, 0.0, 0.0, 0.0, 0.0, 0.0);   // "-" until first shot

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
				}
			}
		}
	}
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::One,      IE_Pressed, this, &AGolfRangeHUD::SelectClub0);
	InputComponent->BindKey(EKeys::Two,      IE_Pressed, this, &AGolfRangeHUD::SelectClub1);
	InputComponent->BindKey(EKeys::Three,    IE_Pressed, this, &AGolfRangeHUD::SelectClub2);
	InputComponent->BindKey(EKeys::Four,     IE_Pressed, this, &AGolfRangeHUD::SelectClub3);
	InputComponent->BindKey(EKeys::Five,     IE_Pressed, this, &AGolfRangeHUD::SelectClub4);
	InputComponent->BindKey(EKeys::Six,      IE_Pressed, this, &AGolfRangeHUD::SelectClub5);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AGolfRangeHUD::FireRandom);
	InputComponent->BindKey(EKeys::Left,     IE_Pressed,  this, &AGolfRangeHUD::TurnLeftPressed);
	InputComponent->BindKey(EKeys::Left,     IE_Released, this, &AGolfRangeHUD::TurnLeftReleased);
	InputComponent->BindKey(EKeys::Right,    IE_Pressed,  this, &AGolfRangeHUD::TurnRightPressed);
	InputComponent->BindKey(EKeys::Right,    IE_Released, this, &AGolfRangeHUD::TurnRightReleased);
	InputComponent->BindKey(EKeys::M,        IE_Pressed,  this, &AGolfRangeHUD::ToggleManualDialog);
	bInputBound = true;
}

void AGolfRangeHUD::SelectClub(int32 Index)
{
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
			if (APawn* Pawn = PC->GetPawn())
			{
				Loc = Pawn->GetActorLocation();
			}
		}
		Ball = GetOrSpawnBallAt(World, Loc, Rot);
		if (Ball)
		{
			Ball->PlayTrajectory(Out.Trajectory);
		}
	}

	const double CarryYd   = Out.CarryM         * 1.0936132983;   // m -> yd
	const double OfflineYd = Out.LateralOffsetM * 1.0936132983;   // signed; + = right

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
	bCarryAnimating    = true;

	if (Panel)
	{
		// First frame: static metrics now, carry starting at the ball's live downrange (~0 at launch).
		const double StartCarryYd = (Ball && Ball->IsPlaying())
			? Ball->GetCurrentCarryMeters() * 1.0936132983 : CarryYd;
		Panel->UpdateMetrics(ClubName, SpeedMph, LaunchDeg, SpinRpm,
			StartCarryYd, OfflineYd, Out.bSpinEstimated);
	}
	if (ManualDialog && bManualOpen)
	{
		ManualDialog->SetResult(CarryYd, OfflineYd);   // live "dial in a shot" feedback (final)
	}
	UE_LOG(LogTemp, Display,
		TEXT("golfsim range: shot.outcome carry=%.1fm lateral=%.1fm -> ball played, panel updated"),
		Out.CarryM, Out.LateralOffsetM);
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
	}
}
