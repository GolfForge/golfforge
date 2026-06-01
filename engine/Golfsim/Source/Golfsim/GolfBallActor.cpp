#include "GolfBallActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "Physics/BallRender.h"

AGolfBallActor::AGolfBallActor()
{
	PrimaryActorTick.bCanEverTick = true;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	RootComponent = BallMesh;
	BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
		BallMesh->SetRelativeScale3D(FVector(0.12f));   // ~12 cm: visible while still ball-ish
	}
}

FVector AGolfBallActor::SampleToWorld(const FTrajectorySample& Sample, int32 SampleIdx) const
{
	return GolfBallRender::SampleToWorld(
		Sample, SampleIdx, Trajectory.LandingSampleIndex,
		LaunchOriginUU, LaunchRotation, MetersToUU,
		BallRestHeightUU, PostLandingGroundCacheUU);
}

void AGolfBallActor::PlayTrajectory(const FBallTrajectory& InTrajectory)
{
	Trajectory = InTrajectory;
	LaunchOriginUU = GetActorLocation();
	LaunchRotation = GetActorRotation();
	ElapsedSeconds = 0.f;
	CurrentCarryMeters = 0.f;
	bPlaying = Trajectory.bValid && Trajectory.Samples.Num() >= 2;

	UWorld* World = GetWorld();
	if (World)
	{
		FlushPersistentDebugLines(World);
	}

	// GOL-110: pre-trace each post-landing sample's world XY against the landscape so the visualizer
	// can snap bounce + roll Z to terrain. The provider is the only piece that talks to UWorld; the
	// math itself lives in pure GolfBallRender for headless-testability. Pawn collision is ignored
	// (it would otherwise eat the trace from inside the capsule on shots fired from a pawn).
	const AActor* PawnToIgnore = nullptr;
	if (World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PawnToIgnore = PC->GetPawn();
		}
	}
	GolfBallRender::CachePostLandingGroundZ(
		Trajectory, LaunchOriginUU, LaunchRotation, MetersToUU,
		[World, PawnToIgnore](double X, double Y) -> TOptional<float>
		{
			if (!World) { return {}; }
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GolfBallTerrainTrace), /*bTraceComplex=*/true);
			if (PawnToIgnore) { Params.AddIgnoredActor(PawnToIgnore); }
			FHitResult Hit;
			const FVector Start(X, Y,  50000.0);
			const FVector End  (X, Y, -50000.0);
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
			{
				return static_cast<float>(Hit.ImpactPoint.Z);
			}
			return {};
		},
		PostLandingGroundCacheUU);

	if (bPlaying)
	{
		const FVector Start = SampleToWorld(Trajectory.Samples[0], 0);
		SetActorLocation(Start);
		PrevDrawPos = Start;   // tracer trail grows from here as the ball flies (see Tick)
	}
}

void AGolfBallActor::ResetAndReplay()
{
	if (Trajectory.bValid)
	{
		PlayTrajectory(Trajectory);
	}
}

void AGolfBallActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPlaying)
	{
		return;
	}

	ElapsedSeconds += DeltaSeconds * FMath::Max(PlaybackSpeed, 0.01f);

	const TArray<FTrajectorySample>& S = Trajectory.Samples;
	const float EndTime = static_cast<float>(S.Last().TimeSeconds);

	// Position for this frame: clamp to landing once we pass the flight time.
	FVector NewPos;
	bool bReachedEnd = false;
	if (ElapsedSeconds >= EndTime)
	{
		NewPos = SampleToWorld(S.Last(), S.Num() - 1);
		CurrentCarryMeters = static_cast<float>(S.Last().PositionMeters.X);   // downrange = carry
		bReachedEnd = true;
	}
	else
	{
		// Find the two samples bracketing the current playback time and lerp between them.
		int32 Hi = 1;
		while (Hi < S.Num() && static_cast<float>(S[Hi].TimeSeconds) < ElapsedSeconds)
		{
			++Hi;
		}
		const int32 Lo = Hi - 1;
		const float T0 = static_cast<float>(S[Lo].TimeSeconds);
		const float T1 = static_cast<float>(S[Hi].TimeSeconds);
		const float Alpha = (T1 > T0) ? (ElapsedSeconds - T0) / (T1 - T0) : 0.f;
		NewPos = FMath::Lerp(SampleToWorld(S[Lo], Lo), SampleToWorld(S[Hi], Hi), Alpha);
		CurrentCarryMeters = FMath::Lerp(static_cast<float>(S[Lo].PositionMeters.X),
			static_cast<float>(S[Hi].PositionMeters.X), Alpha);
	}

	SetActorLocation(NewPos);

	// Grow the tracer trail behind the ball (Toptracer-style): one persistent segment per frame,
	// so the full arc remains on screen after landing.
	if (bDrawDebugArc)
	{
		if (UWorld* World = GetWorld())
		{
			DrawDebugLine(World, PrevDrawPos, NewPos, FColor::Yellow, /*bPersistentLines=*/true,
				/*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/2.f);
		}
	}
	PrevDrawPos = NewPos;

	if (bReachedEnd)
	{
		bPlaying = false;
	}
}
