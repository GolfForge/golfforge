// Automation tests for the pure GolfsimKeyboardSwing:: math (no world/RHI). GOL-67.
// Run: UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests Golfsim.KeyboardSwing; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Input/KeyboardSwingComponent.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	GolfsimKeyboardSwing::FClubPreset MakeSevenIron()
	{
		GolfsimKeyboardSwing::FClubPreset C;
		C.Name = TEXT("7-Iron");
		C.NominalSpeedMps = 55.0;
		C.LaunchDeg = 16.3;
		C.SpinRpm = 7097.0;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingTriangleWaveTest, "Golfsim.KeyboardSwing.TriangleWave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingTriangleWaveTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	TestTrue(TEXT("t=0 -> 0"),       FMath::IsNearlyEqual(TriangleWave(0.00, 1.0), 0.0, 1e-9));
	TestTrue(TEXT("t=0.25 -> 0.5"),  FMath::IsNearlyEqual(TriangleWave(0.25, 1.0), 0.5, 1e-9));
	TestTrue(TEXT("t=0.5 -> 1.0"),   FMath::IsNearlyEqual(TriangleWave(0.50, 1.0), 1.0, 1e-9));
	TestTrue(TEXT("t=0.75 -> 0.5"),  FMath::IsNearlyEqual(TriangleWave(0.75, 1.0), 0.5, 1e-9));
	TestTrue(TEXT("t=1.0 -> 0 (wrap)"), FMath::IsNearlyEqual(TriangleWave(1.00, 1.0), 0.0, 1e-9));
	TestTrue(TEXT("t=1.5 -> 1.0 (next cycle peak)"), FMath::IsNearlyEqual(TriangleWave(1.50, 1.0), 1.0, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingResolveSweetTest, "Golfsim.KeyboardSwing.ResolveSweetSpot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingResolveSweetTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	const FClubPreset C7 = MakeSevenIron();
	const FResolution R = ResolveShot(/*Power=*/1.0, /*Accuracy=*/0.85, C7);

	TestFalse(TEXT("full power not a whiff"), R.bWhiffed);
	TestTrue(TEXT("full power -> nominal speed"), FMath::IsNearlyEqual(R.BallSpeedMps, 55.0, 1e-9));
	TestTrue(TEXT("launch preserved"),            FMath::IsNearlyEqual(R.LaunchAngleDeg, 16.3, 1e-9));
	TestTrue(TEXT("spin scales with power=1"),    FMath::IsNearlyEqual(R.BackspinRpm, 7097.0, 1e-9));
	TestTrue(TEXT("sweet spot -> straight (az=0)"), FMath::IsNearlyEqual(R.AzimuthDeg, 0.0, 1e-9));
	TestTrue(TEXT("sweet spot -> no sidespin"),     FMath::IsNearlyEqual(R.SidespinRpm, 0.0, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingResolveAccuracyTest, "Golfsim.KeyboardSwing.ResolveAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingResolveAccuracyTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	const FClubPreset C7 = MakeSevenIron();

	// Below sweet: push right + fade spin.
	const FResolution Push = ResolveShot(1.0, /*Accuracy=*/0.50, C7);
	TestTrue(TEXT("below-sweet: az right (positive)"), Push.AzimuthDeg > 0.0);
	TestTrue(TEXT("below-sweet: sidespin fade (positive)"), Push.SidespinRpm > 0.0);

	// Above sweet: pull left + draw spin.
	const FResolution Pull = ResolveShot(1.0, /*Accuracy=*/0.95, C7);
	TestTrue(TEXT("above-sweet: az left (negative)"), Pull.AzimuthDeg < 0.0);
	TestTrue(TEXT("above-sweet: sidespin draw (negative)"), Pull.SidespinRpm < 0.0);

	// Far-low mishit: launch reduced.
	const FResolution Chop = ResolveShot(1.0, /*Accuracy=*/0.05, C7);
	TestTrue(TEXT("far-low mishit: launch reduced below nominal"), Chop.LaunchAngleDeg < 16.3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingWhiffTest, "Golfsim.KeyboardSwing.Whiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingWhiffTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	const FClubPreset C7 = MakeSevenIron();
	const FResolution R = ResolveShot(/*Power=*/0.05, /*Accuracy=*/0.85, C7);
	TestTrue(TEXT("low power -> whiff"), R.bWhiffed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingStateMachineTest, "Golfsim.KeyboardSwing.StateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingStateMachineTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	const FClubPreset C7 = MakeSevenIron();
	FState S;
	FConfig C;
	FResolution Out;

	// Press 1: idle -> power. Returns false (no shot yet).
	TestFalse(TEXT("press 1: no shot"), OnSpace(S, C, C7, Out));
	TestTrue(TEXT("state = power"), S.State == EState::Power);

	// Tick to the power-bar peak (period/2). The bar now oscillates 0 -> 1 -> 0 just like the
	// accuracy bar, so peak power happens at half the period and is the natural Space-2 target.
	Tick(S, C, C.PowerOscillationPeriodS * 0.5);
	TestTrue(TEXT("power at peak 1.0 at period/2"), FMath::IsNearlyEqual(S.Power, 1.0, 1e-6));

	// Press 2: power -> accuracy. Power locks at the peak value (1.0).
	TestFalse(TEXT("press 2: no shot"), OnSpace(S, C, C7, Out));
	TestTrue(TEXT("state = accuracy"), S.State == EState::Accuracy);
	TestTrue(TEXT("power locked at 1.0"), FMath::IsNearlyEqual(S.Power, 1.0, 1e-6));

	// Tick the accuracy bar to its peak (period/2 -> 1.0).
	Tick(S, C, C.AccuracyOscillationPeriodS * 0.5);
	TestTrue(TEXT("accuracy at peak 1.0"), FMath::IsNearlyEqual(S.Accuracy, 1.0, 1e-6));

	// Press 3: accuracy -> idle. Resolves a shot (accuracy 1.0 = pull / draw / mishit).
	TestTrue(TEXT("press 3: shot resolved"), OnSpace(S, C, C7, Out));
	TestTrue(TEXT("state back to idle"), S.State == EState::Idle);
	TestFalse(TEXT("not a whiff at full power"), Out.bWhiffed);
	TestTrue(TEXT("ball speed = nominal at peak power"), FMath::IsNearlyEqual(Out.BallSpeedMps, C7.NominalSpeedMps, 1e-6));
	TestTrue(TEXT("accuracy 1.0 -> pull (az negative)"), Out.AzimuthDeg < 0.0);
	return true;
}

// --- Difficulty profile presets (GOL-122) ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingDifficultyPresetsTest, "Golfsim.SwingDifficulty.PresetValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingDifficultyPresetsTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;

	const FSwingDifficultyProfile E = FSwingDifficultyProfile::Easy();
	TestTrue(TEXT("Easy.MaxAzimuthDeg = 6.0"),     FMath::IsNearlyEqual(E.MaxAzimuthDeg,     6.0,    1e-9));
	TestTrue(TEXT("Easy.SidespinPushRpm = 600"),   FMath::IsNearlyEqual(E.SidespinPushRpm,   600.0,  1e-9));
	TestTrue(TEXT("Easy.MishitLaunchScale = 0.80"),FMath::IsNearlyEqual(E.MishitLaunchScale, 0.80,   1e-9));
	TestTrue(TEXT("Easy.NormSpan = 0.40"),         FMath::IsNearlyEqual(E.NormSpan,          0.40,   1e-9));
	TestTrue(TEXT("Easy.GimmeRadiusFt = 8.0"),     FMath::IsNearlyEqual(E.GimmeRadiusFt,     8.0,    1e-9));

	const FSwingDifficultyProfile N = FSwingDifficultyProfile::Normal();
	TestTrue(TEXT("Normal.MaxAzimuthDeg = 8.0"),     FMath::IsNearlyEqual(N.MaxAzimuthDeg,     8.0,    1e-9));
	TestTrue(TEXT("Normal.SidespinPushRpm = 900"),   FMath::IsNearlyEqual(N.SidespinPushRpm,   900.0,  1e-9));
	TestTrue(TEXT("Normal.MishitLaunchScale = 0.65"),FMath::IsNearlyEqual(N.MishitLaunchScale, 0.65,   1e-9));
	TestTrue(TEXT("Normal.NormSpan = 0.30"),         FMath::IsNearlyEqual(N.NormSpan,          0.30,   1e-9));
	TestTrue(TEXT("Normal.GimmeRadiusFt = 6.0"),     FMath::IsNearlyEqual(N.GimmeRadiusFt,     6.0,    1e-9));

	const FSwingDifficultyProfile P = FSwingDifficultyProfile::Pro();
	TestTrue(TEXT("Pro.MaxAzimuthDeg = 10.0"),    FMath::IsNearlyEqual(P.MaxAzimuthDeg,     10.0,   1e-9));
	TestTrue(TEXT("Pro.SidespinPushRpm = 1200"),  FMath::IsNearlyEqual(P.SidespinPushRpm,   1200.0, 1e-9));
	TestTrue(TEXT("Pro.MishitLaunchScale = 0.55"),FMath::IsNearlyEqual(P.MishitLaunchScale, 0.55,   1e-9));
	TestTrue(TEXT("Pro.NormSpan = 0.20"),         FMath::IsNearlyEqual(P.NormSpan,          0.20,   1e-9));
	TestTrue(TEXT("Pro.GimmeRadiusFt = 3.0"),     FMath::IsNearlyEqual(P.GimmeRadiusFt,     3.0,    1e-9));

	// For() dispatch matches the named factories.
	const FSwingDifficultyProfile ForEasy   = FSwingDifficultyProfile::For(EGolfDifficulty::Easy);
	const FSwingDifficultyProfile ForNormal = FSwingDifficultyProfile::For(EGolfDifficulty::Normal);
	const FSwingDifficultyProfile ForPro    = FSwingDifficultyProfile::For(EGolfDifficulty::Pro);
	TestTrue(TEXT("For(Easy) == Easy()"),     FMath::IsNearlyEqual(ForEasy.MaxAzimuthDeg,   E.MaxAzimuthDeg, 1e-9));
	TestTrue(TEXT("For(Normal) == Normal()"), FMath::IsNearlyEqual(ForNormal.MaxAzimuthDeg, N.MaxAzimuthDeg, 1e-9));
	TestTrue(TEXT("For(Pro) == Pro()"),       FMath::IsNearlyEqual(ForPro.MaxAzimuthDeg,    P.MaxAzimuthDeg, 1e-9));
	return true;
}

// The promise on GOL-122's Done-when: Pro punishes the same below-sweet swing harder than Easy.
// Same club + same Power + same below-sweet Accuracy -> Pro has larger |Azimuth| AND |Sidespin|.
// (Note: at Accuracy=0.50 with SweetSpotLow=0.80, Below=0.30/NormSpan. Easy NormSpan=0.40 clamps
// to 0.75 of the ramp; Pro NormSpan=0.20 clamps to 1.0 of the ramp AND has the higher MaxAzimuth
// + SidespinPushRpm. Both effects compound -> Pro penalty strictly larger.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimSwingDifficultyHarderProfileTest, "Golfsim.SwingDifficulty.HarderProfileHarderPenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGolfsimSwingDifficultyHarderProfileTest::RunTest(const FString&)
{
	using namespace GolfsimKeyboardSwing;
	const FClubPreset C7 = MakeSevenIron();

	FConfig EasyCfg;     EasyCfg.Profile     = FSwingDifficultyProfile::Easy();
	FConfig ProCfg;      ProCfg.Profile      = FSwingDifficultyProfile::Pro();

	const FResolution Easy = ResolveShot(/*Power=*/1.0, /*Accuracy=*/0.50, C7, EasyCfg);
	const FResolution Pro  = ResolveShot(/*Power=*/1.0, /*Accuracy=*/0.50, C7, ProCfg);

	TestTrue(TEXT("Pro |Azimuth| > Easy |Azimuth| for the same below-sweet swing"),
		FMath::Abs(Pro.AzimuthDeg) > FMath::Abs(Easy.AzimuthDeg));
	TestTrue(TEXT("Pro |Sidespin| > Easy |Sidespin| for the same below-sweet swing"),
		FMath::Abs(Pro.SidespinRpm) > FMath::Abs(Easy.SidespinRpm));

	// Sweet-spot strikes stay straight under every profile (sweet-spot is not a tuning knob).
	const FResolution EasySweet = ResolveShot(1.0, 0.85, C7, EasyCfg);
	const FResolution ProSweet  = ResolveShot(1.0, 0.85, C7, ProCfg);
	TestTrue(TEXT("Easy sweet -> straight"), FMath::IsNearlyZero(EasySweet.AzimuthDeg, 1e-9));
	TestTrue(TEXT("Pro sweet -> straight"),  FMath::IsNearlyZero(ProSweet.AzimuthDeg,  1e-9));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
