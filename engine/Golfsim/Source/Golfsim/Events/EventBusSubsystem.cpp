#include "Events/EventBusSubsystem.h"

#include "Physics/BallFlightSolver.h"
#include "Physics/BallFlightTypes.h"
#include "Physics/GroundRoll.h"

#include "Engine/Engine.h"          // GEngine->GetWorldFromContextObject
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// --- FGolfEventBus (pure C++) ------------------------------------------------------------------

FGolfEventSubscription FGolfEventBus::Subscribe(EEventKind Kind, FGolfEventDelegate Callback)
{
	const int64 Id = NextId++;
	Channels.FindOrAdd(Kind).Add(FEntry{ Id, MoveTemp(Callback) });
	return FGolfEventSubscription{ Id, Kind };
}

void FGolfEventBus::Unsubscribe(const FGolfEventSubscription& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	if (TArray<FEntry>* Entries = Channels.Find(Handle.Kind))
	{
		Entries->RemoveAll([&Handle](const FEntry& E) { return E.Id == Handle.Id; });
	}
}

int32 FGolfEventBus::NumSubscribers(EEventKind Kind) const
{
	const TArray<FEntry>* Entries = Channels.Find(Kind);
	return Entries ? Entries->Num() : 0;
}

void FGolfEventBus::Dispatch(EEventKind Kind, const FGolfEvent& Event)
{
	const TArray<FEntry>* Entries = Channels.Find(Kind);
	if (!Entries || Entries->Num() == 0)
	{
		return;
	}
	// Snapshot so a subscriber may (un)subscribe or publish a follow-on event during dispatch
	// without reallocating the array we're iterating.
	TArray<FEntry> Snapshot = *Entries;
	for (const FEntry& E : Snapshot)
	{
		if (E.Callback)
		{
			E.Callback(Event);
		}
	}
}

// --- UEventBusSubsystem ------------------------------------------------------------------------

void UEventBusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IntegratorSub = EventBus.Subscribe(EEventKind::ShotTaken,
		[this](const FGolfEvent& Event) { OnShotTaken(Event); });

	UE_LOG(LogTemp, Display,
		TEXT("golfsim EventBus: subsystem initialized (integrator subscribed to shot.taken)"));
}

void UEventBusSubsystem::Deinitialize()
{
	EventBus.Unsubscribe(IntegratorSub);
	Super::Deinitialize();
}

UEventBusSubsystem* UEventBusSubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UEventBusSubsystem>();
		}
	}
	return nullptr;
}

