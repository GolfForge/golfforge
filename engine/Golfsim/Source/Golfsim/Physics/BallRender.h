// Trajectory -> world transform helpers (GOL-110). Pulled out of AGolfBallActor so the math is
// pure-C++ (no UWorld) and headless-testable. Two pieces:
//   1. CachePostLandingGroundZ -- walks the post-landing samples once and asks a provider for the
//      landscape Z under each sample's world XY, caching the result (or a sentinel for misses).
//   2. SampleToWorld -- the standard launch-local -> world map, but for post-landing samples with
//      a valid cached ground Z, replaces the world Z with (ground + ball-rest + hop-height) so the
//      ball tracks terrain while keeping its bounce arc.
//
// The hop-height insight: post-landing Sample.PositionMeters.Z is HEIGHT-ABOVE-LOCAL-GROUND
// (0 for the roll polyline, > 0 for parabola apexes). Replacing local-ground with actual-ground
// preserves the bounce shape; only the absolute Z shifts to match the landscape.

#pragma once

#include "CoreMinimal.h"
#include "Physics/BallFlightTypes.h"

namespace GolfBallRender
{
	/** Sentinel cache value meaning "the provider returned no ground beneath this sample". */
	inline constexpr float NoGroundZUU = TNumericLimits<float>::Lowest();

	/**
	 * Walk Trajectory.Samples[LandingSampleIndex..end] in launch-local SI, compute the world XY
	 * via the standard mapping, call Provider(WorldXUU, WorldYUU), and store the returned Z (or
	 * NoGroundZUU) in OutCacheUU. Cache is resized to (Samples.Num() - LandingSampleIndex). If the
	 * trajectory is invalid or has no landing index, OutCacheUU is cleared and no probes are made.
	 */
	void CachePostLandingGroundZ(
		const FBallTrajectory& Trajectory,
		const FVector& LaunchOriginUU,
		const FRotator& LaunchRotation,
		float MetersToUU,
		TFunctionRef<TOptional<float>(double WorldXUU, double WorldYUU)> Provider,
		TArray<float>& OutCacheUU);

	/**
	 * Standard launch-local -> world map for one sample. For SampleIdx >= LandingSampleIndex with
	 * a valid cached ground Z (PostLandingCacheUU[SampleIdx - LandingSampleIndex] != NoGroundZUU),
	 * the returned world Z is (CachedGroundZ + BallRestHeightUU + Sample.PositionMeters.Z * MetersToUU).
	 * Otherwise behaves identically to the GOL-9 mapping (in-flight samples never look at the cache).
	 */
	FVector SampleToWorld(
		const FTrajectorySample& Sample,
		int32 SampleIdx,
		int32 LandingSampleIndex,
		const FVector& LaunchOriginUU,
		const FRotator& LaunchRotation,
		float MetersToUU,
		float BallRestHeightUU,
		const TArray<float>& PostLandingCacheUU);
}
