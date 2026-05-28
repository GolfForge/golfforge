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
#include "GolfRangeEnvironment.h"
#include "Events/EventBusSubsystem.h"
#include "Drivers/LaunchMonitorManager.h"
#include "Drivers/LaunchMonitorDriver.h"

namespace
{
	// Place the ball at the "tee" -- the player pawn's location, facing its heading (pitch/roll
	// flattened so +X downrange is horizontal). Falls back to world origin if there's no pawn.
	// Reuses an existing ball, repositioning it each shot so repeated shots launch from the player.
	AGolfBallActor* GetOrSpawnBall(UWorld* World)
	{
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Loc = Pawn->GetActorLocation();
				Rot = Pawn->GetActorRotation();
				Rot.Pitch = 0.f;
				Rot.Roll = 0.f;
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

	// The range's time-of-day/weather director, if present. Range-only -- BethPage has none.
	AGolfRangeEnvironment* FindRangeEnv(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGolfRangeEnvironment> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	void SetTimeCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetTime <index>  (0=Dawn 1=Morning 2=Noon 3=Dusk 4=Night)"));
			return;
		}
		if (AGolfRangeEnvironment* Env = FindRangeEnv(World))
		{
			Env->SetTime(FCString::Atoi(*Args[0]));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetTime: no AGolfRangeEnvironment in this level (range only)"));
		}
	}

	void SetSkyCmd(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: golfsim.SetSky <index>  (0=Clear 1=Cloudy 2=Overcast)"));
			return;
		}
		if (AGolfRangeEnvironment* Env = FindRangeEnv(World))
		{
			Env->SetSky(FCString::Atoi(*Args[0]));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("golfsim.SetSky: no AGolfRangeEnvironment in this level (range only)"));
		}
	}

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
		FString Payload;
		if (Args.Num() == 0)
		{
			// OpenFlight's real shot shape (the {shot,stats} wrapper; spin tilt -> draw).
			Payload = TEXT("{\"shot\":{\"ball_speed_mph\":167.0,\"launch_angle_vertical\":10.9,")
				TEXT("\"launch_angle_horizontal\":-1.8,\"spin_rpm\":2686,\"spin_axis_deg\":-4.0,")
				TEXT("\"club\":\"driver\",\"smash_factor\":1.48}}");
		}
		else if (Args[0].Equals(TEXT("nospin"), ESearchCase::IgnoreCase))
		{
			Payload = TEXT("{\"shot\":{\"ball_speed_mph\":120.0,\"launch_angle_vertical\":24.0,\"club\":\"pw\"}}");   // no spin -> heuristic -> est
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

static FAutoConsoleCommandWithWorldAndArgs GSetTimeCmd(
	TEXT("golfsim.SetTime"),
	TEXT("Range time-of-day preset: golfsim.SetTime <index>  (0=Dawn 1=Morning 2=Noon 3=Dusk 4=Night)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTimeCmd));

static FAutoConsoleCommandWithWorldAndArgs GSetSkyCmd(
	TEXT("golfsim.SetSky"),
	TEXT("Range sky/weather preset: golfsim.SetSky <index>  (0=Clear 1=Cloudy 2=Overcast)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSkyCmd));

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
