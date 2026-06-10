#include "Physics/GroundRoll.h"

namespace
{
	constexpr double GravityMps2 = 9.81;
	constexpr double Tiny = 1e-6;
	constexpr double RefSpinRpm = 6000.0;   // landing-spin scale: spin/this -> spin factor in [0,1]

	// GOL-38 bounce-phase tunables. Apply ABOVE the per-surface FSurfaceRoll knobs.
	constexpr int32  MaxBounces       = 6;        // cap so a high-spring surface can't infinite-loop
	constexpr double MinBounceVvMps   = 0.40;     // upward Vv below which we settle into the roll phase
	constexpr double MinHopHorizontalM = 0.05;    // skip emitting samples for sub-cm hops
	constexpr double HopSampleDt      = 0.05;     // target sampling rate for the parabola polyline
	constexpr double RollSampleDt     = 0.06;     // matches the GOL-9 rate; preserves caller timing
	constexpr int32  HopSamplesMin    = 4;
	constexpr int32  HopSamplesMax    = 30;
	constexpr int32  RollStepsMax     = 4000;     // safety cap on the stepped roll (no infinite loop on ~0 friction)

	// GOL-39 green spin-back tunables. A green ball that arrives with enough backspin and a steep
	// enough descent checks forward, then rolls BACKWARD. Backward launch speed (m/s) =
	//   SpinBackGain * clamp((spin - Threshold)/Scale, 0, 1) * clamp(descentDeg/SteepRef, 0, 1)
	// then a green-friction roll backward to rest. Gated on EGolfLie::Green AND SpinBackGain > 0.
	constexpr double SpinBackThresholdRpm = 3500.0;   // below this landing spin, no spin-back
	constexpr double SpinBackSpinScaleRpm = 4000.0;   // spin span (above threshold) over which gain ramps to full
	constexpr double SpinBackSteepRefDeg  = 40.0;     // descent at/above which spin-back is fully effective
}

FString LieToProtocol(EGolfLie Lie)
{
	switch (Lie)
	{
	case EGolfLie::Tee:      return TEXT("tee");
	case EGolfLie::Fairway:  return TEXT("fairway");
	case EGolfLie::Rough:    return TEXT("rough");
	case EGolfLie::Bunker:   return TEXT("bunker");
	case EGolfLie::Green:    return TEXT("green");
	case EGolfLie::CartPath: return TEXT("cart_path");
	case EGolfLie::OB:       return TEXT("ob");
	default:                 return TEXT("unknown");
	}
}

EGolfLie LieFromProtocol(const FString& Lie)
{
	if (Lie.Equals(TEXT("tee"), ESearchCase::IgnoreCase))       { return EGolfLie::Tee; }
	if (Lie.Equals(TEXT("fairway"), ESearchCase::IgnoreCase))   { return EGolfLie::Fairway; }
	if (Lie.Equals(TEXT("rough"), ESearchCase::IgnoreCase))     { return EGolfLie::Rough; }
	if (Lie.Equals(TEXT("bunker"), ESearchCase::IgnoreCase))    { return EGolfLie::Bunker; }
	if (Lie.Equals(TEXT("green"), ESearchCase::IgnoreCase))     { return EGolfLie::Green; }
	if (Lie.Equals(TEXT("cart_path"), ESearchCase::IgnoreCase)) { return EGolfLie::CartPath; }
	if (Lie.Equals(TEXT("ob"), ESearchCase::IgnoreCase))        { return EGolfLie::OB; }
	return EGolfLie::Unknown;
}

namespace GolfBallFlight
{
	FSurfaceRoll PutterSurfaceRoll(double StimpFt)
	{
		// Stimpmeter math: stimp reading IS rollout in feet at ~2 m/s release.
		//   v^2 = 2 * mu * g * d  -> mu = v^2 / (2 * g * d) = 4 / (19.62 * StimpFt * 0.3048) ≈ 0.67 / StimpFt
		// Putt scrape: no bounce, no spin check (residual putt spin doesn't bite the scrape).
		const double Safe = FMath::Max(StimpFt, 1.0);   // clamp; sub-1-stimp is unphysical
		FSurfaceRoll C;
		C.RollFriction         = 0.67 / Safe;
		C.Restitution          = 0.0;
		C.BounceHorizontalKeep = 1.0;
		C.SpinCheck            = 0.0;
		C.SpinBackGain         = 0.0;   // a putt scrapes; residual putt spin doesn't bite
		return C;
	}

