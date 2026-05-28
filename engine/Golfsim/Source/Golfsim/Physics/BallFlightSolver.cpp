#include "Physics/BallFlightSolver.h"

namespace
{
	// Physical constants (SI). Not tunable -- the tunable knobs live in FAeroCoefficients.
	constexpr double Pi = 3.14159265358979323846;
	constexpr double BallMassKg = 0.04593;     // 45.93 g (USGA max)
	constexpr double BallRadiusM = 0.021335;   // 42.67 mm diameter / 2
	constexpr double GravityMps2 = 9.81;
	constexpr double AirViscosity = 1.46e-5;   // kinematic viscosity of air, m^2/s (for Reynolds)

	constexpr double StepDt = 0.001;           // RK4 fixed timestep, s
	constexpr int32 SampleEveryNSteps = 10;    // store a path point every 10 ms
	constexpr int32 MaxSteps = 30000;          // 15 s safety cap
	constexpr double MinLaunchSpeed = 0.1;     // below this, the input is degenerate
	constexpr double Tiny = 1e-9;

	double RpmToRadPerSec(double Rpm) { return Rpm * 2.0 * Pi / 60.0; }
	double RadPerSecToRpm(double Rad) { return Rad * 60.0 / (2.0 * Pi); }
}

namespace GolfBallFlight
{
	FBallTrajectory Simulate(const FShotInput& Shot, const FAeroCoefficients& C, bool bDisableAero)
	{
		if (Shot.BallSpeedMps < MinLaunchSpeed)
		{
			return FBallTrajectory();   // bValid stays false
		}

		const double Area = Pi * BallRadiusM * BallRadiusM;   // cross-section, m^2
		const double InvTau = (C.SpinDecayTau > 0.0) ? 1.0 / C.SpinDecayTau : 0.0;

		// Initial velocity from speed / launch / azimuth.
		const double El = FMath::DegreesToRadians(Shot.LaunchAngleDeg);
		const double Az = FMath::DegreesToRadians(Shot.AzimuthDeg);
		const double V0 = Shot.BallSpeedMps;
		FVector Vel(
			V0 * FMath::Cos(El) * FMath::Cos(Az),
			V0 * FMath::Cos(El) * FMath::Sin(Az),
			V0 * FMath::Sin(El));

		// Spin vector: backspin lifts (axis = -(up x v_horizontal)); sidespin curves about world up
		// (+ = right). For v_horizontal = +X this gives omega_back = -b*Y and omega_side = +s*Z, so
		// the Magnus force omega x v points up (lift) and to the right for positive sidespin.
		FVector Vh(Vel.X, Vel.Y, 0.0);
		Vh = Vh.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : Vh.GetSafeNormal();
		const FVector BackAxis = FVector::CrossProduct(FVector::UpVector, Vh).GetSafeNormal();
		FVector Omega = (-RpmToRadPerSec(Shot.BackspinRpm)) * BackAxis
			+ RpmToRadPerSec(Shot.SidespinRpm) * FVector::UpVector;

		// Acceleration as a function of velocity + spin (position-independent: still air, constant rho).
		auto Accel = [&](const FVector& V, const FVector& W) -> FVector
		{
			const FVector Fg(0.0, 0.0, -BallMassKg * GravityMps2);
			if (bDisableAero)
			{
				return Fg / BallMassKg;
			}
			const double Speed = V.Size();
			if (Speed < Tiny)
			{
				return Fg / BallMassKg;
			}
			const double S = W.Size() * BallRadiusM / Speed;             // spin ratio
			const double Re = Speed * (2.0 * BallRadiusM) / AirViscosity;
			const double Cl = FMath::Clamp(C.Cl0 + C.Cl1 * S + C.Cl2 * S * S, 0.0, C.ClMax);
			const double Cd = FMath::Clamp(C.Cd0 + C.Cd1 * S + C.Cd2 * S * S + C.kRe * (Re - C.ReRef), 0.0, C.CdMax);

			const double Q = 0.5 * C.AirDensity * Area;
			const FVector Fdrag = -Q * Cd * Speed * V;                   // -0.5 rho A Cd V^2 v_hat
			FVector Flift = FVector::ZeroVector;
			const FVector WxV = FVector::CrossProduct(W, V);
			const double WxVLen = WxV.Size();
			if (WxVLen > Tiny)
			{
				Flift = (Q * Cl * Speed * Speed) * (WxV / WxVLen);       // 0.5 rho A Cl V^2 lift_dir
			}
			return (Fg + Fdrag + Flift) / BallMassKg;
		};

		FBallTrajectory Out;
		FVector Pos(0.0, 0.0, 0.0);
		double Time = 0.0;
		double ApexZ = 0.0;

		Out.Samples.Reserve(MaxSteps / SampleEveryNSteps + 2);
		Out.Samples.Add({ 0.0, Pos, Vel });

		bool bLanded = false;
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			const FVector PrevPos = Pos;
			const FVector PrevVel = Vel;
			const FVector PrevOmega = Omega;
			const double PrevTime = Time;

			// RK4 over (Pos, Vel, Omega): dPos/dt = Vel, dVel/dt = Accel(V,W), dOmega/dt = -Omega/tau.
			const FVector k1v = Accel(Vel, Omega);
			const FVector k1w = -Omega * InvTau;

			const FVector v2 = Vel + 0.5 * StepDt * k1v;
			const FVector w2 = Omega + 0.5 * StepDt * k1w;
			const FVector k2v = Accel(v2, w2);
			const FVector k2w = -w2 * InvTau;

			const FVector v3 = Vel + 0.5 * StepDt * k2v;
			const FVector w3 = Omega + 0.5 * StepDt * k2w;
			const FVector k3v = Accel(v3, w3);
			const FVector k3w = -w3 * InvTau;

			const FVector v4 = Vel + StepDt * k3v;
			const FVector w4 = Omega + StepDt * k3w;
			const FVector k4v = Accel(v4, w4);
			const FVector k4w = -w4 * InvTau;

			Pos += (StepDt / 6.0) * (Vel + 2.0 * v2 + 2.0 * v3 + v4);    // dPos uses stage velocities
			Vel += (StepDt / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
			Omega += (StepDt / 6.0) * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
			Time += StepDt;

			ApexZ = FMath::Max(ApexZ, Pos.Z);

			if (PrevPos.Z > 0.0 && Pos.Z <= 0.0)
			{
				const double Denom = PrevPos.Z - Pos.Z;
				const double Alpha = (Denom > Tiny) ? PrevPos.Z / Denom : 1.0;
				const FVector LandPos = FMath::Lerp(PrevPos, Pos, Alpha);
				const FVector LandVel = FMath::Lerp(PrevVel, Vel, Alpha);
				const double LandSpin = FMath::Lerp(PrevOmega.Size(), Omega.Size(), Alpha);
				const double LandTime = PrevTime + Alpha * StepDt;
				Out.LandingSampleIndex = Out.Samples.Num();   // the landing sample we're about to add
				Out.Samples.Add({ LandTime, LandPos, LandVel });

				Out.LandingPositionM = LandPos;
				Out.LandingSpeedMps = LandVel.Size();
				Out.LandingSpinRpm = RadPerSecToRpm(LandSpin);
				Out.FlightTimeS = LandTime;
				Out.CarryM = FMath::Sqrt(LandPos.X * LandPos.X + LandPos.Y * LandPos.Y);
				Out.LateralOffsetM = LandPos.Y;
				const double Horiz = FMath::Sqrt(LandVel.X * LandVel.X + LandVel.Y * LandVel.Y);
				Out.DescentAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(-LandVel.Z, Horiz));
				bLanded = true;
				break;
			}

			if (((Step + 1) % SampleEveryNSteps) == 0)
			{
				Out.Samples.Add({ Time, Pos, Vel });
			}
		}

