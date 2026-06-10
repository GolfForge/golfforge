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
#include "Game/GolfDifficulty.h"       // EGolfDifficulty -- stamped on FRoundStartEvent
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
	RoundStart,         // round.start          -- GOL-115 -- a single-player round begins
	RoundComplete,      // round.complete       -- GOL-115 -- a single-player round ends (all 18 played)
	HoleStart,          // hole.start           -- GOL-115 -- pawn teed up on a new hole
	HoleComplete,       // hole.complete        -- GOL-115 -- a hole was holed out (or abandoned at stroke cap)
	PracticeShotScored, // practice.shot_scored -- GOL-73 -- a CTP attempt was scored (distance / strokes)
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
	UPROPERTY() bool bSpinEstimated = false; // true if backspin was computed (not measured by the LM)

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

	// The originating shot's launch metrics, copied by the integrator from shot.taken. Lets a
	// consumer (the range panel) show "what produced this outcome" from the outcome event alone --
	// without depending on the order shot.taken subscribers run in (the integrator publishes the
	// outcome mid-dispatch, so a separate shot.taken stash would lag a shot behind).
	UPROPERTY() FString Club;
	UPROPERTY() double BallSpeedMps = 0.0;
	UPROPERTY() double LaunchAngleDeg = 0.0;
	UPROPERTY() double BackspinRpm = 0.0;
	UPROPERTY() bool bSpinEstimated = false;

	// In-process render payload: the sampled flight path the view replays. Deliberately NOT a
	// UPROPERTY -- it never serializes (shot_outcome is local/non-replicated), it only carries the
	// solver's result from the integrator to whatever animates the ball this frame.
	FBallTrajectory Trajectory;

	FShotOutcomeEvent() { Kind = EEventKind::ShotOutcome; }
};

/**
 * round.start -- a single-player round just began (GOL-115). Published by URoundSubsystem from
 * StartRound(); consumers reset their per-round state. RoundId is generated at publish time and
 * carried by every subsequent hole.start / hole.complete / round.complete for the round.
 */
USTRUCT()
struct FRoundStartEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() FString CourseId;                                   // e.g. "golfforge-demo-black"
	UPROPERTY() FString RoundId;                                    // uuid v4 string; ties the round's events together
	UPROPERTY() EGolfDifficulty Difficulty = EGolfDifficulty::Easy; // picked in the pre-round screen (GOL-121)
	UPROPERTY() int32 TotalHoles = 18;

	FRoundStartEvent() { Kind = EEventKind::RoundStart; }
};

/**
 * hole.start -- pawn teed up on the next hole (GOL-115). Carries everything the tee-up / pin-place
 * / aim subscribers need: world locations are pre-resolved by URoundSubsystem (it reads the
 * splatmap/hole.geojson once at round start), so consumers don't need their own pipeline access.
 */
USTRUCT()
struct FHoleStartEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() FString RoundId;
	UPROPERTY() int32 HoleRef = 0;                       // 1..18 (matches OSM golf=hole "ref")
	UPROPERTY() int32 Par = 0;
	UPROPERTY() int32 Handicap = 0;
	UPROPERTY() FVector TeeWorldLoc = FVector::ZeroVector;
	UPROPERTY() FVector GreenWorldLoc = FVector::ZeroVector;
	UPROPERTY() FVector PinWorldLoc = FVector::ZeroVector;

	FHoleStartEvent() { Kind = EEventKind::HoleStart; }
};

/**
 * hole.complete -- a hole was finished (GOL-115). Fired by URoundSubsystem when GOL-119's hole-out
 * detector says the ball is within the gimme radius or when the per-hole stroke cap trips. The
 * scorecard accumulator (GOL-120) keys off this.
 */
USTRUCT()
struct FHoleCompleteEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() FString RoundId;
	UPROPERTY() int32 HoleRef = 0;
	UPROPERTY() int32 StrokesUsed = 0;
	UPROPERTY() int32 ScoreVsPar = 0;   // StrokesUsed - Par (negative = under)

	FHoleCompleteEvent() { Kind = EEventKind::HoleComplete; }
};

/**
 * round.complete -- final scorecard (GOL-115). PerHoleStrokes is parallel to HoleRef 1..N (index
 * 0 = hole 1, etc.). GOL-120's scorecard modal subscribes and auto-shows on receipt.
 */
USTRUCT()
struct FRoundCompleteEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() FString RoundId;
	UPROPERTY() int32 TotalStrokes = 0;
	UPROPERTY() int32 TotalScoreVsPar = 0;
	UPROPERTY() TArray<int32> PerHoleStrokes;   // length == TotalHoles from the round.start

	FRoundCompleteEvent() { Kind = EEventKind::RoundComplete; }
};

/**
 * practice.shot_scored -- a closest-to-the-pin attempt was scored (GOL-73). Published by
 * UPracticeModeSubsystem when the HUD reports a settled carry-only shot or a completed putt-out
 * sequence. Local, non-replicated (like shot.outcome). Distance fields are the carry-only metric;
 * Strokes/bPuttedOut carry the putt-out metric. Best/Avg are the running session stats AFTER this
 * attempt, so a readout can repaint from the event alone. Decoupled from GolfsimPractice's enum --
 * the consumer needs the numbers, not the mode type.
 */
USTRUCT()
struct FPracticeShotScoredEvent : public FGolfEvent
{
	GENERATED_BODY()

	UPROPERTY() double DistanceToPinM = 0.0;   // final lie -> pin, XY (the carry-only score)
	UPROPERTY() int32  Strokes = 1;            // 1 for carry-only; n (approach + putts) for putt-out
	UPROPERTY() bool   bPuttedOut = false;     // true if the attempt was holed out with the putter
	UPROPERTY() int32  AttemptIndex = 0;       // 1-based position in the session

	UPROPERTY() double BestDistanceM = 0.0;    // session best (closest) after this attempt
	UPROPERTY() double AvgDistanceM = 0.0;     // session mean distance
	UPROPERTY() int32  BestStrokes = 0;        // session fewest strokes (putt-out)
	UPROPERTY() double AvgStrokes = 0.0;       // session mean strokes (putt-out)

	FPracticeShotScoredEvent() { Kind = EEventKind::PracticeShotScored; }
};

namespace GolfsimEvents
{
	/** Fixed singleplayer player UUID (protocol rule 4: every event is player-scoped). */
	inline FString LocalPlayerId() { return TEXT("00000000-0000-0000-0000-000000000001"); }
}
