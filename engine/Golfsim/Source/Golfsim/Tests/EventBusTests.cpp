// Automation tests for the in-process EventBus (GOL-7). They exercise the pure-C++ FGolfEventBus
// directly -- no UGameInstance, no world, no RHI -- which is exactly why the bus is split out from
// the UGameInstanceSubsystem wrapper. Mirrors the BallFlight test style.
// Run headless: UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests Golfsim.EventBus; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Events/EventBusSubsystem.h"

#if WITH_AUTOMATION_TESTS

// --- Publish reaches a subscriber with the payload intact (the core round-trip) ----------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimEventBusRoundTripTest, "Golfsim.EventBus.PublishReachesSubscriber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimEventBusRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;

	int32 Hits = 0;
	FShotTakenEvent Received;
	Bus.Subscribe(EEventKind::ShotTaken,
		[&Hits, &Received](const FGolfEvent& Event)
		{
			++Hits;
			Received = static_cast<const FShotTakenEvent&>(Event);
		});

	FShotTakenEvent Shot;
	Shot.Source = TEXT("test");
	Shot.BallSpeedMps = 64.8;
	Shot.LaunchAngleDeg = 14.2;
	Shot.BackspinRpm = 6800.0;
	Shot.Club = TEXT("7-Iron");
	Bus.Publish(Shot);

	TestEqual(TEXT("subscriber fired exactly once"), Hits, 1);
	TestEqual(TEXT("envelope version stamped"), Received.V, 1);
	TestEqual(TEXT("kind discriminator preserved"), (int32)Received.Kind, (int32)EEventKind::ShotTaken);
	TestTrue(TEXT("club payload preserved"), Received.Club == TEXT("7-Iron"));
	TestTrue(TEXT("ball speed payload preserved"), FMath::IsNearlyEqual(Received.BallSpeedMps, 64.8));
	TestTrue(TEXT("launch angle payload preserved"), FMath::IsNearlyEqual(Received.LaunchAngleDeg, 14.2));
	TestTrue(TEXT("timestamp stamped"), Received.TsMs > 0);
	return true;
}

// --- A subscriber only hears its own channel ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimEventBusChannelIsolationTest, "Golfsim.EventBus.ChannelIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimEventBusChannelIsolationTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;

	int32 OutcomeHits = 0;
	Bus.Subscribe(EEventKind::ShotOutcome, [&OutcomeHits](const FGolfEvent&) { ++OutcomeHits; });

	FShotTakenEvent Shot;
	Shot.BallSpeedMps = 50.0;
	Bus.Publish(Shot);   // a different kind than the subscriber registered for

	TestEqual(TEXT("shot.outcome subscriber not called by shot.taken"), OutcomeHits, 0);
	return true;
}

// --- Unsubscribe stops delivery ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimEventBusUnsubscribeTest, "Golfsim.EventBus.UnsubscribeStopsDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimEventBusUnsubscribeTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;

	int32 Hits = 0;
	FGolfEventSubscription Sub = Bus.Subscribe(EEventKind::ShotTaken, [&Hits](const FGolfEvent&) { ++Hits; });

	FShotTakenEvent Shot;
	Shot.BallSpeedMps = 50.0;

	Bus.Publish(Shot);          // delivered
	Bus.Unsubscribe(Sub);
	Bus.Publish(Shot);          // not delivered

	TestEqual(TEXT("delivered once before unsubscribe, not after"), Hits, 1);
	TestEqual(TEXT("channel has no subscribers after unsubscribe"), Bus.NumSubscribers(EEventKind::ShotTaken), 0);
	return true;
}

