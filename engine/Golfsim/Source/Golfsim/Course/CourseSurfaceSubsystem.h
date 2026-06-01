// Course-level lie source (GOL-40). Activates on known course level names (see
// CourseIdByLevelName() table), loads the matching FCourseSurfaceSampler, and wires the
// EventBusSubsystem::SurfaceProvider seam so the ground-roll integrator (GOL-9, GOL-38)
// reads lies from the shipped splatmap PNGs. Same role AGolfRangeHUD plays for the range
// today, but as a UWorldSubsystem so the course level doesn't need a HUD actor yet --
// when the game loop lands (GOL-69) it can subscribe to events without touching this.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Physics/CourseSurface.h"
#include "CourseSurfaceSubsystem.generated.h"

class UEventBusSubsystem;

UCLASS()
class GOLFSIM_API UCourseSurfaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Direct probe for console commands / tests. Returns Unknown when the sampler isn't loaded. */
	EGolfLie ClassifyAt(double WorldXm, double WorldYm) const { return Sampler.ClassifyAt(WorldXm, WorldYm); }

	bool IsValid() const { return Sampler.IsValid(); }

private:
	FCourseSurfaceSampler Sampler;
	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;   // cleared in Deinitialize to drop the provider
};
