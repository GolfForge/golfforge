// Practice-range shot tool, drawn in PIE. Number keys 1-6 select a club from a
// preset bag, Space hits a randomized shot with that club (realistic dispersion),
// and the selected club + last carry render bottom-right. Set this as the level
// GameMode's HUDClass. Reuses GolfBallFlight::Simulate + AGolfBallActor; the
// pure-C++ solver/visualizer are untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GolfRangePanel.h"
#include "GolfDisplaySettings.h"   // FGolfDisplaySettings (ApplyDisplaySettings param)
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription member + EventBus access
#include "GolfRangeHUD.generated.h"

class UManualShotDialog;
class USettingsMenu;
class AGolfBallActor;
class ACameraActor;
struct FManualShotValues;

UCLASS()
class GOLFSIM_API AGolfRangeHUD : public AHUD
{
	GENERATED_BODY()

public:
	AGolfRangeHUD();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

private:
	void EnsureInputBound();
	void SelectClub(int32 Index);
	void FireRandom();

	// Shared publish half for both fire paths: stamp the launch transform (tee + aim), remember the
	// input-derived panel metrics, build + publish the shot.taken envelope through the bus.
	void PublishShotTaken(double BallMps, double LaunchDeg, double AzDeg, double BackRpm,
		double SideRpm, const FString& Club, const FString& Source);

	// Manual-shot dialog (GOL-8): M toggles it (hiding the auto-fire panel); Fire routes here.
	void ToggleManualDialog();
	void FireManualShot(const FManualShotValues& Values);

	// Settings/credits menu (GOL-52/GOL-59): Esc/Tab toggles a centered modal; gameplay keys are gated
	// while it's open. ApplyDisplaySettings runs the chosen values through UGameUserSettings.
	void EnsureSettingsMenu();
	void ToggleSettingsMenu();
	void ApplyDisplaySettings(const FGolfDisplaySettings& S);
public:
	void OpenCreditsSection();   // golfsim.Credits entry point
private:

	// Follow camera: the "Camera" dropdown picks Tee (0, fixed pawn view) or Follow (1, chase cam).
	// SetCameraMode switches the view target; UpdateFollowCam (from Tick) chases the active ball and
	// parks on the resting ball until the next shot or a switch back to Tee.
	void SetCameraMode(int32 Index);
	void UpdateFollowCam(float DeltaSeconds);
	ACameraActor* GetOrSpawnFollowCam();

	// Follow-cam orbit: hold right mouse + drag to circle the camera around the ball (Follow mode only).
	void OrbitPressed();
	void OrbitReleased();
	void OnOrbitYaw(float Value);    // MouseX axis delta (accumulated while orbiting)
	void OnOrbitPitch(float Value);  // MouseY axis delta

	// EventBus subscriber: ShotOutcome plays the ball + refreshes the panel. The outcome carries the
	// source shot's launch metrics (club/speed/launch/spin), so the panel reads everything from it --
	// no separate shot.taken stash, which would lag a shot behind (integrator publishes mid-dispatch).
	void OnShotOutcome(const FGolfEvent& Event);

	// BindKey needs parameterless members; thin shims onto SelectClub(i).
	void SelectClub0() { SelectClub(0); }
	void SelectClub1() { SelectClub(1); }
	void SelectClub2() { SelectClub(2); }
	void SelectClub3() { SelectClub(3); }
	void SelectClub4() { SelectClub(4); }
	void SelectClub5() { SelectClub(5); }

	// Arrow-key aim: press/release toggle a held flag; Tick integrates the yaw.
	void TurnLeftPressed()   { if (!bSettingsOpen) { bTurnLeft = true; } }
	void TurnLeftReleased()  { bTurnLeft = false; }
	void TurnRightPressed()  { if (!bSettingsOpen) { bTurnRight = true; } }
	void TurnRightReleased() { bTurnRight = false; }

	int32 ActiveClub = 0;
	bool bInputBound = false;
	bool bControlsLocked = false;   // move + mouse-look ignored once (range-only)
	bool bTurnLeft = false;
	bool bTurnRight = false;

	// Cached at subscribe time so EndPlay can Unsubscribe reliably -- resolving the subsystem via
	// world-context at teardown can return null, which would leave the dead subscriber in the bus.
	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	FGolfEventSubscription OutcomeSub;   // shot.outcome subscription; released in EndPlay

	bool bManualOpen = false;            // is the manual-shot dialog showing (auto-fire panel hidden)
	bool bSettingsOpen = false;          // is the settings/credits modal showing (gameplay keys gated)

	// Carry counts up during flight. On shot.outcome the static metrics + final carry/offline are
	// cached; Tick then pushes the in-flight ball's live downrange distance into the panel's Carry
	// each frame until the ball lands, then snaps to the exact AnimTargetCarryYd.
	bool bCarryAnimating = false;
	bool bAnimSpinEstimated = false;
	TWeakObjectPtr<AGolfBallActor> AnimBall;
	FString AnimClub;
	double AnimSpeedMph = 0.0;
	double AnimLaunchDeg = 0.0;
	double AnimSpinRpm = 0.0;
	double AnimOfflineYd = 0.0;
	double AnimTargetCarryYd = 0.0;
	double AnimTargetTotalYd = 0.0;   // carry + ground roll; Total counts up through the rollout

	// Follow-cam state. FollowCam is a find-or-spawned ACameraActor used as the view target while in
	// Follow mode; FollowDownrangeDir is the launch aim captured at fire time (so a curving ball doesn't
	// swing the chase around). bFollowChasing = tracking a live ball; bFollowParked = frozen on the rest.
	bool bFollowCam = false;
	bool bFollowChasing = false;
	bool bFollowParked = false;
	FVector FollowDownrangeDir = FVector::ForwardVector;
	TWeakObjectPtr<ACameraActor> FollowCam;

	// Orbit state (right-mouse drag around the ball). Angles are spherical about the ball; pending
	// deltas accumulate from the mouse axes between Tick consumes.
	bool bOrbiting = false;
	float OrbitYawDeg = 0.f;
	float OrbitPitchDeg = 20.f;
	float OrbitDistUU = 1000.f;
	float PendingOrbitDX = 0.f;
	float PendingOrbitDY = 0.f;

	UPROPERTY(Transient) TObjectPtr<UGolfRangePanel> Panel;
	UPROPERTY(Transient) TObjectPtr<UManualShotDialog> ManualDialog;
	UPROPERTY(Transient) TObjectPtr<USettingsMenu> SettingsMenu;
};
