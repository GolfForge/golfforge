// Automation tests for the pure GolfsimRound:: state machine + hole-schedule parser (GOL-116).
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.Round; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Round/RoundState.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	GolfsimRound::FHoleSpec MakePar4(int32 Ref)
	{
		GolfsimRound::FHoleSpec H;
		H.Ref = Ref;
		H.Par = 4;
		H.Handicap = Ref;
		H.Name = FString::Printf(TEXT("Test %d"), Ref);
		// XY irrelevant for state-machine tests; subsystem snaps Z separately.
		return H;
	}

	TArray<GolfsimRound::FHoleSpec> MakePar72Schedule()
	{
		TArray<GolfsimRound::FHoleSpec> S;
		for (int32 i = 1; i <= 18; ++i) { S.Add(MakePar4(i)); }
		return S;
	}
}

// Drive 18 holes through OnHoleHoled with 4 shot outcomes per hole. Round ends at +0 / 72 strokes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundHappyPathTest, "Golfsim.Round.HappyPath18Holes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundHappyPathTest::RunTest(const FString&)
{
	using namespace GolfsimRound;

	FRoundState S;
	const FRoundStep Start = StartRound(S, TEXT("test-course"), EGolfDifficulty::Normal, MakePar72Schedule());
	TestTrue(TEXT("RoundStart published on first step"), Start.bRoundStart);
	TestTrue(TEXT("HoleStart published with first step"), Start.bHoleStart);
	TestEqual(TEXT("HoleStart -> hole 1"), Start.HoleRefForHoleStart, 1);
	TestTrue(TEXT("Round is active"), S.bActive);

	for (int32 i = 0; i < 18; ++i)
	{
		// 4 shots to par each hole.
		for (int32 k = 0; k < 4; ++k)
		{
			const FRoundStep ShotStep = OnShotOutcome(S);
			TestFalse(TEXT("Par shot does not fire HoleComplete"), ShotStep.bHoleComplete);
		}
		TestEqual(TEXT("StrokesThisHole = 4 after par"), S.StrokesThisHole, 4);
		const FRoundStep Hole = OnHoleHoled(S);
		TestTrue(TEXT("HoleComplete fires"), Hole.bHoleComplete);
		TestEqual(TEXT("Strokes = 4"), Hole.StrokesForHoleComplete, 4);
		TestEqual(TEXT("ScoreVsPar = 0 (par)"), Hole.ScoreVsParForHoleComplete, 0);

		if (i < 17)
		{
			TestTrue(TEXT("HoleStart fires on next hole"), Hole.bHoleStart);
			TestEqual(TEXT("Next hole ref +1"), Hole.HoleRefForHoleStart, i + 2);
			TestFalse(TEXT("No RoundComplete mid-round"), Hole.bRoundComplete);
		}
		else
		{
			TestFalse(TEXT("No HoleStart after final hole"), Hole.bHoleStart);
			TestTrue(TEXT("RoundComplete fires after hole 18"), Hole.bRoundComplete);
			TestEqual(TEXT("Total strokes = 72"), Hole.TotalStrokesForRoundComplete, 72);
			TestEqual(TEXT("Vs par = 0"), Hole.TotalScoreVsParForRoundComplete, 0);
			TestEqual(TEXT("PerHoleStrokes length = 18"), Hole.PerHoleStrokesForRoundComplete.Num(), 18);
			TestFalse(TEXT("Round no longer active"), S.bActive);
		}
	}
	return true;
}

