#include "Round/RoundPinSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"   // TActorIterator
#include "Events/EventTypes.h"
#include "Input/KeyboardSwingComponent.h"   // FSwingDifficultyProfile::For (GOL-123 gimme ring)
#include "Range/GolfPinActor.h"
#include "Round/RoundState.h"               // FHoleSpec / FRoundState (the hole schedule)
#include "Round/RoundSubsystem.h"           // schedule + active difficulty lookup

void URoundPinSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UGameInstance* GI = InWorld.GetGameInstance();
	UEventBusSubsystem* Bus = GI ? GI->GetSubsystem<UEventBusSubsystem>() : nullptr;
	if (!Bus)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoundPinSubsystem: no EventBus on world; pin will not follow round events"));
		return;
	}
	EventBusWeak = Bus;

	Subscriptions.Add(Bus->Subscribe(EEventKind::RoundStart,
		[this](const FGolfEvent& Event) { OnRoundStart(Event); }));
	Subscriptions.Add(Bus->Subscribe(EEventKind::HoleStart,
		[this](const FGolfEvent& Event) { OnHoleStart(Event); }));
	Subscriptions.Add(Bus->Subscribe(EEventKind::RoundComplete,
		[this](const FGolfEvent& Event) { OnRoundComplete(Event); }));
}

void URoundPinSubsystem::Deinitialize()
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
	HolePins.Reset();
	ActiveHoleRef = INDEX_NONE;
	Super::Deinitialize();
}

void URoundPinSubsystem::OnRoundStart(const FGolfEvent& /*Event*/)
{
	// Wipe the range HUD's pin (if any) so it doesn't compete, then plant a pin at every hole on the
	// course so all greens read as flagged -- not just the one in play (GOL-165 follow-up). The
	// hole.start handler that always immediately follows (see URoundSubsystem::ApplyStep) lights up
	// hole 1's gimme ring.
	DestroyAllPins();
	SpawnAllHolePins();
}

void URoundPinSubsystem::SpawnAllHolePins()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	const URoundSubsystem* Round = URoundSubsystem::Get(this);
	if (!Round) { return; }   // OnHoleStart still find-or-spawns the active pin as a fallback

	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 Spawned = 0;
	for (const GolfsimRound::FHoleSpec& H : Round->GetState().Schedule)
	{
		// The schedule's PinWorldLoc is the raw lon/lat projection (Z=0); snap each to the landscape
		// ourselves -- only the active hole's pin gets a pre-snapped location via the hole.start event.
		const FVector PinLoc = SnapPinToGround(World, H.PinWorldLoc);
		AGolfPinActor* Pin = World->SpawnActor<AGolfPinActor>(AGolfPinActor::StaticClass(),
			PinLoc, FRotator::ZeroRotator, Sp);
		if (!Pin) { continue; }
		Pin->SetGimmeRadiusFt(0.0);   // decorative until it becomes the active hole
		HolePins.Add(H.Ref, Pin);
		++Spawned;
	}
	UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: placed %d hole pins"), Spawned);
}

void URoundPinSubsystem::OnHoleStart(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::HoleStart) { return; }
	const FHoleStartEvent& HS = static_cast<const FHoleStartEvent&>(Event);
	// PinWorldLoc was already ground-snapped by URoundSubsystem::SnapToGround before publish.
	SetActiveHole(HS.HoleRef, HS.PinWorldLoc);
}

void URoundPinSubsystem::SetActiveHole(int32 HoleRef, const FVector& SnappedPinLoc)
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	// Collapse the ring on the hole we're leaving.
	if (ActiveHoleRef != HoleRef)
	{
		if (TWeakObjectPtr<AGolfPinActor>* Prev = HolePins.Find(ActiveHoleRef))
		{
			if (AGolfPinActor* PrevPin = Prev->Get()) { PrevPin->SetGimmeRadiusFt(0.0); }
		}
	}
	ActiveHoleRef = HoleRef;

	// Find-or-spawn the active hole's pin. SpawnAllHolePins normally already made it; this covers the
	// case where the schedule wasn't readable at round.start, or a range-to-round single-hole handoff.
	TWeakObjectPtr<AGolfPinActor>& Slot = HolePins.FindOrAdd(HoleRef);
	AGolfPinActor* Pin = Slot.Get();
	if (!Pin)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Pin = World->SpawnActor<AGolfPinActor>(AGolfPinActor::StaticClass(),
			SnappedPinLoc, FRotator::ZeroRotator, Sp);
		Slot = Pin;
	}
	if (!Pin) { return; }

	// The event's PinWorldLoc is the authoritative snap -- keep the active pin exactly there (it should
	// already match SpawnAllHolePins' own trace) and size its gimme ring to the round difficulty.
	Pin->SetActorLocationAndRotation(SnappedPinLoc, FRotator::ZeroRotator);
	Pin->SetGimmeRadiusFt(ActiveGimmeRadiusFt());

	UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: active hole %d at (%.0f, %.0f, %.0f)"),
		HoleRef, SnappedPinLoc.X, SnappedPinLoc.Y, SnappedPinLoc.Z);
}

double URoundPinSubsystem::ActiveGimmeRadiusFt() const
{
	// GOL-123: ring radius tracks the active difficulty. 0 when no round is active (collapses the ring).
	if (const URoundSubsystem* Round = URoundSubsystem::Get(this))
	{
		if (Round->IsActive())
		{
			return GolfsimKeyboardSwing::FSwingDifficultyProfile::For(Round->GetState().Difficulty).GimmeRadiusFt;
		}
	}
	return 0.0;
}

void URoundPinSubsystem::OnRoundComplete(const FGolfEvent& /*Event*/)
{
	DestroyAllPins();
	UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: round complete -- all hole pins destroyed; range HUD will respawn its own."));
}

void URoundPinSubsystem::DestroyAllPins()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	for (TActorIterator<AGolfPinActor> It(World); It; ++It)
	{
		if (AActor* A = *It) { A->Destroy(); }
	}
	HolePins.Reset();
	ActiveHoleRef = INDEX_NONE;
}

FVector URoundPinSubsystem::SnapPinToGround(UWorld* World, const FVector& WorldXyz)
{
	if (!World) { return WorldXyz; }
	const FVector Start(WorldXyz.X, WorldXyz.Y,  200000.0);   // 2 km up; matches URoundSubsystem
	const FVector End  (WorldXyz.X, WorldXyz.Y, -200000.0);
	FCollisionQueryParams P(SCENE_QUERY_STAT(GolfsimRoundPinGroundTrace), /*bTraceComplex=*/true);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, P))
	{
		return FVector(WorldXyz.X, WorldXyz.Y, Hit.ImpactPoint.Z);
	}
	return WorldXyz;   // off-landscape -- leave the raw Z
}
