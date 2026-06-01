#include "Physics/GroundRoll.h"

namespace
{
	constexpr double GravityMps2 = 9.81;
	constexpr double Tiny = 1e-6;
	constexpr double RefSpinRpm = 6000.0;   // landing-spin scale: spin/this -> spin factor in [0,1]
	constexpr double MinVisibleRollM = 0.05;

	// GOL-38 bounce-phase tunables. Apply ABOVE the per-surface FSurfaceRoll knobs.
	constexpr int32  MaxBounces       = 6;        // cap so a high-spring surface can't infinite-loop
	constexpr double MinBounceVvMps   = 0.40;     // upward Vv below which we settle into the roll phase
	constexpr double MinHopHorizontalM = 0.05;    // skip emitting samples for sub-cm hops
	constexpr double HopSampleDt      = 0.05;     // target sampling rate for the parabola polyline
	constexpr double RollSampleDt     = 0.06;     // matches the GOL-9 rate; preserves caller timing
	constexpr int32  HopSamplesMin    = 4;
	constexpr int32  HopSamplesMax    = 30;
	constexpr int32  RollSamplesMax   = 120;
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
	FSurfaceRoll SurfaceRollFor(EGolfLie Lie)
	{
		// { RollFriction, Restitution (V-COR), BounceHorizontalKeep, SpinCheck }.
		// GOL-38 recalibration: Restitution is now the vertical bounce coefficient and
		// BounceHorizontalKeep is per-bounce horizontal decay. Hand-checked driver lands
		// ~280 yd fairway total (carry ~264 + 3 visible hops + a short roll).
		switch (Lie)
		{
		case EGolfLie::Fairway:  return { 0.30, 0.35, 0.55, 0.20 };  // 3 visible hops then a few m of roll
		case EGolfLie::Rough:    return { 0.65, 0.20, 0.40, 0.15 };  // grass grabs -> 1-2 small hops, short roll
		case EGolfLie::Green:    return { 0.22, 0.30, 0.50, 0.55 };  // smooth, but backspin bites the scrape
		case EGolfLie::Bunker:   return { 3.00, 0.05, 0.10, 0.10 };  // plugs -> essentially no hops, no roll
		case EGolfLie::Tee:      return { 0.32, 0.35, 0.55, 0.20 };
		case EGolfLie::CartPath: return { 0.05, 0.65, 0.85, 0.00 };  // hardpan -> high hops and a long run
		case EGolfLie::OB:       return { 0.65, 0.25, 0.40, 0.10 };
		default:                 return { 0.30, 0.35, 0.55, 0.20 };  // Unknown -> fairway-like fallback
		}
	}

	FGroundRollResult SimulateGroundRoll(const FBallTrajectory& Flight, EGolfLie /*Lie*/, const FSurfaceRoll& C)
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

		// Horizontal travel direction at landing.
		FVector Horiz(LandVel.X, LandVel.Y, 0.0);
		double Vh = Horiz.Size();
		if (Vh < Tiny)
		{
			return R;   // dropped straight down -> no roll
		}
		const FVector Dir = Horiz / Vh;

		// Initial landing scrape: a steep descent digs in more, landing backspin checks the run-out.
		// Applied ONCE on first contact (no per-bounce re-attenuation; the surface table's
		// BounceHorizontalKeep handles subsequent bounces). SpinAtten hits BOTH horizontal AND
		// vertical -- high spin bleeds into ground deformation + rotation on the first contact,
		// so a high-spin wedge bounces lower (real check-up), not just shorter horizontally.
		const double DescentAtten = FMath::Clamp(FMath::Cos(FMath::DegreesToRadians(Flight.DescentAngleDeg)), 0.0, 1.0);
		const double SpinFactor   = FMath::Clamp(Flight.LandingSpinRpm / RefSpinRpm, 0.0, 1.0);
		const double SpinAtten    = FMath::Clamp(1.0 - C.SpinCheck * SpinFactor, 0.0, 1.0);
		Vh *= DescentAtten * SpinAtten;

