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
	constexpr double RollSlopeAccelCap = GravityMps2;   // GOL-75: defensive cap on fall-line accel (the projection self-limits to ~g/2)

	// GOL-206 low-speed settle floor: below this the ball is at rest. Real greens aren't frictionless
	// at a crawl (grass imperfections hold a sub-5-cm/s ball). Without it the fall-line feed
	// re-energizes a friction-stopped ball every step -- at putter friction on a steep bank the roll
	// can creep until RollStepsMax (240 simulated seconds). Rest shift vs a v->0 stop is v^2/(2a),
	// sub-mm on a green.
	constexpr double RollSettleSpeedMps = 0.05;

	// GOL-75 fall-line strength. A uniform sphere ROLLING (not sliding) down an incline accelerates at
	// (5/7)*g*sin(theta) -- the rotational inertia eats 2/7 of the drive vs a frictionless slide. Real
	// greens break a touch less still (grain + rolling hysteresis), so the remaining 0.5 is a
	// provisional empirical damping knob (LM-gated, promote alongside FSurfaceRoll in GOL-195).
	// Combined: slope accel = RollSlopeGain * g * Nz * |(Nx,Ny)|. Tune this one number to taste.
	constexpr double RollSlopeGain = (5.0 / 7.0) * 0.5;

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
		// GOL-206: BreakSlopeMaxDeg stays at the 45-deg default -- putts deliberately keep FULL break
		// (a putt across a steep famous-green feature should swing hard); only approach-shot roll on
		// Green is clamped, in SurfaceRollFor.
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
		// { RollFriction, Restitution (V-COR), BounceHorizontalKeep, SpinCheck, SpinBackGain,
		//   BreakSlopeMaxDeg (defaults 45 = uncapped when omitted) }.
		// GOL-38: Restitution is the vertical bounce coefficient, BounceHorizontalKeep the per-bounce
		// horizontal decay. Hand-checked driver lands ~280 yd fairway total (carry ~264 + 3 hops +
		// short roll). GOL-39: added SpinBackGain (green-only backward roll) and raised Green SpinCheck
		// 0.55 -> 0.70 so a high-spin wedge's forward roll is small enough for the spin-back to dominate
		// (real greens check hard). GOL-206: Green caps the fall-line slope at 3.5 deg -- LIDAR greens
		// have 6-deg+ bank/false-front cells classified "green" and an uncapped break ran a settling
		// ball ~3 m "to a random spot"; normal pin-area slope (<3.5 deg) still breaks in full.
		// Magnitudes stay provisional until Square Omni LM data lands.
		switch (Lie)
		{
		case EGolfLie::Fairway:  return { 0.30, 0.35, 0.55, 0.20, 0.0 };  // 3 visible hops then a few m of roll
		case EGolfLie::Rough:    return { 0.65, 0.20, 0.40, 0.15, 0.0 };  // grass grabs -> 1-2 small hops, short roll
		case EGolfLie::Green:    return { 0.22, 0.30, 0.50, 0.70, 4.5, 3.5 };  // smooth; backspin checks hard + spins back; break capped
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

		// --- Roll phase: stepped friction slide that now FOLLOWS THE FALL LINE (GOL-75) -------------
		// At each step gravity's horizontal component along the slope curves the heading (break) and
		// modulates speed (uphill slows + stops short, downhill runs on). The heading is carried as a 2D
		// velocity so it can rotate. On a FLAT normal (0,0,1) the slope accel is zero and this reduces
		// EXACTLY to the old fixed-heading roll -- the trapezoidal step equals Vh*dt - 0.5*a*dt^2
		// term-for-term, so Vh^2/(2a) telescoping and all (guarded by the cross-surface-equals-single
		// equivalence test). Friction still re-samples per step so it changes at a boundary (fairway ->
		// bunker stops fast in sand); a putt running off the green onto the fringe slows there too.
		{
			FVector2D Vel = Dir * Vh;   // carry heading + speed out of the bounce phase
			int32 Steps = 0;
			while (Vel.Size() > RollSettleSpeedMps && Steps < RollStepsMax)
			{
				C = CoefAt(Pos);
				const double Speed  = Vel.Size();
				const double Accel  = FMath::Max(C.RollFriction, Tiny) * GravityMps2;
				const double DtStep = FMath::Min(RollSampleDt, Speed / Accel);   // clip toward a clean friction stop

				// Fall-line (downhill) acceleration: horizontal projection of gravity on the slope. For a
				// unit normal N (Nz > 0) the downhill horizontal direction is (Nx, Ny) and the horizontal
				// accel magnitude is g * Nz * |(Nx, Ny)| (self-limits to ~g/2 near 45 deg). Flat -> zero.
				// GOL-206: the Nz * |(Nx,Ny)| product is sin(theta)*cos(theta) -- monotonic below 45 deg --
				// so clamping it at BreakSlopeMaxDeg's value caps the break-driving slope angle per surface
				// (Green = 3.5 deg: a 6-deg LIDAR bank cell can't run a settling ball yards downhill).
				FVector N = NormalAt(FVector(Pos.X, Pos.Y, 0.0));
				if (!N.Normalize() || N.Z <= 0.0) { N = FVector::UpVector; }
				const FVector2D Down(N.X, N.Y);
				const double    DownMag    = Down.Size();
				const double    MaxRad     = FMath::DegreesToRadians(C.BreakSlopeMaxDeg);
				const double    SlopeTerm  = FMath::Min(N.Z * DownMag, FMath::Sin(MaxRad) * FMath::Cos(MaxRad));
				const double    SlopeAccel = FMath::Min(RollSlopeGain * GravityMps2 * SlopeTerm, RollSlopeAccelCap);
				const FVector2D SlopeDir   = DownMag > Tiny ? Down / DownMag : FVector2D::ZeroVector;

				// Semi-implicit step: friction first (opposes motion, never reverses it), then add slope.
				const FVector2D VHat   = Vel / Speed;
				const double    NewSpd = FMath::Max(Speed - Accel * DtStep, 0.0);
				const FVector2D NewVel = VHat * NewSpd + SlopeDir * (SlopeAccel * DtStep);

				Pos += (Vel + NewVel) * 0.5 * DtStep;   // trapezoidal; on flat == Vh*dt - 0.5*a*dt^2 exactly
				TimeAccum += DtStep;
				Vel = NewVel;
				if (Vel.Size() > Tiny) { Dir = Vel / Vel.Size(); }   // keep the last heading for the spin-back leg

				FTrajectorySample Sample;
				Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum;
				Sample.PositionMeters = FVector(Pos.X, Pos.Y, 0.0);
				Sample.VelocityMps    = FVector(Vel.X, Vel.Y, 0.0);
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
