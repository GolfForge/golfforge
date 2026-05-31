// Automation tests for the ball-flight aerodynamics solver.
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.BallFlight; Quit" -unattended -nullrhi
// Pure-math tests: no world, no RHI needed.

#include "Misc/AutomationTest.h"
#include "Physics/BallFlightSolver.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// Records a labelled failure (with the numbers) so the GREEN tuning phase can see how far off
	// each metric is. Returns whether the check passed.
	bool CheckClose(FAutomationTestBase& T, const FString& Label, double Actual, double Expected, double Tol)
	{
		const bool bOk = FMath::Abs(Actual - Expected) <= Tol;
		if (!bOk)
		{
			T.AddError(FString::Printf(TEXT("%s: got %.3f, expected %.3f (tol +/-%.3f)"),
				*Label, Actual, Expected, Tol));
		}
		return bOk;
	}
}

// --- Sanity: vacuum ballistics match the closed-form projectile solution -----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightVacuumTest, "Golfsim.BallFlight.VacuumParabola",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightVacuumTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Shot;
	Shot.BallSpeedMps = 70.0;
	Shot.LaunchAngleDeg = 20.0;

	const FBallTrajectory Traj = GolfBallFlight::Simulate(Shot, FAeroCoefficients(), /*bDisableAero=*/true);

	TestTrue(TEXT("trajectory valid"), Traj.bValid);
	TestTrue(TEXT("has samples"), Traj.Samples.Num() >= 2);

	const double g = 9.81;
	const double V = 70.0;
	const double Th = FMath::DegreesToRadians(20.0);
	const double ExpRange = V * V * FMath::Sin(2.0 * Th) / g;          // 321.07 m
	const double ExpApex = V * V * FMath::Square(FMath::Sin(Th)) / (2.0 * g); // 29.22 m
	const double ExpTime = 2.0 * V * FMath::Sin(Th) / g;              // 4.881 s

	CheckClose(*this, TEXT("vacuum carry"), Traj.CarryM, ExpRange, 1.0);
	CheckClose(*this, TEXT("vacuum apex"), Traj.ApexM, ExpApex, 0.5);
	CheckClose(*this, TEXT("vacuum flight time"), Traj.FlightTimeS, ExpTime, 0.05);
	CheckClose(*this, TEXT("vacuum descent angle"), Traj.DescentAngleDeg, 20.0, 0.5);
	return true;
}

// --- A shot with backspin only (no sidespin, no azimuth) flies straight ------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightStraightTest, "Golfsim.BallFlight.ZeroSidespinFliesStraight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightStraightTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Shot;
	Shot.BallSpeedMps = 60.0;
	Shot.LaunchAngleDeg = 14.0;
	Shot.BackspinRpm = 6000.0;     // pure backspin

	const FBallTrajectory Traj = GolfBallFlight::Simulate(Shot);

	TestTrue(TEXT("trajectory valid"), Traj.bValid);
	CheckClose(*this, TEXT("lateral offset"), Traj.LateralOffsetM, 0.0, 1.0);
	return true;
}

// --- Sidespin sign: + curves right, - curves left ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightSidespinTest, "Golfsim.BallFlight.SidespinSignCurvesRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightSidespinTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Base;
	Base.BallSpeedMps = 65.0;
	Base.LaunchAngleDeg = 13.0;
	Base.BackspinRpm = 3000.0;

	FShotInput Right = Base; Right.SidespinRpm = 1000.0;
	FShotInput Left = Base;  Left.SidespinRpm = -1000.0;

	const FBallTrajectory TR = GolfBallFlight::Simulate(Right);
	const FBallTrajectory TL = GolfBallFlight::Simulate(Left);

	TestTrue(TEXT("+sidespin lands right of center"), TR.LateralOffsetM > 1.0);
	TestTrue(TEXT("-sidespin lands left of center"), TL.LateralOffsetM < -1.0);
	return true;
}

// --- Lift: realistic backspin carries farther than near-zero spin ------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightBackspinCarryTest, "Golfsim.BallFlight.BackspinExtendsCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightBackspinCarryTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Low;  Low.BallSpeedMps = 74.0;  Low.LaunchAngleDeg = 11.0;  Low.BackspinRpm = 300.0;
	FShotInput High; High.BallSpeedMps = 74.0; High.LaunchAngleDeg = 11.0; High.BackspinRpm = 2600.0;

	const FBallTrajectory TLow = GolfBallFlight::Simulate(Low);
	const FBallTrajectory THigh = GolfBallFlight::Simulate(High);

	TestTrue(TEXT("both valid"), TLow.bValid && THigh.bValid);
	TestTrue(TEXT("backspin lift extends carry"), THigh.CarryM > TLow.CarryM);
	TestTrue(TEXT("backspin raises apex"), THigh.ApexM > TLow.ApexM);
	return true;
}

