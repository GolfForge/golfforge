// Automation tests for the pure GolfsimPractice:: CTP core (GOL-73). No world / no RHI.
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.Practice; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Practice/PracticeMode.h"

#if WITH_AUTOMATION_TESTS

using namespace GolfsimPractice;

// NextPin: a seeded stream is deterministic and every draw lands inside the configured range.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticeNextPinBoundsTest, "Golfsim.Practice.NextPinBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticeNextPinBoundsTest::RunTest(const FString&)
{
	FCtpConfig Cfg;
	Cfg.MinM = 50.0 * MetersPerYard;
	Cfg.MaxM = 250.0 * MetersPerYard;
	Cfg.bSideOffset = false;

	FRandomStream Stream(12345);
	for (int32 i = 0; i < 500; ++i)
	{
		const FCtpPin Pin = NextPin(Cfg, Stream);
		TestTrue(TEXT("distance >= Min"), Pin.DistanceM >= Cfg.MinM - KINDA_SMALL_NUMBER);
		TestTrue(TEXT("distance <= Max"), Pin.DistanceM <= Cfg.MaxM + KINDA_SMALL_NUMBER);
		TestEqual(TEXT("no side offset when disabled"), Pin.SideOffsetM, 0.0);
	}

	// Same seed -> identical first draw (determinism the tests rely on).
	FRandomStream A(999), B(999);
	TestEqual(TEXT("seeded streams agree"), NextPin(Cfg, A).DistanceM, NextPin(Cfg, B).DistanceM);
	return true;
}

// Side offset stays within +/-MaxSide and gets clamped to the lane wall when over-configured.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticeSideOffsetTest, "Golfsim.Practice.SideOffsetClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticeSideOffsetTest::RunTest(const FString&)
{
	FCtpConfig Cfg;
	Cfg.bSideOffset = true;
	Cfg.MaxSideM = 1000.0;   // absurd -> must clamp to LaneHalfWidthM

	FRandomStream Stream(7);
	for (int32 i = 0; i < 500; ++i)
	{
		const FCtpPin Pin = NextPin(Cfg, Stream);
		TestTrue(TEXT("|side| within lane"), FMath::Abs(Pin.SideOffsetM) <= LaneHalfWidthM + KINDA_SMALL_NUMBER);
	}
	return true;
}

// Min/Max reversed still produces in-range draws (order-normalized).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticeReversedRangeTest, "Golfsim.Practice.ReversedRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticeReversedRangeTest::RunTest(const FString&)
{
	FCtpConfig Cfg;
	Cfg.MinM = 200.0 * MetersPerYard;   // deliberately > Max
	Cfg.MaxM = 100.0 * MetersPerYard;

	FRandomStream Stream(3);
	for (int32 i = 0; i < 200; ++i)
	{
		const FCtpPin Pin = NextPin(Cfg, Stream);
		TestTrue(TEXT("distance >= 100yd"), Pin.DistanceM >= 100.0 * MetersPerYard - KINDA_SMALL_NUMBER);
		TestTrue(TEXT("distance <= 200yd"), Pin.DistanceM <= 200.0 * MetersPerYard + KINDA_SMALL_NUMBER);
	}
	return true;
}

// ScoreDistanceM: XY-only planar distance in meters, Z ignored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticeScoreTest, "Golfsim.Practice.ScoreDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticeScoreTest::RunTest(const FString&)
{
	// 3-4-5 triangle in cm: dx=300cm, dy=400cm -> 500cm -> 5 m. Z difference must not matter.
	const FVector Ball(300.0, 400.0, 9999.0);
	const FVector Pin(0.0, 0.0, -1234.0);
	TestEqual(TEXT("5 m planar"), ScoreDistanceM(Ball, Pin), 5.0);

	const double Yards1 = 91.44;   // 1 yd in cm
	TestEqual(TEXT("1 yd -> 0.9144 m"), ScoreDistanceM(FVector(Yards1, 0, 0), FVector::ZeroVector), MetersPerYard);
	return true;
}

// RecordAttempt + carry-only stats (best = closest, avg, last). Empty session is safe.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticeCarryStatsTest, "Golfsim.Practice.CarryStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticeCarryStatsTest::RunTest(const FString&)
{
	FCtpSession S;
	S.Mode = EPracticeMode::ClosestToPin;

	// Empty-session safety (the "end session with zero shots" pitfall).
	TestEqual(TEXT("empty count"), AttemptCount(S), 0);
	TestEqual(TEXT("empty best"), BestDistanceM(S), 0.0);
	TestEqual(TEXT("empty avg"), AvgDistanceM(S), 0.0);
	TestEqual(TEXT("empty last"), LastDistanceM(S), 0.0);

	for (double D : { 10.0, 4.0, 7.0 })
	{
		FCtpAttempt A; A.DistanceM = D; A.Strokes = 1; A.bPuttedOut = false;
		RecordAttempt(S, A);
	}
	TestEqual(TEXT("count 3"), AttemptCount(S), 3);
	TestEqual(TEXT("best = closest (4)"), BestDistanceM(S), 4.0);
	TestEqual(TEXT("avg = 7"), AvgDistanceM(S), 7.0);
	TestEqual(TEXT("last = 7"), LastDistanceM(S), 7.0);
	return true;
}

