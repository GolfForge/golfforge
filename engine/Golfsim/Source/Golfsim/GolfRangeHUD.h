// Practice-range shot tool, drawn in PIE. Q/E cycle through a 14-club preset bag
// (driver -> putter), Space hits a randomized shot with that club (realistic
// dispersion), and the selected club + last carry render bottom-right. Set this
// as the level GameMode's HUDClass. Reuses GolfBallFlight::Simulate +
// AGolfBallActor; the pure-C++ solver/visualizer are untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GolfRangePanel.h"
#include "GolfDisplaySettings.h"   // FGolfDisplaySettings (ApplyDisplaySettings param)
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription member + EventBus access
#include "Input/KeyboardSwingComponent.h"   // GOL-67: Game-mode swing state
#include "Game/GolfDifficulty.h"
#include "GolfRangeHUD.generated.h"

class UManualShotDialog;
class USettingsMenu;
class UMainMenu;
class UShotHistoryPanel;
class UPreviousSessionsList;
class UPreRoundPicker;
class UScorecardPanel;
class UCheatSheetPanel;
class USwingMeterWidget;
class AGolfBallActor;
class AGolfPinActor;
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
	// GOL-123: ungated club switch used by SelectPutterIfAvailable + SelectClub. The public
	// SelectClub honors InputGated(); this path doesn't, so context-driven swaps land even when
	// a modal is open.
	void ApplyClubSelection(int32 Index);
	void FireRandom();
	// GOL-67: Space dispatcher. In Sim mode -> FireRandom (or no-op if an LM owns the stream).
	// In Game mode -> advance the swing state machine (Idle -> Power -> Accuracy -> publish).
	void OnSpaceForCurrentMode();

	// Shared publish half for both fire paths: stamp the launch transform (tee + aim), remember the
	// input-derived panel metrics, build + publish the shot.taken envelope through the bus.
	void PublishShotTaken(double BallMps, double LaunchDeg, double AzDeg, double BackRpm,
		double SideRpm, const FString& Club, const FString& Source);

	// Manual-shot dialog (GOL-8): M toggles it (hiding the auto-fire panel); Fire routes here.
	void ToggleManualDialog();
	void FireManualShot(const FManualShotValues& Values);

	// GOL-65: H toggles the in-range history view (current session only).
	void ToggleHistoryFromKey() { ToggleHistoryPanel(); }

	// Lazy mount + show/close for the two history widgets. The panel is single-session (caller fills
	// it with whichever session's data); the list is shown only from the main menu.
	void EnsureHistoryPanel();
	void EnsureSessionsList();
	void OpenHistoryForSession(const FString& SessionId, bool bFromList);
	void CloseHistoryPanel();
	void CloseSessionsList();

	// GOL-121 pre-round picker. Same Ensure/Open/Close trio as the sessions list; picker reports
	// chosen course + difficulty + name via TFunction wired in EnsureMainMenu.
	void EnsurePreRoundPicker();
	void ClosePreRoundPicker();

	// GOL-120 end-of-round scorecard. Auto-opens when round.complete fires; "Back to Menu" loads
	// PracticeRange + shows main menu via the standard EnsureInputBound -> ShowMainMenu path.
	void EnsureScorecardPanel();
	void OpenScorecardForState(const TArray<int32>& Pars, const TArray<int32>& Strokes);
	void CloseScorecardPanel();

	// GOL-125: closes any open modal + abandons the active round + LoadMaps PracticeRange. Used by
	// the scorecard's Back-to-Menu AND the settings modal's new Main Menu button. The standard
	// post-load EnsureInputBound -> ShowMainMenu path takes over once the range world is up.
	void ReturnToMainMenu();

	// Settings/credits menu (GOL-52/GOL-59): Esc/Tab toggles a centered modal; gameplay keys are gated
	// while it's open. ApplyDisplaySettings runs the chosen values through UGameUserSettings.
	void EnsureSettingsMenu();
	void ToggleSettingsMenu();
	// GOL-139: the bento Settings tile opens settings ABOVE the still-shown main menu (ToggleSettingsMenu
	// refuses to open while bMenuOpen). CloseSettings hides it and refocuses the menu (if up) or the game.
	void OpenSettingsOverMenu();
	void CloseSettings();
	void ApplyDisplaySettings(const FGolfDisplaySettings& S);
