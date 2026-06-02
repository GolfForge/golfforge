#include "Round/RoundTeeUpSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"   // TActorIterator
#include "Events/EventTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GolfBallActor.h"
#include "Round/RoundSubsystem.h"

namespace
{
	// Fallback if the pawn isn't an ACharacter (no capsule to query). ACharacter's default capsule
	// half-height is 88 cm; the FirstPerson template uses the default.
	constexpr float DefaultCapsuleHalfHeightCm = 88.f;

	float QueryCapsuleHalfHeight(APawn* Pawn)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			if (UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
			{
				return Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		return DefaultCapsuleHalfHeightCm;
	}
}

void URoundTeeUpSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UGameInstance* GI = InWorld.GetGameInstance();
	UEventBusSubsystem* Bus = GI ? GI->GetSubsystem<UEventBusSubsystem>() : nullptr;
	if (!Bus)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoundTeeUpSubsystem: no EventBus on world; pawn will not teleport on round events"));
		return;
	}
	EventBusWeak = Bus;

	Subscriptions.Add(Bus->Subscribe(EEventKind::HoleStart,
		[this](const FGolfEvent& Event) { OnHoleStart(Event); }));
	Subscriptions.Add(Bus->Subscribe(EEventKind::ShotOutcome,
		[this](const FGolfEvent& Event) { OnShotOutcome(Event); }));
}

void URoundTeeUpSubsystem::Deinitialize()
{
	if (UEventBusSubsystem* Bus = EventBusWeak.Get())
	{
		for (const FGolfEventSubscription& Sub : Subscriptions)
		{
			Bus->Unsubscribe(Sub);
		}
	}
	Subscriptions.Reset();
	EventBusWeak.Reset();
	ShotBallActor.Reset();
	Super::Deinitialize();
}

void URoundTeeUpSubsystem::Tick(float /*DeltaTime*/)
{
	if (!bTeeUpPending && !bAwaitingBallSettle) { return; }

	if (bTeeUpPending)
	{
		if (TryApplyTeeUp())
		{
			bTeeUpPending = false;
		}
		// else: pawn not possessed yet; retry next frame.
	}

	if (bAwaitingBallSettle)
	{
		AGolfBallActor* Ball = ShotBallActor.Get();
		if (!Ball || !Ball->IsPlaying())
		{
			// Either the ball was destroyed (defensive) or playback finished -- teleport pawn there.
			if (Ball && RoundIsActive())
			{
				ApplyBetweenShotTeleport(Ball);
			}
			bAwaitingBallSettle = false;
			ShotBallActor.Reset();
		}
	}
}

void URoundTeeUpSubsystem::OnHoleStart(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::HoleStart) { return; }
	const FHoleStartEvent& HS = static_cast<const FHoleStartEvent&>(Event);

	PendingTeeLoc = HS.TeeWorldLoc;
	// XY-only facing direction: green - tee, Z zeroed so the camera doesn't pitch toward the green's
	// elevation (would feel like looking at the ground or into the sky depending on relative height).
	FVector Dir = HS.GreenWorldLoc - HS.TeeWorldLoc;
	Dir.Z = 0.0;
	if (Dir.IsNearlyZero())
	{
		// Degenerate hole (tee == green); face +X by default.
		PendingFacingDir = FVector::ForwardVector;
	}
	else
	{
		PendingFacingDir = Dir.GetSafeNormal();
	}
	bTeeUpPending = true;

	// Try immediately so the common case (pawn already possessed) lands without a frame of delay.
	if (TryApplyTeeUp())
	{
		bTeeUpPending = false;
	}
	// Cancel any pending between-shot teleport from a prior hole (defensive: HoleStart should always
	// follow a HoleComplete, but if it doesn't we don't want to teleport the pawn to the previous
	// hole's ball rest position).
	bAwaitingBallSettle = false;
	ShotBallActor.Reset();
}

void URoundTeeUpSubsystem::OnShotOutcome(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotOutcome) { return; }
	if (!RoundIsActive())
	{
		// Range shots / post-round shots: leave the pawn alone.
		return;
	}
	UWorld* World = GetWorld();
	if (!World) { return; }

	// There's typically one AGolfBallActor in PIE -- the range HUD or console fire reuses it.
	for (TActorIterator<AGolfBallActor> It(World); It; ++It)
	{
		ShotBallActor = *It;
		break;
	}
	if (ShotBallActor.IsValid())
	{
		bAwaitingBallSettle = true;
	}
}

bool URoundTeeUpSubsystem::TryApplyTeeUp()
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) { return false; }
	APawn* Pawn = PC->GetPawn();
	if (!Pawn) { return false; }

	const float HalfHeight = QueryCapsuleHalfHeight(Pawn);
	const FVector Target(PendingTeeLoc.X, PendingTeeLoc.Y, PendingTeeLoc.Z + HalfHeight);
	Pawn->SetActorLocation(Target, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);

	const FRotator FacingRot = PendingFacingDir.ToOrientationRotator();
	// Zero pitch + roll for first-person camera comfort; only yaw matters for "face the green".
	const FRotator ClampedRot(0.f, FacingRot.Yaw, 0.f);
	Pawn->SetActorRotation(ClampedRot);
	PC->SetControlRotation(ClampedRot);

	UE_LOG(LogTemp, Display,
		TEXT("RoundTeeUpSubsystem: teleported pawn to tee (%.0f, %.0f, %.0f) yaw=%.1f"),
		Target.X, Target.Y, Target.Z, ClampedRot.Yaw);
	return true;
}

void URoundTeeUpSubsystem::ApplyBetweenShotTeleport(AGolfBallActor* Ball)
{
	UWorld* World = GetWorld();
	if (!World || !Ball) { return; }
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) { return; }

	const FVector BallLoc = Ball->GetActorLocation();
	const float HalfHeight = QueryCapsuleHalfHeight(Pawn);
	// Ball Z is its mesh center (~6 cm above ground per BallRestHeightUU); subtract that + add the
	// pawn's half-height so the capsule sits on the same turf the ball is resting on.
	const float GroundZ = BallLoc.Z - AGolfBallActor::BallRestHeightUU;
	const FVector Target(BallLoc.X, BallLoc.Y, GroundZ + HalfHeight);
	// Preserve current yaw -- the player was facing the green at tee-up; keep that aim. Future
	// polish (auto-aim toward the pin) lives in a sibling ticket.
	Pawn->SetActorLocation(Target, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);

	UE_LOG(LogTemp, Display,
		TEXT("RoundTeeUpSubsystem: ball settled, teleported pawn to (%.0f, %.0f, %.0f)"),
		Target.X, Target.Y, Target.Z);
}

bool URoundTeeUpSubsystem::RoundIsActive() const
{
	if (const URoundSubsystem* Sub = URoundSubsystem::Get(this))
	{
		return Sub->IsActive();
	}
	return false;
}
