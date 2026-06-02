#include "Round/RoundSubsystem.h"

#include "Events/EventTypes.h"   // FRound/Hole events + EEventKind
#include "GolfRangeHUD.h"        // best-effort difficulty handshake
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"   // OpenLevel for course-load
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"   // FCoreUObjectDelegates

namespace
{
	// Tee/green/pin XY land at Z=0 from the pure-namespace loader. Trace down from well above the
	// landscape so we hit it from any reasonable elevation. Mirrors build_hole_markers.py:_ground_z.
	constexpr double TraceStartZCm = 200000.0;   // 2 km up; comfortably above any reasonable terrain
	constexpr double TraceEndZCm   = -200000.0;
}

void URoundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UEventBusSubsystem::StaticClass());

	if (UEventBusSubsystem* EBus = GetGameInstance()->GetSubsystem<UEventBusSubsystem>())
	{
		EventBusWeak = EBus;
		OutcomeSub = EBus->Subscribe(EEventKind::ShotOutcome,
			[this](const FGolfEvent& Event) { OnShotOutcome(Event); });
	}

	// Survive level transitions: when an OpenLevel call (from a deferred StartRound) finishes,
	// finish the round-start on the new world.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &URoundSubsystem::OnPostLoadMap);
}

void URoundSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (UEventBusSubsystem* EBus = EventBusWeak.Get())
	{
		EBus->Unsubscribe(OutcomeSub);
	}
	OutcomeSub = FGolfEventSubscription{};
	EventBusWeak = nullptr;
	Super::Deinitialize();
}

FString URoundSubsystem::LevelNameForCourse(const FString& CourseId)
{
	// Mirror of CourseIdByLevelName in Course/CourseSurfaceSubsystem.cpp. Hardcoded for now;
	// when the second course lands, hoist this table into a shared header (one source of truth).
	static const TMap<FString, FString> Map = {
		{ TEXT("golfforge-demo-black"), TEXT("GolfForgeDemoBlack") },
	};
	if (const FString* Found = Map.Find(CourseId)) { return *Found; }
	return FString();
}

URoundSubsystem* URoundSubsystem::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext) { return nullptr; }
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<URoundSubsystem>();
		}
	}
	return nullptr;
}

void URoundSubsystem::StartRound(const FString& CourseId, EGolfDifficulty Difficulty)
{
	// If the caller is on a different level than the course's map, defer + OpenLevel. The actual
	// StartRound logic runs again from OnPostLoadMap once the new world is up. Without this,
	// hole.start would publish XY coordinates that only resolve on the course's landscape -- the
	// pin would land at world Z=0 on the wrong map (effectively invisible).
	const FString TargetLevel = LevelNameForCourse(CourseId);
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && !TargetLevel.IsEmpty())
	{
		const FString CurrentLevel = FPaths::GetBaseFilename(World->GetMapName());
		// PIE wraps maps as `UEDPIE_<n>_<MapName>`; Contains() sidesteps the prefix.
		if (!CurrentLevel.Contains(TargetLevel))
		{
			UE_LOG(LogTemp, Display,
				TEXT("URoundSubsystem::StartRound: current map '%s' != target '%s'; loading target..."),
				*CurrentLevel, *TargetLevel);
			bPendingStart = true;
			PendingCourseId = CourseId;
			PendingDifficulty = Difficulty;
			UGameplayStatics::OpenLevel(World, FName(*TargetLevel));
			return;
		}
	}
	else if (TargetLevel.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("URoundSubsystem::StartRound: no level mapping for course '%s' (add to LevelNameForCourse)"),
			*CourseId);
		// Continue anyway -- caller might be running on the correct map already.
	}

	TArray<GolfsimRound::FHoleSpec> Schedule;
	FString Err;
	if (!GolfsimRound::LoadHoleSchedule(CourseId, Schedule, Err))
	{
		UE_LOG(LogTemp, Warning, TEXT("URoundSubsystem::StartRound: %s -> %s"), *CourseId, *Err);
		return;
	}

	const GolfsimRound::FRoundStep Step =
		GolfsimRound::StartRound(State, CourseId, Difficulty, MoveTemp(Schedule));

	ApplyDifficultyToHUDIfPresent(Difficulty);
	UE_LOG(LogTemp, Display,
		TEXT("URoundSubsystem::StartRound: course=%s difficulty=%d round=%s holes=%d"),
		*CourseId, (int32)Difficulty, *State.RoundId, State.Schedule.Num());

	ApplyStep(Step);
}

void URoundSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (!bPendingStart || !LoadedWorld) { return; }
	// Verify the loaded world is the one we asked for; ignore unrelated transitions.
	const FString MapName = FPaths::GetBaseFilename(LoadedWorld->GetMapName());
	const FString Target = LevelNameForCourse(PendingCourseId);
	if (Target.IsEmpty() || !MapName.Contains(Target))
	{
		return;
	}
	bPendingStart = false;
	const FString CourseId = PendingCourseId;
	const EGolfDifficulty Difficulty = PendingDifficulty;
	PendingCourseId.Empty();
	UE_LOG(LogTemp, Display,
		TEXT("URoundSubsystem::OnPostLoadMap: target map '%s' loaded; resuming StartRound"), *MapName);
	StartRound(CourseId, Difficulty);
}

void URoundSubsystem::OnHoleHoled()
{
	if (!State.bActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("URoundSubsystem::OnHoleHoled: no active round"));
		return;
	}
	const GolfsimRound::FRoundStep Step = GolfsimRound::OnHoleHoled(State);
	ApplyStep(Step);
}

void URoundSubsystem::AbandonRound()
{
	if (!State.bActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("URoundSubsystem::AbandonRound: no active round"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("URoundSubsystem::AbandonRound: round=%s hole_index=%d"),
		*State.RoundId, State.HoleIndex);
	GolfsimRound::AbandonRound(State);
}

void URoundSubsystem::OnShotOutcome(const FGolfEvent& Event)
{
	if (!State.bActive || Event.Kind != EEventKind::ShotOutcome) { return; }
	const GolfsimRound::FRoundStep Step = GolfsimRound::OnShotOutcome(State);
	ApplyStep(Step);   // empty unless the Par+5 backstop just tripped
}

void URoundSubsystem::ApplyStep(const GolfsimRound::FRoundStep& Step)
{
	if (Step.bRoundStart)    { PublishRoundStart(); }
	if (Step.bHoleStart)     { PublishHoleStart(Step.HoleRefForHoleStart); }
	if (Step.bHoleComplete)  { PublishHoleComplete(Step.HoleRefForHoleComplete, Step.StrokesForHoleComplete, Step.ScoreVsParForHoleComplete); }
	if (Step.bRoundComplete)
	{
		PublishRoundComplete(Step.TotalStrokesForRoundComplete, Step.TotalScoreVsParForRoundComplete,
			Step.PerHoleStrokesForRoundComplete);
	}
}

void URoundSubsystem::PublishRoundStart()
{
	UEventBusSubsystem* EBus = EventBusWeak.Get();
	if (!EBus) { return; }
	FRoundStartEvent E;
	E.Source     = TEXT("round-subsystem");
	E.PlayerId   = GolfsimEvents::LocalPlayerId();
	E.CourseId   = State.CourseId;
	E.RoundId    = State.RoundId;
	E.Difficulty = State.Difficulty;
	E.TotalHoles = State.Schedule.Num();
	EBus->Publish(E);
	UE_LOG(LogTemp, Display, TEXT("round.start: round=%s course=%s holes=%d"),
		*E.RoundId, *E.CourseId, E.TotalHoles);
}

void URoundSubsystem::PublishHoleStart(int32 HoleRef)
{
	UEventBusSubsystem* EBus = EventBusWeak.Get();
	if (!EBus || !State.Schedule.IsValidIndex(State.HoleIndex)) { return; }
	const GolfsimRound::FHoleSpec& H = State.Schedule[State.HoleIndex];

	FHoleStartEvent E;
	E.Source         = TEXT("round-subsystem");
	E.PlayerId       = GolfsimEvents::LocalPlayerId();
	E.RoundId        = State.RoundId;
	E.HoleRef        = H.Ref;
	E.Par            = H.Par;
	E.Handicap       = H.Handicap;
	E.TeeWorldLoc    = SnapToGround(H.TeeWorldLoc);
	E.GreenWorldLoc  = SnapToGround(H.GreenWorldLoc);
	E.PinWorldLoc    = SnapToGround(H.PinWorldLoc);
	EBus->Publish(E);

	UE_LOG(LogTemp, Display,
		TEXT("hole.start: hole=%d par=%d hcp=%d name=%s tee=(%.0f,%.0f,%.0f) green=(%.0f,%.0f,%.0f)"),
		H.Ref, H.Par, H.Handicap, *H.Name,
		E.TeeWorldLoc.X, E.TeeWorldLoc.Y, E.TeeWorldLoc.Z,
		E.GreenWorldLoc.X, E.GreenWorldLoc.Y, E.GreenWorldLoc.Z);
	(void)HoleRef;
}

void URoundSubsystem::PublishHoleComplete(int32 HoleRef, int32 Strokes, int32 ScoreVsPar)
{
	UEventBusSubsystem* EBus = EventBusWeak.Get();
	if (!EBus) { return; }
	FHoleCompleteEvent E;
	E.Source       = TEXT("round-subsystem");
	E.PlayerId     = GolfsimEvents::LocalPlayerId();
	E.RoundId      = State.RoundId;
	E.HoleRef      = HoleRef;
	E.StrokesUsed  = Strokes;
	E.ScoreVsPar   = ScoreVsPar;
	EBus->Publish(E);
	UE_LOG(LogTemp, Display, TEXT("hole.complete: hole=%d strokes=%d vs_par=%+d"),
		HoleRef, Strokes, ScoreVsPar);
}

void URoundSubsystem::PublishRoundComplete(int32 TotalStrokes, int32 TotalScoreVsPar, const TArray<int32>& PerHole)
{
	UEventBusSubsystem* EBus = EventBusWeak.Get();
	if (!EBus) { return; }
	FRoundCompleteEvent E;
	E.Source            = TEXT("round-subsystem");
	E.PlayerId          = GolfsimEvents::LocalPlayerId();
	E.RoundId           = State.RoundId;
	E.TotalStrokes      = TotalStrokes;
	E.TotalScoreVsPar   = TotalScoreVsPar;
	E.PerHoleStrokes    = PerHole;
	EBus->Publish(E);
	UE_LOG(LogTemp, Display, TEXT("round.complete: round=%s total=%d vs_par=%+d holes=%d"),
		*E.RoundId, TotalStrokes, TotalScoreVsPar, PerHole.Num());
}

FVector URoundSubsystem::SnapToGround(const FVector& WorldXyz) const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) { return WorldXyz; }
	const FVector Start(WorldXyz.X, WorldXyz.Y, TraceStartZCm);
	const FVector End  (WorldXyz.X, WorldXyz.Y, TraceEndZCm);
	FCollisionQueryParams P(SCENE_QUERY_STAT(GolfsimRoundGroundTrace), /*bTraceComplex=*/true);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, P))
	{
		return FVector(WorldXyz.X, WorldXyz.Y, Hit.ImpactPoint.Z);
	}
	return WorldXyz;   // off-landscape -- caller logs at the publish site
}

void URoundSubsystem::ApplyDifficultyToHUDIfPresent(EGolfDifficulty D)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) { return; }
	APlayerController* PC = World->GetFirstPlayerController();
	AGolfRangeHUD* HUD = PC ? Cast<AGolfRangeHUD>(PC->GetHUD()) : nullptr;
	if (!HUD)
	{
		UE_LOG(LogTemp, Display,
			TEXT("URoundSubsystem: no AGolfRangeHUD on this level -- difficulty stored on the round (in FRoundStartEvent) but not applied to swing meter; a course-level HUD/listener will pick it up at hole.start"));
		return;
	}
	HUD->SetSwingDifficulty(D);
}
