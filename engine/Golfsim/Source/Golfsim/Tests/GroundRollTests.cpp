// Automation tests for ground interaction + roll (GOL-9): the pure-SI roll model and the range
// lie classifier. No world, no RHI -- the physics and classification are pure functions.
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.GroundRoll; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Physics/BallFlightSolver.h"
#include "Physics/GroundRoll.h"
#include "Physics/RangeSurface.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// Synthesize a landed trajectory at the origin with a given landing speed / descent / spin, so a
	// roll test doesn't need to fly a flight. Total distance then equals roll distance (landing at 0).
	FBallTrajectory MakeLanding(double SpeedMps, double DescentDeg, double SpinRpm)
	{
		const double D = FMath::DegreesToRadians(DescentDeg);
		FBallTrajectory F;
		F.Samples.Add({ 0.0, FVector::ZeroVector,
			FVector(SpeedMps * FMath::Cos(D), 0.0, -SpeedMps * FMath::Sin(D)) });
		F.LandingSampleIndex = 0;
		F.LandingPositionM = FVector::ZeroVector;
		F.LandingSpeedMps = SpeedMps;
		F.LandingSpinRpm = SpinRpm;
		F.DescentAngleDeg = DescentDeg;
		F.bValid = true;
		return F;
	}

	double RollFor(EGolfLie Lie, const FBallTrajectory& F)
	{
		return GolfBallFlight::SimulateGroundRoll(F, Lie, GolfBallFlight::SurfaceRollFor(Lie)).RollDistanceM;
	}

	// GOL-196: flat-ground normal for the cross-surface calls (reproduces the pre-GOL-196 straight bounce).
	FVector FlatNormal(const FVector&) { return FVector::UpVector; }
}

// --- Lie classification at known landing positions (done-when #4) -------------------------------
// World XY in meters, landscape centered at origin. Layout mirrors build_range_splatmap.py:
// 400x70-yd lane (half 182.88 x 32.00 m), 50-yd fairway strip (half 22.86 m), tee box at the -X end.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollLieTest, "Golfsim.GroundRoll.LieClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollLieTest::RunTest(const FString& /*Parameters*/)
{
	using namespace GolfRangeSurface;
	TestTrue(TEXT("tee box (-180, 0) is tee"),          ClassifyLie(-180.0,  0.0) == EGolfLie::Tee);
	TestTrue(TEXT("lane center (100, 0) is fairway"),    ClassifyLie( 100.0,  0.0) == EGolfLie::Fairway);
	TestTrue(TEXT("near tee but downrange is fairway"),  ClassifyLie(-150.0,  0.0) == EGolfLie::Fairway);
	TestTrue(TEXT("lane shoulder (100, 30) is rough"),   ClassifyLie( 100.0, 30.0) == EGolfLie::Rough);
	TestTrue(TEXT("just past fairway edge is rough"),    ClassifyLie( 100.0, 24.0) == EGolfLie::Rough);
	TestTrue(TEXT("tree wall (100, 40) reads as rough"), ClassifyLie( 100.0, 40.0) == EGolfLie::Rough);
	TestTrue(TEXT("far downrange (300, 0) is rough"),    ClassifyLie( 300.0,  0.0) == EGolfLie::Rough);
	return true;
}

// --- Surface ordering: fairway rolls most, rough less, bunker ~0 -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollOrderingTest, "Golfsim.GroundRoll.SurfaceOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollOrderingTest::RunTest(const FString& /*Parameters*/)
{
	const FBallTrajectory Land = MakeLanding(/*speed*/25.0, /*descent*/38.0, /*spin*/2000.0);

	const double Fairway = RollFor(EGolfLie::Fairway, Land);
	const double Rough   = RollFor(EGolfLie::Rough, Land);
	const double Bunker  = RollFor(EGolfLie::Bunker, Land);

	TestTrue(TEXT("fairway rolls a meaningful distance"), Fairway > 2.0);
	TestTrue(TEXT("fairway rolls farther than rough"), Fairway > Rough);
	TestTrue(TEXT("rough rolls farther than bunker"), Rough > Bunker);
	TestTrue(TEXT("bunker is essentially no roll"), Bunker < 0.5);
	return true;
}

