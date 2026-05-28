#include "Physics/GroundRoll.h"

namespace
{
	constexpr double GravityMps2 = 9.81;
	constexpr double Tiny = 1e-6;
	constexpr double RefSpinRpm = 6000.0;   // landing-spin scale: spin/this -> spin factor in [0,1]
	constexpr double MinVisibleRollM = 0.05;
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
		// { RollFriction, Restitution, SpinCheck }. Sensible defaults tuned for the ordering
		// fairway > rough >> bunker; tune the absolute fairway number against a live driver.
		switch (Lie)
		{
		case EGolfLie::Fairway:  return { 0.26, 0.55, 0.20 };   // calibrated: Trackman driver -> ~280 yd total
		case EGolfLie::Rough:    return { 0.65, 0.35, 0.15 };   // grass grabs -> short roll
		case EGolfLie::Green:    return { 0.22, 0.50, 0.55 };   // smooth, but backspin bites
		case EGolfLie::Bunker:   return { 3.00, 0.08, 0.10 };   // plugs -> essentially no roll
		case EGolfLie::Tee:      return { 0.32, 0.55, 0.20 };
		case EGolfLie::CartPath: return { 0.05, 0.75, 0.00 };   // hardpan -> bounces and runs
		case EGolfLie::OB:       return { 0.65, 0.30, 0.10 };
		default:                 return { 0.30, 0.55, 0.20 };   // Unknown -> fairway-like fallback
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
		const double Vh = Horiz.Size();
		if (Vh < Tiny)
		{
			return R;   // dropped straight down -> no roll
		}
		const FVector Dir = Horiz / Vh;

		// Speed surviving the bounce into the rolling phase: restitution, attenuated by a steep
		// descent (digs in) and by landing backspin (checks up).
		const double DescentAtten = FMath::Clamp(FMath::Cos(FMath::DegreesToRadians(Flight.DescentAngleDeg)), 0.0, 1.0);
		const double SpinFactor = FMath::Clamp(Flight.LandingSpinRpm / RefSpinRpm, 0.0, 1.0);
		const double SpinAtten = FMath::Clamp(1.0 - C.SpinCheck * SpinFactor, 0.0, 1.0);
		const double VRoll = Vh * FMath::Max(C.Restitution, 0.0) * DescentAtten * SpinAtten;

		const double Accel = FMath::Max(C.RollFriction, Tiny) * GravityMps2;   // rolling-friction decel
		const double Dist = (VRoll * VRoll) / (2.0 * Accel);
		if (Dist < MinVisibleRollM || VRoll < Tiny)
		{
			return R;   // bunker / plugged / dead stop
		}

		R.RestPositionM = LandPos + Dir * Dist;
		R.RestPositionM.Z = 0.0;
		R.RollDistanceM = Dist;
		R.TotalDistanceM = FMath::Sqrt(R.RestPositionM.X * R.RestPositionM.X + R.RestPositionM.Y * R.RestPositionM.Y);

		// Decelerating ground polyline so the ball visibly rolls out. Constant decel: stops at t=VRoll/a.
		const double TStop = VRoll / Accel;
		const int32 NumSteps = FMath::Clamp(FMath::RoundToInt(TStop / 0.06), 1, 120);
		R.RollSamples.Reserve(NumSteps);
		for (int32 i = 1; i <= NumSteps; ++i)
		{
			const double T = (i == NumSteps) ? TStop : (TStop * i) / NumSteps;
			const double S = FMath::Min(VRoll * T - 0.5 * Accel * T * T, Dist);
			const double V = FMath::Max(VRoll - Accel * T, 0.0);
			FTrajectorySample Sample;
			Sample.TimeSeconds = Flight.FlightTimeS + T;
			Sample.PositionMeters = FVector(LandPos.X + Dir.X * S, LandPos.Y + Dir.Y * S, 0.0);
			Sample.VelocityMps = Dir * V;
			R.RollSamples.Add(Sample);
		}
		return R;
	}
}