// Shot outcomes increment StrokesThisHole, no events fire.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundShotIncrementsTest, "Golfsim.Round.ShotIncrementsStrokeCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundShotIncrementsTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	FRoundState S;
	StartRound(S, TEXT("test"), EGolfDifficulty::Easy, MakePar72Schedule());
	const FRoundStep S1 = OnShotOutcome(S);
	const FRoundStep S2 = OnShotOutcome(S);
	const FRoundStep S3 = OnShotOutcome(S);
	TestEqual(TEXT("StrokesThisHole == 3"), S.StrokesThisHole, 3);
	TestFalse(TEXT("Step 1 no events"),  S1.bHoleComplete || S1.bHoleStart || S1.bRoundComplete);
	TestFalse(TEXT("Step 2 no events"),  S2.bHoleComplete || S2.bHoleStart || S2.bRoundComplete);
	TestFalse(TEXT("Step 3 no events"),  S3.bHoleComplete || S3.bHoleStart || S3.bRoundComplete);
	return true;
}

// Par+5 cap trips an advance even without OnHoleHoled.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundMaxStrokesTest, "Golfsim.Round.MaxStrokesAdvancesHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundMaxStrokesTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	TestEqual(TEXT("MaxStrokes(par 4) = 9"), MaxStrokesForPar(4), 9);
	TestEqual(TEXT("MaxStrokes(par 3) = 8"), MaxStrokesForPar(3), 8);
	TestEqual(TEXT("MaxStrokes(par 5) = 10"), MaxStrokesForPar(5), 10);

	FRoundState S;
	StartRound(S, TEXT("test"), EGolfDifficulty::Easy, MakePar72Schedule());

	// First 8 shots on a par 4 -> no advance (cap is 9).
	for (int32 i = 0; i < 8; ++i)
	{
		const FRoundStep Step = OnShotOutcome(S);
		TestFalse(TEXT("No advance below cap"), Step.bHoleComplete);
	}
	TestEqual(TEXT("StrokesThisHole == 8 (one short of cap)"), S.StrokesThisHole, 8);

	// 9th shot trips the cap -> HoleComplete + HoleStart for hole 2.
	const FRoundStep Cap = OnShotOutcome(S);
	TestTrue(TEXT("Cap triggers HoleComplete"), Cap.bHoleComplete);
	TestEqual(TEXT("Strokes recorded = 9"), Cap.StrokesForHoleComplete, 9);
	TestEqual(TEXT("Score vs par = +5"), Cap.ScoreVsParForHoleComplete, 5);
	TestTrue(TEXT("Next HoleStart fires"), Cap.bHoleStart);
	TestEqual(TEXT("HoleIndex advanced"), S.HoleIndex, 1);
	TestEqual(TEXT("StrokesThisHole reset to 0"), S.StrokesThisHole, 0);
	return true;
}

// ParseHoleScheduleJson reads features regardless of input order; output is sorted by ref.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundScheduleSortsByRefTest, "Golfsim.Round.HoleScheduleSortsByRef",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundScheduleSortsByRefTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const FString Json = TEXT(R"({
		"type": "FeatureCollection",
		"features": [
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "ref": "3", "par": "3", "handicap": "18", "name": "T 3" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.45, 40.74], [-73.451, 40.741]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "ref": "1", "par": "4", "handicap": "8",  "name": "T 1" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.455, 40.743], [-73.453, 40.745]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "ref": "2", "par": "4", "handicap": "16", "name": "T 2" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.450, 40.744], [-73.448, 40.746]] }
			}
		]
	})");

	TArray<FHoleSpec> Out;
	FString Err;
	const bool bOK = ParseHoleScheduleJson(Json,
		/*MinLon=*/-73.4555, /*MinLat=*/40.7423, /*MaxLon=*/-73.4345, /*MaxLat=*/40.7571,
		/*TrackName=*/FString(),
		Out, Err);
	TestTrue(FString::Printf(TEXT("Parse ok: %s"), *Err), bOK);
	TestEqual(TEXT("3 holes"), Out.Num(), 3);
	TestEqual(TEXT("Refs sorted ascending [0]"), Out[0].Ref, 1);
	TestEqual(TEXT("Refs sorted ascending [1]"), Out[1].Ref, 2);
	TestEqual(TEXT("Refs sorted ascending [2]"), Out[2].Ref, 3);
	TestEqual(TEXT("Hole 1 par"), Out[0].Par, 4);
	TestEqual(TEXT("Hole 3 par"), Out[2].Par, 3);
	TestTrue(TEXT("Hole 1 name"), Out[0].Name == TEXT("T 1"));

	// Tee != Green (LineString endpoint convention).
	TestFalse(TEXT("Tee XY differs from Green XY for hole 1"),
		Out[0].TeeWorldLoc.Equals(Out[0].GreenWorldLoc, 1e-3));
	// Pin == Green per the v1 convention.
	TestTrue(TEXT("Pin XY == Green XY for hole 1 (v1)"),
		Out[0].PinWorldLoc.Equals(Out[0].GreenWorldLoc, 1e-9));
	return true;
}

