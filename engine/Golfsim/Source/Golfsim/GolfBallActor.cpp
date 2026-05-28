#include "GolfBallActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

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

FVector AGolfBallActor::SampleToWorld(const FTrajectorySample& Sample) const
{
	const FVector LocalUU = Sample.PositionMeters * MetersToUU;   // SI m -> cm, launch-local frame
	return LaunchOriginUU + LaunchRotation.RotateVector(LocalUU);
}

void AGolfBallActor::PlayTrajectory(const FBallTrajectory& InTrajectory)
{
	Trajectory = InTrajectory;
	LaunchOriginUU = GetActorLocation();
	LaunchRotation = GetActorRotation();
	ElapsedSeconds = 0.f;
	CurrentCarryMeters = 0.f;
	bPlaying = Trajectory.bValid && Trajectory.Samples.Num() >= 2;

	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
	if (bPlaying)
	{
		const FVector Start = SampleToWorld(Trajectory.Samples[0]);
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
		NewPos = SampleToWorld(S.Last());
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
		NewPos = FMath::Lerp(SampleToWorld(S[Lo]), SampleToWorld(S[Hi]), Alpha);
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
