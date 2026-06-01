// Automation tests for the course-side lie classifier (GOL-40). Headless: the sampler is
// pure C++, loads the demo course splatmaps from disk (no world, no RHI), and the test asserts
// hand-picked world-XY -> EGolfLie pairs. The fixtures were lifted by a one-shot Python audit
// (pipeline scratch script, not committed) that found pure-layer pixels in the actual shipped
// splat_*.png / layer_*.png files; if you ever re-cook the demo course at a different bbox or
// size, regenerate these or the test stops being a pinning test.
//
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.CourseSurface; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Physics/CourseSurface.h"
#include "Physics/GroundRoll.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	constexpr const TCHAR* DemoCourseId = TEXT("golfforge-demo-black");

	// Hand-picked from the live splat PNGs (size_px = 2017). Each point's higher-priority layers
	// are confirmed zero, so the sampler's priority order classifies them unambiguously.
	struct FCase
	{
		const TCHAR* Name;
		double Xm;
		double Ym;
		EGolfLie Expected;
	};

	static const FCase Cases[] = {
		{ TEXT("fairway-clean"), 382.0, -605.0, EGolfLie::Fairway },
		{ TEXT("green-clean"),   595.0, -489.0, EGolfLie::Green   },
		{ TEXT("bunker-clean"),  333.0, -605.0, EGolfLie::Bunker  },
		{ TEXT("tee-clean"),     532.0, -577.0, EGolfLie::Tee     },
		{ TEXT("rough-catchall"),-403.0, -403.0, EGolfLie::Rough  },
	};
}

// --- Off-landscape returns Unknown (no PNGs read, no load required) ---------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimCourseSurfaceUnloadedTest,
	"Golfsim.CourseSurface.UnloadedReturnsUnknown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimCourseSurfaceUnloadedTest::RunTest(const FString& /*Parameters*/)
{
	FCourseSurfaceSampler S;
	TestFalse(TEXT("a fresh sampler is not loaded"), S.IsValid());
	TestTrue(TEXT("unloaded sampler returns Unknown"), S.ClassifyAt(0.0, 0.0) == EGolfLie::Unknown);
	return true;
}

// --- Load + classify the demo course at known points ------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimCourseSurfaceClassifyTest,
	"Golfsim.CourseSurface.ClassifyDemoCoursePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimCourseSurfaceClassifyTest::RunTest(const FString& /*Parameters*/)
{
	FCourseSurfaceSampler S;
	if (!S.Load(DemoCourseId))
	{
		// Missing splatmaps on this machine is a hard failure: the demo course is committed to LFS.
		AddError(FString::Printf(TEXT("could not load %s splatmaps; is the courses/ tree present?"), DemoCourseId));
		return false;
	}
	TestTrue(TEXT("loaded sampler is valid"), S.IsValid());
	TestEqual(TEXT("size_px matches the committed 2017"), S.GetSizePx(), 2017);

	// Off-landscape: ±2000 m is well beyond the ±1008 m half-extent.
	TestTrue(TEXT("(2000,0) off-landscape -> Unknown"),    S.ClassifyAt(2000.0,   0.0) == EGolfLie::Unknown);
	TestTrue(TEXT("(0,-2000) off-landscape -> Unknown"),   S.ClassifyAt(   0.0,-2000.0) == EGolfLie::Unknown);
	TestTrue(TEXT("(-3000,3000) off-landscape -> Unknown"),S.ClassifyAt(-3000.0,3000.0) == EGolfLie::Unknown);

	// Hand-picked in-bounds fixtures.
	for (const FCase& C : Cases)
	{
		const EGolfLie Got = S.ClassifyAt(C.Xm, C.Ym);
		TestTrue(*FString::Printf(TEXT("%s: (%.1f, %.1f) -> expected=%s got=%s"),
			C.Name, C.Xm, C.Ym, *LieToProtocol(C.Expected), *LieToProtocol(Got)),
			Got == C.Expected);
	}

	// Determinism: classifying the same point twice agrees.
	TestTrue(TEXT("classify is deterministic"),
		S.ClassifyAt(Cases[0].Xm, Cases[0].Ym) == S.ClassifyAt(Cases[0].Xm, Cases[0].Ym));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
