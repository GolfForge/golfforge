// Automation tests for the minimap projection math (GOL-209). Headless: pure C++ structs in
// UI/HoleMapProjection.{h,cpp} -- no world, no RHI, no widget. These pin the transform contract
// the HoleMapView widget and the click-to-aim inverse both depend on: if WorldToWidget and
// WidgetToWorld ever disagree, clicking the map aims somewhere other than where the user clicked.
//
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.HoleMap; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "UI/HoleMapProjection.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	constexpr double Tol = 0.01;   // px / unit tolerance for double round-trips

	bool NearlyEqual(const FVector2D& A, const FVector2D& B, double T = Tol)
	{
		return FMath::Abs(A.X - B.X) <= T && FMath::Abs(A.Y - B.Y) <= T;
	}
}

// --- Hole framing: tee bottom-center, pin top-center, round-trip inverse ----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleMapFramingTest,
	"Golfsim.HoleMap.HoleFramingTeeBottomPinTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleMapFramingTest::RunTest(const FString&)
{
	const FVector2D View(248.0, 248.0);
	// A diagonal hole so the rotation actually does something: 400 m northeast-ish.
	const FVector2D Tee(10000.0, -5000.0);
	const FVector2D Pin(38000.0, 23000.0);

	const GolfMap::FHoleMapProjection P = GolfMap::MakeHoleFraming(Tee, Pin, View);
	TestTrue(TEXT("framing is valid"), P.IsValid());

	const FVector2D TeePx = P.WorldToWidget(Tee);
	const FVector2D PinPx = P.WorldToWidget(Pin);

	// Both on the vertical centerline (the hole axis maps to widget "up").
	TestTrue(TEXT("tee on the vertical centerline"), FMath::Abs(TeePx.X - View.X * 0.5) <= Tol);
	TestTrue(TEXT("pin on the vertical centerline"), FMath::Abs(PinPx.X - View.X * 0.5) <= Tol);
	// Tee at the bottom (larger widget y), pin at the top.
	TestTrue(TEXT("tee below pin (widget y down)"), TeePx.Y > PinPx.Y + 10.0);
	// Margin keeps both inside the view.
	TestTrue(TEXT("tee inside view"), TeePx.Y < View.Y && TeePx.Y > 0.0);
	TestTrue(TEXT("pin inside view"), PinPx.Y > 0.0 && PinPx.Y < View.Y);

	// Round-trip: WidgetToWorld(WorldToWidget(x)) == x, across the view.
	const FVector2D Samples[] = { Tee, Pin, FVector2D(20000.0, 1000.0), FVector2D(-90000.0, 90000.0) };
	for (const FVector2D& W : Samples)
	{
		TestTrue(TEXT("world->widget->world round-trips"),
			NearlyEqual(P.WidgetToWorld(P.WorldToWidget(W)), W, 0.5));
	}

	// GetAffine must agree with WorldToWidget exactly (the widget draws the texture with it).
	double A, B, C, D, Tx, Ty;
	P.GetAffine(A, B, C, D, Tx, Ty);
	const FVector2D Probe(-50000.0, 30000.0);
	const FVector2D ViaAffine(A * Probe.X + B * Probe.Y + Tx, C * Probe.X + D * Probe.Y + Ty);
	TestTrue(TEXT("affine matches WorldToWidget"), NearlyEqual(ViaAffine, P.WorldToWidget(Probe)));

	return true;
}

// --- Par-3 min-span clamp + degenerate axis ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleMapMinSpanTest,
	"Golfsim.HoleMap.MinSpanAndDegenerateAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleMapMinSpanTest::RunTest(const FString&)
{
	const FVector2D View(248.0, 248.0);

	// 80 yd pitch hole: the 160 yd floor must cap the zoom (PxPerCm == View.Y / MinSpan).
	const FVector2D Tee(0.0, 0.0);
	const FVector2D Pin(0.0, 80.0 * 91.44);
	const GolfMap::FHoleMapProjection P = GolfMap::MakeHoleFraming(Tee, Pin, View);
	TestEqual(TEXT("min-span floor applies"), P.PxPerCm, View.Y / 14630.0, 1e-9);

	// Degenerate tee==pin: valid, north-up (Theta 0).
	const GolfMap::FHoleMapProjection D = GolfMap::MakeHoleFraming(Tee, Tee + FVector2D(50.0, 0.0), View);
	TestTrue(TEXT("degenerate framing still valid"), D.IsValid());
	TestEqual(TEXT("degenerate framing is north-up"), D.ThetaRad, 0.0, 1e-12);

	return true;
}

// --- Zoom: anchor invariance + clamping --------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleMapZoomTest,
	"Golfsim.HoleMap.ZoomAnchorInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleMapZoomTest::RunTest(const FString&)
{
	const FVector2D View(248.0, 248.0);
	GolfMap::FHoleMapProjection P = GolfMap::MakeHoleFraming(
		FVector2D(10000.0, -5000.0), FVector2D(38000.0, 23000.0), View);
	const double Base = P.PxPerCm;

	// The world point under the anchor must stay under the anchor through a zoom.
	const FVector2D Anchor(60.0, 190.0);
	const FVector2D AnchorWorld = P.WidgetToWorld(Anchor);
	P.ZoomAt(2.0, Anchor, Base, Base * 8.0);
	TestEqual(TEXT("zoom scales PxPerCm"), P.PxPerCm, Base * 2.0, 1e-12);
	TestTrue(TEXT("anchor world point is invariant under zoom"),
		NearlyEqual(P.WorldToWidget(AnchorWorld), Anchor, 0.01));

	// Clamps: can't zoom below the base framing or beyond 8x.
	P.ZoomAt(0.01, Anchor, Base, Base * 8.0);
	TestEqual(TEXT("zoom-out clamps at base"), P.PxPerCm, Base, 1e-12);
	P.ZoomAt(1000.0, Anchor, Base, Base * 8.0);
	TestEqual(TEXT("zoom-in clamps at 8x"), P.PxPerCm, Base * 8.0, 1e-12);

	return true;
}

