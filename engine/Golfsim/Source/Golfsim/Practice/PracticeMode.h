// Closest-to-the-pin (CTP) practice drill -- pure C++, no UObject / no UWorld, so the pin RNG,
// scoring, and session stats are unit-testable headlessly (mirrors the GolfsimRound:: split in
// Round/RoundState.h). The UPracticeModeSubsystem wrapper glues this to the EventBus + the active
// session; AGolfRangeHUD owns the world (pin actor, ball, settle timing) and feeds completed
// attempts back in. GOL-73.
//
// SI throughout: distances are METERS. Yards live only at the UI/console boundary (the 5-yd spinbox
// step, ApplyPinDistance) -- convert there, never store 4.572 m. Lane geometry mirrors the range
// corridor the HUD already enforces (400 yd long, +/-35 yd tree wall; GolfRangeHUD.cpp).

#pragma once

#include "CoreMinimal.h"

namespace GolfsimPractice
{
	/** Which drill the range is running. Free = the legacy free-fire range; ClosestToPin = CTP;
	 *  Putting = the GOL-75 putting drill (no approach -- the player putts from the tee, every putt is
	 *  a counted stroke). Putting reuses FCtpConfig/FCtpSession + the pin RNG; it differs only in how
	 *  attempts are scored (strokes-to-hole) and how the panel renders distances (feet, not yards). */
	enum class EPracticeMode : uint8 { Free = 0, ClosestToPin = 1, Putting = 2 };

	/** How an attempt is scored. HoleOut = number of putts to hole (the putting headline metric);
	 *  DistanceToPin = rest distance after a single shot (the CTP metric, also a putting option). */
	enum class EScoreMode : uint8 { HoleOut = 0, DistanceToPin = 1 };

	/** Range-corridor bounds (meters), matching the HUD's pin placement clamps (yards -> m). */
	inline constexpr double MetersPerYard      = 0.9144;
	inline constexpr double MetersPerFoot      = 0.3048;                  // putting distances are feet
	inline constexpr double CorridorMaxM       = 400.0 * MetersPerYard;   // pin can't spawn past the lane
	inline constexpr double LaneHalfWidthM     = 35.0  * MetersPerYard;   // +/- tree wall

	/** CTP / putting configuration -- the user-tunable knobs (set from the panel/console, kept SI
	 *  here). Distances are METERS regardless of how the panel renders them (yards for CTP, feet for
	 *  putting); convert at the UI boundary, never store the rounded display value. */
	struct GOLFSIM_API FCtpConfig
	{
		double MinM        = 50.0  * MetersPerYard;   // nearest the pin may spawn
		double MaxM        = 250.0 * MetersPerYard;   // farthest the pin may spawn
		bool   bSideOffset = false;                   // allow left/right pins off the centerline
		double MaxSideM    = 20.0  * MetersPerYard;   // |offset| cap when bSideOffset (clamped to lane)
		bool   bPuttOut    = false;                   // putt out when a shot finishes within PuttWithinM
		double PuttWithinM = 10.0  * MetersPerYard;   // approach must settle inside this to trigger a putt-out
		double GimmeRadiusFt = 3.0;                   // putt-out "holed" tolerance (feet; XY-only)
		EScoreMode Score   = EScoreMode::DistanceToPin;   // CTP scores by distance; putting defaults to hole-out
	};

	/** Default config for the GOL-75 putting drill: a short 5-30 ft range, hole-out scoring, and a
	 *  tight gimme (putting is finished at the cup, not a generous range gimme). Distances are SI. */
	GOLFSIM_API FCtpConfig MakePuttingDefaults();

	/** A pin spawn relative to the tee: DistanceM downrange (+X), SideOffsetM lateral (+ = right). */
	struct GOLFSIM_API FCtpPin
	{
		double DistanceM   = 0.0;
		double SideOffsetM = 0.0;
	};

	/** One completed CTP attempt. Carry-only: Strokes=1, bPuttedOut=false, DistanceM = lie->pin.
	 *  Putt-out: Strokes=n (approach + putts), bPuttedOut=true, DistanceM = final lie->pin (~0). */
	struct GOLFSIM_API FCtpAttempt
	{
		double DistanceM = 0.0;
		int32  Strokes   = 1;
		bool   bPuttedOut = false;
	};

	/** A single-mode CTP session: the running list of attempts. Stats are computed on demand. */
	struct GOLFSIM_API FCtpSession
	{
		EPracticeMode Mode = EPracticeMode::ClosestToPin;
		TArray<FCtpAttempt> Attempts;
	};

	/** Pick the next pin. Distance uniform in the (order-normalized, lane-clamped) [Min,Max]; side
	 *  uniform in +/-MaxSide (lane-clamped) when enabled, else 0. Deterministic for a seeded stream. */
	GOLFSIM_API FCtpPin NextPin(const FCtpConfig& Config, FRandomStream& Stream);

	/** XY-only distance (meters) between a ball-rest and a pin, both world-space CENTIMETERS (UE
	 *  world units). Z ignored -- same rationale as GolfsimRound::IsWithinGimme. */
	GOLFSIM_API double ScoreDistanceM(const FVector& BallWorldCm, const FVector& PinWorldCm);

	/** Append a completed attempt. */
	GOLFSIM_API void RecordAttempt(FCtpSession& Session, const FCtpAttempt& Attempt);

	// --- Stat accessors. All guard the empty session (return 0) so an end-of-session summary on a
	//     zero-shot session never divides by zero / reads an empty array (ticket pitfall). ----------

	GOLFSIM_API int32  AttemptCount(const FCtpSession& Session);

	/** Distance stats (meters) -- the carry-only score. */
	GOLFSIM_API double BestDistanceM(const FCtpSession& Session);   // min (closest)
	GOLFSIM_API double AvgDistanceM(const FCtpSession& Session);
	GOLFSIM_API double LastDistanceM(const FCtpSession& Session);

	/** Stroke stats -- the putt-out score. */
	GOLFSIM_API int32  BestStrokes(const FCtpSession& Session);     // fewest
	GOLFSIM_API double AvgStrokes(const FCtpSession& Session);
	GOLFSIM_API int32  LastStrokes(const FCtpSession& Session);
}
