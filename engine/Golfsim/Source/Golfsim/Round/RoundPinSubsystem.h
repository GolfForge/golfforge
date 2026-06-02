// URoundPinSubsystem (GOL-117) -- the first visible child of GOL-112 / GOL-116. Listens to the
// round events URoundSubsystem publishes and find-or-spawns an AGolfPinActor at each hole's
// PinWorldLoc. Same actor type the range uses (GOL-29); just managed by the round flow instead
// of the range HUD's spinner.
//
// Co-existence with the range HUD: at round.start, we destroy every existing pin in the level
// (the range HUD's pin included). The HUD's Tick-retry respawn is guarded by URoundSubsystem::
// IsActive() so it doesn't immediately resurrect the range pin during the round. On round.complete,
// our pin is destroyed and the HUD takes over again on the next frame.

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

	/** The active pin (null when no round is active). Exposed for future siblings (GOL-119
	 *  hole-out detection wants the pin world XY without re-reading the schedule). */
	AGolfPinActor* GetPin() const { return ManagedPin.Get(); }

private:
	void OnRoundStart(const FGolfEvent& Event);
	void OnHoleStart(const FGolfEvent& Event);
	void OnRoundComplete(const FGolfEvent& Event);

	/** Destroy every AGolfPinActor in the world (covers the range HUD's pin + a leak from any
	 *  prior abandoned round). Resets the managed pointer. */
	void DestroyAllPins();

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	TArray<FGolfEventSubscription> Subscriptions;
	TWeakObjectPtr<AGolfPinActor> ManagedPin;
};
