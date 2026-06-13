#include "GolfBallActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
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

	// Golf-ball look: off-white semi-gloss + tiled dimple normal (M_GolfBall, authored by
	// engine/scripts/build_golfball_assets.py). Replaces the bare gray engine sphere material.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BallMat(TEXT("/Game/Models/Golf/M_GolfBall.M_GolfBall"));
	if (BallMat.Succeeded())
	{
		BallMesh->SetMaterial(0, BallMat.Object);
	}
}

FVector AGolfBallActor::SampleToWorld(const FTrajectorySample& Sample, int32 SampleIdx) const
{
	return GolfBallRender::SampleToWorld(
		Sample, SampleIdx, Trajectory.LandingSampleIndex,
		LaunchOriginUU, LaunchRotation, MetersToUU,
		BallRestHeightUU, PostLandingGroundCacheUU);
}

void AGolfBallActor::StartCupDrop(const FVector& CupCenterUU, TFunction<void()> OnDone, float DurationSec)
{
	bPlaying = false;   // a holed putt is done rolling -- the drop owns the ball now
	bCupDropping = true;
	CupDropSeconds = 0.f;
	CupDropDuration = FMath::Max(DurationSec, 0.05f);
	CupDropFrom = GetActorLocation();
	// Sink to the cup center, two ball-heights below the lip -- enough that the ball reads as
	// swallowed without poking through the landscape under the cup decal.
	CupDropTo = FVector(CupCenterUU.X, CupCenterUU.Y, CupDropFrom.Z - 2.f * BallRestHeightUU);
	CupDropOnDone = MoveTemp(OnDone);
	SetActorHiddenInGame(false);
}

void AGolfBallActor::PlayTrajectory(const FBallTrajectory& InTrajectory)
{
	// A new shot cancels any in-flight cup-drop animation (and un-hides the swallowed ball).
	bCupDropping = false;
	CupDropOnDone = nullptr;
	SetActorHiddenInGame(false);

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

	// GOL-203: holed putt sinking into the cup. XY eases out toward the cup center (the ball is
	// nearly there already), Z accelerates down like a real drop. Hide at the bottom -- the ball
	// is "in the hole" until the next address/shot un-hides it.
	if (bCupDropping)
	{
		CupDropSeconds += DeltaSeconds;
		const float T = FMath::Clamp(CupDropSeconds / CupDropDuration, 0.f, 1.f);
		const float AlphaXY = FMath::Sin(T * UE_HALF_PI);   // ease-out
		const float AlphaZ = T * T;                          // accelerating fall
		FVector P;
		P.X = FMath::Lerp(CupDropFrom.X, CupDropTo.X, AlphaXY);
		P.Y = FMath::Lerp(CupDropFrom.Y, CupDropTo.Y, AlphaXY);
		P.Z = FMath::Lerp(CupDropFrom.Z, CupDropTo.Z, AlphaZ);
		SetActorLocation(P);
		if (T >= 1.f)
		{
			bCupDropping = false;
			SetActorHiddenInGame(true);   // swallowed by the cup
			if (CupDropOnDone)
			{
				TFunction<void()> Done = MoveTemp(CupDropOnDone);
				CupDropOnDone = nullptr;
				Done();
			}
		}
		return;
	}

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
	// so the full arc remains on screen after landing. Putts (GOL-203) draw thinner + pale --
	// the trail follows the roll, so it reads as the break line on the green, not a flight arc.
	if (bDrawDebugArc)
	{
		if (UWorld* World = GetWorld())
		{
			const FColor TraceColor = bPuttTracer ? FColor(225, 233, 228) : FColor::Yellow;
			const float TraceThickness = bPuttTracer ? 1.4f : 2.f;
			DrawDebugLine(World, PrevDrawPos, NewPos, TraceColor, /*bPersistentLines=*/true,
				/*LifeTime=*/-1.f, /*DepthPriority=*/0, TraceThickness);
		}
	}
	PrevDrawPos = NewPos;

	if (bReachedEnd)
	{
		bPlaying = false;
	}
}
