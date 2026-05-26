#include "Events/EventBusSubsystem.h"

#include "Physics/BallFlightSolver.h"
#include "Physics/BallFlightTypes.h"

#include "Engine/Engine.h"          // GEngine->GetWorldFromContextObject
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// --- FGolfEventBus (pure C++) ------------------------------------------------------------------

FGolfEventSubscription FGolfEventBus::Subscribe(EEventKind Kind, FGolfEventDelegate Callback)
{
	const int64 Id = NextId++;
	Channels.FindOrAdd(Kind).Add(FEntry{ Id, MoveTemp(Callback) });
	return FGolfEventSubscription{ Id, Kind };
}

void FGolfEventBus::Unsubscribe(const FGolfEventSubscription& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	if (TArray<FEntry>* Entries = Channels.Find(Handle.Kind))
	{
		Entries->RemoveAll([&Handle](const FEntry& E) { return E.Id == Handle.Id; });
	}
}

int32 FGolfEventBus::NumSubscribers(EEventKind Kind) const
{
	const TArray<FEntry>* Entries = Channels.Find(Kind);
	return Entries ? Entries->Num() : 0;
}

void FGolfEventBus::Dispatch(EEventKind Kind, const FGolfEvent& Event)
{
	const TArray<FEntry>* Entries = Channels.Find(Kind);
	if (!Entries || Entries->Num() == 0)
	{
		return;
	}
	// Snapshot so a subscriber may (un)subscribe or publish a follow-on event during dispatch
	// without reallocating the array we're iterating.
	TArray<FEntry> Snapshot = *Entries;
	for (const FEntry& E : Snapshot)
	{
		if (E.Callback)
		{
			E.Callback(Event);
		}
	}
}

// --- UEventBusSubsystem ------------------------------------------------------------------------

void UEventBusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IntegratorSub = EventBus.Subscribe(EEventKind::ShotTaken,
		[this](const FGolfEvent& Event) { OnShotTaken(Event); });

	UE_LOG(LogTemp, Display,
		TEXT("golfsim EventBus: subsystem initialized (integrator subscribed to shot.taken)"));
}

void UEventBusSubsystem::Deinitialize()
{
	EventBus.Unsubscribe(IntegratorSub);
	Super::Deinitialize();
}

UEventBusSubsystem* UEventBusSubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UEventBusSubsystem>();
		}
	}
	return nullptr;
}

void UEventBusSubsystem::OnShotTaken(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotTaken)
	{
		return;   // defensive: only ever wired to the ShotTaken channel
	}
	const FShotTakenEvent& Shot = static_cast<const FShotTakenEvent&>(Event);

	// shot.taken (the bus envelope) -> FShotInput (the solver's pure-SI struct).
	FShotInput In;
	In.BallSpeedMps   = Shot.BallSpeedMps;
	In.LaunchAngleDeg = Shot.LaunchAngleDeg;
	In.AzimuthDeg     = Shot.AzimuthDeg;
	In.BackspinRpm    = Shot.BackspinRpm;
	In.SidespinRpm    = Shot.SidespinRpm;
	In.SmashFactor    = Shot.SmashFactor;

	const FBallTrajectory T = GolfBallFlight::Simulate(In);

	FShotOutcomeEvent Out;
	Out.Source         = TEXT("sim");
	Out.PlayerId       = Shot.PlayerId;
	Out.Trajectory     = T;                  // in-process render payload for the view
	Out.CarryM         = T.CarryM;
	Out.TotalM         = T.CarryM;           // no ground roll yet (GOL-9)
	Out.LateralOffsetM = T.LateralOffsetM;
	Out.FinalLie       = TEXT("unknown");    // course collision not wired on the range
	Out.bInHole        = false;

	UE_LOG(LogTemp, Display,
		TEXT("golfsim EventBus: shot.taken(%s) -> session.shot_outcome carry=%.1fm lateral=%.1fm valid=%d"),
		*Shot.Club, Out.CarryM, Out.LateralOffsetM, T.bValid ? 1 : 0);

	EventBus.Publish(Out);
}
