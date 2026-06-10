#include "Practice/PracticeModeSubsystem.h"

#include "Events/EventBusSubsystem.h"
#include "Events/EventTypes.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

using namespace GolfsimPractice;

void UPracticeModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UEventBusSubsystem::StaticClass());

	if (UEventBusSubsystem* EBus = GetGameInstance()->GetSubsystem<UEventBusSubsystem>())
	{
		EventBusWeak = EBus;
	}
}

void UPracticeModeSubsystem::Deinitialize()
{
	EventBusWeak = nullptr;
	Super::Deinitialize();
}

UPracticeModeSubsystem* UPracticeModeSubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UPracticeModeSubsystem>();
		}
	}
	return nullptr;
}

void UPracticeModeSubsystem::StartCtpSession(const FCtpConfig& InConfig)
{
	Mode = EPracticeMode::ClosestToPin;
	Config = InConfig;
	Session = FCtpSession{};
	Session.Mode = EPracticeMode::ClosestToPin;

	// Seed from the wall clock so each session's pin sequence differs; the pure core stays
	// deterministic (its tests pass their own seeded stream).
	Stream.Initialize((int32)(FDateTime::Now().GetTicks() & 0x7FFFFFFF));

	UE_LOG(LogTemp, Display, TEXT("golfsim Practice: CTP session started [min=%.1fm max=%.1fm side=%d puttout=%d]"),
		Config.MinM, Config.MaxM, Config.bSideOffset ? 1 : 0, Config.bPuttOut ? 1 : 0);
}

void UPracticeModeSubsystem::EndSession()
{
	Mode = EPracticeMode::Free;
	UE_LOG(LogTemp, Display, TEXT("golfsim Practice: session ended [%d attempts]"), AttemptCount(Session));
}

FCtpPin UPracticeModeSubsystem::NextPin()
{
	return GolfsimPractice::NextPin(Config, Stream);
}

void UPracticeModeSubsystem::RecordCarry(double DistanceM)
{
	FCtpAttempt A;
	A.DistanceM = DistanceM;
	A.Strokes = 1;
	A.bPuttedOut = false;
	RecordAttempt(Session, A);
	PublishScored(A);
}

void UPracticeModeSubsystem::RecordPuttOut(int32 Strokes, double FinalDistanceM)
{
	FCtpAttempt A;
	A.DistanceM = FinalDistanceM;
	A.Strokes = FMath::Max(1, Strokes);
	A.bPuttedOut = true;
	RecordAttempt(Session, A);
	PublishScored(A);
}

void UPracticeModeSubsystem::PublishScored(const FCtpAttempt& Attempt)
{
	UEventBusSubsystem* EBus = EventBusWeak.Get();
	if (!EBus)
	{
		return;
	}

	FPracticeShotScoredEvent Ev;
	Ev.Source         = TEXT("practice-ctp");
	Ev.PlayerId       = GolfsimEvents::LocalPlayerId();
	Ev.DistanceToPinM = Attempt.DistanceM;
	Ev.Strokes        = Attempt.Strokes;
	Ev.bPuttedOut     = Attempt.bPuttedOut;
	Ev.AttemptIndex   = AttemptCount(Session);   // 1-based: the attempt was just appended
	Ev.BestDistanceM  = BestDistanceM(Session);
	Ev.AvgDistanceM   = AvgDistanceM(Session);
	Ev.BestStrokes    = BestStrokes(Session);
	Ev.AvgStrokes     = AvgStrokes(Session);
	EBus->Publish(Ev);
}
