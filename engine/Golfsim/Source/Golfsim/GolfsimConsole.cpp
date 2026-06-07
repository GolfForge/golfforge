// Console commands to fire shots in PIE/Simulate: build a shot, run the solver, fly the visualizer.
// Registered at module load via file-scope statics. Invoke from the ~ console, or programmatically
// (e.g. MCP execute_console_command). Results are logged to LogTemp; read them with get_log_lines.

#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

#include "Physics/BallFlightSolver.h"
#include "Physics/GroundRoll.h"
#include "GolfBallActor.h"
#include "GolfEnvironment.h"
#include "GolfRangeHUD.h"
#include "GolfDisplaySettings.h"
#include "Events/EventBusSubsystem.h"
#include "Drivers/LaunchMonitorManager.h"
#include "Drivers/LaunchMonitorDriver.h"
#include "Course/CourseSurfaceSubsystem.h"
#include "Session/ShotHistorySubsystem.h"
#include "Round/RoundSubsystem.h"

namespace
{
	// Place the ball at the "tee" -- the player pawn's location, facing its heading (pitch/roll
	// flattened so +X downrange is horizontal). Falls back to world origin if there's no pawn.
	// Reuses an existing ball, repositioning it each shot so repeated shots launch from the player.
	AGolfBallActor* GetOrSpawnBall(UWorld* World)
	{
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		APawn* Pawn = nullptr;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			Pawn = PC->GetPawn();
			if (Pawn)
			{
				Loc = Pawn->GetActorLocation();
				Rot = Pawn->GetActorRotation();
				Rot.Pitch = 0.f;
				Rot.Roll = 0.f;
				// GOL-110: trace launch origin to the floor so a course-fired shot starts on the
				// landscape, not at pawn capsule center. Mirrors AGolfRangeHUD's recipe -- complex
				// collision (landscape simple-collision sits above the visible heightfield) +
				// ignore the pawn so the trace doesn't get eaten by the capsule.
				FCollisionQueryParams P(SCENE_QUERY_STAT(GolfsimFireShotFloorTrace), /*bTraceComplex=*/true);
				P.AddIgnoredActor(Pawn);
				FHitResult Ground;
				if (World->LineTraceSingleByChannel(Ground, Loc, Loc - FVector(0.f, 0.f, 100000.f), ECC_WorldStatic, P))
				{
					Loc.Z = Ground.ImpactPoint.Z + AGolfBallActor::BallRestHeightUU;
				}
			}
		}

		for (TActorIterator<AGolfBallActor> It(World); It; ++It)
		{
			It->SetActorLocationAndRotation(Loc, Rot);
			return *It;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGolfBallActor>(AGolfBallActor::StaticClass(), Loc, Rot, Params);
	}