// --- A driver on fairway: total exceeds carry by a sensible rollout ----------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollDriverTest, "Golfsim.GroundRoll.FairwayDriverTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollDriverTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Drive;
	Drive.BallSpeedMps = 74.6;
	Drive.LaunchAngleDeg = 10.9;
	Drive.BackspinRpm = 2686.0;

	const FBallTrajectory Flight = GolfBallFlight::Simulate(Drive);
	TestTrue(TEXT("flight valid"), Flight.bValid);

	const FGroundRollResult R =
		GolfBallFlight::SimulateGroundRoll(Flight, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));
	TestTrue(TEXT("roll valid"), R.bValid);
	TestTrue(TEXT("total exceeds carry"), R.TotalDistanceM > Flight.CarryM);
	TestTrue(TEXT("rollout is sensible (>2 m)"), R.RollDistanceM > 2.0);
	TestTrue(TEXT("rollout is not runaway (<50 m)"), R.RollDistanceM < 50.0);
	TestTrue(TEXT("emits a rollout polyline"), R.RollSamples.Num() > 0);

	AddInfo(FString::Printf(TEXT("driver carry %.1f yd, total %.1f yd (roll %.1f yd)"),
		Flight.CarryM * 1.0936132983, R.TotalDistanceM * 1.0936132983, R.RollDistanceM * 1.0936132983));
	return true;
}

// --- Full-bag report: total >= carry, plus per-club hops/peaks/total info ----------------------
// Covers the Trackman PGA-Tour averages from AGolfRangeHUD::GBag. The asserts are conservative
// (total >= carry + each total > 0); AddInfo emits the per-club hop signature so the model can be
// hand-checked against expectations (driver runs out, wedges check up). Adjust ground-roll
// coefficients in Physics/GroundRoll.cpp::SurfaceRollFor based on this readout.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollTotalGEQCarryTest, "Golfsim.GroundRoll.TotalNeverLessThanCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollTotalGEQCarryTest::RunTest(const FString& /*Parameters*/)
{
	// Mirror of AGolfRangeHUD::GBag -- if you add or tune a club there, update here too.
	struct FCase { const TCHAR* Name; double Speed; double Launch; double Spin; };
	static const FCase Cases[] = {
		{ TEXT("Driver"),         74.6, 10.9,  2686.0 },
		{ TEXT("3-Wood"),         70.6,  9.2,  3655.0 },
		{ TEXT("5-Wood"),         67.4,  9.4,  4350.0 },
		{ TEXT("4-Iron"),         64.4, 10.4,  4630.0 },
		{ TEXT("5-Iron"),         60.3, 11.9,  5280.0 },
		{ TEXT("6-Iron"),         57.7, 14.1,  6204.0 },
		{ TEXT("7-Iron"),         55.0, 16.3,  7097.0 },
		{ TEXT("8-Iron"),         52.6, 18.1,  7998.0 },
		{ TEXT("9-Iron"),         48.7, 20.4,  8647.0 },
		{ TEXT("Pitching Wedge"), 45.6, 24.2,  9304.0 },
		{ TEXT("Gap Wedge 50"),   43.0, 26.0,  9750.0 },
		{ TEXT("Sand Wedge 56"),  40.0, 28.0, 10500.0 },
		{ TEXT("Lob Wedge 60"),   36.0, 32.0, 11500.0 },
		{ TEXT("Putter"),          4.0,  1.0,   100.0 },   // placeholder
	};

	constexpr double YdPerM = 1.0936132983;
	constexpr double AirborneEps = 0.01;   // m

	for (const FCase& Cse : Cases)
	{
		FShotInput Shot;
		Shot.BallSpeedMps  = Cse.Speed;
		Shot.LaunchAngleDeg = Cse.Launch;
		Shot.BackspinRpm   = Cse.Spin;
		const FBallTrajectory Flight = GolfBallFlight::Simulate(Shot);
		const FGroundRollResult R = GolfBallFlight::SimulateGroundRoll(
			Flight, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));

		TestTrue(*FString::Printf(TEXT("%s: roll valid"), Cse.Name), R.bValid);
		TestTrue(*FString::Printf(TEXT("%s: total >= carry"), Cse.Name),
			R.TotalDistanceM >= Flight.CarryM - 0.01);

		// Walk RollSamples to extract hop peaks (parabola apex per airborne run).
		TArray<double> HopPeaks;
		double CurPeak = 0.0;
		bool   bInHop = false;
		for (const FTrajectorySample& S : R.RollSamples)
		{
			const double Z = S.PositionMeters.Z;
			if (Z > AirborneEps)
			{
				bInHop = true;
				CurPeak = FMath::Max(CurPeak, Z);
			}
			else if (bInHop)
			{
				HopPeaks.Add(CurPeak);
				CurPeak = 0.0;
				bInHop = false;
			}
		}
		if (bInHop) { HopPeaks.Add(CurPeak); }

		const FString PeaksStr = HopPeaks.Num() == 0 ? TEXT("none")
			: FString::JoinBy(HopPeaks, TEXT(" "), [](double P) { return FString::Printf(TEXT("%.2f"), P); });
		AddInfo(FString::Printf(
			TEXT("%-15s carry=%5.1f yd  hops=%d  peaks(m)=[ %s ]  roll-phase=%4.1f yd  total=%5.1f yd  descent=%4.1f deg  landSpin=%5.0f rpm"),
			Cse.Name,
			Flight.CarryM * YdPerM,
			HopPeaks.Num(),
			*PeaksStr,
			R.RollDistanceM * YdPerM,
			R.TotalDistanceM * YdPerM,
			Flight.DescentAngleDeg,
			Flight.LandingSpinRpm));
	}
	return true;
}

