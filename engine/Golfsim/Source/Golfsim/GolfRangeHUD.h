// Practice-range shot tool, drawn in PIE. Number keys 1-6 select a club from a
// preset bag, Space hits a randomized shot with that club (realistic dispersion),
// and the selected club + last carry render bottom-right. Set this as the level
// GameMode's HUDClass. Reuses GolfBallFlight::Simulate + AGolfBallActor; the
// pure-C++ solver/visualizer are untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GolfRangePanel.h"
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription member + EventBus access
#include "GolfRangeHUD.generated.h"

class UManualShotDialog;
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

	// EventBus subscriber: the integrator publishes the resolved flight; we play the ball + refresh
	// the panel here. (GOL-7: the HUD publishes shot.taken instead of running the solver itself.)
	void OnShotOutcome(const FGolfEvent& Event);

	// BindKey needs parameterless members; thin shims onto SelectClub(i).
	void SelectClub0() { SelectClub(0); }
	void SelectClub1() { SelectClub(1); }
	void SelectClub2() { SelectClub(2); }
	void SelectClub3() { SelectClub(3); }
	void SelectClub4() { SelectClub(4); }
	void SelectClub5() { SelectClub(5); }

	// Arrow-key aim: press/release toggle a held flag; Tick integrates the yaw.
	void TurnLeftPressed()   { bTurnLeft = true; }
	void TurnLeftReleased()  { bTurnLeft = false; }
	void TurnRightPressed()  { bTurnRight = true; }
	void TurnRightReleased() { bTurnRight = false; }

	int32 ActiveClub = 0;
	bool bInputBound = false;
	bool bControlsLocked = false;   // move + mouse-look ignored once (range-only)
	bool bTurnLeft = false;
	bool bTurnRight = false;

	// Last shot's launch transform (tee + aim) and input-derived panel metrics, stashed at fire
	// time. The carry/offline arrive later with the outcome event, where the ball is played and the
	// panel refreshed in one update. One shot in flight at a time on the range, so "last" is exact.
	FVector LastLaunchLoc = FVector::ZeroVector;
	FRotator LastLaunchRot = FRotator::ZeroRotator;
	FString LastClubName;
	double LastSpeedMph = 0.0;
	double LastLaunchDeg = 0.0;
	double LastSpinRpm = 0.0;

	// Cached at subscribe time so EndPlay can Unsubscribe reliably -- resolving the subsystem via
	// world-context at teardown can return null, which would leave the dead subscriber in the bus.
	TWeakObjectPtr<UEventBusSubsystem> EventBusWeak;
	FGolfEventSubscription OutcomeSub;   // shot.outcome subscription; released in EndPlay

	bool bManualOpen = false;            // is the manual-shot dialog showing (auto-fire panel hidden)

	UPROPERTY(Transient) TObjectPtr<UGolfRangePanel> Panel;
	UPROPERTY(Transient) TObjectPtr<UManualShotDialog> ManualDialog;
};
