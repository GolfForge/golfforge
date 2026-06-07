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

// --- All of TotalSpin+Back+Side present: measured Back/Side win (not the axis decomposition) --------
// The real squaregolf connector sends all three, and they need not be self-consistent
// (TotalSpin != hypot(Back,Side)); the device's measured components are authoritative.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProMeasuredSpinWinsTest, "Golfsim.GSProConnect.MeasuredSpinWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProMeasuredSpinWinsTest::RunTest(const FString& /*Parameters*/)
{
	// TotalSpin 179 + SpinAxis -5.5 would decompose to back=178, side=-17 (left). The measured
	// components say back=109, side=+17 (right). The parser must use the measured ones.
	const FString Json = TEXT("{\"BallData\":{\"Speed\":59.0,\"SpinAxis\":-5.5,\"TotalSpin\":179,")
		TEXT("\"BackSpin\":109,\"SideSpin\":17,\"HLA\":0.1,\"VLA\":1.2},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":false,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = true;
	const bool bOk = UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded"), bOk);
	TestFalse(TEXT("measured, not estimated"), bSpinEstimated);
	TestTrue(TEXT("backspin = measured BackSpin (not total*cos)"), FMath::IsNearlyEqual(Out.BackspinRpm, 109.0, 0.5));
	TestTrue(TEXT("sidespin = measured SideSpin, + = right (not decomposed -17)"), FMath::IsNearlyEqual(Out.SidespinRpm, 17.0, 0.5));
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

// --- Streaming object extractor: handles both newline-delimited and concatenated framing ------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProExtractObjectsTest, "Golfsim.GSProConnect.ExtractJsonObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProExtractObjectsTest::RunTest(const FString& /*Parameters*/)
{
	{
		// Concatenated, no delimiter (springbok framing).
		TArray<FString> Objs;
		const int32 Consumed = UGSProConnectDriver::ExtractJsonObjects(TEXT("{\"a\":1}{\"b\":2}"), Objs);
		TestEqual(TEXT("concatenated -> 2 objects"), Objs.Num(), 2);
		TestEqual(TEXT("obj0"), Objs.IsValidIndex(0) ? Objs[0] : FString(), FString(TEXT("{\"a\":1}")));
		TestEqual(TEXT("obj1"), Objs.IsValidIndex(1) ? Objs[1] : FString(), FString(TEXT("{\"b\":2}")));
		TestEqual(TEXT("consumed all"), Consumed, 14);
	}
	{
		// Newline-delimited (squaregolf framing) + trailing newline.
		TArray<FString> Objs;
		UGSProConnectDriver::ExtractJsonObjects(TEXT("{\"a\":1}\n{\"b\":2}\n"), Objs);
		TestEqual(TEXT("newline-delimited -> 2 objects"), Objs.Num(), 2);
	}
	{
		// Nested object + a string value containing braces/quotes must not fool the brace counter.
		TArray<FString> Objs;
		UGSProConnectDriver::ExtractJsonObjects(TEXT("{\"m\":\"a}b{c\",\"n\":{\"x\":1}}"), Objs);
		TestEqual(TEXT("brace-in-string -> 1 object"), Objs.Num(), 1);
	}
	{
		// Partial trailing object: nothing complete -> 0 objects, 0 consumed (caller keeps it buffered).
		TArray<FString> Objs;
		const int32 Consumed = UGSProConnectDriver::ExtractJsonObjects(TEXT("{\"a\":1"), Objs);
		TestEqual(TEXT("partial -> 0 objects"), Objs.Num(), 0);
		TestEqual(TEXT("partial -> 0 consumed"), Consumed, 0);
	}
	{
		// One complete object followed by a partial: extract the first, leave the tail unconsumed.
		TArray<FString> Objs;
		const int32 Consumed = UGSProConnectDriver::ExtractJsonObjects(TEXT("{\"a\":1}{\"b\":"), Objs);
		TestEqual(TEXT("complete+partial -> 1 object"), Objs.Num(), 1);
		TestEqual(TEXT("consumed only the complete one"), Consumed, 7);
	}
	return true;
}

// --- springbok lowercase "Backspin" is read as backspin --------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimGSProBackspinCasingTest, "Golfsim.GSProConnect.BackspinCasing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimGSProBackspinCasingTest::RunTest(const FString& /*Parameters*/)
{
	// springbok BallData omits TotalSpin's authority and uses "Backspin"/"SideSpin"; no TotalSpin here,
	// so the measured-components path must pick up the lowercase key.
	const FString Json = TEXT("{\"BallData\":{\"Speed\":150.0,\"Backspin\":3000.0,\"SideSpin\":-250.0,\"VLA\":12.0},")
		TEXT("\"ShotDataOptions\":{\"ContainsBallData\":true,\"ContainsClubData\":false,\"IsHeartBeat\":false}}");

	FShotTakenEvent Out;
	bool bSpinEstimated = true;
	const bool bOk = UGSProConnectDriver::ParseShot(Json, Out, bSpinEstimated);

	TestTrue(TEXT("parse succeeded"), bOk);
	TestFalse(TEXT("measured, not estimated"), bSpinEstimated);
	TestTrue(TEXT("lowercase Backspin read"), FMath::IsNearlyEqual(Out.BackspinRpm, 3000.0, 0.5));
	TestTrue(TEXT("sidespin used"), FMath::IsNearlyEqual(Out.SidespinRpm, -250.0, 0.5));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
