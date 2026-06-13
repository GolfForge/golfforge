// In-world visualizer: animates a ball mesh along a precomputed FBallTrajectory and draws the arc.
// Knows nothing about the solver -- it just consumes a trajectory.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physics/BallFlightTypes.h"
#include "GolfBallActor.generated.h"

UCLASS(BlueprintType)
class GOLFSIM_API AGolfBallActor : public AActor
{
	GENERATED_BODY()

public:
	AGolfBallActor();

	/**
	 * Animate the ball along a trajectory. The launch origin and aim are taken from the actor's
	 * current transform (place/rotate the actor at the tee), so the SI +X "downrange" axis maps to
	 * the actor's forward vector. Plain C++ (FBallTrajectory is not a USTRUCT), not a UFUNCTION.
	 */
	void PlayTrajectory(const FBallTrajectory& InTrajectory);

	UFUNCTION(BlueprintCallable, Category = "Golfsim")
	void ResetAndReplay();

	// True while the ball is mid-flight (animating along the trajectory). Lets the HUD tick the
	// carry readout up in sync, then snap to the exact final once this goes false.
	bool IsPlaying() const { return bPlaying; }

	// GOL-203: animate the holed putt sinking into the cup (ease XY to the cup center while the
	// ball accelerates ~12 cm down) instead of teleporting. OnDone (optional) fires once the ball
	// has disappeared below the lip -- the HUD hangs the toast/score off it. Cancels any playback.
	void StartCupDrop(const FVector& CupCenterUU, TFunction<void()> OnDone = nullptr,
		float DurationSec = 0.35f);

	// GOL-203: putt styling for the tracer trail. Set by the HUD before the trajectory plays:
	// putts draw a thin pale ground line (the roll path IS the line); full shots keep the yellow arc.
	bool bPuttTracer = false;

	// Live downrange distance (launch-local +X), SI meters, at the current playback time -- grows from
	// ~0 at launch to the final carry. Source for the panel's counting-up carry number.
	float GetCurrentCarryMeters() const { return CurrentCarryMeters; }

	// SI meters -> Unreal units (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golfsim")
	float MetersToUU = 100.f;

	// Draw a persistent arc + landing marker so the flight is visible even when it's fast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golfsim")
	bool bDrawDebugArc = true;

	// 1.0 = real-time hang time; < 1 = slow motion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golfsim")
	float PlaybackSpeed = 1.f;

	UPROPERTY(VisibleAnywhere, Category = "Golfsim")
	UStaticMeshComponent* BallMesh;

	virtual void Tick(float DeltaSeconds) override;

	// Ball-center launch height above the traced ground so the ~6 cm-radius sphere mesh sits flush on
	// the turf. Single source of truth for the floor-trace consumers (range HUD spawn, console fire,
	// post-landing terrain snap). Matches the BallMesh scale of 0.12 on a 100 cm Engine sphere.
	static constexpr float BallRestHeightUU = 6.f;

private:
	FBallTrajectory Trajectory;
	FVector LaunchOriginUU = FVector::ZeroVector;
	FRotator LaunchRotation = FRotator::ZeroRotator;
	float ElapsedSeconds = 0.f;
	bool bPlaying = false;
	float CurrentCarryMeters = 0.f;              // live downrange distance, updated each Tick
	FVector PrevDrawPos = FVector::ZeroVector;   // last point of the growing tracer trail

	// GOL-203 cup-drop state (StartCupDrop). Runs in Tick when bCupDropping; mutually exclusive
	// with bPlaying (starting the drop cancels playback, a new trajectory cancels the drop).
	bool bCupDropping = false;
	float CupDropSeconds = 0.f;
	float CupDropDuration = 0.35f;
	FVector CupDropFrom = FVector::ZeroVector;
	FVector CupDropTo = FVector::ZeroVector;
	TFunction<void()> CupDropOnDone;

	// Per-sample landscape Z in world UU for Trajectory.Samples[LandingSampleIndex..end], populated
	// in PlayTrajectory via a downward line trace. Empty (or sentinel-filled) means "no terrain
	// info available", and SampleToWorld falls back to the flat GOL-9 mapping. GOL-110.
	TArray<float> PostLandingGroundCacheUU;

	// Map sample i into world space. For post-landing samples with valid cached ground Z, the world
	// Z is replaced with (ground + BallRestHeightUU + sample.LocalZ * MetersToUU) so the ball
	// tracks terrain while keeping its bounce arc. SI launch-local frame: +X downrange, +Y right,
	// +Z up.
	FVector SampleToWorld(const FTrajectorySample& Sample, int32 SampleIdx) const;
};