// --- Golden regression vs Trackman PGA-Tour averages -------------------------------------------
// Source: Trackman PGA Tour averages. Tolerances reflect what a real-time, 2-parameter spin-dependent
// Cd/Cl model achieves across the whole bag (driver -> wedge) with a single coefficient set:
// carry ~6%, apex ~8%, descent ~5 deg. Hitting tighter than this across every club simultaneously
// requires a per-ball CFD / spin-axis lookup model (studio-grade ballistics), out of scope for the
// in-game solver. The per-club got-vs-expected numbers are logged below so the real accuracy is
// always visible.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightTrackmanTest, "Golfsim.BallFlight.TrackmanReferenceTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightTrackmanTest::RunTest(const FString& /*Parameters*/)
{
	struct FRefShot
	{
		const TCHAR* Club;
		double BallMps; double LaunchDeg; double BackspinRpm;
		double CarryM; double ApexM; double DescentDeg;
	};

	static const FRefShot Ref[] = {
		{ TEXT("Driver"), 74.6, 10.9, 2686.0, 251.0, 31.1, 38.0 },
		{ TEXT("3-wood"), 70.6,  9.2, 3655.0, 222.0, 29.0, 43.0 },
		{ TEXT("5-iron"), 60.4, 11.9, 5361.0, 177.0, 29.3, 45.0 },
		{ TEXT("7-iron"), 53.6, 16.1, 7097.0, 157.0, 30.2, 48.0 },
		{ TEXT("9-iron"), 46.9, 20.4, 8647.0, 130.0, 29.6, 50.0 },
		{ TEXT("PW"),     43.4, 24.2, 9304.0, 113.0, 27.4, 52.0 },
	};

	for (const FRefShot& R : Ref)
	{
		FShotInput Shot;
		Shot.BallSpeedMps = R.BallMps;
		Shot.LaunchAngleDeg = R.LaunchDeg;
		Shot.BackspinRpm = R.BackspinRpm;

		const FBallTrajectory T = GolfBallFlight::Simulate(Shot);
		if (!T.bValid)
		{
			AddError(FString::Printf(TEXT("%s: solver returned invalid trajectory"), R.Club));
			continue;
		}

		// Always log got-vs-expected so the achieved accuracy is visible even when the test passes.
		AddInfo(FString::Printf(TEXT("%-7s carry %.1f/%.1f m  apex %.1f/%.1f m  descent %.1f/%.1f deg"),
			R.Club, T.CarryM, R.CarryM, T.ApexM, R.ApexM, T.DescentAngleDeg, R.DescentDeg));

		const double CarryTol = FMath::Max(0.06 * R.CarryM, 7.0);
		const double ApexTol = FMath::Max(0.08 * R.ApexM, 2.5);
		CheckClose(*this, FString::Printf(TEXT("%s carry"), R.Club), T.CarryM, R.CarryM, CarryTol);
		CheckClose(*this, FString::Printf(TEXT("%s apex"), R.Club), T.ApexM, R.ApexM, ApexTol);
		CheckClose(*this, FString::Printf(TEXT("%s descent"), R.Club), T.DescentAngleDeg, R.DescentDeg, 5.0);
	}
	return true;
}

// --- Trace mode reproduces the launch monitor's reported summary -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightTraceTest, "Golfsim.BallFlight.TraceMatchesResolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightTraceTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Shot;
	Shot.BallSpeedMps = 53.6;
	Shot.LaunchAngleDeg = 16.1;
	Shot.BackspinRpm = 7097.0;

	FResolvedFlight Resolved;
	Resolved.CarryM = 160.0;
	Resolved.ApexM = 32.0;
	Resolved.DescentAngleDeg = 50.0;

	const FBallTrajectory T = GolfBallFlight::TraceFromResolved(Shot, Resolved);

	TestTrue(TEXT("trace valid"), T.bValid);
	TestTrue(TEXT("source tagged as traced"), T.Source == EBallTrajectorySource::TracedFromSummary);
	TestTrue(TEXT("has samples"), T.Samples.Num() >= 2);
	CheckClose(*this, TEXT("trace carry"), T.CarryM, Resolved.CarryM, 1.0);
	CheckClose(*this, TEXT("trace apex"), T.ApexM, Resolved.ApexM, 1.0);
	CheckClose(*this, TEXT("trace descent"), T.DescentAngleDeg, Resolved.DescentAngleDeg, 2.0);
	return true;
}

// --- Degenerate input is flagged, not crashed --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallFlightInvalidTest, "Golfsim.BallFlight.InvalidInputIsFlagged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallFlightInvalidTest::RunTest(const FString& /*Parameters*/)
{
	FShotInput Shot;            // zero ball speed
	Shot.LaunchAngleDeg = 12.0;

	const FBallTrajectory T = GolfBallFlight::Simulate(Shot);
	TestFalse(TEXT("zero-speed shot is invalid"), T.bValid);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
