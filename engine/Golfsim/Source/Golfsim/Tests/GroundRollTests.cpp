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