// Putt-out stroke stats (best = fewest, avg). Empty session is safe.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticePuttOutStatsTest, "Golfsim.Practice.PuttOutStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticePuttOutStatsTest::RunTest(const FString&)
{
	FCtpSession S;
	S.Mode = EPracticeMode::ClosestToPin;

	TestEqual(TEXT("empty best strokes"), BestStrokes(S), 0);
	TestEqual(TEXT("empty avg strokes"), AvgStrokes(S), 0.0);

	for (int32 N : { 3, 2, 4 })
	{
		FCtpAttempt A; A.DistanceM = 0.2; A.Strokes = N; A.bPuttedOut = true;
		RecordAttempt(S, A);
	}
	TestEqual(TEXT("best = fewest (2)"), BestStrokes(S), 2);
	TestEqual(TEXT("avg = 3"), AvgStrokes(S), 3.0);
	TestEqual(TEXT("last = 4"), LastStrokes(S), 4);
	return true;
}

// GOL-75 putting defaults: a short 5-30 ft range, hole-out scoring, always played out, and every
// pin draw lands inside that range (well within the lane, so no clamping interferes).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticePuttingDefaultsTest, "Golfsim.Practice.PuttingDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticePuttingDefaultsTest::RunTest(const FString&)
{
	const FCtpConfig Cfg = MakePuttingDefaults();
	TestEqual(TEXT("min = 5 ft"), Cfg.MinM, 5.0 * MetersPerFoot);
	TestEqual(TEXT("max = 30 ft"), Cfg.MaxM, 30.0 * MetersPerFoot);
	TestTrue(TEXT("scores by hole-out"), Cfg.Score == EScoreMode::HoleOut);
	TestTrue(TEXT("always putt out"), Cfg.bPuttOut);
	TestFalse(TEXT("no side offset"), Cfg.bSideOffset);

	FRandomStream Stream(2024);
	for (int32 i = 0; i < 500; ++i)
	{
		const FCtpPin Pin = NextPin(Cfg, Stream);
		TestTrue(TEXT("pin >= 5 ft"),  Pin.DistanceM >= 5.0  * MetersPerFoot - KINDA_SMALL_NUMBER);
		TestTrue(TEXT("pin <= 30 ft"), Pin.DistanceM <= 30.0 * MetersPerFoot + KINDA_SMALL_NUMBER);
		TestEqual(TEXT("dead ahead"), Pin.SideOffsetM, 0.0);
	}
	return true;
}

// GOL-75 putting scoring: hole-out records putts-to-hole (best = fewest, avg), while a distance-to-pin
// attempt records a single stroke + the rest distance. Mirrors how the HUD feeds the subsystem.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPracticePuttingScoringTest, "Golfsim.Practice.PuttingScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPracticePuttingScoringTest::RunTest(const FString&)
{
	FCtpSession S;
	S.Mode = EPracticeMode::Putting;

	// Hole-out: three holed putts of 2, 3, 1 strokes (DistanceM ~0 at the cup, bPuttedOut = true).
	for (int32 Putts : { 2, 3, 1 })
	{
		FCtpAttempt A; A.DistanceM = 0.05; A.Strokes = Putts; A.bPuttedOut = true;
		RecordAttempt(S, A);
	}
	TestEqual(TEXT("count 3"), AttemptCount(S), 3);
	TestEqual(TEXT("best = fewest (1)"), BestStrokes(S), 1);
	TestEqual(TEXT("avg = 2 putts"), AvgStrokes(S), 2.0);
	TestEqual(TEXT("last = 1 putt"), LastStrokes(S), 1);

	// Distance-to-pin: a single putt, scored by rest distance (one stroke, not played out).
	FCtpSession D;
	D.Mode = EPracticeMode::Putting;
	FCtpAttempt A; A.DistanceM = 0.6; A.Strokes = 1; A.bPuttedOut = false;
	RecordAttempt(D, A);
	TestEqual(TEXT("one stroke"), LastStrokes(D), 1);
	TestEqual(TEXT("rest distance 0.6 m"), LastDistanceM(D), 0.6);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