	void FireShotCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (Args.Num() < 5)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: golfsim.FireShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm>"));
			return;
		}

		FShotInput Shot;
		Shot.BallSpeedMps = FCString::Atod(*Args[0]);
		Shot.LaunchAngleDeg = FCString::Atod(*Args[1]);
		Shot.AzimuthDeg = FCString::Atod(*Args[2]);
		Shot.BackspinRpm = FCString::Atod(*Args[3]);
		Shot.SidespinRpm = FCString::Atod(*Args[4]);

		FBallTrajectory T = GolfBallFlight::Simulate(Shot);

		// Roll it out on fairway by default so a console-fired ball visibly rolls past the landing
		// marker (the bus integrator does the real per-surface lie; this is just the no-world path).
		double TotalM = T.CarryM;
		const FGroundRollResult Roll =
			GolfBallFlight::SimulateGroundRoll(T, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));
		if (Roll.bValid)
		{
			TotalM = Roll.TotalDistanceM;
			T.Samples.Append(Roll.RollSamples);
		}

		UE_LOG(LogTemp, Display,
			TEXT("golfsim.FireShot: carry=%.1fm total=%.1fm(fairway) apex=%.1fm descent=%.1fdeg lateral=%.1fm flight=%.2fs samples=%d valid=%d"),
			T.CarryM, TotalM, T.ApexM, T.DescentAngleDeg, T.LateralOffsetM, T.FlightTimeS, T.Samples.Num(), T.bValid ? 1 : 0);

		if (AGolfBallActor* Ball = GetOrSpawnBall(World))
		{
			Ball->PlayTrajectory(T);
		}
	}

	void TraceShotCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (Args.Num() < 8)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: golfsim.TraceShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm> <carry_m> <apex_m> <descent_deg>"));
			return;
		}

		FShotInput Shot;
		Shot.BallSpeedMps = FCString::Atod(*Args[0]);
		Shot.LaunchAngleDeg = FCString::Atod(*Args[1]);
		Shot.AzimuthDeg = FCString::Atod(*Args[2]);
		Shot.BackspinRpm = FCString::Atod(*Args[3]);
		Shot.SidespinRpm = FCString::Atod(*Args[4]);

		FResolvedFlight Resolved;
		Resolved.CarryM = FCString::Atod(*Args[5]);
		Resolved.ApexM = FCString::Atod(*Args[6]);
		Resolved.DescentAngleDeg = FCString::Atod(*Args[7]);

		const FBallTrajectory T = GolfBallFlight::TraceFromResolved(Shot, Resolved);
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.TraceShot: carry=%.1fm apex=%.1fm descent=%.1fdeg flight=%.2fs samples=%d valid=%d"),
			T.CarryM, T.ApexM, T.DescentAngleDeg, T.FlightTimeS, T.Samples.Num(), T.bValid ? 1 : 0);

		if (AGolfBallActor* Ball = GetOrSpawnBall(World))
		{
			Ball->PlayTrajectory(T);
		}
	}

	// Ground-roll bench (GOL-9): synthesize a landing state and roll it out on a chosen surface, no
	// flight needed. Use it to compare surfaces headlessly: fairway > rough >> bunker (~0).
	void TestGroundRollCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 3)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: golfsim.TestGroundRoll <landing_speed_mps> <descent_deg> <lie> [spin_rpm]  (lie: fairway/rough/bunker/green/tee)"));
			return;
		}
		const double Speed = FCString::Atod(*Args[0]);
		const double DescentDeg = FCString::Atod(*Args[1]);
		const EGolfLie Lie = LieFromProtocol(Args[2]);
		const double SpinRpm = Args.Num() > 3 ? FCString::Atod(*Args[3]) : 0.0;

		const double Descent = FMath::DegreesToRadians(DescentDeg);
		FBallTrajectory Flight;
		Flight.Samples.Add({ 0.0, FVector::ZeroVector,
			FVector(Speed * FMath::Cos(Descent), 0.0, -Speed * FMath::Sin(Descent)) });
		Flight.LandingSampleIndex = 0;
		Flight.LandingSpeedMps = Speed;
		Flight.LandingSpinRpm = SpinRpm;
		Flight.DescentAngleDeg = DescentDeg;
		Flight.bValid = true;

		const FGroundRollResult R =
			GolfBallFlight::SimulateGroundRoll(Flight, Lie, GolfBallFlight::SurfaceRollFor(Lie));
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.TestGroundRoll: speed=%.1f m/s descent=%.1f deg spin=%.0f rpm lie=%s -> roll=%.1f m (%.1f yd)"),
			Speed, DescentDeg, SpinRpm, *LieToProtocol(Lie), R.RollDistanceM, R.RollDistanceM * 1.0936132983);
	}

	// Course lie probe (GOL-40): map a world XY in METERS to its painted EGolfLie via the active
	// UCourseSurfaceSubsystem. The subsystem only exists on known course levels (see
	// CourseIdByLevelName); on the range / unrecognized maps this command logs a hint and stops.
	void TestCourseLieCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: golfsim.TestCourseLie <world_x_m> <world_y_m>  (course-level only; uses the splatmap)"));
			return;
		}
		UCourseSurfaceSubsystem* CSS = World ? World->GetSubsystem<UCourseSurfaceSubsystem>() : nullptr;
		if (!CSS)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("golfsim.TestCourseLie: no UCourseSurfaceSubsystem on this level (not a known course)."));
			return;
		}
		if (!CSS->IsValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("golfsim.TestCourseLie: course-surface subsystem present but sampler not loaded (check earlier warnings)."));
			return;
		}
		const double X = FCString::Atod(*Args[0]);
		const double Y = FCString::Atod(*Args[1]);
		const EGolfLie Lie = CSS->ClassifyAt(X, Y);
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.TestCourseLie: (%.1f, %.1f) m -> %s"), X, Y, *LieToProtocol(Lie));
	}

	// The level's time-of-day/weather director, if present. The range find-or-spawns one in PIE; the
	// course has one placed in its umap. Returns the first found, or null on maps without a director.
	AGolfEnvironment* FindGolfEnv(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGolfEnvironment> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	void SetTimeCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetTime <index>  (0=Dawn 1=Morning 2=Noon 3=Dusk 4=Afternoon)"));
			return;
		}
		if (AGolfEnvironment* Env = FindGolfEnv(World))
		{
			Env->SetTime(FCString::Atoi(*Args[0]));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetTime: no AGolfEnvironment in this level"));
		}
	}

	void SetSkyCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetSky <index>  (0=Clear 1=Cloudy 2=Overcast)"));
			return;
		}
		if (AGolfEnvironment* Env = FindGolfEnv(World))
		{
			Env->SetSky(FCString::Atoi(*Args[0]));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetSky: no AGolfEnvironment in this level"));
		}
	}

