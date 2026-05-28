// The in-process pub/sub the whole architecture rides on (CLAUDE.md invariant #2, docs/plan.md
// day-one decision). Two layers:
//
//   FGolfEventBus      -- pure C++ (no UObject, no engine delegates): a typed channel per
//                         EEventKind, synchronous game-thread dispatch, zero serialization.
//                         Unit-testable headless (no world / no RHI) -- see Tests/EventBusTests.cpp.
//   UEventBusSubsystem -- a UGameInstanceSubsystem that owns one FGolfEventBus and hosts the
//                         built-in integrator subscriber (shot.taken -> solver -> shot.outcome).
//
// Reach it from any Actor: UEventBusSubsystem::Get(this) (or
// GetGameInstance()->GetSubsystem<UEventBusSubsystem>()).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Events/EventTypes.h"
#include "Physics/GroundRoll.h"   // EGolfLie (the SurfaceProvider return type)

#include <type_traits>   // std::is_base_of (Publish<> payload guard); after the UE includes

#include "EventBusSubsystem.generated.h"

/** A subscriber callback. Receives the envelope base; downcast on Event.Kind. */
using FGolfEventDelegate = TFunction<void(const FGolfEvent& /*Event*/)>;

/** Opaque handle returned by Subscribe; pass to Unsubscribe. */
struct FGolfEventSubscription
{
	int64 Id = 0;
	EEventKind Kind = EEventKind::None;

	bool IsValid() const { return Id != 0; }
};

/**
 * In-process typed pub/sub. Single channel per EEventKind. Publish dispatches synchronously on the
 * calling (game) thread to a snapshot of the channel's subscribers, so a subscriber may publish a
 * follow-on event or unsubscribe mid-dispatch without invalidating iteration. No serialization --
 * multiplayer later wraps the same envelope for the wire.
 */
class GOLFSIM_API FGolfEventBus
{
public:
	FGolfEventSubscription Subscribe(EEventKind Kind, FGolfEventDelegate Callback);
	void Unsubscribe(const FGolfEventSubscription& Handle);

	/** Publish a concrete payload to every subscriber of its Kind. */
	template <typename TEvent>
	void Publish(const TEvent& Event)
	{
		static_assert(std::is_base_of<FGolfEvent, TEvent>::value,
			"FGolfEventBus::Publish requires an FGolfEvent-derived payload");
		Dispatch(Event.Kind, static_cast<const FGolfEvent&>(Event));
	}

	int32 NumSubscribers(EEventKind Kind) const;

private:
	void Dispatch(EEventKind Kind, const FGolfEvent& Event);

	struct FEntry
	{
		int64 Id = 0;
		FGolfEventDelegate Callback;
	};

	TMap<EEventKind, TArray<FEntry>> Channels;
	int64 NextId = 1;
};

/**
 * GameInstance-scoped owner of the event bus. Auto-created for every UGameInstance; subscribes the
 * built-in ball-flight integrator at Initialize so shot.taken events resolve into shot.outcome
 * events without any HUD/driver doing it directly.
 */
UCLASS()
class GOLFSIM_API UEventBusSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The owned bus. Producers/subscribers go through Publish/Subscribe below; this is for tests. */
	FGolfEventBus& Bus() { return EventBus; }

	template <typename TEvent>
	void Publish(const TEvent& Event) { EventBus.Publish(Event); }

	FGolfEventSubscription Subscribe(EEventKind Kind, FGolfEventDelegate Callback)
	{
		return EventBus.Subscribe(Kind, MoveTemp(Callback));
	}
	void Unsubscribe(const FGolfEventSubscription& Handle) { EventBus.Unsubscribe(Handle); }

	/** Resolve the subsystem from any UObject with a world. Null outside a running game/PIE world. */
	static UEventBusSubsystem* Get(const UObject* WorldContext);

	/**
	 * Maps a solver-frame landing position (SI meters, launch-local: +X downrange, +Y right) to the
	 * surface there. Injected by whatever owns the world geometry (the range HUD registers one that
	 * maps launch-local -> world -> ClassifyRangeLie). Keeps the integrator world-agnostic: unset
	 * (headless / non-range) means no ground roll (total stays == carry, final lie "unknown").
	 */
	TFunction<EGolfLie(const FVector& /*LandingLocalSIm*/)> SurfaceProvider;

private:
	/** Built-in subscriber: shot.taken -> GolfBallFlight::Simulate -> publish session.shot_outcome. */
	void OnShotTaken(const FGolfEvent& Event);

	FGolfEventBus EventBus;
	FGolfEventSubscription IntegratorSub;
};
