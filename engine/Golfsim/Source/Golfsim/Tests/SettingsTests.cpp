// Automation tests for the pure GolfDisplay helpers (no world/RHI). Mirrors the EventBus test style.
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.Settings; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"   // GConfig + GGameUserSettingsIni for the pin-distance tests
#include "GolfDisplaySettings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsParseResTest, "Golfsim.Settings.ParseResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsParseResTest::RunTest(const FString&)
{
	TOptional<FIntPoint> Ok = GolfDisplay::ParseResolution(TEXT("1920x1080"));
	TestTrue(TEXT("valid parses"), Ok.IsSet());
	TestEqual(TEXT("width"), Ok.GetValue().X, 1920);
	TestEqual(TEXT("height"), Ok.GetValue().Y, 1080);
	TestTrue(TEXT("uppercase X parses"), GolfDisplay::ParseResolution(TEXT("1280X720")).IsSet());
	TestFalse(TEXT("garbage rejected"), GolfDisplay::ParseResolution(TEXT("abc")).IsSet());
	TestFalse(TEXT("missing height rejected"), GolfDisplay::ParseResolution(TEXT("1920x")).IsSet());
	TestFalse(TEXT("zero rejected"), GolfDisplay::ParseResolution(TEXT("0x0")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsClampTest, "Golfsim.Settings.Clamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsClampTest::RunTest(const FString&)
{
	TestEqual(TEXT("quality high clamps to 3"), GolfDisplay::ClampQualityLevel(9), 3);
	TestEqual(TEXT("quality low clamps to 0"), GolfDisplay::ClampQualityLevel(-2), 0);
	TestEqual(TEXT("quality in-range passes"), GolfDisplay::ClampQualityLevel(2), 2);
	TestEqual(TEXT("window mode clamps to 2"), GolfDisplay::ClampWindowModeIndex(5), 2);
	TestEqual(TEXT("window mode clamps to 0"), GolfDisplay::ClampWindowModeIndex(-1), 0);
	TestEqual(TEXT("upscaler clamps to 2"), GolfDisplay::ClampUpscalerIndex(9), 2);
	TestEqual(TEXT("upscaler clamps to 0"), GolfDisplay::ClampUpscalerIndex(-5), 0);
	TestTrue(TEXT("upscaler 0 is TSR"), GolfDisplay::UpscalerName(0) == TEXT("TSR"));
	TestTrue(TEXT("upscaler 1 is DLSS"), GolfDisplay::UpscalerName(1) == TEXT("DLSS"));
	TestTrue(TEXT("upscaler 2 is XeSS"), GolfDisplay::UpscalerName(2) == TEXT("XeSS"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsCreditsTest, "Golfsim.Settings.CreditsContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsCreditsTest::RunTest(const FString&)
{
	const FString C = GolfDisplay::CreditsText();
	TestTrue(TEXT("credits OSM"), C.Contains(TEXT("OpenStreetMap")));
	TestTrue(TEXT("credits Unreal"), C.Contains(TEXT("Unreal")));
	TestTrue(TEXT("credits USGS"), C.Contains(TEXT("USGS")));
	TestTrue(TEXT("credits license"), C.Contains(TEXT("AGPL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsUpscaleModeTest, "Golfsim.Settings.UpscaleModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsUpscaleModeTest::RunTest(const FString&)
{
	TestEqual(TEXT("TSR has 5 tiers"), GolfDisplay::UpscaleModeNames(0).Num(), 5);
	TestEqual(TEXT("DLSS has 5 tiers"), GolfDisplay::UpscaleModeNames(1).Num(), 5);
	TestEqual(TEXT("XeSS has 7 tiers"), GolfDisplay::UpscaleModeNames(2).Num(), 7);
	TestTrue(TEXT("DLSS DLAA is 100%"), FMath::IsNearlyEqual(GolfDisplay::ScreenPctForMode(1, 0), 100.f));
	TestEqual(TEXT("DLSS 50% -> Performance (3)"), GolfDisplay::ModeForScreenPct(1, 50.f), 3);
	TestTrue(TEXT("XeSS top tier is Native AA"), GolfDisplay::UpscaleModeNames(2)[0] == TEXT("Native AA"));
	return true;
}

// GOL-29 pin-distance persistence (GConfig round-trip via GGameUserSettingsIni).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsPinDistanceRoundTripTest, "Golfsim.Settings.PinDistanceRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsPinDistanceRoundTripTest::RunTest(const FString&)
{
	GolfDisplay::WritePinDistanceYd(175.0);
	TestTrue(TEXT("175 round-trips"), FMath::IsNearlyEqual(GolfDisplay::ReadPinDistanceYd(), 175.0));

	GolfDisplay::WritePinDistanceYd(50.0);
	TestTrue(TEXT("50 round-trips"), FMath::IsNearlyEqual(GolfDisplay::ReadPinDistanceYd(), 50.0));

	// Clamps: above the corridor and below zero.
	GolfDisplay::WritePinDistanceYd(500.0);
	TestTrue(TEXT("500 clamps to 400"), FMath::IsNearlyEqual(GolfDisplay::ReadPinDistanceYd(), 400.0));
	GolfDisplay::WritePinDistanceYd(-25.0);
	TestTrue(TEXT("-25 clamps to 0"), FMath::IsNearlyEqual(GolfDisplay::ReadPinDistanceYd(), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSettingsPinDistanceDefaultTest, "Golfsim.Settings.PinDistanceDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSettingsPinDistanceDefaultTest::RunTest(const FString&)
{
	// Wipe the key so ReadPinDistanceYd has to fall back to the default. Other tests in this file
	// may have written it; flush so the unset state is observable.
	if (GConfig)
	{
		GConfig->RemoveKey(TEXT("GolfForge.Range"), TEXT("PinDistanceYd"), GGameUserSettingsIni);
		GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
	}
	TestTrue(TEXT("missing key -> 150 yd default"),
		FMath::IsNearlyEqual(GolfDisplay::ReadPinDistanceYd(), 150.0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