// --- Steeper descent checks up: less roll on the same surface ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollSteepTest, "Golfsim.GroundRoll.SteepLandsShorter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollSteepTest::RunTest(const FString& /*Parameters*/)
{
	const double Shallow = RollFor(EGolfLie::Fairway, MakeLanding(25.0, 35.0, 2000.0));
	const double Steep   = RollFor(EGolfLie::Fairway, MakeLanding(25.0, 55.0, 2000.0));
	TestTrue(TEXT("shallower descent rolls farther"), Shallow > Steep);
	return true;
}

// --- GOL-38 multi-bounce phase: driver shows >= 2 visible hops, peaks strictly decreasing ------
//
// Peak detection over RollSamples: a "hop" is a maximal contiguous run where Z > AirborneEps; its
// peak is the max Z in the run. Stricter epsilon than 0 because the parabola's last sample is
// clamped to z=0 but the prior step may carry sub-cm float residual.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollDriverHopsTest, "Golfsim.GroundRoll.DriverHops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollDriverHopsTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Drive;
	Drive.BallSpeedMps = 74.6;
	Drive.LaunchAngleDeg = 10.9;
	Drive.BackspinRpm = 2686.0;

	const FBallTrajectory Flight = GolfBallFlight::Simulate(Drive);
	const FGroundRollResult R =
		GolfBallFlight::SimulateGroundRoll(Flight, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));
	TestTrue(TEXT("roll valid"), R.bValid);

	constexpr double AirborneEps = 0.01;   // m
	TArray<double> HopPeaks;
	double CurrentPeak = 0.0;
	bool   bInHop = false;
	for (const FTrajectorySample& S : R.RollSamples)
	{
		const double Z = S.PositionMeters.Z;
		if (Z > AirborneEps)
		{
			bInHop = true;
			CurrentPeak = FMath::Max(CurrentPeak, Z);
		}
		else if (bInHop)
		{
			HopPeaks.Add(CurrentPeak);
			CurrentPeak = 0.0;
			bInHop = false;
		}
	}
	if (bInHop) { HopPeaks.Add(CurrentPeak); }   // trailing hop (shouldn't happen w/ z=0 clamp, but be safe)

	TestTrue(TEXT("driver shows visible hops (>= 2)"), HopPeaks.Num() >= 2);
	for (int32 i = 1; i < HopPeaks.Num(); ++i)
	{
		TestTrue(TEXT("hop peaks strictly decrease"), HopPeaks[i] < HopPeaks[i - 1]);
	}

	AddInfo(FString::Printf(TEXT("driver hops=%d, peaks (m)=%s; total=%.1f yd (carry=%.1f yd, roll-phase=%.1f yd)"),
		HopPeaks.Num(),
		HopPeaks.Num() == 0 ? TEXT("(none)")
			: *FString::JoinBy(HopPeaks, TEXT(", "), [](double P) { return FString::Printf(TEXT("%.2f"), P); }),
		R.TotalDistanceM * 1.0936132983, Flight.CarryM * 1.0936132983, R.RollDistanceM * 1.0936132983));
	return true;
}

// --- GOL-109 putter: stimp scales rollout distance; bounce loop self-skips with Vv = 0 ---------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollPutterStimpTest,
	"Golfsim.GroundRoll.PutterStimpScalesDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollPutterStimpTest::RunTest(const FString& /*Parameters*/)
{
	// 4 m/s putt with descent = 0 (no vertical component) so the bounce loop has nothing to launch.
	const FBallTrajectory Land = MakeLanding(/*speed*/4.0, /*descent*/0.0, /*spin*/100.0);

	const FGroundRollResult Slow = GolfBallFlight::SimulateGroundRoll(
		Land, EGolfLie::Green, GolfBallFlight::PutterSurfaceRoll(11.0));   // tour green
	const FGroundRollResult Fast = GolfBallFlight::SimulateGroundRoll(
		Land, EGolfLie::Green, GolfBallFlight::PutterSurfaceRoll(14.0));   // Augusta-fast

	constexpr double FtPerM = 3.28084;
	const double SlowFt = Slow.RollDistanceM * FtPerM;
	const double FastFt = Fast.RollDistanceM * FtPerM;

	TestTrue(TEXT("stimp 11 rolls >= 30 ft"), SlowFt >= 30.0);
	TestTrue(TEXT("stimp 14 rolls farther than stimp 11"), FastFt > SlowFt);

	// Putt scrapes -- no parabola hops emitted.
	int32 HopSamples = 0;
	for (const FTrajectorySample& S : Slow.RollSamples)
	{
		if (S.PositionMeters.Z > 0.01) { ++HopSamples; }
	}
	TestTrue(TEXT("no bounce samples on a putt"), HopSamples == 0);

	AddInfo(FString::Printf(TEXT("putter @ 4 m/s: stimp 11 -> %.1f ft, stimp 14 -> %.1f ft"),
		SlowFt, FastFt));
	return true;
}