		if (!bLanded)
		{
			return FBallTrajectory();   // never landed (degenerate input) -> invalid
		}

		Out.ApexM = ApexZ;
		Out.LaunchSpeedMps = V0;
		Out.Source = EBallTrajectorySource::Simulated;
		Out.bValid = true;
		return Out;
	}

	FBallTrajectory TraceFromResolved(const FShotInput& Shot, const FResolvedFlight& Resolved, const FAeroCoefficients& C)
	{
		// Run the physics from the launch conditions, then stretch the arc so it lands at the
		// monitor's reported carry and reaches its reported apex. Carry and apex match exactly;
		// the descent angle follows from the stretched shape (close, since real-data scales are ~1).
		const FBallTrajectory Sim = Simulate(Shot, C);
		if (!Sim.bValid || Sim.Samples.Num() < 2 || Resolved.CarryM <= 0.0 || Sim.CarryM <= Tiny)
		{
			return FBallTrajectory();
		}

		const double HScale = Resolved.CarryM / Sim.CarryM;
		const double VScale = (Resolved.ApexM > 0.0 && Sim.ApexM > Tiny) ? Resolved.ApexM / Sim.ApexM : 1.0;

		FBallTrajectory Out;
		Out.Samples.Reserve(Sim.Samples.Num());
		double ApexZ = 0.0;
		for (const FTrajectorySample& S : Sim.Samples)
		{
			FTrajectorySample N;
			N.TimeSeconds = S.TimeSeconds;
			N.PositionMeters = FVector(S.PositionMeters.X * HScale, S.PositionMeters.Y * HScale, S.PositionMeters.Z * VScale);
			N.VelocityMps = FVector(S.VelocityMps.X * HScale, S.VelocityMps.Y * HScale, S.VelocityMps.Z * VScale);
			ApexZ = FMath::Max(ApexZ, N.PositionMeters.Z);
			Out.Samples.Add(N);
		}

		const FTrajectorySample& Land = Out.Samples.Last();
		Out.LandingSampleIndex = Out.Samples.Num() - 1;   // trace mode tracks no spin (LandingSpinRpm stays 0)
		Out.LandingPositionM = Land.PositionMeters;
		Out.LandingSpeedMps = Land.VelocityMps.Size();
		Out.FlightTimeS = Land.TimeSeconds;
		Out.CarryM = FMath::Sqrt(Land.PositionMeters.X * Land.PositionMeters.X + Land.PositionMeters.Y * Land.PositionMeters.Y);
		Out.LateralOffsetM = Land.PositionMeters.Y;
		const double Horiz = FMath::Sqrt(Land.VelocityMps.X * Land.VelocityMps.X + Land.VelocityMps.Y * Land.VelocityMps.Y);
		Out.DescentAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(-Land.VelocityMps.Z, Horiz));
		Out.ApexM = ApexZ;
		Out.LaunchSpeedMps = Sim.LaunchSpeedMps;
		Out.Source = EBallTrajectorySource::TracedFromSummary;
		Out.bValid = true;
		return Out;
	}
}
