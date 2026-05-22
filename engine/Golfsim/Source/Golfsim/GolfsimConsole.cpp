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
}

static FAutoConsoleCommandWithWorldAndArgs GFireShotCmd(
	TEXT("golfsim.FireShot"),
	TEXT("Simulate and fly a shot: golfsim.FireShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FireShotCmd));

static FAutoConsoleCommandWithWorldAndArgs GTraceShotCmd(
	TEXT("golfsim.TraceShot"),
	TEXT("Fly a launch-monitor-resolved shot: golfsim.TraceShot <ballspeed_mps> <launch_deg> <azimuth_deg> <backspin_rpm> <sidespin_rpm> <carry_m> <apex_m> <descent_deg>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TraceShotCmd));