// --- A short-tempo putt lands around real-world expectations ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollPutterShortTest,
	"Golfsim.GroundRoll.PutterShortDistanceCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollPutterShortTest::RunTest(const FString& /*Parameters*/)
{
	// 2.5 m/s ~ tour-tempo 20 ft putt impact speed.
	const FBallTrajectory Land = MakeLanding(/*speed*/2.5, /*descent*/0.0, /*spin*/100.0);
	const FGroundRollResult R = GolfBallFlight::SimulateGroundRoll(
		Land, EGolfLie::Green, GolfBallFlight::PutterSurfaceRoll(11.0));

	constexpr double FtPerM = 3.28084;
	const double Ft = R.RollDistanceM * FtPerM;
	// Math: 2.5^2 / (2 * (0.67/11) * 9.81) = 6.25 / 1.195 ≈ 5.23 m ≈ 17.2 ft. Allow ±3 ft slack.
	TestTrue(TEXT("2.5 m/s rolls in the 14-20 ft band on stimp 11"), Ft > 14.0 && Ft < 20.0);
	AddInfo(FString::Printf(TEXT("putter @ 2.5 m/s, stimp 11 -> %.1f ft"), Ft));
	return true;
}

// --- GOL-39 cross-surface roll: a ball rolling fairway -> bunker grabs in the sand --------------
// Pure-roll landing (shallow descent, low spin -> no hops) so the boundary crossing happens during
// the roll phase. With a bunker beyond x=5 m, the ball decelerates hard once it crosses in.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollCrossSurfaceTest, "Golfsim.GroundRoll.CrossSurfaceFairwayToBunker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollCrossSurfaceTest::RunTest(const FString& /*Parameters*/)
{
	const FBallTrajectory Land = MakeLanding(/*speed*/10.0, /*descent*/5.0, /*spin*/500.0);
	const double Boundary = 5.0;   // meters along the roll: fairway before, bunker after

	auto AllFairway = [](const FVector&) { return EGolfLie::Fairway; };
	auto Mixed = [Boundary](const FVector& P) { return P.X < Boundary ? EGolfLie::Fairway : EGolfLie::Bunker; };

	const FGroundRollResult Far = GolfBallFlight::SimulateGroundRollCrossSurface(
		Land, AllFairway, &GolfBallFlight::SurfaceRollFor, &FlatNormal);
	const FGroundRollResult Trap = GolfBallFlight::SimulateGroundRollCrossSurface(
		Land, Mixed, &GolfBallFlight::SurfaceRollFor, &FlatNormal);

	TestTrue(TEXT("both rolls valid"), Far.bValid && Trap.bValid);
	TestTrue(TEXT("ball reaches the sand (past the boundary)"), Trap.RestPositionM.X > Boundary);
	TestTrue(TEXT("sand stops it shortly past the boundary"), Trap.RestPositionM.X < Boundary + 3.0);
	TestTrue(TEXT("crossing into the bunker shortens the roll vs all-fairway"),
		Trap.RestPositionM.X < Far.RestPositionM.X - 5.0);

	AddInfo(FString::Printf(TEXT("all-fairway rest %.1f m, fairway->bunker rest %.1f m (boundary %.0f m)"),
		Far.RestPositionM.X, Trap.RestPositionM.X, Boundary));
	return true;
}

// --- GOL-39 single-surface equivalence: a constant surface == the 3-arg wrapper -----------------
// Guards the refactor: SimulateGroundRollCrossSurface with a constant-fairway field must reproduce
// the single-surface SimulateGroundRoll exactly (the stepped roll telescopes to the closed form).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollEquivalenceTest, "Golfsim.GroundRoll.CrossSurfaceConstantEqualsSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollEquivalenceTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Drive;
	Drive.BallSpeedMps = 74.6;
	Drive.LaunchAngleDeg = 10.9;
	Drive.BackspinRpm = 2686.0;
	const FBallTrajectory Flight = GolfBallFlight::Simulate(Drive);

	const FGroundRollResult Single = GolfBallFlight::SimulateGroundRoll(
		Flight, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));
	auto Fairway = [](const FVector&) { return EGolfLie::Fairway; };
	const FGroundRollResult Cross = GolfBallFlight::SimulateGroundRollCrossSurface(
		Flight, Fairway, &GolfBallFlight::SurfaceRollFor, &FlatNormal);

	TestEqual(TEXT("same sample count"), Cross.RollSamples.Num(), Single.RollSamples.Num());
	TestTrue(TEXT("same roll distance"), FMath::Abs(Cross.RollDistanceM - Single.RollDistanceM) < 1e-6);
	TestTrue(TEXT("same rest X"), FMath::Abs(Cross.RestPositionM.X - Single.RestPositionM.X) < 1e-6);
	return true;
}

