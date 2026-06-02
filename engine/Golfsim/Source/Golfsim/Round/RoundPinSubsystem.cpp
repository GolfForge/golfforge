#include "Round/RoundPinSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"   // TActorIterator
#include "Events/EventTypes.h"
#include "Range/GolfPinActor.h"

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
	ManagedPin.Reset();
	Super::Deinitialize();
}

void URoundPinSubsystem::OnRoundStart(const FGolfEvent& /*Event*/)
{
	// Wipe the range HUD's pin (if any) so we don't have two pins competing for attention. The
	// hole.start handler (which always immediately follows -- see URoundSubsystem::ApplyStep)
	// will place our managed pin at hole 1's green.
	DestroyAllPins();
}

void URoundPinSubsystem::OnHoleStart(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::HoleStart) { return; }
	const FHoleStartEvent& HS = static_cast<const FHoleStartEvent&>(Event);
	UWorld* World = GetWorld();
	if (!World) { return; }

	// PinWorldLoc was already ground-snapped by URoundSubsystem::SnapToGround before publish; no
	// extra trace needed here.
	AGolfPinActor* Pin = ManagedPin.Get();
	if (!Pin)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Pin = World->SpawnActor<AGolfPinActor>(AGolfPinActor::StaticClass(),
			HS.PinWorldLoc, FRotator::ZeroRotator, Sp);
		ManagedPin = Pin;
		UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: spawned pin for hole %d at (%.0f, %.0f, %.0f)"),
			HS.HoleRef, HS.PinWorldLoc.X, HS.PinWorldLoc.Y, HS.PinWorldLoc.Z);
	}
	else
	{
		Pin->SetActorLocationAndRotation(HS.PinWorldLoc, FRotator::ZeroRotator);
		UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: moved pin to hole %d at (%.0f, %.0f, %.0f)"),
			HS.HoleRef, HS.PinWorldLoc.X, HS.PinWorldLoc.Y, HS.PinWorldLoc.Z);
	}
}

void URoundPinSubsystem::OnRoundComplete(const FGolfEvent& /*Event*/)
{
	if (AGolfPinActor* Pin = ManagedPin.Get())
	{
		Pin->Destroy();
	}
	ManagedPin.Reset();
	UE_LOG(LogTemp, Display, TEXT("RoundPinSubsystem: round complete -- pin destroyed; range HUD will respawn its own."));
}

void URoundPinSubsystem::DestroyAllPins()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	for (TActorIterator<AGolfPinActor> It(World); It; ++It)
	{
		if (AActor* A = *It) { A->Destroy(); }
	}
	ManagedPin.Reset();
}
