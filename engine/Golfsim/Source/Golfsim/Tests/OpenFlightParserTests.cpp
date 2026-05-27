// Automation tests for UOpenFlightDriver::ParseShot (GOL-11). Pure -- no socket, no world, no RHI --
// since ParseShot is a static function. Uses OpenFlight's real shot schema (Socket.IO event payload).
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.OpenFlight; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Drivers/OpenFlightDriver.h"
#include "Events/EventTypes.h"

#if WITH_AUTOMATION_TESTS

// --- Full {shot,stats}-wrapped payload normalizes to the SI envelope (measured, pure backspin) -

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightFullPayloadTest, "Golfsim.OpenFlight.FullPayloadToSI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightFullPayloadTest::RunTest(const FString& /*Parameters*/)
{
	// OpenFlight's "shot" event payload: { "shot": <fields>, "stats": {...} }.
	const FString Json = TEXT("{\"shot\":{\"ball_speed_mph\":152.3,\"launch_angle_vertical\":12.4,")
		TEXT("\"launch_angle_horizontal\":-1.8,\"spin_rpm\":2680.0,\"spin_axis_deg\":0.0,")
		TEXT("\"club\":\"driver\",\"smash_factor\":1.35}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = true;   // expect cleared by a measured shot
	const bool bOk = UOpenFlightDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded"), bOk);
	TestTrue(TEXT("ball speed mph->m/s"), FMath::IsNearlyEqual(Out.BallSpeedMps, 152.3 * 0.44704, 0.01));
	TestTrue(TEXT("launch_angle_vertical -> launch (deg)"), FMath::IsNearlyEqual(Out.LaunchAngleDeg, 12.4));
	TestTrue(TEXT("launch_angle_horizontal -> azimuth (deg)"), FMath::IsNearlyEqual(Out.AzimuthDeg, -1.8));
	TestTrue(TEXT("axis 0 -> all backspin"), FMath::IsNearlyEqual(Out.BackspinRpm, 2680.0, 0.5));
	TestTrue(TEXT("axis 0 -> no sidespin"), FMath::IsNearlyEqual(Out.SidespinRpm, 0.0, 0.5));
	TestTrue(TEXT("club preserved"), Out.Club == TEXT("driver"));
	TestTrue(TEXT("smash preserved"), FMath::IsNearlyEqual(Out.SmashFactor, 1.35));
	TestFalse(TEXT("measured spin not flagged estimated"), bSpinEstimated);
	TestTrue(TEXT("source tagged openflight"), Out.Source == TEXT("openflight"));
	TestEqual(TEXT("kind is shot.taken"), (int32)Out.Kind, (int32)EEventKind::ShotTaken);
	return true;
}

// --- spin_rpm + spin_axis_deg decomposes into back/side (+ axis = fade/right) -------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightSpinAxisTest, "Golfsim.OpenFlight.SpinAxisDecomposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightSpinAxisTest::RunTest(const FString& /*Parameters*/)
{
	// 4000 rpm tilted +30 deg (fade): back = 4000*cos30 = 3464.1, side = 4000*sin30 = 2000 (right).
	const FString Json = TEXT("{\"shot\":{\"ball_speed_mph\":150.0,\"spin_rpm\":4000.0,\"spin_axis_deg\":30.0}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	UOpenFlightDriver::ParseShot(Json, Out, bSpinEstimated);

	TestFalse(TEXT("measured, not estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin = total*cos(axis)"), FMath::IsNearlyEqual(Out.BackspinRpm, 4000.0 * FMath::Cos(FMath::DegreesToRadians(30.0)), 1.0));
	TestTrue(TEXT("sidespin = total*sin(axis), + = right/fade"), FMath::IsNearlyEqual(Out.SidespinRpm, 2000.0, 1.0));
	return true;
}

// --- A bare shot object (no {shot} wrapper) still parses ----------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightBareObjectTest, "Golfsim.OpenFlight.BareObjectParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightBareObjectTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"ball_speed_mph\":150.0,\"launch_angle_vertical\":12.0,\"spin_rpm\":3000.0}");

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	const bool bOk = UOpenFlightDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("bare object parse succeeded"), bOk);
	TestTrue(TEXT("ball speed mph->m/s"), FMath::IsNearlyEqual(Out.BallSpeedMps, 150.0 * 0.44704, 0.01));
	TestTrue(TEXT("backspin preserved"), FMath::IsNearlyEqual(Out.BackspinRpm, 3000.0, 0.5));
	return true;
}

// --- Missing spin -> launch-angle heuristic + flagged estimated --------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightMissingSpinTest, "Golfsim.OpenFlight.MissingSpinEstimated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightMissingSpinTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"shot\":{\"ball_speed_mph\":120.0,\"launch_angle_vertical\":24.0,\"club\":\"pw\"}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	const bool bOk = UOpenFlightDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded without spin"), bOk);
	TestTrue(TEXT("spin flagged estimated"), bSpinEstimated);
	TestTrue(TEXT("envelope flag set"), Out.bSpinEstimated);
	TestTrue(TEXT("heuristic backspin = clamp(24*350)=8400"), FMath::IsNearlyEqual(Out.BackspinRpm, 8400.0));
	TestTrue(TEXT("no sidespin when estimated"), FMath::IsNearlyEqual(Out.SidespinRpm, 0.0));
	return true;
}

// --- Heuristic clamps at the low end -----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightSpinClampTest, "Golfsim.OpenFlight.SpinHeuristicClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightSpinClampTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"shot\":{\"ball_speed_mph\":160.0,\"launch_angle_vertical\":2.0}}");   // 2*350=700 -> 1500 floor

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	UOpenFlightDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin clamped to floor 1500"), FMath::IsNearlyEqual(Out.BackspinRpm, 1500.0));
	return true;
}

// --- Malformed / non-shot input is rejected, not crashed ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimOpenFlightInvalidTest, "Golfsim.OpenFlight.InvalidInputRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimOpenFlightInvalidTest::RunTest(const FString& /*Parameters*/)
{
	FShotTakenEvent Out;
	bool bSpinEstimated = false;

	TestFalse(TEXT("garbage json rejected"), UOpenFlightDriver::ParseShot(TEXT("not json {{{"), Out, bSpinEstimated));
	TestFalse(TEXT("non-shot json (no ball speed) rejected"),
		UOpenFlightDriver::ParseShot(TEXT("{\"shot\":{\"club\":\"driver\"}}"), Out, bSpinEstimated));
	TestFalse(TEXT("zero ball speed rejected"),
		UOpenFlightDriver::ParseShot(TEXT("{\"shot\":{\"ball_speed_mph\":0.0}}"), Out, bSpinEstimated));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