// --- Green framing contains every vert ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleMapGreenFramingTest,
	"Golfsim.HoleMap.GreenFramingContainsVerts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleMapGreenFramingTest::RunTest(const FString&)
{
	const FVector2D View(248.0, 248.0);
	const double Theta = -PI * 0.5 - FMath::Atan2(1.0, 1.0);   // some non-trivial hole rotation

	// An off-center, elongated "green" outline.
	TArray<FVector2D> Verts = {
		FVector2D(50000.0, 50000.0), FVector2D(53000.0, 50500.0), FVector2D(54000.0, 52500.0),
		FVector2D(52000.0, 54000.0), FVector2D(49500.0, 53000.0), FVector2D(49000.0, 51000.0),
	};
	const GolfMap::FHoleMapProjection P = GolfMap::MakeGreenFraming(Verts, FVector2D::ZeroVector, Theta, View);
	TestTrue(TEXT("green framing valid"), P.IsValid());
	TestEqual(TEXT("green framing keeps the hole rotation"), P.ThetaRad, Theta, 1e-12);

	for (const FVector2D& V : Verts)
	{
		const FVector2D Px = P.WorldToWidget(V);
		TestTrue(TEXT("vert inside view"), Px.X >= 0.0 && Px.X <= View.X && Px.Y >= 0.0 && Px.Y <= View.Y);
	}

	// Empty outline: fallback box around the given center, still valid.
	const GolfMap::FHoleMapProjection F = GolfMap::MakeGreenFraming(
		TArray<FVector2D>(), FVector2D(7000.0, 7000.0), Theta, View);
	TestTrue(TEXT("fallback framing valid"), F.IsValid());
	TestTrue(TEXT("fallback centered on the given point"),
		NearlyEqual(F.WorldToWidget(FVector2D(7000.0, 7000.0)), View * 0.5));

	return true;
}

// --- Slope grid: synthetic tilted plane --------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleMapSlopeGridTest,
	"Golfsim.HoleMap.SlopeGridTiltedPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleMapSlopeGridTest::RunTest(const FString&)
{
	// A 4x3 grid of 1 m cells on a plane rising 3 cm per meter toward +X: every cell
	// must report 3% slope falling toward -X.
	GolfMap::FGreenSlopeGrid Grid;
	Grid.OriginCm = FVector2D(1000.0, 2000.0);
	Grid.CellCm = 100.0;
	Grid.NX = 4;
	Grid.NY = 3;
	Grid.bInGreen.Init(true, Grid.NX * Grid.NY);

	TArray<double> Corners;
	Corners.SetNum((Grid.NX + 1) * (Grid.NY + 1));
	for (int32 J = 0; J <= Grid.NY; ++J)
	{
		for (int32 I = 0; I <= Grid.NX; ++I)
		{
			const double XCm = Grid.OriginCm.X + (I - 0.5) * Grid.CellCm;
			Corners[J * (Grid.NX + 1) + I] = 0.03 * XCm;   // 3% grade in +X
		}
	}

	GolfMap::ComputeSlopeGrid(Corners, Grid);
	TestEqual(TEXT("slope array sized"), Grid.SlopePct.Num(), Grid.NX * Grid.NY);
	for (int32 Cell = 0; Cell < Grid.NX * Grid.NY; ++Cell)
	{
		TestEqual(TEXT("3% slope on every cell"), static_cast<double>(Grid.SlopePct[Cell]), 3.0, 0.01);
		TestTrue(TEXT("falls toward -X"), NearlyEqual(Grid.FallDirWorld[Cell], FVector2D(-1.0, 0.0), 0.001));
		TestTrue(TEXT("cell stays in-green"), Grid.bInGreen[Cell]);
	}

	// One bad corner (trace miss) excludes exactly the cells touching it.
	Corners[1 * (Grid.NX + 1) + 2] = GolfMap::InvalidHeightCm;   // corner (2,1)
	GolfMap::ComputeSlopeGrid(Corners, Grid);
	// Corner (2,1) touches cells (1,0), (2,0), (1,1), (2,1).
	TestFalse(TEXT("cell (1,0) excluded"), Grid.bInGreen[0 * Grid.NX + 1]);
	TestFalse(TEXT("cell (2,0) excluded"), Grid.bInGreen[0 * Grid.NX + 2]);
	TestFalse(TEXT("cell (1,1) excluded"), Grid.bInGreen[1 * Grid.NX + 1]);
	TestFalse(TEXT("cell (2,1) excluded"), Grid.bInGreen[1 * Grid.NX + 2]);
	TestTrue(TEXT("far cell unaffected"), Grid.bInGreen[2 * Grid.NX + 0]);

	return true;
}

#endif   // WITH_AUTOMATION_TESTS
