// Automation tests for the trajectory-Z snap math (GOL-110). The renderer functions are pure C++
// (no UWorld, no actor), so a synthetic provider drives the test cases. The cases below cover
// flat ground (the GOL-9 baseline), stepped terrain (the actual GOL-110 behavior), and a
// no-ground provider that exercises the fall-back to the launch-Z mapping.

#include "Misc/AutomationTest.h"
#include "Physics/BallRender.h"
#include "Physics/BallFlightTypes.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// Build a trivial trajectory: 2 flight samples (apex + descent) then 3 post-landing samples
	// (one roll, one bounce apex, one roll). LandingSampleIndex points at the first z=0 sample.
	// Sample.PositionMeters.X advances downrange in 5m steps so the world XY moves through the
	// provider's coordinate space; Y stays at 0.
	FBallTrajectory MakeMixedTrajectory()
	{
		FBallTrajectory T;
		// 2 flight samples at z > 0
		T.Samples.Add({ 0.5,  FVector( 5.0, 0.0, 10.0), FVector::ZeroVector });
		T.Samples.Add({ 1.0,  FVector(10.0, 0.0,  4.0), FVector::ZeroVector });
		// Landing: z = 0 at X = 15 m
		T.Samples.Add({ 1.5,  FVector(15.0, 0.0,  0.0), FVector::ZeroVector });
		T.LandingSampleIndex = 2;
		// Bounce apex: z = 1 m at X = 18 m
		T.Samples.Add({ 1.7,  FVector(18.0, 0.0,  1.0), FVector::ZeroVector });
		// Settled roll: z = 0 at X = 22 m
		T.Samples.Add({ 2.5,  FVector(22.0, 0.0,  0.0), FVector::ZeroVector });
		T.LandingPositionM = T.Samples[T.LandingSampleIndex].PositionMeters;
		T.bValid = true;
		return T;
	}

	constexpr float MtoUU = 100.f;        // SI m -> UE cm
	constexpr float BallRest = 6.f;       // ball-rest height (matches AGolfBallActor::BallRestHeightUU)
	constexpr double Tol = 0.01;          // FVector::Z is double; matching the comparand type

	bool NearlyEqualD(double A, double B) { return FMath::IsNearlyEqual(A, B, Tol); }
}

// --- Flat ground: cache returns launch-Z everywhere -> the snap is a no-op vs the GOL-9 mapping --

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallRenderFlatGroundTest,
	"Golfsim.BallRender.FlatGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallRenderFlatGroundTest::RunTest(const FString& /*Parameters*/)
{
	const FBallTrajectory T = MakeMixedTrajectory();
	const FVector LaunchUU(0.f, 0.f, 1000.f);   // launch at world Z = 10 m (1000 UU)
	const FRotator LaunchRot(0.f, 0.f, 0.f);

	// Provider: ground sits at (LaunchZ - BallRest) so a Sample.LocalZ=0 maps to LaunchZ.
	const float FlatGroundZ = LaunchUU.Z - BallRest;
	auto Provider = [FlatGroundZ](double, double) -> TOptional<float> { return FlatGroundZ; };

	TArray<float> Cache;
	GolfBallRender::CachePostLandingGroundZ(T, LaunchUU, LaunchRot, MtoUU, Provider, Cache);
	TestEqual(TEXT("cache covers all post-landing samples"),
		Cache.Num(), T.Samples.Num() - T.LandingSampleIndex);

	// Landing sample (LocalZ = 0) -> world Z == LaunchZ.
	const FVector Land = GolfBallRender::SampleToWorld(
		T.Samples[2], 2, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("landing sample world Z == LaunchZ"), NearlyEqualD(Land.Z, LaunchUU.Z));

	// Bounce apex (LocalZ = 1.0 m -> 100 UU above ground) -> world Z == LaunchZ + 100.
	const FVector Apex = GolfBallRender::SampleToWorld(
		T.Samples[3], 3, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("bounce apex world Z == LaunchZ + 100"), NearlyEqualD(Apex.Z, LaunchUU.Z + 100.0));

	// In-flight sample (idx 0) -> the cache is irrelevant; uses the legacy GOL-9 mapping
	// (LaunchZ + LocalZ*MtoUU = 1000 + 10*100 = 2000).
	const FVector Flight = GolfBallRender::SampleToWorld(
		T.Samples[0], 0, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("flight sample ignores cache"), NearlyEqualD(Flight.Z, LaunchUU.Z + 10.0 * MtoUU));

	return true;
}

