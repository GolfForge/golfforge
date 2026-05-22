// Golf ball-flight aerodynamics solver: drag + gravity + Magnus lift + spin decay.
// Pure SI, UE-agnostic. Flight-only (carry); ground bounce/roll is later Chaos work.

#pragma once

#include "CoreMinimal.h"
#include "Physics/BallFlightTypes.h"
#include "Physics/AeroCoefficients.h"

namespace GolfBallFlight
{
	/**
	 * Simulate mode: integrate a full ball-flight trajectory from launch conditions.
	 * @param Shot         launch conditions (mirrors shot.taken)
	 * @param Coeffs       aerodynamic coefficients / environment (tunable knobs)
	 * @param bDisableAero if true, drag and lift are zero (pure vacuum ballistics) -- test hook
	 */
	FBallTrajectory Simulate(const FShotInput& Shot,
		const FAeroCoefficients& Coeffs = FAeroCoefficients(),
		bool bDisableAero = false);

	/**
	 * Trace mode: reconstruct a trajectory that matches a launch monitor's own reported summary
	 * metrics. Runs Simulate from the launch conditions, then affine-corrects the arc to pass
	 * through the reported apex and landing (preserving the realistic steep-descent asymmetry).
	 */
	FBallTrajectory TraceFromResolved(const FShotInput& Shot,
		const FResolvedFlight& Resolved,
		const FAeroCoefficients& Coeffs = FAeroCoefficients());
}
