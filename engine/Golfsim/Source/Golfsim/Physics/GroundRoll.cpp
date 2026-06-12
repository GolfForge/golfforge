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
	constexpr double SpinBackThresholdRpm = 3500.0;   // below this REMAINING spin (post-decay), no spin-back
	constexpr double SpinBackSpinScaleRpm = 4000.0;   // spin span (above threshold) over which gain ramps to full
	constexpr double SpinBackSteepRefDeg  = 40.0;     // descent at/above which spin-back is fully effective

	// GOL-207 spin-aware ground check. Spin is now a decaying STATE through the ground phase (a stand-in
	// until the GOL-208 Penner spin-vector rework): each impact consumes spin, remaining spin kills
	// forward speed per bounce (a really spinny wedge doesn't bounce forward much) and brakes the
	// forward trickle on a green so the check reads immediate, and the backward leg is driven by the
	// DECAYED spin, speed-capped, and ramped up (no instantaneous 4.5 m/s backward jump after the ball
	// has visually settled -- that read as "settles, then warps 1-5 m away"). Provisional, GOL-195.
	constexpr double BounceSpinRetain     = 0.75;   // spin fraction surviving each ground impact
	constexpr double RollSpinDecayRpmPerS = 4000.0; // ground contact scrubs spin during the roll
	constexpr double SpinRollBrakeGain    = 1.5;    // extra roll-friction multiplier at full spin (green-like only)
	constexpr double SpinBackSpeedCapMps  = 2.0;    // hard ceiling on PEAK backward speed (max total zip ~1.4 m on a green)
	constexpr double SpinBackDriveFactor  = 3.0;    // spin-drive accel = factor * friction accel; friction eats 1/factor during the ramp, so net ramp ~= 2x friction
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
		double SpinRpm    = Flight.LandingSpinRpm;   // GOL-207: decaying ground-spin state (impacts + roll scrub it)

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
			// GOL-207: remaining backspin bites EVERY contact, not just the landing scrape -- a spinny
			// wedge's later hops barely move forward. First contact (NumBounces == 0) is already covered
			// by the scrape above, so the per-bounce kill starts at the second impact.
			const double BounceSpinAtten = NumBounces > 0
				? FMath::Clamp(1.0 - C.SpinCheck * FMath::Clamp(SpinRpm / RefSpinRpm, 0.0, 1.0), 0.0, 1.0)
				: 1.0;
			const FVector VOut = VT * (FMath::Max(C.BounceHorizontalKeep, 0.0) * BounceSpinAtten)
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
			SpinRpm *= BounceSpinRetain;   // GOL-207: each impact consumes spin
		}

		// --- Roll phase: stepped friction slide that FOLLOWS THE FALL LINE (GOL-75) -----------------
		// One shared stepper for BOTH ground-roll legs (forward trickle and GOL-39 spin-back). At each
		// step gravity's horizontal slope component curves the heading (break) and modulates speed
		// (uphill dies short, downhill runs on); an optional spin DRIVE injects speed along DriveDir up
		// to DrivePeak (the spin-back ramp). On a FLAT normal with no drive this reduces EXACTLY to the
		// old fixed-heading roll -- the trapezoidal step equals Vh*dt - 0.5*a*dt^2 term-for-term
		// (guarded by the cross-surface-equals-single equivalence test). Friction re-samples per step so
		// it changes at a boundary (fairway -> bunker stops fast in sand).
		// GOL-207b: Dir (the heading the spin-back leg launches against) only updates while the ball
		// still really rolls (> settle floor). Without that gate the dying creep's only velocity is the
		// fall-line feed, so Dir degraded to "downhill" and the spin-back ran the ball BACKWARD along
		// it = straight UP the slope it just landed on (seen on demo Black's 2nd green).
		auto RollLeg = [&](FVector2D Vel, const FVector2D& DriveDir, double DrivePeak, double DriveAccel)
		{
			bool bDriving = DrivePeak > Tiny;
			int32 Steps = 0;
			while (Steps < RollStepsMax)
			{
				const double Speed = Vel.Size();
				if (!bDriving && Speed <= RollSettleSpeedMps) { break; }

				C = CoefAt(Pos);
				// GOL-207: remaining backspin grips a green-like surface (SpinBackGain > 0 marks it) and
				// brakes the forward trickle hard -- the check reads immediate instead of "settle, then
				// warp backward". Spin-free or non-green rolls are untouched (Brake == 1). The spin-back
				// leg enters with SpinRpm already converted into its drive, so it never self-brakes.
				const double Brake  = C.SpinBackGain > 0.0
					? 1.0 + SpinRollBrakeGain * FMath::Clamp(SpinRpm / RefSpinRpm, 0.0, 1.0)
					: 1.0;
				const double Accel  = FMath::Max(C.RollFriction, Tiny) * GravityMps2 * Brake;
				const double DtStep = bDriving
					? RollSampleDt
					: FMath::Min(RollSampleDt, Speed / Accel);   // clip toward a clean friction stop

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

				// Semi-implicit step: friction first (opposes motion, never reverses it), then the spin
				// drive (if any), then the slope feed -- so the spin-back leg breaks downhill too.
				const double    NewSpd = Speed > Tiny ? FMath::Max(Speed - Accel * DtStep, 0.0) : 0.0;
				FVector2D NewVel = (Speed > Tiny ? Vel / Speed : FVector2D::ZeroVector) * NewSpd;
				if (bDriving)
				{
					NewVel += DriveDir * (DriveAccel * DtStep);
					if (NewVel.Size() >= DrivePeak)
					{
						NewVel *= DrivePeak / NewVel.Size();   // spin spent at the target speed -> coast
						bDriving = false;
					}
				}
				NewVel += SlopeDir * (SlopeAccel * DtStep);

				Pos += (Vel + NewVel) * 0.5 * DtStep;   // trapezoidal; on flat == Vh*dt - 0.5*a*dt^2 exactly
				TimeAccum += DtStep;
				Vel = NewVel;
				SpinRpm = FMath::Max(SpinRpm - RollSpinDecayRpmPerS * DtStep, 0.0);   // GOL-207: contact scrubs spin
				if (Vel.Size() > RollSettleSpeedMps) { Dir = Vel / Vel.Size(); }   // GOL-207b: heading only while really rolling

				FTrajectorySample Sample;
				Sample.TimeSeconds    = Flight.FlightTimeS + TimeAccum;
				Sample.PositionMeters = FVector(Pos.X, Pos.Y, 0.0);
				Sample.VelocityMps    = FVector(Vel.X, Vel.Y, 0.0);
				R.RollSamples.Add(Sample);
				++Steps;
			}
		};

		RollLeg(Dir * Vh, FVector2D::ZeroVector, 0.0, 0.0);   // forward trickle out of the bounce phase

		// --- GOL-39 green spin-back: a green ball with enough backspin + steep descent checks forward
		// (above), then rolls BACKWARD against the heading it was checked on. Gated on rest = Green.
		// GOL-207: driven by the DECAYED ground spin (not the raw landing spin), speed-capped, ramped up
		// at SpinBackDriveFactor * friction accel (the residual spin torques the ball back, it doesn't
		// get launched). GOL-207b: runs through the shared fall-line stepper, so on a slope the zip-back
		// curls downhill and settles instead of marching in a rigid straight line.
		{
			const EGolfLie     RestLie = SurfaceAt(FVector(Pos.X, Pos.Y, 0.0));
			const FSurfaceRoll GC      = CoefsFor(RestLie);
			if (RestLie == EGolfLie::Green && GC.SpinBackGain > 0.0
				&& SpinRpm > SpinBackThresholdRpm)
			{
				const double SpinFrac  = FMath::Clamp((SpinRpm - SpinBackThresholdRpm) / SpinBackSpinScaleRpm, 0.0, 1.0);
				const double SteepFrac = FMath::Clamp(Flight.DescentAngleDeg / SpinBackSteepRefDeg, 0.0, 1.0);
				const double VbackPeak = FMath::Min(GC.SpinBackGain * SpinFrac * SteepFrac, SpinBackSpeedCapMps);
				const double Drive     = SpinBackDriveFactor * FMath::Max(GC.RollFriction, Tiny) * GravityMps2;
				SpinRpm = 0.0;   // the remaining spin IS the backward drive; don't let it also brake the leg
				RollLeg(FVector2D::ZeroVector, -Dir, VbackPeak, Drive);
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