void UEventBusSubsystem::OnShotTaken(const FGolfEvent& Event)
{
	if (Event.Kind != EEventKind::ShotTaken)
	{
		return;   // defensive: only ever wired to the ShotTaken channel
	}
	const FShotTakenEvent& Shot = static_cast<const FShotTakenEvent&>(Event);

	// shot.taken (the bus envelope) -> FShotInput (the solver's pure-SI struct).
	FShotInput In;
	In.BallSpeedMps   = Shot.BallSpeedMps;
	In.LaunchAngleDeg = Shot.LaunchAngleDeg;
	In.AzimuthDeg     = Shot.AzimuthDeg;
	In.BackspinRpm    = Shot.BackspinRpm;
	In.SidespinRpm    = Shot.SidespinRpm;
	In.SmashFactor    = Shot.SmashFactor;

	FBallTrajectory T = GolfBallFlight::Simulate(In);

	FShotOutcomeEvent Out;
	Out.Source         = TEXT("sim");
	Out.PlayerId       = Shot.PlayerId;
	Out.CarryM         = T.CarryM;
	Out.TotalM         = T.CarryM;           // default (no surface provider): total == carry
	Out.LateralOffsetM = T.LateralOffsetM;
	Out.FinalLie       = TEXT("unknown");    // default: surface unknown without a provider
	Out.bInHole        = false;

	// Ground interaction (GOL-9): if a surface provider is wired (the range registers one), classify
	// the landing lie, roll the ball out, and report total distance + the resting lie. The roll is
	// appended to the trajectory so the view replays the rollout. Pure hand-off at the landing instant.
	if (T.bValid && SurfaceProvider)
	{
		const EGolfLie LandingLie = SurfaceProvider(T.LandingPositionM);
		// GOL-109: putter-tagged shots override the per-surface coefficients with stimp-aware green
		// friction and stay single-surface (a putt scrapes on the green). The bounce loop self-skips
		// (Vv ~ 0) so the trajectory is roll-only.
		// GOL-39: full shots re-sample the surface as they roll, so coefficients change at boundaries
		// (fairway -> bunker on roll-out, green -> rough on overshoot) and green spin-back applies.
		const bool bIsPutt = Shot.Club.Equals(TEXT("Putter"), ESearchCase::IgnoreCase);
		// GOL-196/GOL-75: surface-normal source for the bounce reflection AND the roll fall-line break;
		// flat (0,0,1) when none is wired (range synthetic green -> putts stay straight).
		auto NormalProv = [this](const FVector& P) -> FVector
		{
			return GroundNormalProvider ? GroundNormalProvider(P) : FVector::UpVector;
		};
		// GOL-75: a putt scrapes at stimp (green) friction on any MOWN/SMOOTH surface (green, fairway,
		// tee) and grabs with the surface's own friction on PENAL surfaces (rough, bunker, etc.). A
		// putter stroke is a putt, so it must roll at stimp on the putting surface even though the lie
		// classifier doesn't reliably mark it Green (the range analytic classifier never returns Green,
		// so the green/fairway/tee bucket is what keeps range putts rolling instead of dying in ~1 yd).
		// The cross-surface roll re-samples per step, so a putt that runs OFF the green into rough/sand
		// slows/stops there (no more rocketing through the rough into a bunker). On a uniformly smooth
		// surface this telescopes to the old single-surface PutterSurfaceRoll rollout exactly; the
		// fall-line break still applies (the slope curves the heading).
		const double Stimp = UEventBusSubsystem::GreenStimpFt;
		auto PuttCoefs = [Stimp](EGolfLie L) -> FSurfaceRoll
		{
			switch (L)
			{
			case EGolfLie::Green:
			case EGolfLie::Fairway:
			case EGolfLie::Tee:
				return GolfBallFlight::PutterSurfaceRoll(Stimp);   // mown + smooth -> putt rolls
			case EGolfLie::Rough:
			{
				// A putt trickling off the green into the fringe/first cut should slow over a meter or
				// two, not a centimeter. Use a moderate friction -- NOT the full-shot rough grab (~0.65,
				// tuned for a ball plugging on a landing), which stops a putt almost instantly at the
				// boundary. Keep it a clean scrape (no bounce/spin) like a putt, just higher drag.
				FSurfaceRoll Fringe = GolfBallFlight::PutterSurfaceRoll(Stimp);
				Fringe.RollFriction = 0.25;   // ~4x green; tune to taste (a fast putt runs ~1-2 m into rough)
				return Fringe;
			}
			default:
				return GolfBallFlight::SurfaceRollFor(L);          // bunker/cartpath/etc grab/stop the putt
			}
		};
		const FGroundRollResult Roll = bIsPutt
			? GolfBallFlight::SimulateGroundRollCrossSurface(T, SurfaceProvider, PuttCoefs, NormalProv)
			: GolfBallFlight::SimulateGroundRollCrossSurface(T, SurfaceProvider, &GolfBallFlight::SurfaceRollFor, NormalProv);
		if (Roll.bValid)
		{
			Out.TotalM         = Roll.TotalDistanceM;
			Out.LateralOffsetM = Roll.RestPositionM.Y;
			Out.FinalLie       = LieToProtocol(SurfaceProvider(Roll.RestPositionM));
			T.Samples.Append(Roll.RollSamples);   // ball rolls out past the landing marker
		}
	}

	Out.Trajectory = T;                      // in-process render payload for the view (flight + roll)

	// Carry the source shot's launch metrics so the UI shows the current shot's club/speed/launch/spin
	// from the outcome alone (not a separately-stashed shot.taken, which would lag a shot behind).
	Out.Club           = Shot.Club;
	Out.BallSpeedMps   = Shot.BallSpeedMps;
	Out.LaunchAngleDeg = Shot.LaunchAngleDeg;
	Out.BackspinRpm    = Shot.BackspinRpm;
	Out.bSpinEstimated = Shot.bSpinEstimated;

	UE_LOG(LogTemp, Display,
		TEXT("golfsim EventBus: shot.taken(%s) -> session.shot_outcome carry=%.1fm total=%.1fm lie=%s valid=%d"),
		*Shot.Club, Out.CarryM, Out.TotalM, *Out.FinalLie, T.bValid ? 1 : 0);

	EventBus.Publish(Out);
}
