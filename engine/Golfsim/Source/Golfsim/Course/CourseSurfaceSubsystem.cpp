#include "Course/CourseSurfaceSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "CollisionQueryParams.h"   // GOL-196: terrain-normal trace for bounce deflection
#include "Misc/Paths.h"
#include "Events/EventBusSubsystem.h"
#include "Physics/GroundRoll.h"   // LieToProtocol (logging)
#include "Course/CourseLevelMap.h"   // shared course-id <-> level table

bool UCourseSurfaceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	// Editor world is allowed so the sampler is available for golfsim.TestCourseLie probes
	// before PIE; SurfaceProvider wiring still happens in OnWorldBeginPlay (PIE/Game only).
	// Skip asset previews + transient worlds.
	const EWorldType::Type WT = World->WorldType;
	if (WT != EWorldType::Game && WT != EWorldType::PIE && WT != EWorldType::Editor)
	{
		return false;
	}
	const FString MapName = FPaths::GetBaseFilename(World->GetMapName());
	return !GolfsimCourseMap::CourseIdForLevel(MapName).IsEmpty();
}

void UCourseSurfaceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The level name is already validated by ShouldCreateSubsystem; safe to look it up here.
	const FString MapName  = FPaths::GetBaseFilename(GetWorld()->GetMapName());
	const FString CourseId = GolfsimCourseMap::CourseIdForLevel(MapName);
	if (CourseId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurfaceSubsystem: map %s has no course mapping (Initialize)"), *MapName);
		return;
	}
	if (!Sampler.Load(CourseId))
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurfaceSubsystem: sampler load failed for %s"), *CourseId);
	}
}

void UCourseSurfaceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Sampler.IsValid())
	{
		return;
	}
	UGameInstance* GI = InWorld.GetGameInstance();
	UEventBusSubsystem* Bus = GI ? GI->GetSubsystem<UEventBusSubsystem>() : nullptr;
	if (!Bus)
	{
		UE_LOG(LogTemp, Warning, TEXT("CourseSurfaceSubsystem: no EventBus subsystem on world; SurfaceProvider not wired"));
		return;
	}
	EventBusWeak = Bus;

	// Wire the lie source. NOTE: the integrator passes LandingLocalSIm in the SHOT's launch-local
	// frame. Until the course game loop (GOL-69) lands and fires shots through a tee/aim transform,
	// the only thing currently publishing on a course level is golfsim.TestCourseLie, which sends
	// world meters straight in. So this lambda treats LandingLocalSIm as world meters directly. When
	// GOL-69 wires a shot pipeline, either it converts to world meters before publishing or this
	// lambda gains a tee+aim transform layer (mirroring AGolfRangeHUD).
	TWeakObjectPtr<UCourseSurfaceSubsystem> WeakSelf(this);
	Bus->SurfaceProvider = [WeakSelf](const FVector& LandingLocalSIm) -> EGolfLie
	{
		const UCourseSurfaceSubsystem* Self = WeakSelf.Get();
		return (Self && Self->Sampler.IsValid())
			? Self->Sampler.ClassifyAt(LandingLocalSIm.X, LandingLocalSIm.Y)
			: EGolfLie::Unknown;
	};
	UE_LOG(LogTemp, Display, TEXT("CourseSurfaceSubsystem: SurfaceProvider wired for the course."));

	// GOL-196 + GOL-75: terrain-normal source for the bounce reflection AND the roll fall-line break.
	// A single landscape-triangle normal is jittery on LIDAR-derived terrain, which would make a
	// rolling/putting ball wiggle; sample a small cross around the point and average so the normal
	// reflects the green's MACRO slope (the break) rather than per-triangle micro-relief. Same
	// launch-local == world-meters treatment as the lie source above.
	Bus->GroundNormalProvider = [WeakSelf](const FVector& LandingLocalSIm) -> FVector
	{
		const UCourseSurfaceSubsystem* Self = WeakSelf.Get();
		UWorld* World = Self ? Self->GetWorld() : nullptr;
		if (!World) { return FVector::UpVector; }

		auto TraceNormalAt = [World](double Xcm, double Ycm, FVector& OutN) -> bool
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GolfsimCourseNormalTrace), /*bTraceComplex=*/true);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, FVector(Xcm, Ycm, 100000.0), FVector(Xcm, Ycm, -100000.0),
				ECC_WorldStatic, Params))
			{
				OutN = Hit.ImpactNormal;   // world normal; on a course the launch-local frame == world here
				return true;
			}
			return false;
		};

		const double Xcm = LandingLocalSIm.X * 100.0;
		const double Ycm = LandingLocalSIm.Y * 100.0;
		constexpr double Rcm = 75.0;   // ~0.75 m kernel radius: above triangle size, below green scale
		const double Offsets[5][2] = { {0.0, 0.0}, {Rcm, 0.0}, {-Rcm, 0.0}, {0.0, Rcm}, {0.0, -Rcm} };
		FVector Sum = FVector::ZeroVector;
		int32 Hits = 0;
		for (int32 i = 0; i < 5; ++i)
		{
			FVector N;
			if (TraceNormalAt(Xcm + Offsets[i][0], Ycm + Offsets[i][1], N)) { Sum += N; ++Hits; }
		}
		if (Hits == 0) { return FVector::UpVector; }
		const FVector Avg = Sum.GetSafeNormal();
		return Avg.IsNearlyZero() ? FVector::UpVector : Avg;
	};
}

void UCourseSurfaceSubsystem::Deinitialize()
{
	if (UEventBusSubsystem* Bus = EventBusWeak.Get())
	{
		Bus->SurfaceProvider = nullptr;   // drop the lie source so the bus doesn't see a dangling lambda
		Bus->GroundNormalProvider = nullptr;   // GOL-196: same for the normal source
	}
	EventBusWeak.Reset();
	Super::Deinitialize();
}