// --- Stepped terrain: provider returns different Z based on world X --------------------------
// Roll samples on opposite sides of X = 0 should land on the corresponding ground level.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallRenderSteppedTerrainTest,
	"Golfsim.BallRender.SteppedTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallRenderSteppedTerrainTest::RunTest(const FString& /*Parameters*/)
{
	// Step the trajectory so landing (X=15m -> world 1500 UU) and bounce (X=18m -> world 1800 UU)
	// sit on the high side, while a fake pre-landing sample at X=-5 would sit on the low side.
	// All 3 post-landing samples here are on the high side; we verify the snap activates.
	const FBallTrajectory T = MakeMixedTrajectory();
	const FVector LaunchUU(0.f, 0.f, 1000.f);
	const FRotator LaunchRot(0.f, 0.f, 0.f);

	const float HighZ = 5000.f;   // hill: +50 m
	const float LowZ  = -2000.f;  // valley: -20 m
	auto Provider = [HighZ, LowZ](double X, double /*Y*/) -> TOptional<float>
	{
		return X > 0.0 ? TOptional<float>(HighZ) : TOptional<float>(LowZ);
	};

	TArray<float> Cache;
	GolfBallRender::CachePostLandingGroundZ(T, LaunchUU, LaunchRot, MtoUU, Provider, Cache);

	// Landing at X=15m -> world X = 1500 UU (positive) -> HighZ + BallRest.
	const FVector Land = GolfBallRender::SampleToWorld(
		T.Samples[2], 2, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("landing snaps to HighZ"), NearlyEqualD(Land.Z, HighZ + BallRest));

	// Bounce apex: HighZ + BallRest + 1m hop.
	const FVector Apex = GolfBallRender::SampleToWorld(
		T.Samples[3], 3, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("apex snaps to HighZ + 100"), NearlyEqualD(Apex.Z, HighZ + BallRest + 100.0));

	// Settled roll: HighZ + BallRest (LocalZ=0).
	const FVector Rest = GolfBallRender::SampleToWorld(
		T.Samples[4], 4, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("rest snaps to HighZ"), NearlyEqualD(Rest.Z, HighZ + BallRest));

	// In-flight samples now lerp their Z baseline from LaunchOriginUU.Z (at t=0) toward the
	// landing-terrain baseline (cache[0] + BallRest) so the touchdown transition is continuous
	// regardless of launch/landing terrain difference.
	// At idx 1 (LandingIdx=2 -> T = 0.5), baseline = LERP(1000, HighZ+BallRest, 0.5) =
	// (1000 + 5006) / 2 = 3003. World Z = baseline + Sample.localZ*MtoUU = 3003 + 400 = 3403.
	const FVector Flight = GolfBallRender::SampleToWorld(
		T.Samples[1], 1, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	const double ExpectedBaseline = 0.5 * (LaunchUU.Z + (HighZ + BallRest));
	TestTrue(TEXT("flight Z lerps baseline toward landing terrain"),
		NearlyEqualD(Flight.Z, ExpectedBaseline + 4.0 * MtoUU));

	// Idx 0 sample uses pure launch baseline (T = 0 -> LERP picks LaunchUU.Z).
	const FVector FlightStart = GolfBallRender::SampleToWorld(
		T.Samples[0], 0, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("flight idx 0 stays at launch baseline"),
		NearlyEqualD(FlightStart.Z, LaunchUU.Z + 10.0 * MtoUU));

	return true;
}

// --- No-ground fallback: empty/sentinel cache -> GOL-9 launch-Z mapping ----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimBallRenderNoGroundFallbackTest,
	"Golfsim.BallRender.NoGroundFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimBallRenderNoGroundFallbackTest::RunTest(const FString& /*Parameters*/)
{
	const FBallTrajectory T = MakeMixedTrajectory();
	const FVector LaunchUU(0.f, 0.f, 1000.f);
	const FRotator LaunchRot(0.f, 0.f, 0.f);

	// Provider always says "no ground" -- cache fills with NoGroundZUU sentinel.
	auto Provider = [](double, double) -> TOptional<float> { return {}; };
	TArray<float> Cache;
	GolfBallRender::CachePostLandingGroundZ(T, LaunchUU, LaunchRot, MtoUU, Provider, Cache);
	TestEqual(TEXT("cache still sized but sentinel-filled"),
		Cache.Num(), T.Samples.Num() - T.LandingSampleIndex);
	for (float V : Cache)
	{
		TestTrue(TEXT("each entry is sentinel"), V == GolfBallRender::NoGroundZUU);
	}

	// Landing sample falls back to legacy GOL-9 mapping (LaunchZ + LocalZ*MtoUU == LaunchZ).
	const FVector Land = GolfBallRender::SampleToWorld(
		T.Samples[2], 2, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, Cache);
	TestTrue(TEXT("landing falls back to LaunchZ"), NearlyEqualD(Land.Z, LaunchUU.Z));

	// Empty cache (degenerate: no provider ever ran) -- same fallback.
	TArray<float> EmptyCache;
	const FVector Empty = GolfBallRender::SampleToWorld(
		T.Samples[3], 3, T.LandingSampleIndex, LaunchUU, LaunchRot, MtoUU, BallRest, EmptyCache);
	TestTrue(TEXT("empty cache also falls back"), NearlyEqualD(Empty.Z, LaunchUU.Z + 1.0 * MtoUU));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
