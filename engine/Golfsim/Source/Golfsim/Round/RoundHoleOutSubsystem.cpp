#include "Round/RoundHoleOutSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"   // TActorIterator
#include "Events/EventTypes.h"
#include "GolfBallActor.h"
#include "Input/KeyboardSwingComponent.h"   // FSwingDifficultyProfile::For
#include "Round/RoundState.h"               // GolfsimRound::IsWithinGimme
#include "Round/RoundSubsystem.h"

void URoundHoleOutSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UGameInstance* GI = InWorld.GetGameInstance();
	UEventBusSubsystem* Bus = GI ? GI->GetSubsystem<UEventBusSubsystem>() : nullptr;
	if (!Bus)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoundHoleOutSubsystem: no EventBus on world; hole-out detection disabled"));
		return;
	}
	EventBusWeak = Bus;
	OutcomeSub = Bus->Subscribe(EEventKind::ShotOutcome,
		[this](const FGolfEvent& Event) { OnShotOutcome(Event); });
}

void URoundHoleOutSubsystem::Deinitialize()
{
	if (UEventBusSubsystem* Bus = EventBusWeak.Get())
	{
		Bus->Unsubscribe(OutcomeSub);
	}
	OutcomeSub = FGolfEventSubscription{};
	EventBusWeak.Reset();
	ShotBallActor.Reset();
	Super::Deinitialize();
}

void URoundHoleOutSubsystem::Tick(float /*DeltaTime*/)
{
	if (!bAwaitingBallSettle) { return; }

	AGolfBallActor* Ball = ShotBallActor.Get();
	if (!Ball)
	{
		bAwaitingBallSettle = false;
		return;
	}
	if (Ball->IsPlaying()) { return; }

	// Ball settled. Check distance to pin against the active difficulty's gimme radius.
	bAwaitingBallSettle = false;
	const FVector BallLoc = Ball->GetActorLocation();
	ShotBallActor.Reset();

	URoundSubsystem* Round = URoundSubsystem::Get(this);
	if (!Round || !Round->IsActive()) { return; }   // round ended between shot.outcome and settle

	const GolfsimRound::FRoundState& State = Round->GetState();
	if (!State.Schedule.IsValidIndex(State.HoleIndex)) { return; }
	const FVector PinLoc = State.Schedule[State.HoleIndex].PinWorldLoc;

	const auto Profile = GolfsimKeyboardSwing::FSwingDifficultyProfile::For(State.Difficulty);
	if (!GolfsimRound::IsWithinGimme(BallLoc, PinLoc, Profile.GimmeRadiusFt))
	{
		return;
	}

	const FVector2D Delta(BallLoc.X - PinLoc.X, BallLoc.Y - PinLoc.Y);
	UE_LOG(LogTemp, Display,
		TEXT("RoundHoleOutSubsystem: ball settled %.1f cm from pin (gimme %.1f ft) -- holing out"),
		Delta.Size(), Profile.GimmeRadiusFt);
	Round->OnHoleHoled();
}

void URoundHoleOutSubsystem::OnShotOutcome(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotOutcome) { return; }
	if (!RoundIsActive()) { return; }
	UWorld* World = GetWorld();
	if (!World) { return; }

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

bool URoundHoleOutSubsystem::RoundIsActive() const
{
	if (const URoundSubsystem* Sub = URoundSubsystem::Get(this))
	{
		return Sub->IsActive();
	}
	return false;
}
