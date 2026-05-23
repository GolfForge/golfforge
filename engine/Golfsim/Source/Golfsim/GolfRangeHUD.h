// Practice-range shot tool, drawn in PIE. Number keys 1-6 select a club from a
// preset bag, Space hits a randomized shot with that club (realistic dispersion),
// and the selected club + last carry render bottom-right. Set this as the level
// GameMode's HUDClass. Reuses GolfBallFlight::Simulate + AGolfBallActor; the
// pure-C++ solver/visualizer are untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GolfRangePanel.h"
#include "GolfRangeHUD.generated.h"

UCLASS()
class GOLFSIM_API AGolfRangeHUD : public AHUD
{
	GENERATED_BODY()

public:
	AGolfRangeHUD();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

private:
	void EnsureInputBound();
	void SelectClub(int32 Index);
	void FireRandom();

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

	UPROPERTY(Transient) TObjectPtr<UGolfRangePanel> Panel;
};