// --- GOL-39 green spin-back: a high-spin, steep wedge checks and rolls BACKWARD on a green -------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollSpinBackTest, "Golfsim.GroundRoll.GreenSpinBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollSpinBackTest::RunTest(const FString& /*Parameters*/)
{
	auto Green   = [](const FVector&) { return EGolfLie::Green; };
	auto Fairway = [](const FVector&) { return EGolfLie::Fairway; };

	// High backspin + steep descent on a green: lands at origin, ends BEHIND it (negative X).
	const FGroundRollResult HighSpin = GolfBallFlight::SimulateGroundRollCrossSurface(
		MakeLanding(/*speed*/18.0, /*descent*/48.0, /*spin*/9000.0), Green, &GolfBallFlight::SurfaceRollFor, &FlatNormal);
	// Low backspin on the same green: rolls forward, no spin-back.
	const FGroundRollResult LowSpin = GolfBallFlight::SimulateGroundRollCrossSurface(
		MakeLanding(18.0, 48.0, 2000.0), Green, &GolfBallFlight::SurfaceRollFor, &FlatNormal);
	// Same high spin on a fairway: spin-back is green-only, so it rolls forward.
	const FGroundRollResult HighSpinFairway = GolfBallFlight::SimulateGroundRollCrossSurface(
		MakeLanding(18.0, 48.0, 9000.0), Fairway, &GolfBallFlight::SurfaceRollFor, &FlatNormal);

	TestTrue(TEXT("high-spin green ball ends behind its landing (X < 0)"), HighSpin.RestPositionM.X < -0.5);
	TestTrue(TEXT("low-spin green ball rolls forward (X > 0)"), LowSpin.RestPositionM.X > 0.0);
	TestTrue(TEXT("high spin on fairway rolls forward (green-only spin-back)"), HighSpinFairway.RestPositionM.X > 0.0);
	TestTrue(TEXT("more spin -> further back than less spin"), HighSpin.RestPositionM.X < LowSpin.RestPositionM.X);

	// A backward leg actually exists (some sample moving toward the tee, velocity.X < 0).
	bool bSawBackward = false;
	for (const FTrajectorySample& S : HighSpin.RollSamples)
	{
		if (S.VelocityMps.X < -1e-3) { bSawBackward = true; break; }
	}
	TestTrue(TEXT("emits a backward-moving sample run"), bSawBackward);

	AddInfo(FString::Printf(TEXT("spin-back: high-spin rest %.2f m, low-spin rest %.2f m, fairway rest %.2f m"),
		HighSpin.RestPositionM.X, LowSpin.RestPositionM.X, HighSpinFairway.RestPositionM.X));
	return true;
}