// --- Round-event round-trips (GOL-115). One per envelope, asserts payload + Kind discriminator. --

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundStartRoundTripTest, "Golfsim.RoundEvents.RoundStartRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimRoundStartRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;
	int32 Hits = 0;
	FRoundStartEvent Received;
	Bus.Subscribe(EEventKind::RoundStart,
		[&Hits, &Received](const FGolfEvent& Event)
		{
			++Hits;
			Received = static_cast<const FRoundStartEvent&>(Event);
		});

	FRoundStartEvent E;
	E.Source = TEXT("test");
	E.CourseId = TEXT("golfforge-demo-black");
	E.RoundId = TEXT("11111111-2222-3333-4444-555555555555");
	E.Difficulty = EGolfDifficulty::Pro;
	E.TotalHoles = 18;
	Bus.Publish(E);

	TestEqual(TEXT("subscriber fired once"), Hits, 1);
	TestEqual(TEXT("kind = RoundStart"), (int32)Received.Kind, (int32)EEventKind::RoundStart);
	TestTrue(TEXT("course id"), Received.CourseId == TEXT("golfforge-demo-black"));
	TestTrue(TEXT("round id"), Received.RoundId == TEXT("11111111-2222-3333-4444-555555555555"));
	TestEqual(TEXT("difficulty"), (int32)Received.Difficulty, (int32)EGolfDifficulty::Pro);
	TestEqual(TEXT("total holes"), Received.TotalHoles, 18);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleStartRoundTripTest, "Golfsim.RoundEvents.HoleStartRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleStartRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;
	int32 Hits = 0;
	FHoleStartEvent Received;
	Bus.Subscribe(EEventKind::HoleStart,
		[&Hits, &Received](const FGolfEvent& Event)
		{
			++Hits;
			Received = static_cast<const FHoleStartEvent&>(Event);
		});

	FHoleStartEvent E;
	E.RoundId = TEXT("r-1");
	E.HoleRef = 7;
	E.Par = 4;
	E.Handicap = 3;
	E.TeeWorldLoc = FVector(100.0, 200.0, 30.0);
	E.GreenWorldLoc = FVector(40000.0, 800.0, 25.0);
	E.PinWorldLoc = FVector(40050.0, 820.0, 25.0);
	Bus.Publish(E);

	TestEqual(TEXT("subscriber fired once"), Hits, 1);
	TestEqual(TEXT("kind = HoleStart"), (int32)Received.Kind, (int32)EEventKind::HoleStart);
	TestEqual(TEXT("hole ref"), Received.HoleRef, 7);
	TestEqual(TEXT("par"), Received.Par, 4);
	TestEqual(TEXT("handicap"), Received.Handicap, 3);
	TestTrue(TEXT("tee loc"), Received.TeeWorldLoc.Equals(FVector(100.0, 200.0, 30.0)));
	TestTrue(TEXT("green loc"), Received.GreenWorldLoc.Equals(FVector(40000.0, 800.0, 25.0)));
	TestTrue(TEXT("pin loc"), Received.PinWorldLoc.Equals(FVector(40050.0, 820.0, 25.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimHoleCompleteRoundTripTest, "Golfsim.RoundEvents.HoleCompleteRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimHoleCompleteRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;
	int32 Hits = 0;
	FHoleCompleteEvent Received;
	Bus.Subscribe(EEventKind::HoleComplete,
		[&Hits, &Received](const FGolfEvent& Event)
		{
			++Hits;
			Received = static_cast<const FHoleCompleteEvent&>(Event);
		});

	FHoleCompleteEvent E;
	E.RoundId = TEXT("r-1");
	E.HoleRef = 9;
	E.StrokesUsed = 3;
	E.ScoreVsPar = -1;   // birdie on a par 4
	Bus.Publish(E);

	TestEqual(TEXT("subscriber fired once"), Hits, 1);
	TestEqual(TEXT("kind = HoleComplete"), (int32)Received.Kind, (int32)EEventKind::HoleComplete);
	TestEqual(TEXT("hole ref"), Received.HoleRef, 9);
	TestEqual(TEXT("strokes"), Received.StrokesUsed, 3);
	TestEqual(TEXT("score vs par"), Received.ScoreVsPar, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimRoundCompleteRoundTripTest, "Golfsim.RoundEvents.RoundCompleteRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimRoundCompleteRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;
	int32 Hits = 0;
	FRoundCompleteEvent Received;
	Bus.Subscribe(EEventKind::RoundComplete,
		[&Hits, &Received](const FGolfEvent& Event)
		{
			++Hits;
			Received = static_cast<const FRoundCompleteEvent&>(Event);
		});

	FRoundCompleteEvent E;
	E.RoundId = TEXT("r-1");
	E.TotalStrokes = 76;
	E.TotalScoreVsPar = 4;   // +4 on a par 72
	E.PerHoleStrokes = {4, 5, 3, 4, 4, 4, 5, 4, 4, 5, 4, 3, 4, 5, 4, 4, 5, 5};
	Bus.Publish(E);

	TestEqual(TEXT("subscriber fired once"), Hits, 1);
	TestEqual(TEXT("kind = RoundComplete"), (int32)Received.Kind, (int32)EEventKind::RoundComplete);
	TestEqual(TEXT("total strokes"), Received.TotalStrokes, 76);
	TestEqual(TEXT("vs par"), Received.TotalScoreVsPar, 4);
	TestEqual(TEXT("per-hole length"), Received.PerHoleStrokes.Num(), 18);
	int32 PerHoleSum = 0;
	for (int32 S : Received.PerHoleStrokes) { PerHoleSum += S; }
	TestEqual(TEXT("per-hole sum matches total"), PerHoleSum, 76);
	return true;
}

// --- A subscriber publishing a follow-on event mid-dispatch is safe (the integrator's pattern) -

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGolfsimEventBusNestedPublishTest, "Golfsim.EventBus.NestedPublishIsSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGolfsimEventBusNestedPublishTest::RunTest(const FString& /*Parameters*/)
{
	FGolfEventBus Bus;

	int32 OutcomeHits = 0;
	double SeenCarry = 0.0;
	Bus.Subscribe(EEventKind::ShotOutcome,
		[&OutcomeHits, &SeenCarry](const FGolfEvent& Event)
		{
			++OutcomeHits;
			SeenCarry = static_cast<const FShotOutcomeEvent&>(Event).CarryM;
		});

	// Mimic the real integrator: on shot.taken, publish a shot.outcome from within the handler.
	Bus.Subscribe(EEventKind::ShotTaken,
		[&Bus](const FGolfEvent& /*Event*/)
		{
			FShotOutcomeEvent Out;
			Out.CarryM = 150.0;
			Bus.Publish(Out);   // nested publish into a different channel
		});

	FShotTakenEvent Shot;
	Shot.BallSpeedMps = 55.0;
	Bus.Publish(Shot);

	TestEqual(TEXT("nested outcome delivered once"), OutcomeHits, 1);
	TestTrue(TEXT("nested outcome payload intact"), FMath::IsNearlyEqual(SeenCarry, 150.0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
