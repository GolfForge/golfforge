// Console commands to fire shots in PIE/Simulate: build a shot, run the solver, fly the visualizer.
// Registered at module load via file-scope statics. Invoke from the ~ console, or programmatically
// (e.g. MCP execute_console_command). Results are logged to LogTemp; read them with get_log_lines.

#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

#include "Physics/BallFlightSolver.h"
#include "GolfBallActor.h"
#include "GolfRangeEnvironment.h"
#include "Events/EventBusSubsystem.h"

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

		const FBallTrajectory T = GolfBallFlight::Simulate(Shot);
		UE_LOG(LogTemp, Display,
			TEXT("golfsim.FireShot: carry=%.1fm apex=%.1fm descent=%.1fdeg lateral=%.1fm flight=%.2fs samples=%d valid=%d"),
			T.CarryM, T.ApexM, T.DescentAngleDeg, T.LateralOffsetM, T.FlightTimeS, T.Samples.Num(), T.bValid ? 1 : 0);

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
}

static FAutoConsoleCommandWithWorldAndArgs GFireShotCmd(
	TEXT("golfsim.FireShot"),
	TEXT("Simulate and fly a shot: golfsim.FireShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FireShotCmd));

static FAutoConsoleCommandWithWorldAndArgs GTraceShotCmd(
	TEXT("golfsim.TraceShot"),
	TEXT("Fly a launch-monitor-resolved shot: golfsim.TraceShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm> <carry_m> <apex_m> <descent_deg>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TraceShotCmd));

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