// --- GOL-207 high-spin green check: the ball stays NEAR its touchdown, no runaway ----------------
// The bug: a high-spin wedge bounced, trickled forward, nearly settled, then got LAUNCHED backward at
// 4.5 m/s (up to ~4.7 m, "settles then warps away"). Now spin decays through the ground phase, kills
// the forward hops per bounce, brakes the green trickle, and the backward leg ramps from ~0 with a
// hard speed cap -- so a spinny wedge drops, checks, and zips back modestly.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollHighSpinCheckTest, "Golfsim.GroundRoll.HighSpinStaysNearTouchdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollHighSpinCheckTest::RunTest(const FString& /*Parameters*/)
{
	auto Green = [](const FVector&) { return EGolfLie::Green; };
	auto Run = [&](double SpinRpm)
	{
		return GolfBallFlight::SimulateGroundRollCrossSurface(
			MakeLanding(/*speed*/18.0, /*descent*/48.0, SpinRpm), Green, &GolfBallFlight::SurfaceRollFor, &FlatNormal);
	};

	const FGroundRollResult S2000 = Run(2000.0);
	const FGroundRollResult S6000 = Run(6000.0);
	const FGroundRollResult S8000 = Run(8000.0);
	const FGroundRollResult S9500 = Run(9500.0);

	TestTrue(TEXT("all valid"), S2000.bValid && S6000.bValid && S8000.bValid && S9500.bValid);

	// The headline: a high-spin wedge ends NEAR its touchdown -- a modest zip-back, not a 3-5 m run.
	TestTrue(TEXT("8000 rpm rests within 1.6 m of touchdown"), FMath::Abs(S8000.RestPositionM.X) < 1.6 && FMath::Abs(S8000.RestPositionM.Y) < 0.1);
	TestTrue(TEXT("8000 rpm checks (no forward release past 0.6 m)"), S8000.RestPositionM.X < 0.6);
	TestTrue(TEXT("9500 rpm also stays within 1.6 m"), FMath::Abs(S9500.RestPositionM.X) < 1.6);
	TestTrue(TEXT("low spin still releases forward"), S2000.RestPositionM.X > 1.5);
	TestTrue(TEXT("mid spin sits between the extremes"), S6000.RestPositionM.X < S2000.RestPositionM.X
		&& S6000.RestPositionM.X > S8000.RestPositionM.X - 0.1);

	// The backward leg ramps up from ~rest (no instantaneous backward launch) and respects the cap.
	double FirstBackSpeed = -1.0, MaxBackSpeed = 0.0, MaxForwardX = 0.0;
	for (const FTrajectorySample& S : S8000.RollSamples)
	{
		MaxForwardX = FMath::Max(MaxForwardX, S.PositionMeters.X);
		if (S.VelocityMps.X < -1e-3)
		{
			const double Spd = S.VelocityMps.Size();
			if (FirstBackSpeed < 0.0) { FirstBackSpeed = Spd; }
			MaxBackSpeed = FMath::Max(MaxBackSpeed, Spd);
		}
	}
	TestTrue(TEXT("a backward leg exists"), FirstBackSpeed >= 0.0);
	TestTrue(TEXT("backward leg ramps up (first backward sample < 0.6 m/s)"), FirstBackSpeed < 0.6);
	TestTrue(TEXT("backward speed honors the 2.0 m/s cap"), MaxBackSpeed < 2.05);
	TestTrue(TEXT("forward excursion before the check is short (< 1.5 m)"), MaxForwardX < 1.5);

	AddInfo(FString::Printf(TEXT("net roll X by spin: 2000 %.2f, 6000 %.2f, 8000 %.2f, 9500 %.2f | 8000: fwd peak %.2f, back first %.2f peak %.2f m/s"),
		S2000.RestPositionM.X, S6000.RestPositionM.X, S8000.RestPositionM.X, S9500.RestPositionM.X,
		MaxForwardX, FirstBackSpeed, MaxBackSpeed));
	return true;
}

// --- GOL-196 terrain-aware bounce: the outgoing heading reflects off the surface normal ----------
// Same landing on fairway, four surface normals: flat / down-slope / up-slope / side-slope. The
// outgoing bounce now depends on the slope (flat reproduces the straight-bounce model, guarded by the
// equivalence test above).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollSlopeBounceTest, "Golfsim.GroundRoll.SlopeBounceDeflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollSlopeBounceTest::RunTest(const FString& /*Parameters*/)
{
	const FBallTrajectory Land = MakeLanding(/*speed*/13.0, /*descent*/22.0, /*spin*/300.0);   // travel +X, a few hops
	auto Fairway = [](const FVector&) { return EGolfLie::Fairway; };

	auto DownN = [](const FVector&) { return FVector(0.25, 0.0, 1.0).GetSafeNormal(); };   // ground descends toward +X
	auto UpN   = [](const FVector&) { return FVector(-0.25, 0.0, 1.0).GetSafeNormal(); };  // ground rises toward +X
	auto SideN = [](const FVector&) { return FVector(0.0, 0.25, 1.0).GetSafeNormal(); };   // descends toward +Y

	const FGroundRollResult Flat = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Fairway, &GolfBallFlight::SurfaceRollFor, &FlatNormal);
	const FGroundRollResult Down = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Fairway, &GolfBallFlight::SurfaceRollFor, DownN);
	const FGroundRollResult Up   = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Fairway, &GolfBallFlight::SurfaceRollFor, UpN);
	const FGroundRollResult Side = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Fairway, &GolfBallFlight::SurfaceRollFor, SideN);

	auto MaxZ = [](const FGroundRollResult& Res)
	{
		double M = 0.0;
		for (const FTrajectorySample& S : Res.RollSamples) { M = FMath::Max(M, S.PositionMeters.Z); }
		return M;
	};

	TestTrue(TEXT("all valid"), Flat.bValid && Down.bValid && Up.bValid && Side.bValid);
	TestTrue(TEXT("down-slope kicks forward, runs out longer than flat"), Down.RestPositionM.X > Flat.RestPositionM.X);
	TestTrue(TEXT("down-slope runs longer than up-slope"), Down.RestPositionM.X > Up.RestPositionM.X);
	TestTrue(TEXT("up-slope pops the ball higher than flat"), MaxZ(Up) > MaxZ(Flat) + 0.05);
	TestTrue(TEXT("flat stays straight (no lateral)"), FMath::Abs(Flat.RestPositionM.Y) < 1e-6);
	TestTrue(TEXT("side-slope throws the ball laterally downhill (+Y)"), Side.RestPositionM.Y > 0.5);

	AddInfo(FString::Printf(TEXT("rest X: flat %.1f, down %.1f, up %.1f | apexZ flat %.2f up %.2f | side Y %.1f"),
		Flat.RestPositionM.X, Down.RestPositionM.X, Up.RestPositionM.X, MaxZ(Flat), MaxZ(Up), Side.RestPositionM.Y));
	return true;
}

