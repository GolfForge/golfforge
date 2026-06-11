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

// GOL-142: SelectHoles filters a full schedule to the round's chosen subset (in Ref order).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundSelectHolesTest, "Golfsim.Round.SelectHoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundSelectHolesTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const TArray<FHoleSpec> Full = MakePar72Schedule();   // Refs 1..18

	auto Refs = [](const TArray<FHoleSpec>& S)
	{
		TArray<int32> R; for (const FHoleSpec& H : S) { R.Add(H.Ref); } return R;
	};

	// Full 18 -> all, unchanged order.
	{
		FRoundConfig C; C.HolesMode = ERoundHolesMode::Full18;
		const TArray<FHoleSpec> Out = SelectHoles(Full, C);
		TestEqual(TEXT("Full18 -> 18 holes"), Out.Num(), 18);
		TestEqual(TEXT("Full18 first ref"), Out[0].Ref, 1);
		TestEqual(TEXT("Full18 last ref"), Out[17].Ref, 18);
	}

	// Front 9 -> Refs 1..9.
	{
		FRoundConfig C; C.HolesMode = ERoundHolesMode::Front9;
		const TArray<FHoleSpec> Out = SelectHoles(Full, C);
		TestEqual(TEXT("Front9 -> 9 holes"), Out.Num(), 9);
		TestEqual(TEXT("Front9 first ref"), Out[0].Ref, 1);
		TestEqual(TEXT("Front9 last ref"), Out[8].Ref, 9);
	}

	// Back 9 -> Refs 10..18.
	{
		FRoundConfig C; C.HolesMode = ERoundHolesMode::Back9;
		const TArray<FHoleSpec> Out = SelectHoles(Full, C);
		TestEqual(TEXT("Back9 -> 9 holes"), Out.Num(), 9);
		TestEqual(TEXT("Back9 first ref"), Out[0].Ref, 10);
		TestEqual(TEXT("Back9 last ref"), Out[8].Ref, 18);
	}

	// Custom {3,7,12} -> those three in Ref order (the set order doesn't matter).
	{
		FRoundConfig C; C.HolesMode = ERoundHolesMode::Custom; C.CustomHoles = { 12, 3, 7 };
		const TArray<FHoleSpec> Out = SelectHoles(Full, C);
		TestEqual(TEXT("Custom -> 3 holes"), Out.Num(), 3);
		TestTrue(TEXT("Custom preserves Ref order"), Refs(Out) == (TArray<int32>{ 3, 7, 12 }));
	}

	// Custom with a Ref absent from the course -> intersection only (no phantom holes).
	{
		FRoundConfig C; C.HolesMode = ERoundHolesMode::Custom; C.CustomHoles = { 5, 99 };
		const TArray<FHoleSpec> Out = SelectHoles(Full, C);
		TestEqual(TEXT("Custom intersects available refs"), Out.Num(), 1);
		TestEqual(TEXT("Custom kept the valid ref"), Out[0].Ref, 5);
	}

	return true;
}

// GOL-142: the hole-out radius honors the gimme rule, and a concession only ever loosens it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundGimmeRuleTest, "Golfsim.Round.GimmeRuleRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimRoundGimmeRuleTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const double DiffFt = 6.0;   // e.g. Normal's natural tolerance

	// Everyone holes out -> difficulty tolerance, GimmeFeet ignored.
	{
		FRoundConfig C; C.HoleOutRule = EHoleOutRule::HoleOut; C.GimmeFeet = 8;
		TestEqual(TEXT("HoleOut keeps difficulty radius"), EffectiveGimmeRadiusFt(C, DiffFt), 6.0);
	}
	// Gimme looser than tolerance -> use the gimme.
	{
		FRoundConfig C; C.HoleOutRule = EHoleOutRule::Gimme; C.GimmeFeet = 8;
		TestEqual(TEXT("Gimme 8ft loosens to 8"), EffectiveGimmeRadiusFt(C, DiffFt), 8.0);
	}
	// Gimme tighter than tolerance -> clamp up to tolerance (concession can't make holing harder).
	{
		FRoundConfig C; C.HoleOutRule = EHoleOutRule::Gimme; C.GimmeFeet = 3;
		TestEqual(TEXT("Gimme 3ft can't tighten below 6"), EffectiveGimmeRadiusFt(C, DiffFt), 6.0);
	}
	return true;
}

// --- GOL-191/192 pin-position system ----------------------------------------------------------

