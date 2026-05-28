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

// --- Total is never less than carry, across the bag --------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGroundRollTotalGEQCarryTest, "Golfsim.GroundRoll.TotalNeverLessThanCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGroundRollTotalGEQCarryTest::RunTest(const FString& /*Parameters*/)
{
	struct FCase { double Speed; double Launch; double Spin; };
	static const FCase Cases[] = {
		{ 74.6, 10.9, 2686.0 },   // driver
		{ 55.0, 16.3, 7097.0 },   // 7-iron
		{ 45.6, 24.2, 9304.0 },   // pitching wedge
	};
	for (const FCase& Cse : Cases)
	{
		FShotInput Shot;
		Shot.BallSpeedMps = Cse.Speed;
		Shot.LaunchAngleDeg = Cse.Launch;
		Shot.BackspinRpm = Cse.Spin;
		const FBallTrajectory Flight = GolfBallFlight::Simulate(Shot);
		const FGroundRollResult R =
			GolfBallFlight::SimulateGroundRoll(Flight, EGolfLie::Fairway, GolfBallFlight::SurfaceRollFor(EGolfLie::Fairway));
		TestTrue(TEXT("total >= carry"), R.TotalDistanceM >= Flight.CarryM - 0.01);
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
