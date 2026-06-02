// URoundHoleOutSubsystem (GOL-119) -- auto hole-out detection. Polls AGolfBallActor::IsPlaying()
// on Tick (same trigger as URoundTeeUpSubsystem's between-shot teleport, different action). When
// the ball settles within GimmeRadiusFt of the active hole's pin, fires URoundSubsystem::OnHoleHoled.
//
// Co-existence with URoundTeeUpSubsystem: both poll IsPlaying(), Tick order between subsystems
// isn't guaranteed. The 1-frame visual glitch (TeeUp teleports to ball-rest first, then HoleOut
// fires and TeeUp teleports to the next tee) is acceptable for v1; cleanest case (HoleOut Tick
// first) is handled by TeeUp's existing OnHoleStart clear of bAwaitingBallSettle.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription, EEventKind
#include "RoundHoleOutSubsystem.generated.h"

class AGolfBallActor;

UCLASS()
class GOLFSIM_API URoundHoleOutSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URoundHoleOutSubsystem, STATGROUP_Tickables); }

private:
	void OnShotOutcome(const FGolfEvent& Event);
	bool RoundIsActive() const;

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	FGolfEventSubscription OutcomeSub;

	bool bAwaitingBallSettle = false;
	TWeakObjectPtr<AGolfBallActor> ShotBallActor;
};