namespace
{
	// A square green centered at (Cx,Cy) with half-extent H (cm). Open ring (4 verts).
	GolfsimRound::FGreenPolygon SquareGreen(double Cx, double Cy, double H)
	{
		GolfsimRound::FGreenPolygon G;
		G.VertsCm = { FVector2D(Cx - H, Cy - H), FVector2D(Cx + H, Cy - H),
		              FVector2D(Cx + H, Cy + H), FVector2D(Cx - H, Cy + H) };
		G.CentroidCm = FVector2D(Cx, Cy);
		return G;
	}
}

// Parse a green.geojson polygon: drops the closing duplicate vertex, computes centroid, reads way id.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenParseTest, "Golfsim.Round.GreenPolygonParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimGreenParseTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	// bbox 0,0..1,1: LonLatToWorldCm maps lon/lat 0.5/0.5 -> (0,0). Square green lon/lat [0.4,0.6].
	const FString Json = TEXT(
		"{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
		"\"properties\":{\"osm_way_id\":42,\"osm_tags\":{\"golf\":\"green\"}},"
		"\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[[0.4,0.4],[0.6,0.4],[0.6,0.6],[0.4,0.6],[0.4,0.4]]]}}]}");
	TArray<FGreenPolygon> Greens; FString Err;
	TestTrue(TEXT("parse ok"), ParseGreenPolygonsJson(Json, 0, 0, 1, 1, Greens, Err));
	TestEqual(TEXT("one green"), Greens.Num(), 1);
	TestEqual(TEXT("4 verts (closing dup dropped)"), Greens[0].VertsCm.Num(), 4);
	TestEqual(TEXT("osm way id"), Greens[0].OsmWayId, (int64)42);
	TestTrue(TEXT("centroid ~origin"), Greens[0].CentroidCm.Size() < 1.0);

	TArray<FGreenPolygon> None; FString E2;
	TestFalse(TEXT("empty features -> false"),
		ParseGreenPolygonsJson(TEXT("{\"type\":\"FeatureCollection\",\"features\":[]}"), 0, 0, 1, 1, None, E2));
	return true;
}

// Point-in-polygon + random-in-green always lands inside (seeded, many draws).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenGeometryTest, "Golfsim.Round.GreenGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimGreenGeometryTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const FGreenPolygon G = SquareGreen(0, 0, 1000);   // 20x20 m green
	TestTrue(TEXT("origin inside"), PointInPolygonCm(FVector2D(0, 0), G));
	TestFalse(TEXT("far point outside"), PointInPolygonCm(FVector2D(5000, 0), G));

	FRandomStream Stream(7);
	for (int32 i = 0; i < 200; ++i)
	{
		const FVector2D P = RandomPointInGreen(G, Stream);
		TestTrue(TEXT("random point lands on the green"), PointInPolygonCm(P, G));
	}
	return true;
}

// GOL-199: a putt-from spot a given distance from the pin, on the green. On-green + ~right distance
// when it fits; graceful fallback (on-green, not on top of the cup) when the green is too small.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPuttFromPointTest, "Golfsim.Round.PointOnGreenAtDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPuttFromPointTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const FGreenPolygon G = SquareGreen(0, 0, 1000);   // 20x20 m green
	const FVector2D Pin(0, 0);
	FRandomStream Stream(11);

	// 3 m fits inside a 20 m green -> on-green and ~3 m from the pin.
	for (int32 i = 0; i < 100; ++i)
	{
		const FVector2D P = PointOnGreenAtDistance(G, Pin, 300.0, Stream);
		TestTrue(TEXT("putt-from lands on the green"), PointInPolygonCm(P, G));
		TestTrue(TEXT("putt-from ~3 m from pin"), FMath::Abs(FVector2D::Distance(P, Pin) - 300.0) < 1.0);
	}

	// A distance larger than the green in every direction -> fall back to an on-green point, still
	// off the cup (the fallback guarantees > 1 ft).
	for (int32 i = 0; i < 50; ++i)
	{
		const FVector2D P = PointOnGreenAtDistance(G, Pin, 100000.0, Stream);
		TestTrue(TEXT("fallback lands on the green"), PointInPolygonCm(P, G));
		TestTrue(TEXT("fallback not on the cup"), FVector2D::Distance(P, Pin) > 30.48);
	}
	return true;
}

