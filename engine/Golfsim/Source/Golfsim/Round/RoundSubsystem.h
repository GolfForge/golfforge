// URoundSubsystem (GOL-116) -- the brain of the GOL-112 single-player flow.
//
// Owns the FRoundState (current hole, stroke count, per-hole tally) and publishes the four
// round events from GOL-115 (round.start / hole.start / hole.complete / round.complete).
// Subscribes to session.shot_outcome on the EventBus so every shot increments the live hole's
// stroke count (with a Par+5 backstop in the pure namespace so a stuck player still finishes).
//
// Everything visual (pin actor, pawn teleport, scorecard widget) is a sibling ticket; this
// subsystem just publishes the events those siblings listen to. The HUD difficulty handshake
// is best-effort (calls into AGolfRangeHUD if one exists on the current level).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription, EEventKind
#include "Game/GolfDifficulty.h"
#include "Round/RoundState.h"
#include "RoundSubsystem.generated.h"

UCLASS()
class GOLFSIM_API URoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Resolve from any UObject with a world. Null outside a running game/PIE world. */
	static URoundSubsystem* Get(const UObject* WorldContext);

	/** Load hole.geojson, fill state, publish round.start + first hole.start. Logs + bails on load
	 *  failure (no events fired). Best-effort applies the difficulty to the range HUD if present.
	 *  If the current world's map doesn't match the course's level (e.g. caller is on the practice
	 *  range), stashes a pending-start and OpenLevels to the course; the actual StartRound runs
	 *  after PostLoadMapWithWorld fires on the new world. Course -> level mapping is hard-coded
	 *  (golfforge-demo-black -> GolfForgeDemoBlack); add entries as new courses land. */
	void StartRound(const FString& CourseId, EGolfDifficulty Difficulty);

	/** Course-id -> UE5 level name (mirror of CourseIdByLevelName in CourseSurfaceSubsystem.cpp).
	 *  Empty if the course has no level mapping yet -- caller should error out. */
	static FString LevelNameForCourse(const FString& CourseId);

	/** Manual hole-out trigger. GOL-119's auto-detector replaces the console caller; the API stays. */
	void OnHoleHoled();

	/** Abandon the active round. No round.complete published; consumers see the round simply stop. */
	void AbandonRound();

	bool IsActive() const { return State.bActive; }
	const GolfsimRound::FRoundState& GetState() const { return State; }

private:
	void OnShotOutcome(const FGolfEvent& Event);
	void ApplyStep(const GolfsimRound::FRoundStep& Step);
	void PublishRoundStart();
	void PublishHoleStart(int32 HoleRef);
	void PublishHoleComplete(int32 HoleRef, int32 Strokes, int32 ScoreVsPar);
	void PublishRoundComplete(int32 TotalStrokes, int32 TotalScoreVsPar, const TArray<int32>& PerHole);

	/** Trace XY to landscape Z; falls back to the spec's input Z if no world hit. World-only -- in
	 *  headless tests there's no world so the spec Z (0) survives. */
	FVector SnapToGround(const FVector& WorldXyz) const;

	/** Best-effort: forward Difficulty to AGolfRangeHUD::SetSwingDifficulty if a HUD exists. */
	void ApplyDifficultyToHUDIfPresent(EGolfDifficulty D);

	/** Handler bound to FCoreUObjectDelegates::PostLoadMapWithWorld in Initialize. Drives the
	 *  deferred StartRound when an OpenLevel was needed to reach the course's map. */
	void OnPostLoadMap(UWorld* LoadedWorld);

	GolfsimRound::FRoundState State;
	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	FGolfEventSubscription OutcomeSub;

	// Pending-start state: set by StartRound when an OpenLevel is required; consumed by
	// OnPostLoadMap once the target map is loaded and the new world is ready.
	bool bPendingStart = false;
	FString PendingCourseId;
	EGolfDifficulty PendingDifficulty = EGolfDifficulty::Easy;
	FDelegateHandle PostLoadMapHandle;
};
