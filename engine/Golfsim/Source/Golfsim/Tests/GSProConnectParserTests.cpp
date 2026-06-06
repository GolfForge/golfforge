// Automation tests for UGSProConnectDriver::ParseShot (GOL-178). Pure -- no socket, no world, no RHI --
// since ParseShot is a static function. Uses GSPro Open Connect V1's real message schema (BallData +
// ShotDataOptions), the same bytes the community connectors send.
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.GSProConnect; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Drivers/GSProConnectDriver.h"
#include "Events/EventTypes.h"

#if WITH_AUTOMATION_TESTS

// --- Full GSPro Open Connect message normalizes to the SI envelope (measured TotalSpin+SpinAxis) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProFullMessageTest, "Golfsim.GSProConnect.FullMessageToSI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProFullMessageTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"DeviceID\":\"squaregolf\",\"Units\":\"Yards\",\"ShotNumber\":3,\"APIversion\":\"1\",")
		TEXT("\"BallData\":{\"Speed\":147.5,\"SpinAxis\":0.0,\"TotalSpin\":3250.0,\"HLA\":2.3,\"VLA\":14.3,\"CarryDistance\":256.5},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"LaunchMonitorIsReady\":true,\"LaunchMonitorBallDetected\":true,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = true;   // expect cleared by a measured shot
	const bool bOk = UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded"), bOk);
	TestTrue(TEXT("Speed mph->m/s"), FMath::IsNearlyEqual(Out.BallSpeedMps, 147.5 * 0.44704, 0.01));
	TestTrue(TEXT("VLA -> launch (deg)"), FMath::IsNearlyEqual(Out.LaunchAngleDeg, 14.3));
	TestTrue(TEXT("HLA -> azimuth (deg), + = right"), FMath::IsNearlyEqual(Out.AzimuthDeg, 2.3));
	TestTrue(TEXT("axis 0 -> all backspin"), FMath::IsNearlyEqual(Out.BackspinRpm, 3250.0, 0.5));
	TestTrue(TEXT("axis 0 -> no sidespin"), FMath::IsNearlyEqual(Out.SidespinRpm, 0.0, 0.5));
	TestFalse(TEXT("measured spin not flagged estimated"), bSpinEstimated);
	TestTrue(TEXT("source tagged gsproconnect"), Out.Source == TEXT("gsproconnect"));
	TestEqual(TEXT("kind is shot.taken"), (int32)Out.Kind, (int32)EEventKind::ShotTaken);
	return true;
}

// --- TotalSpin + SpinAxis decomposes into back/side (+ axis = fade/right) ---------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProSpinAxisTest, "Golfsim.GSProConnect.SpinAxisDecomposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProSpinAxisTest::RunTest(const FString& /*Parameters*/)
{
	// 4000 rpm tilted +30 deg (fade): back = 4000*cos30 = 3464.1, side = 4000*sin30 = 2000 (right).
	const FString Json = TEXT("{\"BallData\":{\"Speed\":150.0,\"TotalSpin\":4000.0,\"SpinAxis\":30.0,\"VLA\":12.0},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestFalse(TEXT("measured, not estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin = total*cos(axis)"), FMath::IsNearlyEqual(Out.BackspinRpm, 4000.0 * FMath::Cos(FMath::DegreesToRadians(30.0)), 1.0));
	TestTrue(TEXT("sidespin = total*sin(axis), + = right/fade"), FMath::IsNearlyEqual(Out.SidespinRpm, 2000.0, 1.0));
	return true;
}

// --- Measured BackSpin + SideSpin used directly (no TotalSpin) ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProBackSideSpinTest, "Golfsim.GSProConnect.BackSideSpinDirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProBackSideSpinTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"BallData\":{\"Speed\":150.0,\"BackSpin\":2800.0,\"SideSpin\":-650.0,\"VLA\":12.0},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = true;
	const bool bOk = UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded"), bOk);
	TestFalse(TEXT("measured, not estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin used directly"), FMath::IsNearlyEqual(Out.BackspinRpm, 2800.0, 0.5));
	TestTrue(TEXT("sidespin used directly (- = draw/left)"), FMath::IsNearlyEqual(Out.SidespinRpm, -650.0, 0.5));
	return true;
}

// --- Missing spin -> launch-angle heuristic + flagged estimated -------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProMissingSpinTest, "Golfsim.GSProConnect.MissingSpinEstimated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProMissingSpinTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"BallData\":{\"Speed\":120.0,\"VLA\":24.0},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	const bool bOk = UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded without spin"), bOk);
	TestTrue(TEXT("spin flagged estimated"), bSpinEstimated);
	TestTrue(TEXT("envelope flag set"), Out.bSpinEstimated);
	TestTrue(TEXT("heuristic backspin = clamp(24*350)=8400"), FMath::IsNearlyEqual(Out.BackspinRpm, 8400.0));
	TestTrue(TEXT("no sidespin when estimated"), FMath::IsNearlyEqual(Out.SidespinRpm, 0.0));
	return true;
}

// --- Heuristic clamps at the low end ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProSpinClampTest, "Golfsim.GSProConnect.SpinHeuristicClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProSpinClampTest::RunTest(const FString& /*Parameters*/)
{
	const FString Json = TEXT("{\"BallData\":{\"Speed\":160.0,\"VLA\":2.0}}");   // 2*350=700 -> 1500 floor

	FShotTakenEvent Out;
	bool bSpinEstimated = false;
	UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin clamped to floor 1500"), FMath::IsNearlyEqual(Out.BackspinRpm, 1500.0));
	return true;
}

// --- Heartbeats / no-ball-data messages are rejected (not shots) ------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProHeartbeatTest, "Golfsim.GSProConnect.HeartbeatRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProHeartbeatTest::RunTest(const FString& /*Parameters*/)
{
	FShotTakenEvent Out;
	bool bSpinEstimated = false;

	// IsHeartBeat true -> not a shot, even if BallData is present.
	TestFalse(TEXT("heartbeat rejected"),
		UGSProConnectDriver::ParseShot(
			TEXT("{\"BallData\":{\"Speed\":150.0},\"ShotDataOptions\":{\"ContainsBallData\":false,\"IsHeartBeat\":true}}"),
			Out, bSpinEstimated));

	// ContainsBallData false -> not a shot.
	TestFalse(TEXT("no-ball-data message rejected"),
		UGSProConnectDriver::ParseShot(
			TEXT("{\"ShotDataOptions\":{\"ContainsBallData\":false,\"IsHeartBeat\":false}}"),
			Out, bSpinEstimated));
	return true;
}

// --- Malformed / non-shot input is rejected, not crashed --------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProInvalidTest, "Golfsim.GSProConnect.InvalidInputRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProInvalidTest::RunTest(const FString& /*Parameters*/)
{
	FShotTakenEvent Out;
	bool bSpinEstimated = false;

	TestFalse(TEXT("garbage json rejected"), UGSProConnectDriver::ParseShot(TEXT("not json {{{"), Out, bSpinEstimated));
	TestFalse(TEXT("no BallData rejected"),
		UGSProConnectDriver::ParseShot(TEXT("{\"DeviceID\":\"x\",\"ShotNumber\":1}"), Out, bSpinEstimated));
	TestFalse(TEXT("missing Speed rejected"),
		UGSProConnectDriver::ParseShot(TEXT("{\"BallData\":{\"VLA\":12.0}}"), Out, bSpinEstimated));
	TestFalse(TEXT("zero Speed rejected"),
		UGSProConnectDriver::ParseShot(TEXT("{\"BallData\":{\"Speed\":0.0}}"), Out, bSpinEstimated));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