// --- GOL-75 fall-line roll: the roll phase breaks along the slope (putting break + fairway run) -----
// A pure-roll landing (zero descent, low speed -> no hops) on a green with a constant slope: a SIDE
// slope curves the putt laterally, an UP slope stops it short, a DOWN slope runs it past flat, and a
// FLAT normal holds a dead-straight line. Putter (stimp 11) green friction -> putt-scale numbers.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollFallLineTest, "Golfsim.GroundRoll.RollFollowsFallLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollFallLineTest::RunTest(const FString& /*Parameters*/)
{
	auto Green     = [](const FVector&) { return EGolfLie::Green; };
	auto PuttCoefs = [](EGolfLie) { return GolfBallFlight::PutterSurfaceRoll(11.0); };

	auto SideN = [](const FVector&) { return FVector(0.0,  0.04, 1.0).GetSafeNormal(); };   // descends toward +Y
	auto UpN   = [](const FVector&) { return FVector(-0.04, 0.0, 1.0).GetSafeNormal(); };   // rises toward +X
	auto DownN = [](const FVector&) { return FVector(0.04,  0.0, 1.0).GetSafeNormal(); };   // descends toward +X

	const FBallTrajectory Putt = MakeLanding(/*speed*/3.0, /*descent*/0.0, /*spin*/100.0);   // travels +X

	const FGroundRollResult Flat = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, &FlatNormal);
	const FGroundRollResult Side = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, SideN);
	const FGroundRollResult Up   = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, UpN);
	const FGroundRollResult Down = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, DownN);

	TestTrue(TEXT("all valid"), Flat.bValid && Side.bValid && Up.bValid && Down.bValid);
	TestTrue(TEXT("flat putt holds its line (no lateral break)"), FMath::Abs(Flat.RestPositionM.Y) < 1e-6);
	TestTrue(TEXT("side slope breaks the putt downhill (+Y)"), Side.RestPositionM.Y > 0.05);
	TestTrue(TEXT("up slope stops the putt shorter than flat"), Up.RestPositionM.X < Flat.RestPositionM.X);
	TestTrue(TEXT("down slope runs the putt past flat"), Down.RestPositionM.X > Flat.RestPositionM.X);

	AddInfo(FString::Printf(TEXT("fall-line: flat X %.2f, up X %.2f, down X %.2f | side Y %.3f"),
		Flat.RestPositionM.X, Up.RestPositionM.X, Down.RestPositionM.X, Side.RestPositionM.Y));
	return true;
}

// --- GOL-206 green break clamp: a 6-deg "green" cell breaks no more than a 3.5-deg one -----------
// LIDAR greens have steep bank/false-front cells classified green; the per-surface BreakSlopeMaxDeg
// (Green = 3.5 deg) caps the fall-line feed so a settling approach can't run yards downhill, while
// sub-clamp slope (2 deg vs 3.5 deg) still breaks progressively.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollBreakClampTest, "Golfsim.GroundRoll.GreenBreakSlopeClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollBreakClampTest::RunTest(const FString& /*Parameters*/)
{
	auto Green = [](const FVector&) { return EGolfLie::Green; };
	auto SideNDeg = [](double Deg)
	{
		return [Deg](const FVector&) { return FVector(0.0, FMath::Tan(FMath::DegreesToRadians(Deg)), 1.0).GetSafeNormal(); };
	};

	// Low spin so GOL-39 spin-back stays out of the picture; zero descent -> pure roll, travels +X.
	const FBallTrajectory Land = MakeLanding(/*speed*/3.0, /*descent*/0.0, /*spin*/100.0);

	const FGroundRollResult S2  = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Green, &GolfBallFlight::SurfaceRollFor, SideNDeg(2.0));
	const FGroundRollResult S35 = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Green, &GolfBallFlight::SurfaceRollFor, SideNDeg(3.5));
	const FGroundRollResult S6  = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Green, &GolfBallFlight::SurfaceRollFor, SideNDeg(6.0));

	// Non-green literals omit the field; C++14 aggregate init takes the NSDMI (45), NOT zero-init.
	TestTrue(TEXT("omitted BreakSlopeMaxDeg falls back to the uncapped 45-deg default"),
		FMath::IsNearlyEqual(GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway).BreakSlopeMaxDeg, 45.0, 1e-9));

	TestTrue(TEXT("all valid"), S2.bValid && S35.bValid && S6.bValid);
	TestTrue(TEXT("sub-clamp slope still breaks (2 deg < 3.5 deg)"), S2.RestPositionM.Y < S35.RestPositionM.Y - 0.01);
	TestTrue(TEXT("3.5 deg breaks visibly"), S35.RestPositionM.Y > 0.05);
	TestTrue(TEXT("6 deg is clamped to the 3.5-deg break"), FMath::Abs(S6.RestPositionM.Y - S35.RestPositionM.Y) < 1e-3);
	TestTrue(TEXT("6 deg break is bounded (no multi-meter run-off)"), S6.RestPositionM.Y < 0.6);

	AddInfo(FString::Printf(TEXT("green break Y: 2deg %.3f, 3.5deg %.3f, 6deg %.3f"),
		S2.RestPositionM.Y, S35.RestPositionM.Y, S6.RestPositionM.Y));
	return true;
}

