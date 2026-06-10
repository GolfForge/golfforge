// URoundPinSubsystem (GOL-117) -- the first visible child of GOL-112 / GOL-116. Listens to the
// round events URoundSubsystem publishes and places an AGolfPinActor at each hole's PinWorldLoc.
// Same actor type the range uses (GOL-29); just managed by the round flow instead of the range
// HUD's spinner.
//
// At round.start it spawns a pin at EVERY scheduled hole's green (so all greens read as flagged,
// not just the one in play -- GOL-165 follow-up); hole.start then lights up the active hole's gimme
// ring and hides the others'. Decorative pins are full pins (disc + pole + fluttering flag), just
// with the ring collapsed.
//
// Co-existence with the range HUD: at round.start, we destroy every existing pin in the level
// (the range HUD's pin included). The HUD's Tick-retry respawn is guarded by URoundSubsystem::
// IsActive() so it doesn't immediately resurrect the range pin during the round. On round.complete,
// all our pins are destroyed and the HUD takes over again on the next frame.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription, EEventKind
#include "RoundPinSubsystem.generated.h"

class AGolfPinActor;

UCLASS()
class GOLFSIM_API URoundPinSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** The active hole's pin (null when no round is active). Exposed for future siblings (GOL-119
	 *  hole-out detection wants the pin world XY without re-reading the schedule). */
	AGolfPinActor* GetPin() const
	{
		const TWeakObjectPtr<AGolfPinActor>* Found = HolePins.Find(ActiveHoleRef);
		return Found ? Found->Get() : nullptr;
	}

private:
	void OnRoundStart(const FGolfEvent& Event);
	void OnHoleStart(const FGolfEvent& Event);
	void OnRoundComplete(const FGolfEvent& Event);

	/** Spawn a pin at every scheduled hole's green (ground-snapped, gimme ring collapsed). Best-effort:
	 *  if the schedule can't be read, OnHoleStart still find-or-spawns the active hole's pin. */
	void SpawnAllHolePins();

	/** Make HoleRef active: move its pin to the authoritative snapped PinWorldLoc and show its gimme
	 *  ring (sized to the round difficulty); collapse the previously-active pin's ring. Find-or-spawns
	 *  the pin if SpawnAllHolePins didn't (fallback / single-hole handoff). */
	void SetActiveHole(int32 HoleRef, const FVector& SnappedPinLoc, const FVector& TeeWorldLoc);

	/** Gimme-ring radius (ft) for the active round difficulty; 0 when no round is active. */
	double ActiveGimmeRadiusFt() const;

	/** Destroy every AGolfPinActor in the world (covers the range HUD's pin + a leak from any prior
	 *  abandoned round). Clears the per-hole map + active index. */
	void DestroyAllPins();

	/** Vertical line trace down to the landscape; mirrors URoundSubsystem::SnapToGround so decorative
	 *  pins sit on the ground just like the event-published active pin. */
	static FVector SnapPinToGround(UWorld* World, const FVector& WorldXyz);

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	TArray<FGolfEventSubscription> Subscriptions;
	TMap<int32, TWeakObjectPtr<AGolfPinActor>> HolePins;   // keyed by hole Ref
	int32 ActiveHoleRef = INDEX_NONE;
};