		double VvDown      = FMath::Max(0.0, -LandVel.Z) * SpinAtten;   // downward speed at landing (m/s), bled by spin
		double DistAccum   = 0.0;                           // horizontal distance from landing
		double TimeAccum   = 0.0;                           // time since landing
		int32  NumBounces  = 0;

		// --- Bounce phase: emit 0..MaxBounces parabolic hops --------------------------------------
		// Each iteration: vertical COR launches Vv_out upward; horizontal speed decays by
		// BounceHorizontalKeep; the parabola lasts t = 2*Vv_out/g and is sampled HopSampleDt-ish.
		while (NumBounces < MaxBounces && Vh > Tiny)
		{
			const double VvOut = FMath::Max(C.Restitution, 0.0) * VvDown;
			if (VvOut < MinBounceVvMps)
			{
				break;   // bounce too small to be visible -> settle to roll
			}

			Vh *= FMath::Max(C.BounceHorizontalKeep, 0.0);
			if (Vh < Tiny)
			{
				VvDown = VvOut;
				break;
			}

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
				// Parabola z(t) = VvOut*t - 0.5*g*t^2; clamp the last sample exactly to z=0 to avoid
				// a sub-mm float residual that would pop the ball.
				const double Z = (i == NumHopSteps) ? 0.0
					: FMath::Max(VvOut * T - 0.5 * GravityMps2 * T * T, 0.0);
				FTrajectorySample Sample;
				Sample.TimeSeconds   = Flight.FlightTimeS + TimeAccum + T;
				Sample.PositionMeters = FVector(LandPos.X + Dir.X * (DistAccum + S),
				                                LandPos.Y + Dir.Y * (DistAccum + S),
				                                Z);
				Sample.VelocityMps    = FVector(Dir.X * Vh, Dir.Y * Vh, VvOut - GravityMps2 * T);
				R.RollSamples.Add(Sample);
			}

			DistAccum += HopDx;
			TimeAccum += THop;
			VvDown    = VvOut;   // ballistic descent at end of parabola = same magnitude as launch
			++NumBounces;
		}

		// --- Roll phase: constant-friction slide from the current state ---------------------------
		const double Accel   = FMath::Max(C.RollFriction, Tiny) * GravityMps2;
		const double RollDist = (Vh > Tiny) ? (Vh * Vh) / (2.0 * Accel) : 0.0;
		if (RollDist >= MinVisibleRollM)
		{
			const double TStop = Vh / Accel;
			const int32 NumRollSteps = FMath::Clamp(FMath::RoundToInt(TStop / RollSampleDt), 1, RollSamplesMax);
			R.RollSamples.Reserve(R.RollSamples.Num() + NumRollSteps);
			for (int32 i = 1; i <= NumRollSteps; ++i)
			{
				const double T = (i == NumRollSteps) ? TStop : (TStop * i) / NumRollSteps;
				const double S = FMath::Min(Vh * T - 0.5 * Accel * T * T, RollDist);
				const double V = FMath::Max(Vh - Accel * T, 0.0);
				FTrajectorySample Sample;
				Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum + T;
				Sample.PositionMeters = FVector(LandPos.X + Dir.X * (DistAccum + S),
				                                LandPos.Y + Dir.Y * (DistAccum + S),
				                                0.0);
				Sample.VelocityMps    = Dir * V;
				R.RollSamples.Add(Sample);
			}
			DistAccum += RollDist;
		}

		R.RestPositionM = FVector(LandPos.X + Dir.X * DistAccum, LandPos.Y + Dir.Y * DistAccum, 0.0);
		R.RollDistanceM = DistAccum;
		R.TotalDistanceM = FMath::Sqrt(R.RestPositionM.X * R.RestPositionM.X + R.RestPositionM.Y * R.RestPositionM.Y);
		return R;
	}
}