	FSurfaceRoll SurfaceRollFor(EGolfLie Lie)
	{
		// { RollFriction, Restitution (V-COR), BounceHorizontalKeep, SpinCheck, SpinBackGain }.
		// GOL-38: Restitution is the vertical bounce coefficient, BounceHorizontalKeep the per-bounce
		// horizontal decay. Hand-checked driver lands ~280 yd fairway total (carry ~264 + 3 hops +
		// short roll). GOL-39: added SpinBackGain (green-only backward roll) and raised Green SpinCheck
		// 0.55 -> 0.70 so a high-spin wedge's forward roll is small enough for the spin-back to dominate
		// (real greens check hard). Magnitudes stay provisional until Square Omni LM data lands.
		switch (Lie)
		{
		case EGolfLie::Fairway:  return { 0.30, 0.35, 0.55, 0.20, 0.0 };  // 3 visible hops then a few m of roll
		case EGolfLie::Rough:    return { 0.65, 0.20, 0.40, 0.15, 0.0 };  // grass grabs -> 1-2 small hops, short roll
		case EGolfLie::Green:    return { 0.22, 0.30, 0.50, 0.70, 4.5 };  // smooth; backspin checks hard + spins back
		case EGolfLie::Bunker:   return { 3.00, 0.05, 0.10, 0.10, 0.0 };  // plugs -> essentially no hops, no roll
		case EGolfLie::Tee:      return { 0.32, 0.35, 0.55, 0.20, 0.0 };
		case EGolfLie::CartPath: return { 0.05, 0.65, 0.85, 0.00, 0.0 };  // hardpan -> high hops and a long run
		case EGolfLie::OB:       return { 0.65, 0.25, 0.40, 0.10, 0.0 };
		default:                 return { 0.30, 0.35, 0.55, 0.20, 0.0 };  // Unknown -> fairway-like fallback
		}
	}

