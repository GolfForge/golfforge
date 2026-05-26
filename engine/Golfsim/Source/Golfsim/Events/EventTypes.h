// In-process event-bus envelope + payloads -- the single shape every driver, UI element, and
// (later) multiplayer peer agrees on. See docs/event-protocol.md ("Envelope" + "Event types").
//
// In-process these are plain C++ structs passed by const ref through FGolfEventBus -- no
// serialization. They are USTRUCTs so the fields are reflection-visible for the future wire form
// (multiplayer / JSON-lines persistence) and Blueprint tuning; the bus dispatch itself is our own
// C++ TFunction delegate, NOT an engine delegate (GOL-7 pitfall #1).
//
// Architecture invariant #2 (CLAUDE.md): one envelope, many payloads, opaque to the sim.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"
#include "Physics/BallFlightTypes.h"   // FBallTrajectory -- the in-process render payload on an outcome
#include "EventTypes.generated.h"

/**
 * The dotted event namespace from docs/event-protocol.md. Add kinds as drivers land (the bus is
 * minimal by design); the reserved entries document the namespace the protocol already defines.
 */
UENUM(BlueprintType)
enum class EEventKind : uint8
{
	None = 0,
	ShotTaken,          // shot.taken           -- a driver reports launch conditions
	ShotOutcome,        // session.shot_outcome -- the sim's computed result of a shot
	WalkTick,           // walk.tick            -- (reserved: FTMS / ESP32 walking driver)
	BioHrTick,          // bio.hr_tick          -- (reserved: HR strap)
	SessionStart,       // session.start        -- (reserved)
	SessionEnd,         // session.end          -- (reserved)
	SessionHoleChange,  // session.hole_change  -- (reserved)
};

/**
 * The event envelope: metadata common to every event (docs/event-protocol.md "Envelope").
 * Concrete payloads inherit this and add their fields; a subscriber receives a const FGolfEvent&
 * and downcasts on Kind. No virtuals -- events are value types passed by const ref, never deleted
 * through a base pointer, so the Kind discriminator + static_cast is the (safe) dispatch.
 */
USTRUCT()
struct FGolfEvent
{
	GENERATED_BODY()

	/** Protocol version. Currently 1. */
	UPROPERTY() int32 V = 1;

	/** Unix epoch ms in the publisher's clock. Stamped at construction. */
	UPROPERTY() int64 TsMs = 0;

	/** Stable publisher identifier, e.g. "manual-shot-dialog", "range-fire", "sim". */
	UPROPERTY() FString Source;

	/** Which player this event is about. Singleplayer uses a fixed UUID (protocol rule 4). */
	UPROPERTY() FString PlayerId;

	/** Discriminator for the downcast; each concrete payload's constructor sets it. */
	UPROPERTY() EEventKind Kind = EEventKind::None;

	FGolfEvent() { TsMs = NowUnixMs(); }

	/** Unix milliseconds, source clock. */
	static int64 NowUnixMs()
	{
		return (int64)(FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	}
};

/**
 * shot.taken -- the canonical shot event (docs/event-protocol.md). Mirrors FShotInput
 * (Physics/BallFlightTypes.h) plus the protocol's club + lie. A driver normalizes a launch
 * monitor's quirks into this; the integrator consumes it. All SI; sign conventions match the
 * protocol (AzimuthDeg/SidespinRpm: - left/draw, + right/fade).
 */
USTRUCT()
struct FShotTakenEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() double BallSpeedMps = 0.0;
	UPROPERTY() double LaunchAngleDeg = 0.0;
	UPROPERTY() double AzimuthDeg = 0.0;     // - left, + right, relative to target line
	UPROPERTY() double BackspinRpm = 0.0;
	UPROPERTY() double SidespinRpm = 0.0;    // - draw, + fade
	UPROPERTY() double SmashFactor = 0.0;    // provenance only; the integrator ignores it

	UPROPERTY() FString Club;                // e.g. "7-Iron"
	UPROPERTY() FString Lie = TEXT("tee");   // tee/fairway/rough/bunker/green/...

	FShotTakenEvent() { Kind = EEventKind::ShotTaken; }
};

/**
 * session.shot_outcome -- the sim's computed result of a shot (docs/event-protocol.md). Computed
 * by the integrator from shot.taken, never by drivers. A local, non-replicated event: multiplayer
 * peers recompute their own, so this never crosses the wire.
 */
USTRUCT()
struct FShotOutcomeEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() bool bInHole = false;
	UPROPERTY() double DistanceToPinM = 0.0;
	UPROPERTY() FString FinalLie;
	UPROPERTY() double CarryM = 0.0;
	UPROPERTY() double TotalM = 0.0;          // == CarryM until ground roll (GOL-9) lands
	UPROPERTY() double LateralOffsetM = 0.0;  // signed; + = right (the range "offline" metric)

	// In-process render payload: the sampled flight path the view replays. Deliberately NOT a
	// UPROPERTY -- it never serializes (shot_outcome is local/non-replicated), it only carries the
	// solver's result from the integrator to whatever animates the ball this frame.
	FBallTrajectory Trajectory;

	FShotOutcomeEvent() { Kind = EEventKind::ShotOutcome; }
};

namespace GolfsimEvents
{
	/** Fixed singleplayer player UUID (protocol rule 4: every event is player-scoped). */
	inline FString LocalPlayerId() { return TEXT("00000000-0000-0000-0000-000000000001"); }
}