#if WITH_EDITOR
	// GOL-161 live tuning: drive the active director's sun directly from a 0-24 hour, bypassing the
	// discrete presets, for fast in-PIE iteration of the course look. Editor builds only.
	void SetTimeOfDayCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetTimeOfDay <hour 0-24>"));
			return;
		}
		if (AGolfEnvironment* Env = FindGolfEnv(World))
		{
			Env->ApplyTimeOfDayHour(FCString::Atof(*Args[0]));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetTimeOfDay: no AGolfEnvironment in this level"));
		}
	}
#endif

	// EventBus round-trip exerciser (GOL-7): publish a shot.taken through the bus; the built-in
	// integrator subscriber runs the solver and publishes session.shot_outcome. A temporary local
	// subscriber logs the outcome so the publish->subscribe path is visible headlessly in the log.
	void PublishTestShotCmd(const TArray<FString>& Args, UWorld* World)
	{
		UEventBusSubsystem* EBus = UEventBusSubsystem::Get(World);
		if (!EBus)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("golfsim.PublishTestShot: no EventBus subsystem (need a running game/PIE world)"));
			return;
		}

		// Defaults reproduce a stock 7-iron; optional args override ballspeed/launch/backspin.
		FShotTakenEvent Shot;
		Shot.Source         = TEXT("console-test");
		Shot.PlayerId       = GolfsimEvents::LocalPlayerId();
		Shot.Club           = TEXT("7-Iron");
		Shot.Lie            = TEXT("tee");
		Shot.BallSpeedMps   = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 55.0;
		Shot.LaunchAngleDeg = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 16.3;
		Shot.BackspinRpm    = Args.Num() > 2 ? FCString::Atod(*Args[2]) : 7097.0;

		// Probe the outcome channel for the duration of this one publish (dispatch is synchronous,
		// so the probe fires before we unsubscribe), then remove it so we don't leak a subscriber.
		FGolfEventSubscription Probe = EBus->Subscribe(EEventKind::ShotOutcome,
			[](const FGolfEvent& Event)
			{
				const FShotOutcomeEvent& Out = static_cast<const FShotOutcomeEvent&>(Event);
				UE_LOG(LogTemp, Display,
					TEXT("golfsim.PublishTestShot: received session.shot_outcome carry=%.1fm lateral=%.1fm samples=%d"),
					Out.CarryM, Out.LateralOffsetM, Out.Trajectory.Samples.Num());
			});
		EBus->Publish(Shot);
		EBus->Unsubscribe(Probe);

		UE_LOG(LogTemp, Display,
			TEXT("golfsim.PublishTestShot: published shot.taken through the bus (see outcome above)"));
	}

	// --- Launch-monitor framework (GOL-11) -----------------------------------------------------

	void LMSelectCmd(const TArray<FString>& Args, UWorld* World)
	{
		ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(World);
		if (!Mgr)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.LMSelect: no LM manager (need a running game/PIE world)"));
			return;
		}
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Display, TEXT("golfsim.LMSelect: active=%s. Available drivers:"), *Mgr->GetActiveDriverId());
			for (const FLaunchMonitorDriverInfo& Info : Mgr->GetAvailableDrivers())
			{
				UE_LOG(LogTemp, Display, TEXT("  %s (%s)%s"),
					*Info.Id, *Info.DisplayName.ToString(), Info.bConnected ? TEXT(" [connected]") : TEXT(""));
			}
			return;
		}
		Mgr->SetActiveDriver(Args[0], /*bConnectNow=*/true);
	}

	void LMConnectCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(World)) { Mgr->ConnectActive(); }
		else { UE_LOG(LogTemp, Warning, TEXT("golfsim.LMConnect: no LM manager")); }
	}

	void LMDisconnectCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(World)) { Mgr->DisconnectActive(); }
		else { UE_LOG(LogTemp, Warning, TEXT("golfsim.LMDisconnect: no LM manager")); }
	}

	// Feed the active driver a payload through its parse->publish path (no socket): proves
	// parse -> bus -> solver -> panel in PIE without a server. "nospin" exercises the est heuristic.
	void LMSimulateCmd(const TArray<FString>& Args, UWorld* World)
	{
		ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(World);
		if (!Mgr)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.LMSimulate: no LM manager"));
			return;
		}
		ULaunchMonitorDriver* Driver = Mgr->GetActiveDriver();
		if (!Driver)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.LMSimulate: no active driver (try golfsim.LMSelect openflight)"));
			return;
		}
		// Canned payloads are driver-shaped: each driver's InjectTestMessage parses its own wire form.
		// All GSPro-family entries (gsproconnect / squaregolf / springbok / ...) share the GSPro parser.
		const FString DriverId = Driver->GetDriverId();
		const bool bGSPro = DriverId == TEXT("gsproconnect") || DriverId == TEXT("squaregolf") || DriverId == TEXT("springbok");
		FString Payload;
		if (Args.Num() == 0)
		{
			Payload = bGSPro
				// GSPro Open Connect V1 shot message (BallData + ShotDataOptions; SpinAxis -4 -> slight draw).
				? TEXT("{\"DeviceID\":\"golfforge-test\",\"Units\":\"Yards\",\"ShotNumber\":1,\"APIversion\":\"1\",")
				  TEXT("\"BallData\":{\"Speed\":167.0,\"SpinAxis\":-4.0,\"TotalSpin\":2686,\"HLA\":-1.8,\"VLA\":10.9},")
				  TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"IsHeartBeat\":false}}")
				// OpenFlight's real shot shape (the {shot,stats} wrapper; spin tilt -> draw).
				: TEXT("{\"shot\":{\"ball_speed_mph\":167.0,\"launch_angle_vertical\":10.9,")
				  TEXT("\"launch_angle_horizontal\":-1.8,\"spin_rpm\":2686,\"spin_axis_deg\":-4.0,")
				  TEXT("\"club\":\"driver\",\"smash_factor\":1.48}}");
		}
		else if (Args[0].Equals(TEXT("nospin"), ESearchCase::IgnoreCase))
		{
			Payload = bGSPro
				? TEXT("{\"BallData\":{\"Speed\":120.0,\"VLA\":24.0},\"ShotDataOptions\":{\"ContainsBallData\":true,\"IsHeartBeat\":false}}")
				: TEXT("{\"shot\":{\"ball_speed_mph\":120.0,\"launch_angle_vertical\":24.0,\"club\":\"pw\"}}");   // no spin -> heuristic -> est
		}
		else
		{
			Payload = FString::Join(Args, TEXT(" "));
		}
		Driver->InjectTestMessage(Payload);
	}

	// Ask the active driver's connected device to emit a simulated shot (OpenFlight mock mode):
	// a real round-trip over the wire, vs LMSimulate which parses locally with no server.
	void LMTriggerCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		ULaunchMonitorManager* Mgr = ULaunchMonitorManager::Get(World);
		if (!Mgr)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.LMTrigger: no LM manager"));
			return;
		}
		if (ULaunchMonitorDriver* Driver = Mgr->GetActiveDriver())
		{
			Driver->RequestSimulatedShot();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.LMTrigger: no active driver"));
		}
	}

	// --- Display settings + credits (GOL-52/GOL-59) ---------------------------------------------

	void SetResolutionCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetResolution <W>x<H>  (e.g. 1920x1080)"));
			return;
		}
		const TOptional<FIntPoint> Res = GolfDisplay::ParseResolution(Args[0]);
		if (!Res.IsSet())
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetResolution: could not parse '%s' (expected WxH)"), *Args[0]);
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.Resolution = Res.GetValue();
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetResolution: %dx%d"), Res.GetValue().X, Res.GetValue().Y);
	}

	void SetQualityCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetQuality <0-3>  (0=Low 1=Medium 2=High 3=Epic)"));
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.QualityLevel = GolfDisplay::ClampQualityLevel(FCString::Atoi(*Args[0]));
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetQuality: %d"), S.QualityLevel);
	}

	void SetUpscalerCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetUpscaler <0-2>  (0=TSR 1=DLSS 2=XeSS; vendor needs its plugin)"));
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.UpscalerIndex = GolfDisplay::ClampUpscalerIndex(FCString::Atoi(*Args[0]));
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetUpscaler: %s"), *GolfDisplay::UpscalerName(S.UpscalerIndex));
	}

	// golfsim.SetGrassDetail <0-2> -- 3D fairway grass density (GOL-162). 0=Off (texture only) 1=Low
	// 2=High. Persists + applies via the same display-settings round-trip as quality/upscaler.
	void SetGrassDetailCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetGrassDetail <0-2>  (0=Off 1=Low 2=High; persists)."));
			return;
		}
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.GrassDetailLevel = GolfDisplay::ClampGrassDetail(FCString::Atoi(*Args[0]));
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetGrassDetail: %d"), S.GrassDetailLevel);
	}

	// golfsim.SetFrameGen <0-4> -- DLSS Frame Generation (GOL-189). 0=Off 1=2X 2=3X 3=4X 4=Auto. Maps to
	// the EStreamlineDLSSGMode the Streamline library applies; unsupported modes fall back to Off. NOTE:
	// DLSS-FG is inert in editor PIE -- test in a Standalone Game or the cooked build.
	void SetFrameGenCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetFrameGen <0-4>  (0=Off 1=2X 2=3X 3=4X 4=Auto; NVIDIA DLSS-FG; standalone/cooked only)."));
			return;
		}
		static const int32 ModeForArg[] = { 0, 17, 23, 31, 251 };   // Off / On2X / On3X / On4X / Auto
		const int32 Idx = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 4);
		FGolfDisplaySettings S = GolfDisplay::ReadCurrent();
		S.FrameGenMode = GolfDisplay::ClampFrameGenMode(ModeForArg[Idx]);
		GolfDisplay::Apply(S);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetFrameGen: %s%s"),
			*GolfDisplay::FrameGenModeName(S.FrameGenMode),
			GolfDisplay::IsFrameGenAvailable() ? TEXT("") : TEXT(" (DLSS-FG unavailable on this GPU/build)"));
	}

	void CreditsCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World) { return; }
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AGolfRangeHUD* HUD = Cast<AGolfRangeHUD>(PC->GetHUD()))
			{
				HUD->OpenCreditsSection();
				return;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("golfsim.Credits: no AGolfRangeHUD in this level"));
	}

	// golfsim.SetMode game|simulation  -- GOL-67. Flips the panel Mode + swing-meter / LM-dropdown
	// visibility. Range-only (no AGolfRangeHUD on other levels).
	void SetModeCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetMode <game|simulation>"));
			return;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		AGolfRangeHUD* HUD = PC ? Cast<AGolfRangeHUD>(PC->GetHUD()) : nullptr;
		if (!HUD)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetMode: no AGolfRangeHUD (range only)"));
			return;
		}
		const FString& Arg = Args[0];
		if (Arg.Equals(TEXT("game"), ESearchCase::IgnoreCase))
		{
			HUD->SetInputMode(AGolfRangeHUD::EInputMode::Game);
		}
		else if (Arg.Equals(TEXT("simulation"), ESearchCase::IgnoreCase) ||
		         Arg.Equals(TEXT("sim"), ESearchCase::IgnoreCase))
		{
			HUD->SetInputMode(AGolfRangeHUD::EInputMode::Simulation);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetMode: unknown mode '%s'  (game|simulation)"), *Arg);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetMode: %s"), *Arg);
	}

	// golfsim.SetDifficulty easy|normal|pro  -- GOL-122. Swaps the active FSwingDifficultyProfile
	// on the range HUD's SwingConfig. The pre-round picker (GOL-121) will replace this entry
	// point at round start; the console command stays for headless PIE tuning. Range-only.
	void SetDifficultyCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetDifficulty <easy|normal|pro>"));
			return;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		AGolfRangeHUD* HUD = PC ? Cast<AGolfRangeHUD>(PC->GetHUD()) : nullptr;
		if (!HUD)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetDifficulty: no AGolfRangeHUD (range only)"));
			return;
		}
		const FString& Arg = Args[0];
		EGolfDifficulty D;
		if      (Arg.Equals(TEXT("easy"),   ESearchCase::IgnoreCase)) { D = EGolfDifficulty::Easy;   }
		else if (Arg.Equals(TEXT("normal"), ESearchCase::IgnoreCase)) { D = EGolfDifficulty::Normal; }
		else if (Arg.Equals(TEXT("pro"),    ESearchCase::IgnoreCase)) { D = EGolfDifficulty::Pro;    }
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetDifficulty: unknown '%s'  (easy|normal|pro)"), *Arg);
			return;
		}
		HUD->SetSwingDifficulty(D);
		const GolfsimKeyboardSwing::FSwingDifficultyProfile& P = GolfsimKeyboardSwing::FSwingDifficultyProfile::For(D);
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.SetDifficulty: %s  (azMax=%.1f sideMax=%.0f mishitLaunch=%.2f normSpan=%.2f gimme=%.1fft)"),
			*Arg, P.MaxAzimuthDeg, P.SidespinPushRpm, P.MishitLaunchScale, P.NormSpan, P.GimmeRadiusFt);
	}

	// golfsim.ShotHistory.Show / .Clear  -- GOL-65. Show toggles the in-range table; Clear empties
	// the in-memory + on-disk current session. Range-only (no AGolfRangeHUD on other levels).
	void ShotHistoryShowCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World) { return; }
		APlayerController* PC = World->GetFirstPlayerController();
		AGolfRangeHUD* HUD = PC ? Cast<AGolfRangeHUD>(PC->GetHUD()) : nullptr;
		if (!HUD)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.ShotHistory.Show: no AGolfRangeHUD (range only)"));
			return;
		}
		HUD->ToggleHistoryPanel();
	}

	void ShotHistoryClearCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UShotHistorySubsystem* Sub = UShotHistorySubsystem::Get(World);
		if (!Sub)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.ShotHistory.Clear: no UShotHistorySubsystem (need a running game/PIE world)"));
			return;
		}
		Sub->Clear();
		UE_LOG(LogTemp, Display, TEXT("golfsim.ShotHistory.Clear: in-memory + JSONL truncated"));
	}

	// --- Single-player round (GOL-116) --------------------------------------------------------
	//
	// Round.Start <CourseId> [easy|normal|pro]   -- load hole.geojson, publish round.start + first hole.start
	// Round.HoleOut                              -- finalize current hole; advance or fire round.complete
	// Round.Abandon                              -- clear state silently (no round.complete)
	// Round.Debug                                -- log current round state (for PIE smoke testing)
	//
	// Auto-hole-out detection ships in GOL-119; until then Round.HoleOut is the manual trigger.

	EGolfDifficulty ParseDifficultyArg(const FString& Arg, EGolfDifficulty Fallback)
	{
		if (Arg.Equals(TEXT("easy"),   ESearchCase::IgnoreCase)) { return EGolfDifficulty::Easy;   }
		if (Arg.Equals(TEXT("normal"), ESearchCase::IgnoreCase)) { return EGolfDifficulty::Normal; }
		if (Arg.Equals(TEXT("pro"),    ESearchCase::IgnoreCase)) { return EGolfDifficulty::Pro;    }
		return Fallback;
	}

	void RoundStartCmd(const TArray<FString>& Args, UWorld* World)
	{
		URoundSubsystem* Sub = URoundSubsystem::Get(World);
		if (!Sub)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.Round.Start: no URoundSubsystem (need a running game/PIE world)"));
			return;
		}
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.Round.Start <CourseId> [easy|normal|pro]"));
			return;
		}
		const FString CourseId = Args[0];
		const EGolfDifficulty D = (Args.Num() > 1)
			? ParseDifficultyArg(Args[1], EGolfDifficulty::Easy)
			: EGolfDifficulty::Easy;
		Sub->StartRound(CourseId, D);
	}

	void RoundHoleOutCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		URoundSubsystem* Sub = URoundSubsystem::Get(World);
		if (!Sub) { UE_LOG(LogTemp, Warning, TEXT("golfsim.Round.HoleOut: no URoundSubsystem")); return; }
		Sub->OnHoleHoled();
	}

	void RoundAbandonCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		URoundSubsystem* Sub = URoundSubsystem::Get(World);
		if (!Sub) { UE_LOG(LogTemp, Warning, TEXT("golfsim.Round.Abandon: no URoundSubsystem")); return; }
		Sub->AbandonRound();
	}

	void RoundDebugCmd(const TArray<FString>& /*Args*/, UWorld* World)
	{
		URoundSubsystem* Sub = URoundSubsystem::Get(World);
		if (!Sub) { UE_LOG(LogTemp, Warning, TEXT("golfsim.Round.Debug: no URoundSubsystem")); return; }
		const GolfsimRound::FRoundState& S = Sub->GetState();
		if (!S.bActive)
		{
			UE_LOG(LogTemp, Display, TEXT("golfsim.Round.Debug: no active round"));
			return;
		}
		const int32 N = S.Schedule.Num();
		const int32 HoleIdx = FMath::Clamp(S.HoleIndex, 0, N - 1);
		const GolfsimRound::FHoleSpec& H = S.Schedule[HoleIdx];
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.Round.Debug: round=%s course=%s diff=%d hole=%d/%d (ref %d par %d) strokes=%d"),
			*S.RoundId, *S.CourseId, (int32)S.Difficulty, HoleIdx + 1, N, H.Ref, H.Par, S.StrokesThisHole);
	}

	// golfsim.SetPin <yards>  -- range target distance (GOL-29). Drives the spinner via the HUD,
	// which spawns/moves the AGolfPinActor down the corridor centerline. Range-only; on other
	// levels the AGolfRangeHUD cast fails and the command logs a hint.
	void SetPinCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetPin <yards>  (0-400; range only)"));
			return;
		}
		if (!World) { return; }
		APlayerController* PC = World->GetFirstPlayerController();
		AGolfRangeHUD* HUD = PC ? Cast<AGolfRangeHUD>(PC->GetHUD()) : nullptr;
		if (!HUD)
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetPin: no AGolfRangeHUD in this level (range only)"));
			return;
		}
		const double Yards = FCString::Atod(*Args[0]);
		HUD->ApplyPinDistance(Yards);
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetPin: %.0f yd"), Yards);
	}

	// golfsim.SetStimp <feet>  -- live-tune the green stimp used by the putter-friction override
	// (GOL-109). With no argument, logs the current value. Clamps to a sensible 6-16 ft band.
	void SetStimpCmd(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Display,
				TEXT("golfsim.SetStimp: current = %.1f ft  (friction = %.3f)"),
				UEventBusSubsystem::GreenStimpFt,
				0.67 / FMath::Max(UEventBusSubsystem::GreenStimpFt, 1.0));
			return;
		}
		const double V = FMath::Clamp(FCString::Atod(*Args[0]), 6.0, 16.0);
		UEventBusSubsystem::GreenStimpFt = V;
		UE_LOG(LogTemp, Display, TEXT("golfsim.SetStimp: %.1f ft  -> putter friction %.3f"),
			V, 0.67 / FMath::Max(V, 1.0));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFireShotCmd(
	TEXT("golfsim.FireShot"),
	TEXT("Simulate and fly a shot: golfsim.FireShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FireShotCmd));

static FAutoConsoleCommandWithWorldAndArgs GTraceShotCmd(
	TEXT("golfsim.TraceShot"),
	TEXT("Fly a launch-monitor-resolved shot: golfsim.TraceShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm> <carry_m> <apex_m> <descent_deg>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TraceShotCmd));

static FAutoConsoleCommandWithWorldAndArgs GTestGroundRollCmd(
	TEXT("golfsim.TestGroundRoll"),
	TEXT("Roll a synthetic landing out on a surface: golfsim.TestGroundRoll <landing_speed_mps> <descent_deg> <lie> [spin_rpm]. Compare fairway/rough/bunker."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TestGroundRollCmd));

static FAutoConsoleCommandWithWorldAndArgs GTestCourseLieCmd(
	TEXT("golfsim.TestCourseLie"),
	TEXT("Probe the course splatmap at a world XY in meters: golfsim.TestCourseLie <x_m> <y_m>. Course levels only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TestCourseLieCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetTimeCmd(
	TEXT("golfsim.SetTime"),
	TEXT("Time-of-day preset: golfsim.SetTime <index>  (0=Dawn 1=Morning 2=Noon 3=Dusk 4=Afternoon)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTimeCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetSkyCmd(
	TEXT("golfsim.SetSky"),
	TEXT("Range sky/weather preset: golfsim.SetSky <index>  (0=Clear 1=Cloudy 2=Overcast)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSkyCmd));

#if WITH_EDITOR
static FAutoConsoleCommandWithWorldAndArgs GSetTimeOfDayCmd(
	TEXT("golfsim.SetTimeOfDay"),
	TEXT("Live-tune the active environment director's sun by hour: golfsim.SetTimeOfDay <0-24>. Editor builds only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTimeOfDayCmd));
#endif

static FAutoConsoleCommandWithWorldAndArgs GSetStimpCmd(
	TEXT("golfsim.SetStimp"),
	TEXT("Live-tune the green stimp for putter shots: golfsim.SetStimp <feet>  (6-16, default 11). No arg = print current."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetStimpCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetPinCmd(
	TEXT("golfsim.SetPin"),
	TEXT("Range target distance: golfsim.SetPin <yards>  (0-400; clamped, persisted, syncs the panel spinner)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetPinCmd));

static FAutoConsoleCommandWithWorldAndArgs GShotHistoryShowCmd(
	TEXT("golfsim.ShotHistory.Show"),
	TEXT("Toggle the session shot-history table (range only). Mirrors the H key."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShotHistoryShowCmd));

static FAutoConsoleCommandWithWorldAndArgs GShotHistoryClearCmd(
	TEXT("golfsim.ShotHistory.Clear"),
	TEXT("Empty the in-memory history list AND truncate the current session's JSONL on disk."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShotHistoryClearCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetModeCmd(
	TEXT("golfsim.SetMode"),
	TEXT("Switch range input: golfsim.SetMode <game|simulation>  (game = swing-meter; simulation = LM dropdown)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetModeCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetDifficultyCmd(
	TEXT("golfsim.SetDifficulty"),
	TEXT("Swap the swing-meter difficulty profile: golfsim.SetDifficulty <easy|normal|pro> (range HUD only)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetDifficultyCmd));

static FAutoConsoleCommandWithWorldAndArgs GRoundStartCmd(
	TEXT("golfsim.Round.Start"),
	TEXT("Start a single-player round: golfsim.Round.Start <CourseId> [easy|normal|pro]. Loads hole.geojson, fires round.start + hole 1."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RoundStartCmd));

static FAutoConsoleCommandWithWorldAndArgs GRoundHoleOutCmd(
	TEXT("golfsim.Round.HoleOut"),
	TEXT("Finalize the current hole; advance or fire round.complete on hole 18. (GOL-119 will auto-fire this on ball-at-rest within gimme radius.)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RoundHoleOutCmd));

static FAutoConsoleCommandWithWorldAndArgs GRoundAbandonCmd(
	TEXT("golfsim.Round.Abandon"),
	TEXT("Abandon the active round; no round.complete published. Subsequent shots/HoleOut are no-ops until next Round.Start."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RoundAbandonCmd));

static FAutoConsoleCommandWithWorldAndArgs GRoundDebugCmd(
	TEXT("golfsim.Round.Debug"),
	TEXT("Log current round state (hole, par, stroke count)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RoundDebugCmd));

static FAutoConsoleCommandWithWorldAndArgs GPublishTestShotCmd(
	TEXT("golfsim.PublishTestShot"),
	TEXT("EventBus round-trip: publish shot.taken; logs the session.shot_outcome the integrator returns. [ballspeed_mps launch_deg backspin_rpm]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PublishTestShotCmd));

static FAutoConsoleCommandWithWorldAndArgs GLMSelectCmd(
	TEXT("golfsim.LMSelect"),
	TEXT("List launch-monitor drivers, or select+connect one: golfsim.LMSelect [driverId]  (e.g. openflight)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LMSelectCmd));

static FAutoConsoleCommandWithWorldAndArgs GLMConnectCmd(
	TEXT("golfsim.LMConnect"),
	TEXT("Connect the active launch-monitor driver."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LMConnectCmd));

static FAutoConsoleCommandWithWorldAndArgs GLMDisconnectCmd(
	TEXT("golfsim.LMDisconnect"),
	TEXT("Disconnect the active launch-monitor driver."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LMDisconnectCmd));

static FAutoConsoleCommandWithWorldAndArgs GLMSimulateCmd(
	TEXT("golfsim.LMSimulate"),
	TEXT("Feed the active driver a test payload (no socket): golfsim.LMSimulate [nospin | <json>]. Proves parse->bus->solver->panel."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LMSimulateCmd));

static FAutoConsoleCommandWithWorldAndArgs GLMTriggerCmd(
	TEXT("golfsim.LMTrigger"),
	TEXT("Ask the active driver's connected device to emit a simulated shot (OpenFlight mock mode) -- a real round-trip."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LMTriggerCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetResolutionCmd(
	TEXT("golfsim.SetResolution"),
	TEXT("Set the window resolution: golfsim.SetResolution <W>x<H> (persists)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetResolutionCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetQualityCmd(
	TEXT("golfsim.SetQuality"),
	TEXT("Set the overall scalability level: golfsim.SetQuality <0-3> (persists)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetQualityCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetUpscalerCmd(
	TEXT("golfsim.SetUpscaler"),
	TEXT("Select the temporal upscaler: golfsim.SetUpscaler <0-2> (0=TSR 1=DLSS 2=XeSS; vendor ones need their plugin). Quality = Upscale Mode preset."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetUpscalerCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetGrassDetailCmd(
	TEXT("golfsim.SetGrassDetail"),
	TEXT("3D fairway grass density: golfsim.SetGrassDetail <0-2> (0=Off 1=Low 2=High; persists)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetGrassDetailCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetFrameGenCmd(
	TEXT("golfsim.SetFrameGen"),
	TEXT("NVIDIA DLSS Frame Generation: golfsim.SetFrameGen <0-4> (0=Off 1=2X 2=3X 3=4X 4=Auto; persists). Standalone/cooked only -- inert in editor PIE."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetFrameGenCmd));

static FAutoConsoleCommandWithWorldAndArgs GCreditsCmd(
	TEXT("golfsim.Credits"),
	TEXT("Open the settings menu to the Credits/Attributions section."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CreditsCmd));