	FGroundRollResult SimulateGroundRollCrossSurface(
		const FBallTrajectory& Flight,
		TFunctionRef<EGolfLie(const FVector&)> SurfaceAt,
		TFunctionRef<FSurfaceRoll(EGolfLie)> CoefsFor,
		TFunctionRef<FVector(const FVector&)> NormalAt)
	{
		FGroundRollResult R;
		if (!Flight.bValid || !Flight.Samples.IsValidIndex(Flight.LandingSampleIndex))
		{
			return R;   // bValid stays false: the caller keeps total == carry
		}

		const FVector LandPos = Flight.LandingPositionM;
		const FVector LandVel = Flight.Samples[Flight.LandingSampleIndex].VelocityMps;

		R.RestPositionM = FVector(LandPos.X, LandPos.Y, 0.0);
		R.RollDistanceM = 0.0;
		R.TotalDistanceM = FMath::Sqrt(LandPos.X * LandPos.X + LandPos.Y * LandPos.Y);
		R.bValid = true;

		// Horizontal travel direction at landing. GOL-196: Dir now DEFLECTS per bounce (reflected off
		// the terrain normal), so we track a running 2D ground position instead of a scalar distance
		// along a fixed heading. Dir0 is the landing heading, kept for the signed roll-distance readout.
		double Vh = FVector2D(LandVel.X, LandVel.Y).Size();
		if (Vh < Tiny)
		{
			return R;   // dropped straight down -> no roll
		}
		FVector2D Dir(LandVel.X / Vh, LandVel.Y / Vh);
		const FVector2D Dir0 = Dir;
		FVector2D Pos(LandPos.X, LandPos.Y);

		auto CoefAt = [&](const FVector2D& P) { return CoefsFor(SurfaceAt(FVector(P.X, P.Y, 0.0))); };

		// Initial landing scrape: a steep descent digs in more, landing backspin checks the run-out.
		// Applied ONCE on first contact; SpinAtten hits BOTH horizontal and vertical (a high-spin wedge
		// bounces lower, real check-up).
		FSurfaceRoll C = CoefAt(Pos);
		const double DescentAtten = FMath::Clamp(FMath::Cos(FMath::DegreesToRadians(Flight.DescentAngleDeg)), 0.0, 1.0);
		const double SpinFactor   = FMath::Clamp(Flight.LandingSpinRpm / RefSpinRpm, 0.0, 1.0);
		const double SpinAtten    = FMath::Clamp(1.0 - C.SpinCheck * SpinFactor, 0.0, 1.0);
		Vh *= DescentAtten * SpinAtten;

		double VvDown     = FMath::Max(0.0, -LandVel.Z) * SpinAtten;   // downward speed at landing (m/s), bled by spin
		double TimeAccum  = 0.0;
		int32  NumBounces = 0;

		// --- Bounce phase: reflect each touchdown off the surface normal, re-classifying per hop -----
		// On a FLAT normal (0,0,1) the reflection reduces to: VvOut = Restitution*VvDown, Vh *= Keep,
		// heading unchanged -- i.e. the pre-GOL-196 model exactly (the settle-vs-hop branch keeps the
		// same un-decayed-Vh-on-settle behavior). On a slope the normal tilts the outgoing heading.
		while (NumBounces < MaxBounces && Vh > Tiny)
		{
			C = CoefAt(Pos);

			FVector N = NormalAt(FVector(Pos.X, Pos.Y, 0.0));
			if (!N.Normalize() || N.Z <= 0.0) { N = FVector::UpVector; }   // guard degenerate/inverted
			const FVector VIn(Dir.X * Vh, Dir.Y * Vh, -VvDown);
			const FVector VN   = (VIn | N) * N;                            // into-surface normal component
			const FVector VT   = VIn - VN;                                 // tangential (along the face)
			const FVector VOut = VT * FMath::Max(C.BounceHorizontalKeep, 0.0)
			                   - VN * FMath::Max(C.Restitution, 0.0);

			const double VvOut = FMath::Max(VOut.Z, 0.0);
			if (VvOut < MinBounceVvMps)
			{
				break;   // bounce too small to be visible -> settle to roll (keep current Vh + heading)
			}

			const FVector2D HOut(VOut.X, VOut.Y);
			const double VhOut = HOut.Size();
			if (VhOut < Tiny)
			{
				VvDown = VvOut;
				break;   // straight-up hop -> no horizontal travel
			}
			Dir = HOut / VhOut;   // deflected heading
			Vh  = VhOut;

			const double THop  = 2.0 * VvOut / GravityMps2;
			const double HopDx = Vh * THop;
			if (HopDx < MinHopHorizontalM)
			{
				break;   // nano-hop, skip emitting and let the roll close it out
			}

			const int32 NumHopSteps = FMath::Clamp(FMath::RoundToInt(THop / HopSampleDt), HopSamplesMin, HopSamplesMax);
			R.RollSamples.Reserve(R.RollSamples.Num() + NumHopSteps);
			for (int32 i = 1; i <= NumHopSteps; ++i)
			{
				const double T = (i == NumHopSteps) ? THop : (THop * i) / NumHopSteps;
				const double S = Vh * T;
				// Parabola z(t) = VvOut*t - 0.5*g*t^2; clamp the last sample to z=0 to avoid a sub-mm pop.
				const double Z = (i == NumHopSteps) ? 0.0
					: FMath::Max(VvOut * T - 0.5 * GravityMps2 * T * T, 0.0);
				FTrajectorySample Sample;
				Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum + T;
				Sample.PositionMeters = FVector(Pos.X + Dir.X * S, Pos.Y + Dir.Y * S, Z);
				Sample.VelocityMps    = FVector(Dir.X * Vh, Dir.Y * Vh, VvOut - GravityMps2 * T);
				R.RollSamples.Add(Sample);
			}

			Pos += Dir * HopDx;
			TimeAccum += THop;
			VvDown    = VvOut;
			++NumBounces;
		}

		// --- Roll phase: stepped constant-friction slide along the (now possibly deflected) heading --
		// For a single surface + flat normal this telescopes to RollDist = Vh^2/(2a) exactly; the
		// stepping lets friction change at a boundary (fairway -> bunker stops fast in sand). Roll
		// following the fall-line is out of scope (overlaps green break, GOL-75) -- heading stays fixed.
		{
			int32 Steps = 0;
			while (Vh > Tiny && Steps < RollStepsMax)
			{
				C = CoefAt(Pos);
				const double Accel  = FMath::Max(C.RollFriction, Tiny) * GravityMps2;
				const double DtStep = FMath::Min(RollSampleDt, Vh / Accel);   // clip the final step to stop exactly
				const double S      = Vh * DtStep - 0.5 * Accel * DtStep * DtStep;

				Pos += Dir * S;
				TimeAccum += DtStep;
				Vh = FMath::Max(Vh - Accel * DtStep, 0.0);

				FTrajectorySample Sample;
				Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum;
				Sample.PositionMeters = FVector(Pos.X, Pos.Y, 0.0);
				Sample.VelocityMps    = FVector(Dir.X * Vh, Dir.Y * Vh, 0.0);
				R.RollSamples.Add(Sample);
				++Steps;
			}
		}

		// --- GOL-39 green spin-back: a green ball with enough backspin + steep descent checks forward
		// (above), then rolls BACKWARD here, along the current heading. Gated on rest surface = Green.
		{
			const EGolfLie     RestLie = SurfaceAt(FVector(Pos.X, Pos.Y, 0.0));
			const FSurfaceRoll GC      = CoefsFor(RestLie);
			if (RestLie == EGolfLie::Green && GC.SpinBackGain > 0.0
				&& Flight.LandingSpinRpm > SpinBackThresholdRpm)
			{
				const double SpinFrac  = FMath::Clamp((Flight.LandingSpinRpm - SpinBackThresholdRpm) / SpinBackSpinScaleRpm, 0.0, 1.0);
				const double SteepFrac = FMath::Clamp(Flight.DescentAngleDeg / SpinBackSteepRefDeg, 0.0, 1.0);
				double Vback = GC.SpinBackGain * SpinFrac * SteepFrac;
				const double Accel = FMath::Max(GC.RollFriction, Tiny) * GravityMps2;
				int32 Steps = 0;
				while (Vback > Tiny && Steps < RollStepsMax)
				{
					const double DtStep = FMath::Min(RollSampleDt, Vback / Accel);
					const double S      = Vback * DtStep - 0.5 * Accel * DtStep * DtStep;
					Pos -= Dir * S;   // backward = back along the current heading
					TimeAccum += DtStep;
					Vback = FMath::Max(Vback - Accel * DtStep, 0.0);

					FTrajectorySample Sample;
					Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum;
					Sample.PositionMeters = FVector(Pos.X, Pos.Y, 0.0);
					Sample.VelocityMps    = FVector(-Dir.X * Vback, -Dir.Y * Vback, 0.0);
					R.RollSamples.Add(Sample);
					++Steps;
				}
			}
		}

		R.RestPositionM  = FVector(Pos.X, Pos.Y, 0.0);
		R.RollDistanceM  = FVector2D::DotProduct(Pos - FVector2D(LandPos.X, LandPos.Y), Dir0);   // signed along landing heading
		R.TotalDistanceM = FMath::Sqrt(R.RestPositionM.X * R.RestPositionM.X + R.RestPositionM.Y * R.RestPositionM.Y);
		return R;
	}

	FGroundRollResult SimulateGroundRoll(const FBallTrajectory& Flight, EGolfLie Lie, const FSurfaceRoll& C)
	{
		// Single-surface: constant lie + constant coefficients + flat ground. Identical to the
		// pre-GOL-196 model (flat normal reproduces the straight bounce; the stepped roll telescopes to
		// the old closed form); green spin-back still fires when Lie == Green and C.SpinBackGain > 0.
		return SimulateGroundRollCrossSurface(
			Flight,
			[Lie](const FVector&) { return Lie; },
			[&C](EGolfLie) { return C; },
			[](const FVector&) { return FVector::UpVector; });
	}
}