// GOL-119: gimme-radius distance check (XY-only, Z ignored).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundGimmeRadiusTest, "Golfsim.Round.GimmeRadiusDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundGimmeRadiusTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	constexpr double CmPerFt = 30.48;
	const FVector Pin(1000.0, 2000.0, 50.0);

	// Ball directly on the pin XY -- within any positive radius.
	TestTrue(TEXT("Pin == ball -> in for 8ft"),  IsWithinGimme(Pin, Pin, 8.0));
	TestTrue(TEXT("Pin == ball -> in for 3ft"),  IsWithinGimme(Pin, Pin, 3.0));
	TestFalse(TEXT("Zero radius rejects even exact match"), IsWithinGimme(Pin, Pin, 0.0));

	// 5 ft offset: in for 6ft (Normal), out for 3ft (Pro), in for 8ft (Easy).
	const FVector Ball5ft = Pin + FVector(5.0 * CmPerFt, 0.0, 0.0);
	TestTrue(TEXT("5ft offset -> in for 8ft (Easy)"),    IsWithinGimme(Ball5ft, Pin, 8.0));
	TestTrue(TEXT("5ft offset -> in for 6ft (Normal)"),  IsWithinGimme(Ball5ft, Pin, 6.0));
	TestFalse(TEXT("5ft offset -> out for 3ft (Pro)"),   IsWithinGimme(Ball5ft, Pin, 3.0));

	// Z difference doesn't affect the XY check.
	const FVector BallHigh = Pin + FVector(2.0 * CmPerFt, 0.0, 100000.0);
	TestTrue(TEXT("Z difference ignored; XY 2ft -> in for 3ft"), IsWithinGimme(BallHigh, Pin, 3.0));

	return true;
}