// Match a hole to its green: containing polygon wins; else nearest centroid; empty -> INDEX_NONE.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenMatchTest, "Golfsim.Round.GreenMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimGreenMatchTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	TArray<FGreenPolygon> Greens = { SquareGreen(0, 0, 500), SquareGreen(10000, 0, 500) };

	FHoleSpec Inside;  Inside.GreenWorldLoc = FVector(10000, 0, 0);   // inside green #1
	TestEqual(TEXT("containing polygon"), MatchGreenToHole(Inside, Greens), 1);

	FHoleSpec Nearer;  Nearer.GreenWorldLoc = FVector(400, 9999, 0);  // outside both; nearest centroid = #0
	TestEqual(TEXT("nearest centroid"), MatchGreenToHole(Nearer, Greens), 0);

	TArray<FGreenPolygon> Empty;
	TestEqual(TEXT("no greens -> INDEX_NONE"), MatchGreenToHole(Inside, Empty), (int32)INDEX_NONE);
	return true;
}

// ResolvePinPositions across the three modes + fallbacks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPinResolveTest, "Golfsim.Round.PinResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPinResolveTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	TArray<FGreenPolygon> Greens = { SquareGreen(0, 0, 1000) };

	auto MakeHole = [](int32 Ref)
	{
		FHoleSpec H; H.Ref = Ref;
		H.GreenWorldLoc = FVector(0, 0, 0);          // inside the green
		H.PinWorldLoc   = FVector(50000, 0, 0);      // endpoint far from the green (so changes are visible)
		return H;
	};

	// Static: untouched.
	{
		TArray<FHoleSpec> S = { MakeHole(1) };
		FRandomStream R(1);
		ResolvePinPositions(S, EPinMode::Static, Greens, nullptr, R);
		TestEqual(TEXT("static keeps endpoint"), S[0].PinWorldLoc.X, 50000.0);
	}
	// Random: pin lands on the green.
	{
		TArray<FHoleSpec> S = { MakeHole(1) };
		FRandomStream R(2);
		ResolvePinPositions(S, EPinMode::Random, Greens, nullptr, R);
		TestTrue(TEXT("random pin on green"),
			PointInPolygonCm(FVector2D(S[0].PinWorldLoc.X, S[0].PinWorldLoc.Y), Greens[0]));
	}
	// Tournament: sheet hit -> exact; missing ref -> green centroid fallback.
	{
		FPinSheet Sheet; Sheet.PinXYByRefCm.Add(1, FVector2D(300, -200));
		TArray<FHoleSpec> S = { MakeHole(1), MakeHole(2) };
		FRandomStream R(3);
		ResolvePinPositions(S, EPinMode::Tournament, Greens, &Sheet, R);
		TestEqual(TEXT("tournament uses the sheet (X)"), S[0].PinWorldLoc.X, 300.0);
		TestEqual(TEXT("tournament uses the sheet (Y)"), S[0].PinWorldLoc.Y, -200.0);
		TestTrue(TEXT("missing ref -> centroid fallback"),
			FVector2D(S[1].PinWorldLoc.X, S[1].PinWorldLoc.Y).Equals(Greens[0].CentroidCm, 1.0));
	}
	// Random with no greens: endpoint unchanged (graceful).
	{
		TArray<FHoleSpec> S = { MakeHole(1) };
		FRandomStream R(4);
		TArray<FGreenPolygon> Empty;
		ResolvePinPositions(S, EPinMode::Random, Empty, nullptr, R);
		TestEqual(TEXT("no greens -> static fallback"), S[0].PinWorldLoc.X, 50000.0);
	}
	return true;
}

// Pin-sheet parse: per-hole lon/lat projected with the course bbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimPinSheetParseTest, "Golfsim.Round.PinSheetParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimPinSheetParseTest::RunTest(const FString&)
{
	using namespace GolfsimRound;
	const FString Json = TEXT(
		"{\"name\":\"Championship Pins\",\"pins\":["
		"{\"hole_ref\":1,\"lon\":0.5,\"lat\":0.5},"
		"{\"hole_ref\":2,\"lon\":0.6,\"lat\":0.4}]}");
	FPinSheet Sheet; FString Err;
	TestTrue(TEXT("parse ok"), ParsePinSheetJson(Json, 0, 0, 1, 1, Sheet, Err));
	TestEqual(TEXT("name"), Sheet.Name, FString(TEXT("Championship Pins")));
	TestEqual(TEXT("two pins"), Sheet.PinXYByRefCm.Num(), 2);
	const FVector2D* P1 = Sheet.PinXYByRefCm.Find(1);
	TestNotNull(TEXT("ref 1 present"), P1);
	if (P1) { TestTrue(TEXT("ref 1 ~origin"), P1->Size() < 1.0); }
	return true;
}

#endif // WITH_AUTOMATION_TESTS
