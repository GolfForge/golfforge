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