// TrackName filter selects features by osm_tags["golf:course:name"]; fallback when filter empties.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundTrackFilterTest, "Golfsim.Round.TrackNameFiltersFeatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundTrackFilterTest::RunTest(const FString&)
{
	using namespace GolfsimRound;

	// DeriveTrackName: golfforge-demo-black -> "Black"; "augusta" (no hyphen) -> empty.
	TestTrue(TEXT("Derive Black"), DeriveTrackName(TEXT("golfforge-demo-black")) == TEXT("Black"));
	TestTrue(TEXT("Derive Red"),   DeriveTrackName(TEXT("golfforge-demo-red"))   == TEXT("Red"));
	TestTrue(TEXT("Derive empty for no-hyphen"), DeriveTrackName(TEXT("augusta")).IsEmpty());

	// 2 Black + 2 Red + 1 untagged. Same ref 1+2 across tracks (this is the real demo course shape).
	const FString Json = TEXT(R"({
		"type": "FeatureCollection",
		"features": [
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "golf:course:name": "Black", "ref": "1", "par": "4", "name": "Black 1" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.455, 40.743], [-73.451, 40.745]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "golf:course:name": "Black", "ref": "2", "par": "4", "name": "Black 2" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.450, 40.744], [-73.448, 40.746]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "golf:course:name": "Red", "ref": "1", "par": "3", "name": "Red 1" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.456, 40.742], [-73.454, 40.744]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "golf:course:name": "Red", "ref": "2", "par": "4", "name": "Red 2" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.454, 40.748], [-73.450, 40.749]] }
			},
			{
				"type": "Feature",
				"properties": { "osm_tags": { "golf": "hole", "ref": "1", "par": "5", "name": "Untagged 1" } },
				"geometry": { "type": "LineString", "coordinates": [[-73.453, 40.747], [-73.450, 40.748]] }
			}
		]
	})");

	const double MinLon = -73.4555, MinLat = 40.7423, MaxLon = -73.4345, MaxLat = 40.7571;

	// TrackName="Black" -> only the 2 Black holes.
	{
		TArray<FHoleSpec> Out; FString Err;
		const bool bOK = ParseHoleScheduleJson(Json, MinLon, MinLat, MaxLon, MaxLat, TEXT("Black"), Out, Err);
		TestTrue(TEXT("Black filter ok"), bOK);
		TestEqual(TEXT("Black filter yields 2"), Out.Num(), 2);
		TestTrue(TEXT("Black 1"), Out[0].Name == TEXT("Black 1"));
		TestTrue(TEXT("Black 2"), Out[1].Name == TEXT("Black 2"));
	}

	// TrackName="Red" -> only the 2 Red holes.
	{
		TArray<FHoleSpec> Out; FString Err;
		const bool bOK = ParseHoleScheduleJson(Json, MinLon, MinLat, MaxLon, MaxLat, TEXT("Red"), Out, Err);
		TestTrue(TEXT("Red filter ok"), bOK);
		TestEqual(TEXT("Red filter yields 2"), Out.Num(), 2);
		TestTrue(TEXT("Red 1 sorted first"), Out[0].Name == TEXT("Red 1"));
	}

	// Empty filter -> all 5 features (no filter applied).
	{
		TArray<FHoleSpec> Out; FString Err;
		const bool bOK = ParseHoleScheduleJson(Json, MinLon, MinLat, MaxLon, MaxLat, FString(), Out, Err);
		TestTrue(TEXT("Empty filter ok"), bOK);
		TestEqual(TEXT("Empty filter yields all 5"), Out.Num(), 5);
	}

	// Unmatched filter -> fall back to all features (Display log; no expected-error declaration needed).
	{
		TArray<FHoleSpec> Out; FString Err;
		const bool bOK = ParseHoleScheduleJson(Json, MinLon, MinLat, MaxLon, MaxLat, TEXT("Yellow"), Out, Err);
		TestTrue(TEXT("Unmatched filter still ok (fallback)"), bOK);
		TestEqual(TEXT("Fallback yields all 5"), Out.Num(), 5);
	}
	return true;
}

// Abandon clears state; subsequent shot outcomes are ignored (no events).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundAbandonTest, "Golfsim.Round.AbandonStopsAdvancing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundAbandonTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	FRoundState S;
	StartRound(S, TEXT("test"), EGolfDifficulty::Normal, MakePar72Schedule());
	OnShotOutcome(S);
	OnShotOutcome(S);
	TestTrue(TEXT("Active before abandon"), S.bActive);

	AbandonRound(S);
	TestFalse(TEXT("Not active after abandon"), S.bActive);
	TestEqual(TEXT("Schedule cleared"), S.Schedule.Num(), 0);

	const FRoundStep ShotStep = OnShotOutcome(S);
	TestFalse(TEXT("Post-abandon shot fires no events (1)"), ShotStep.bHoleComplete);
	TestFalse(TEXT("Post-abandon shot fires no events (2)"), ShotStep.bRoundComplete);

	const FRoundStep HoleStep = OnHoleHoled(S);
	TestFalse(TEXT("Post-abandon HoleHoled fires no events (1)"), HoleStep.bHoleComplete);
	TestFalse(TEXT("Post-abandon HoleHoled fires no events (2)"), HoleStep.bRoundComplete);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
