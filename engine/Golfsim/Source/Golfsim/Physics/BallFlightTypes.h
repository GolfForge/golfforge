// Pure-SI ball-flight types. UE-agnostic (uses only Core math), no UObject reflection.
// The aerodynamics solver and the visualizer share these; see docs/event-protocol.md and
// the plan at .claude/plans for context.

#pragma once

#include "CoreMinimal.h"

/** Where a FBallTrajectory came from. */
enum class EBallTrajectorySource : uint8
{
	/** Integrated by the aero solver from launch conditions. */
	Simulated,
	/** Reconstructed to match a launch monitor's own reported carry/apex/descent. */
	TracedFromSummary,
};

/**
 * Launch conditions for one shot. Mirrors the `shot.taken` payload in docs/event-protocol.md
 * (all SI). A driver normalizes a launch monitor's quirks (e.g. total-spin + spin-axis -> back/side)
 * into this shape; the solver consumes only this.
 *
 * Sign conventions match the event protocol: AzimuthDeg and SidespinRpm are negative left,
 * positive right, relative to the target line.
 */
struct FShotInput
{
	double BallSpeedMps = 0.0;    // ball speed, m/s
	double LaunchAngleDeg = 0.0;  // vertical launch angle above horizontal, deg
	double AzimuthDeg = 0.0;      // horizontal launch direction; - = left, + = right
	double BackspinRpm = 0.0;     // backspin, rev/min (>= 0 for normal shots)
	double SidespinRpm = 0.0;     // sidespin, rev/min; - = curve left (draw), + = curve right (fade)
	double SmashFactor = 0.0;     // provenance only; the integrator does not use it
};

/**
 * A launch monitor's own resolved summary metrics for a shot (trace mode input). Monitors
 * report these numbers but not a sample-by-sample path, so trace mode reconstructs an arc that
 * matches them rather than replaying points. Pair with the FShotInput launch conditions.
 */
struct FResolvedFlight
{
	double CarryM = 0.0;            // straight-line ground carry distance, m
	double ApexM = 0.0;             // apex height, m
	double DescentAngleDeg = 0.0;   // landing angle below horizontal, deg
	double FlightTimeS = 0.0;       // hang time, s (<= 0 and bHasFlightTime=false if not provided)
	bool bHasFlightTime = false;
};

/** One point on a trajectory, in the solver's SI frame (+X downrange, +Y right, +Z up; ground z=0). */
struct FTrajectorySample
{
	double TimeSeconds = 0.0;
	FVector PositionMeters = FVector::ZeroVector;
	FVector VelocityMps = FVector::ZeroVector;
};

/**
 * A complete ball flight: the sampled path plus summary metrics. The common output type for both
 * producers (Simulate, TraceFromResolved), so the visualizer is agnostic to where it came from.
 * Flight-only: CarryM is carry, not post-bounce total (ground roll is the separate analytical
 * Physics/GroundRoll model).
 */
struct FBallTrajectory
{
	TArray<FTrajectorySample> Samples;   // [0] = launch (t=0); last = landing (z=0)

	double CarryM = 0.0;                 // straight-line ground distance = sqrt(dx^2 + dy^2)
	double ApexM = 0.0;                  // max height reached
	double DescentAngleDeg = 0.0;        // angle below horizontal at landing
	double FlightTimeS = 0.0;            // hang time
	double LateralOffsetM = 0.0;         // signed Y at landing (+ = right) -- the curve
	FVector LandingPositionM = FVector::ZeroVector;
	double LandingSpeedMps = 0.0;
	double LandingSpinRpm = 0.0;         // |spin| at landing (decayed from launch) -- ground roll uses it
	double LaunchSpeedMps = 0.0;

	// Index of the landing sample in Samples (z=0 touchdown). The ground-roll pass appends post-bounce
	// samples after this point, so the visualizer/HUD can tell flight (<= here) from roll (> here).
	int32 LandingSampleIndex = INDEX_NONE;

	EBallTrajectorySource Source = EBallTrajectorySource::Simulated;
	bool bValid = false;                 // false if the solver bailed (e.g. degenerate input)
};