public:
	void OpenCreditsSection();   // golfsim.Credits entry point

	// GOL-29: range target pin. ApplyPinDistance places the pin actor at <Yards> downrange along the
	// corridor centerline (world +X from the tee, Y=0), ground-snapped via line trace. Idempotent --
	// the pin actor is find-or-spawned, never duplicated. Persisted so the value comes back next PIE.
	// SetPuttMode teleports the pawn onto the green (Putter selected, camera back to Tee view) on, or
	// restores the tee + previous club on off. Console + checkbox both call into these.
	void ApplyPinDistance(double Yards);
	void SetPuttMode(bool bEnabled);

	// GOL-117: true while URoundSubsystem reports an active single-player round. Range HUD uses
	// this to (a) skip the Tick respawn of its own pin, (b) early-return from ApplyPinDistance so
	// the spinner / console SetPin can't fight the URoundPinSubsystem-owned pin.
	bool RoundIsActive() const;

	// GOL-123: round flow calls this when the ball lies on the green. Finds the bag's "Putter"
	// entry and selects it; no-op if already on putter or if the bag has no Putter. Returns true
	// if the active club changed.
	bool SelectPutterIfAvailable();

	// GOL-123: round flow calls this on every hole.start so the player tees off with the driver
	// regardless of the club they finished the previous hole with. No-op if already on driver.
	bool SelectDriverIfNeeded();

	// GOL-123: re-anchor the active camera mode after a hole-transition teleport. Preserves the
	// player's Tee-vs-Follow preference; just re-runs SetCameraMode(current) so the follow cam
	// re-frames around the (now-moved) ball at the new tee instead of staying parked on the
	// previous green.
	void RefreshActiveCamera();

	// GOL-123: pull keyboard focus off whatever Slate widget grabbed it (typically a panel dropdown
	// after a selection). Without this, FInputModeGameAndUI leaves focus on the combo and Space /
	// Q / E get swallowed by the widget instead of firing shots or cycling clubs.
	void ReturnFocusToGame();

	// GOL-65: shot-history table for the live session (H key entry point).
	void ToggleHistoryPanel();
	// Main-menu entry: open the previous-sessions list over the menu. Selecting one opens the table.
	void OpenPreviousSessionsList();
	// GOL-121 main-menu entry: open the pre-round picker over the menu. Picker reports back the
	// chosen course + difficulty + name; HUD calls URoundSubsystem::StartRound + dismisses both.
	void OpenPreRoundPicker();
	// Tab cheat sheet (dev convenience; replaced when a real keybindings UI lands).
	void ToggleCheatSheet();

	// GOL-67: Game / Simulation mode. Game routes Space through the swing meter; Simulation
	// keeps the LM dropdown + the existing random-fire path. Default = Game (lower barrier).
	enum class EInputMode : uint8 { Game = 0, Simulation = 1 };
	void SetInputMode(EInputMode NewMode);

	// GOL-122: swap the swing-meter difficulty profile (Easy / Normal / Pro). Easy on entry; the
	// pre-round picker (GOL-121) will overwrite at round start. Console: golfsim.SetDifficulty.
	void SetSwingDifficulty(EGolfDifficulty D);
	EGolfDifficulty GetSwingDifficulty() const { return ActiveDifficulty; }

private:

	// Startup main menu (Range / Play Course [disabled] / Exit). Shown over the already-loaded range
	// behind a soft blur, so "Range" is an instant dismiss. Gameplay input is gated while it's up.
	void EnsureMainMenu();
	void ShowMainMenu();
	void DismissMainMenu();

	// Gameplay keys (club select, fire, aim) are dead while any modal is up (settings / main menu /
	// shot-history table / previous-sessions list / cheat sheet). The manual-shot dialog has its
	// own visibility flip but does not gate Q/E/Space.
	bool InputGated() const { return bSettingsOpen || bMenuOpen || bHistoryOpen || bSessionsListOpen || bCheatOpen || bPreRoundOpen || bScorecardOpen; }

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

	// Q/E cycle the bag (with wrap); definitions in the .cpp since they need GBagNum.
	void PrevClub();
	void NextClub();

	// Arrow-key aim: press/release toggle a held flag; Tick integrates the yaw.
	void TurnLeftPressed()   { if (!InputGated()) { bTurnLeft = true; } }
	void TurnLeftReleased()  { bTurnLeft = false; }
	void TurnRightPressed()  { if (!InputGated()) { bTurnRight = true; } }
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
	FGolfEventSubscription RoundCompleteSub;   // GOL-120: round.complete -> open scorecard

	bool bManualOpen = false;            // is the manual-shot dialog showing (auto-fire panel hidden)
	bool bSettingsOpen = false;          // is the settings/credits modal showing (gameplay keys gated)
	bool bMenuOpen = false;              // is the startup main menu showing (gameplay keys gated)
	bool bHistoryOpen = false;           // GOL-65: shot-history table showing (gameplay keys gated)
	bool bHistoryFromList = false;       // GOL-65: opened from the previous-sessions list -> close returns to list
	bool bSessionsListOpen = false;      // GOL-65: previous-sessions list overlaying the main menu
	bool bPreRoundOpen = false;          // GOL-121: pre-round picker overlaying the main menu
	bool bScorecardOpen = false;         // GOL-120: end-of-round scorecard modal
	bool bCheatOpen = false;             // Tab cheat sheet showing

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
	UPROPERTY(Transient) TObjectPtr<UMainMenu> MainMenu;
	UPROPERTY(Transient) TObjectPtr<UShotHistoryPanel> HistoryPanel;          // GOL-65
	UPROPERTY(Transient) TObjectPtr<UPreviousSessionsList> SessionsList;      // GOL-65
	UPROPERTY(Transient) TObjectPtr<UPreRoundPicker> PreRoundPicker;          // GOL-121
	UPROPERTY(Transient) TObjectPtr<UScorecardPanel> Scorecard;               // GOL-120
	UPROPERTY(Transient) TObjectPtr<UCheatSheetPanel> CheatSheet;
	UPROPERTY(Transient) TObjectPtr<USwingMeterWidget> SwingMeter;            // GOL-67 (Game mode only)
	EInputMode CurrentInputMode = EInputMode::Game;                           // default Game (renamed to avoid shadowing FInputModeGameAndUI local)
	GolfsimKeyboardSwing::FState SwingState;
	GolfsimKeyboardSwing::FConfig SwingConfig;
	EGolfDifficulty ActiveDifficulty = EGolfDifficulty::Easy;                 // GOL-122 -- mirrors SwingConfig.Profile

	// GOL-29 state. Pin is find-or-spawned (cached weakly so the PIE-spawned actor goes away with
	// the world). Putt mode caches the tee pawn pose + club so the toggle is reversible.
	TWeakObjectPtr<AGolfPinActor> Pin;
	bool bPuttMode = false;
	bool bTeeCached = false;
	FVector TeeOriginalLoc = FVector::ZeroVector;
	FRotator TeeOriginalRot = FRotator::ZeroRotator;
	int32 TeeOriginalClub = 0;
	double CurrentPinYd = 150.0;   // last applied; survives spinner -> SetPuttMode round-trips
};
