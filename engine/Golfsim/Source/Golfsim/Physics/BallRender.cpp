#include "Physics/BallRender.h"

namespace
{
	// Launch-local SI -> world UU, identical to the GOL-9 mapping that lived in AGolfBallActor.
	// Used both to compute the trace XY in CachePostLandingGroundZ and to produce the in-flight
	// world position in SampleToWorld; kept here so the math has one definition.
	FVector LocalToWorldNoSnap(
		const FVector& LocalSIm,
		const FVector& LaunchOriginUU,
		const FRotator& LaunchRotation,
		float MetersToUU)
	{
		const FVector LocalUU = LocalSIm * MetersToUU;
		return LaunchOriginUU + LaunchRotation.RotateVector(LocalUU);
	}
}

namespace GolfBallRender
{
	void CachePostLandingGroundZ(
		const FBallTrajectory& Trajectory,
		const FVector& LaunchOriginUU,
		const FRotator& LaunchRotation,
		float MetersToUU,
		TFunctionRef<TOptional<float>(double WorldXUU, double WorldYUU)> Provider,
		TArray<float>& OutCacheUU)
	{
		OutCacheUU.Reset();
		if (!Trajectory.bValid || Trajectory.LandingSampleIndex == INDEX_NONE)
		{
			return;
		}
		const int32 Start = Trajectory.LandingSampleIndex;
		if (!Trajectory.Samples.IsValidIndex(Start))
		{
			return;
		}
		OutCacheUU.SetNumUninitialized(Trajectory.Samples.Num() - Start);
		for (int32 i = Start; i < Trajectory.Samples.Num(); ++i)
		{
			const FVector W = LocalToWorldNoSnap(
				Trajectory.Samples[i].PositionMeters, LaunchOriginUU, LaunchRotation, MetersToUU);
			const TOptional<float> Hit = Provider(W.X, W.Y);
			OutCacheUU[i - Start] = Hit.IsSet() ? Hit.GetValue() : NoGroundZUU;
		}
	}

	FVector SampleToWorld(
		const FTrajectorySample& Sample,
		int32 SampleIdx,
		int32 LandingSampleIndex,
		const FVector& LaunchOriginUU,
		const FRotator& LaunchRotation,
		float MetersToUU,
		float BallRestHeightUU,
		const TArray<float>& PostLandingCacheUU)
	{
		FVector World = LocalToWorldNoSnap(
			Sample.PositionMeters, LaunchOriginUU, LaunchRotation, MetersToUU);

		if (LandingSampleIndex == INDEX_NONE || PostLandingCacheUU.IsEmpty())
		{
			return World;   // no terrain info -> degenerate to the flat GOL-9 mapping
		}

		if (SampleIdx >= LandingSampleIndex)
		{
			// Post-landing: snap to terrain at this sample's XY (preserves bounce arc).
			const int32 RelIdx = SampleIdx - LandingSampleIndex;
			if (PostLandingCacheUU.IsValidIndex(RelIdx))
			{
				const float Cached = PostLandingCacheUU[RelIdx];
				if (Cached != NoGroundZUU)
				{
					World.Z = Cached + BallRestHeightUU + Sample.PositionMeters.Z * MetersToUU;
				}
			}
			return World;
		}

		// In-flight: the trajectory's local Z is height above launch height. If we render it at
		// `LaunchOriginUU.Z + LocalZ * MtoUU` (the GOL-9 baseline), the last flight sample sits at
		// LaunchOriginUU.Z while the touchdown sample sits at (cache[0] + BallRest) -- a vertical
		// teleport whenever launch terrain Z != landing terrain Z. To stay continuous, we lerp the
		// flight baseline from LaunchOriginUU.Z (at t=0) to (cache[0] + BallRest) (at touchdown)
		// proportional to the sample's position in the flight, then add the parabola Z on top.
		// World XY is still the standard launch-local mapping.
		const float LandingGroundZ = PostLandingCacheUU[0];
		if (LandingGroundZ == NoGroundZUU || LandingSampleIndex <= 0)
		{
			return World;   // off-landscape landing, or trivially-short trajectory -> flat baseline
		}
		const float LandingBaselineZ = LandingGroundZ + BallRestHeightUU;
		const float T = static_cast<float>(SampleIdx) / static_cast<float>(LandingSampleIndex);
		const float BaselineZ = FMath::Lerp(static_cast<float>(LaunchOriginUU.Z), LandingBaselineZ, T);
		World.Z = BaselineZ + Sample.PositionMeters.Z * MetersToUU;
		return World;
	}
}