// --- GOL-206 putter keeps FULL break: no clamp on PutterSurfaceRoll ------------------------------
// Deliberate asymmetry: a putt across a steep famous-green feature should swing harder at 6 deg than
// at 3.5 deg (BreakSlopeMaxDeg stays at the 45-deg default in the putter coefficients).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollPutterBreakTest, "Golfsim.GroundRoll.PutterBreakUnclamped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollPutterBreakTest::RunTest(const FString& /*Parameters*/)
{
	auto Green     = [](const FVector&) { return EGolfLie::Green; };
	auto PuttCoefs = [](EGolfLie) { return GolfBallFlight::PutterSurfaceRoll(11.0); };
	auto SideNDeg  = [](double Deg)
	{
		return [Deg](const FVector&) { return FVector(0.0, FMath::Tan(FMath::DegreesToRadians(Deg)), 1.0).GetSafeNormal(); };
	};

	const FBallTrajectory Putt = MakeLanding(/*speed*/3.0, /*descent*/0.0, /*spin*/100.0);

	const FGroundRollResult P35 = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, SideNDeg(3.5));
	const FGroundRollResult P6  = GolfBallFlight::SimulateGroundRollCrossSurface(Putt, Green, PuttCoefs, SideNDeg(6.0));

	TestTrue(TEXT("both valid"), P35.bValid && P6.bValid);
	TestTrue(TEXT("a putt across 6 deg breaks MORE than across 3.5 deg (unclamped)"),
		P6.RestPositionM.Y > P35.RestPositionM.Y + 0.2);

	AddInfo(FString::Printf(TEXT("putter break Y: 3.5deg %.2f, 6deg %.2f"),
		P35.RestPositionM.Y, P6.RestPositionM.Y));
	return true;
}

// --- GOL-206 low-speed settle floor: a slow ball on a gentle slope STOPS -------------------------
// The fall-line feed re-energizes a friction-stopped ball each step; the settle floor cuts the creep
// tail so the roll terminates promptly with a near-zero final speed (not at the RollStepsMax cap).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollSettleFloorTest, "Golfsim.GroundRoll.SettleFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollSettleFloorTest::RunTest(const FString& /*Parameters*/)
{
	auto Green = [](const FVector&) { return EGolfLie::Green; };
	auto DownN = [](const FVector&) { return FVector(FMath::Tan(FMath::DegreesToRadians(2.0)), 0.0, 1.0).GetSafeNormal(); };   // descends toward +X (the travel direction)

	const FBallTrajectory Land = MakeLanding(/*speed*/3.0, /*descent*/0.0, /*spin*/100.0);
	const FGroundRollResult R  = GolfBallFlight::SimulateGroundRollCrossSurface(Land, Green, &GolfBallFlight::SurfaceRollFor, DownN);

	TestTrue(TEXT("valid"), R.bValid);
	TestTrue(TEXT("has roll samples"), R.RollSamples.Num() > 0);
	TestTrue(TEXT("settles in a sane number of steps (not the 4000-step cap)"), R.RollSamples.Num() < 500);
	if (R.RollSamples.Num() > 0)
	{
		const FTrajectorySample& LastS = R.RollSamples.Last();
		TestTrue(TEXT("final speed is at/below the settle floor"), LastS.VelocityMps.Size() < 0.06);
		TestTrue(TEXT("settles within seconds, no minutes-long creep"), LastS.TimeSeconds < 5.0);
	}
	return true;
}

// --- Lie <-> protocol string round-trips -------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollLieStringTest, "Golfsim.GroundRoll.LieStringRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollLieStringTest::RunTest(const FString& /*Parameters*/)
{
	const EGolfLie All[] = { EGolfLie::Tee, EGolfLie::Fairway, EGolfLie::Rough,
		EGolfLie::Bunker, EGolfLie::Green, EGolfLie::CartPath, EGolfLie::OB, EGolfLie::Unknown };
	for (EGolfLie L : All)
	{
		TestTrue(TEXT("lie round-trips through its protocol string"), LieFromProtocol(LieToProtocol(L)) == L);
	}
	TestTrue(TEXT("unknown string maps to Unknown"), LieFromProtocol(TEXT("nonsense")) == EGolfLie::Unknown);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
