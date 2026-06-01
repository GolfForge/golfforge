// Ground interaction + roll (GOL-9 + GOL-38): turns a flight-only FBallTrajectory's landing state
// into a few visible bounces + a settle roll -> total distance + a rest position. Pure SI,
// UE-agnostic (like the flight solver) -- the bounce/roll is an analytical model, NOT Chaos:
// deterministic and headless-testable, hand-off at the landing instant only (per the ticket
// pitfall). GOL-38 added the multi-bounce phase ahead of the roll; GOL-9 had only a flat slide.

#pragma once

#include "CoreMinimal.h"
#include "Physics/BallFlightTypes.h"

/**
 * Surface a ball can land/rest on. Maps to the protocol's lie strings (docs/event-protocol.md) via
 * LieToProtocol/LieFromProtocol. Plain enum (not a UENUM): the outcome event carries the lie as an
 * FString; this enum is the internal key for the per-surface roll coefficients.
 */
enum class EGolfLie : uint8
{
	Unknown = 0,
	Tee,
	Fairway,
	Rough,
	Bunker,
	Green,
	CartPath,
	OB,
};

/**
 * Per-surface ground-roll tuning. Code-seeded defaults (SurfaceRollFor); promote to a UDataAsset
 * later if designer-facing tuning is wanted. Treat each field as a knob.
 *
 * GOL-38 reinterpreted Restitution: it was "fraction of horizontal speed surviving the bounce" in
 * the GOL-9 flat-slide model; it is now the vertical coefficient of restitution applied per bounce
 * (the real physics meaning), and BounceHorizontalKeep took over the per-bounce horizontal decay.
 */
struct FSurfaceRoll
{
	double RollFriction = 0.30;            // rolling-friction coefficient -> decel a = RollFriction * g (higher = shorter roll)
	double Restitution = 0.35;             // vertical coefficient of restitution (COR) per bounce, 0..1
	double BounceHorizontalKeep = 0.55;    // horizontal-speed retention per bounce, 0..1 (GOL-38)
	double SpinCheck = 0.20;               // how strongly landing backspin kills the initial scrape (0 = none, 1 = full at RefSpin)
};

/** Result of the ground-roll pass. RollSamples is the post-bounce ground polyline for the visualizer. */
struct FGroundRollResult
{
	FVector RestPositionM = FVector::ZeroVector;   // where the ball comes to rest (SI, z=0), launch-local frame
	double RollDistanceM = 0.0;                    // length of the roll itself
	double TotalDistanceM = 0.0;                   // planar distance from the tee (origin) to rest = carry + roll-ish
	TArray<FTrajectorySample> RollSamples;         // decelerating ground samples, TimeSeconds continuing past landing
	bool bValid = false;
};

namespace GolfBallFlight
{
	/** Default roll coefficients for a surface. */
	FSurfaceRoll SurfaceRollFor(EGolfLie Lie);

	/**
	 * Putter-on-green coefficients parameterised by stimpmeter feet (GOL-109). The stimp reading IS
	 * the rollout in feet at the standard release speed (~2 m/s), so v^2 = 2*mu*g*d gives
	 * `RollFriction ≈ 0.67 / StimpFt`. Bounce + spin terms are zeroed (a putt scrapes, doesn't bounce).
	 * Typical input: StimpFt = 10-14 (slow muni -> Augusta-fast); default 11.
	 */
	FSurfaceRoll PutterSurfaceRoll(double StimpFt);

	/**
	 * Compute bounce + roll from a flight's landing state.
	 * @param Flight  a valid, landed flight trajectory (reads its landing sample velocity + spin)
	 * @param Lie     the surface the ball landed on (selects the roll behavior)
	 * @param C       roll coefficients for that surface (usually SurfaceRollFor(Lie))
	 */
	FGroundRollResult SimulateGroundRoll(const FBallTrajectory& Flight, EGolfLie Lie, const FSurfaceRoll& C);
}

/** EGolfLie <-> protocol lie string (docs/event-protocol.md): "fairway"/"rough"/"bunker"/... */
FString LieToProtocol(EGolfLie Lie);
EGolfLie LieFromProtocol(const FString& Lie);
