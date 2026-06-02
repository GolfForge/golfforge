// URoundTeeUpSubsystem (GOL-118) -- pawn teleport at each hole.start + between-shot teleport
// to the ball's rest position. Per-world lifetime; mirrors URoundPinSubsystem's shape but
// adds Tick (pawn-possession retry + AGolfBallActor playback poll for shot settle).
//
// Decisions vs the ticket's spec:
//   - "Option (a) instant teleport to ball" is in scope; walk-up animation is a sibling polish.
//   - No inter-hole camera blend in v1 (instant snap). Sibling polish if it feels harsh.
//   - No input gating (instant snap; nothing to gate).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription, EEventKind
#include "RoundTeeUpSubsystem.generated.h"

class AGolfBallActor;

UCLASS()
class GOLFSIM_API URoundTeeUpSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URoundTeeUpSubsystem, STATGROUP_Tickables); }

private:
	void OnHoleStart(const FGolfEvent& Event);
	void OnShotOutcome(const FGolfEvent& Event);

	/** Teleport pawn to PendingTeeLoc + face PendingFacingDir. Returns false if the pawn isn't
	 *  possessed yet (caller will retry on the next Tick). */
	bool TryApplyTeeUp();

	/** Called from Tick when the ball settles. Teleports pawn to the ball's world location. */
	void ApplyBetweenShotTeleport(AGolfBallActor* Ball);

	bool RoundIsActive() const;

	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	TArray<FGolfEventSubscription> Subscriptions;

	// Pending tee-up (set by OnHoleStart; consumed by Tick retry until pawn is possessed).
	bool bTeeUpPending = false;
	FVector PendingTeeLoc = FVector::ZeroVector;        // tee XYZ in cm (Z already ground-snapped)
	FVector PendingFacingDir = FVector::ForwardVector;  // XY-only direction toward the green

	// Pending between-shot ball-rest teleport (set by OnShotOutcome; consumed by Tick poll).
	bool bAwaitingBallSettle = false;
	TWeakObjectPtr<AGolfBallActor> ShotBallActor;
};
