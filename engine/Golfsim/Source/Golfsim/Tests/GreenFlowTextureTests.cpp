// Automation tests for the green break-grid flow-texture encoding (GOL-203). Headless: pure
// BuildFlowPixels in UI/GreenFlowTexture.{h,cpp} -- no world, no RHI, no texture object. These
// pin the pixel contract M_GreenFlow samples (RG = fall dir [-1,1]->[0,1], B = slope/max,
// A = on-green mask, neutral off-green) so a refactor can't silently re-aim every break dot.
//
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.GreenFlow; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "UI/GreenFlowTexture.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// A 2x2 grid with one steep east-falling cell, one flat in-green cell, one off-green cell,
	// and one half-slope diagonal cell.
	GolfMap::FGreenSlopeGrid MakeTestGrid()
	{
		GolfMap::FGreenSlopeGrid G;
		G.OriginCm = FVector2D(1000.0, 2000.0);
		G.CellCm = 100.0;
		G.NX = 2; G.NY = 2;
		G.SlopePct =     { 8.0f,                      0.0f,  4.0f,                                  5.0f };
		G.FallDirWorld = { FVector2D(1.0, 0.0), FVector2D::ZeroVector,
		                   FVector2D(0.70710678, 0.70710678), FVector2D(-1.0, 0.0) };
		G.bInGreen =     { true, true, true, false };
		return G;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenFlowEncodingTest,
	"Golfsim.GreenFlow.PixelEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGreenFlowEncodingTest::RunTest(const FString&)
{
	const GolfMap::FGreenSlopeGrid G = MakeTestGrid();
	TArray<FColor> Px;
	TestTrue(TEXT("encoding succeeds"), GolfsimGreenFlow::BuildFlowPixels(G, 8.0f, Px));
	TestEqual(TEXT("one pixel per cell"), Px.Num(), 4);

	// Cell 0: due-east fall at the 8% saturation point -> R=255, G=128, B=255, masked in.
	TestEqual(TEXT("east dir R"), (int32)Px[0].R, 255);
	TestEqual(TEXT("east dir G"), (int32)Px[0].G, 128);
	TestEqual(TEXT("saturated slope B"), (int32)Px[0].B, 255);
	TestEqual(TEXT("in-green A"), (int32)Px[0].A, 255);

	// Cell 1: flat but in-green -> neutral dir, zero slope, KEEPS its mask (dots dim, not gone).
	TestEqual(TEXT("flat dir R"), (int32)Px[1].R, 128);
	TestEqual(TEXT("flat dir G"), (int32)Px[1].G, 128);
	TestEqual(TEXT("flat slope B"), (int32)Px[1].B, 0);
	TestEqual(TEXT("flat in-green keeps mask"), (int32)Px[1].A, 255);

	// Cell 2: half slope (4/8) -> B ~ 128; diagonal dir encodes symmetrically.
	TestEqual(TEXT("half slope B"), (int32)Px[2].B, 128);
	TestEqual(TEXT("diagonal RG symmetric"), (int32)Px[2].R, (int32)Px[2].G);
	TestTrue(TEXT("diagonal dir positive"), Px[2].R > 128);

	// Cell 3: off-green -> the neutral pixel regardless of its slope data.
	TestEqual(TEXT("off-green neutral R"), (int32)Px[3].R, 128);
	TestEqual(TEXT("off-green neutral G"), (int32)Px[3].G, 128);
	TestEqual(TEXT("off-green neutral B"), (int32)Px[3].B, 0);
	TestEqual(TEXT("off-green masked out"), (int32)Px[3].A, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenFlowClampTest,
	"Golfsim.GreenFlow.SlopeClampAndWestEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGreenFlowClampTest::RunTest(const FString&)
{
	GolfMap::FGreenSlopeGrid G;
	G.NX = 1; G.NY = 1;
	G.SlopePct = { 20.0f };                       // over the max -> clamps to 255
	G.FallDirWorld = { FVector2D(-1.0, 0.0) };    // due west -> R encodes to 0
	G.bInGreen = { true };

	TArray<FColor> Px;
	TestTrue(TEXT("encoding succeeds"), GolfsimGreenFlow::BuildFlowPixels(G, 8.0f, Px));
	TestEqual(TEXT("over-max slope clamps"), (int32)Px[0].B, 255);
	TestEqual(TEXT("west dir R = 0"), (int32)Px[0].R, 0);
	TestEqual(TEXT("west dir G = 128"), (int32)Px[0].G, 128);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGreenFlowRejectsTest,
	"Golfsim.GreenFlow.RejectsMalformedGrids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGreenFlowRejectsTest::RunTest(const FString&)
{
	TArray<FColor> Px;

	GolfMap::FGreenSlopeGrid Empty;
	TestFalse(TEXT("empty grid rejected"), GolfsimGreenFlow::BuildFlowPixels(Empty, 8.0f, Px));
	TestEqual(TEXT("output emptied"), Px.Num(), 0);

	GolfMap::FGreenSlopeGrid Mismatch = MakeTestGrid();
	Mismatch.SlopePct.Pop();   // arrays no longer NX*NY
	TestFalse(TEXT("mismatched arrays rejected"), GolfsimGreenFlow::BuildFlowPixels(Mismatch, 8.0f, Px));

	GolfMap::FGreenSlopeGrid Good = MakeTestGrid();
	TestFalse(TEXT("non-positive SlopeMax rejected"), GolfsimGreenFlow::BuildFlowPixels(Good, 0.0f, Px));

	return true;
}

#endif   // WITH_AUTOMATION_TESTS
