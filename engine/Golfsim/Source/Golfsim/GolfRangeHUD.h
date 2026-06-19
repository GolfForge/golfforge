// Practice-range shot tool, drawn in PIE. Q/E cycle through a 14-club preset bag
// (driver -> putter), Space hits a randomized shot with that club (realistic
// dispersion), and the selected club + last carry render bottom-right. Set this
// as the level GameMode's HUDClass. Reuses GolfBallFlight::Simulate +
// AGolfBallActor; the pure-C++ solver/visualizer are untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Engine/TimerHandle.h"             // GOL-73: FTimerHandle member (CTP pin respawn)
#include "GolfRangePanel.h"
#include "GolfDisplaySettings.h"   // FGolfDisplaySettings (ApplyDisplaySettings param)
#include "Events/EventBusSubsystem.h"   // FGolfEventSubscription member + EventBus access
#include "Input/KeyboardSwingComponent.h"   // GOL-67: Game-mode swing state
#include "Game/GolfDifficulty.h"
#include "Practice/PracticeMode.h"          // GOL-73: EPracticeMode + FCtpConfig (CTP practice)
#include "Round/RoundState.h"               // GOL-199: FGreenPolygon (putt-on-a-real-green target)
#include "UI/HoleMapProjection.h"           // GOL-203: FGreenSlopeGrid cache (break grid + minimap share it)
#include "GolfRangeHUD.generated.h"

class UManualShotDialog;
class USettingsMenu;
class UMainMenu;
class UShotHistoryPanel;
class UPreviousSessionsList;
class URoundSetupWizard;
class UPracticeSetup;
class UScorecardPanel;
class UCheatSheetPanel;
class USwingMeterWidget;
class URoundHud;
class ULeaveConfirmDialog;
class UHoleOutToast;
class AGolfBallActor;
class AGolfPinActor;
class AGreenBreakGridActor;
class ACameraActor;
class UTexture2D;
struct FManualShotValues;
enum class ELaunchMonitorStatus : uint8;   // Drivers/LaunchMonitorManager.h

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
	// GOL-167: smoothed FPS for the top-left perf readout (DrawHUD). EMA so the
	// number is readable instead of jittering every frame. Toggle: golf.ShowFPS.
	float SmoothedFps = 0.0f;

	// GOL-166: gate placed ambient SFX to gameplay (silent under menu/modals).
	void UpdateAmbientPlayback();
	bool bAmbientCached = false;
	bool bAmbientAudible = false;
	UPROPERTY(Transient) TArray<TObjectPtr<class UAudioComponent>> AmbientComponents;

	// Ball-strike one-shot SFX (CC0 SW_BallStrike), lazy-loaded on first shot; played in OnShotOutcome.
	UPROPERTY(Transient) TObjectPtr<class USoundBase> StrikeSound;

	// GOL-203: cup-drop one-shot SFX (CC-BY SW_CupDrop), lazy-loaded on first hole-out; played at
	// the cup for every hole-out flavor (putting drill sink, CTP putt-out, round hole.complete).
	UPROPERTY(Transient) TObjectPtr<class USoundBase> CupDropSound;
	void PlayCupDropSoundAt(const FVector& WorldLoc);

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

	// Manual-shot dialog (GOL-8): N toggles it (hiding the auto-fire panel); Fire routes here.
	// (Was M until GOL-209 gave M to the in-round hole map.)
	void ToggleManualDialog();
	void FireManualShot(const FManualShotValues& Values);

	// GOL-209: M cycles the in-round hole-map card chip -> card -> large (no-op on the range /
	// under menus).
	void ToggleHoleMap();

	// GOL-149: one-key HUD density cycle. V cycles Full -> Compact -> Hidden (persisted via GolfDisplay).
	// Full = telemetry card + control bar; Compact = the left metrics tower (bar hidden); Hidden = clean
	// (for screenshots). ApplyHudDensity (called every Tick, after UpdateInRoundHud) enforces the layout
	// over the per-tick visibility logic. Values match the persisted int (0/1/2).
	enum class EHudDensity : uint8 { Full = 0, Compact = 1, Hidden = 2 };
	void CycleHudDensity();
	void ApplyHudDensity();
	EHudDensity HudDensity = EHudDensity::Full;

	// Swing-meter polish: the verdict stays up through the flight/roll, then resets to the idle
	// dashes 2 s after the ball settles (whiffs reset 2 s after the whiff -- nothing flies). The
	// launch-monitor readout keeps the last shot -- only the meter cleans up.
	void ScheduleSwingMeterCleanup();

	// GOL-65: H toggles the in-range history view (current session only).
	void ToggleHistoryFromKey() { ToggleHistoryPanel(); }

	// Lazy mount + show/close for the two history widgets. The panel is single-session (caller fills
	// it with whichever session's data); the list is shown only from the main menu.
	void EnsureHistoryPanel();
	void EnsureSessionsList();
	void OpenHistoryForSession(const FString& SessionId, bool bFromList);
	void CloseHistoryPanel();
	void CloseSessionsList();

	// GOL-141 round-setup wizard (replaces GOL-121 pre-round picker). Same Ensure/Open/Close trio as
	// the sessions list; the wizard reports the chosen course via TFunction wired in EnsureMainMenu.
	void EnsureRoundSetup();
	void CloseRoundSetup();

	// GOL-73 practice-drill picker (Ensure/Close mirror the round-setup trio). Opened from the
	// Practice main-menu tile; Start enters CTP on the range, Back/Esc returns to the menu.
	void EnsurePracticeSetup();
	void ClosePracticeSetup();

	// GOL-120 end-of-round scorecard. Auto-opens when round.complete fires; "Back to Menu" loads
	// PracticeRange + shows main menu via the standard EnsureInputBound -> ShowMainMenu path.
	void EnsureScorecardPanel();
	void OpenScorecardForState(const TArray<int32>& Pars, const TArray<int32>& Strokes);
	void CloseScorecardPanel();

	// GOL-125: closes any open modal + abandons the active round + LoadMaps PracticeRange. Used by
	// the scorecard's Back-to-Menu AND the settings modal's new Main Menu button. The standard
	// post-load EnsureInputBound -> ShowMainMenu path takes over once the range world is up.
	void ReturnToMainMenu();

	// GOL-147: mode-aware leave/quit confirmation in front of every leave-to-menu path. The in-round
	// Menu button and the settings modal's Main Menu button call RequestLeaveToMainMenu (which picks
	// course vs range copy + gates input); Confirm routes to ReturnToMainMenu, Cancel/Esc/click-outside
	// dismiss via CloseLeaveDialog.
	void EnsureLeaveDialog();
	void RequestLeaveToMainMenu();
	void CloseLeaveDialog();

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

	// GOL-73: closest-to-pin practice. SetPracticeMode enters/leaves CTP (no-op in a round).
	// ApplyCtpConfig pushes the panel/console settings (display units = yards) into the subsystem;
	// the next pin uses them. Both are public so GolfsimConsole can drive headless validation.
	void SetPracticeMode(GolfsimPractice::EPracticeMode Mode);
	void ApplyCtpConfig(double MinYd, double MaxYd, bool bSideOffset, bool bPuttOut, double WithinYd);
	bool IsCtpActive() const { return CtpMode == GolfsimPractice::EPracticeMode::ClosestToPin; }

	// GOL-75: putting drill. Same SetPracticeMode entry point (Mode = Putting). ApplyPuttingConfig
	// pushes the panel's FEET sliders + scoring toggle into the subsystem; the next pin uses them.
	void ApplyPuttingConfig(double MinFt, double MaxFt, bool bHoleOut);
	bool IsPuttingActive() const { return CtpMode == GolfsimPractice::EPracticeMode::Putting; }

	// GOL-199: load <CourseId>'s map and putt on hole <HoleRef>'s real green. Stashes the target on
	// the (travel-surviving) practice subsystem, then OpenLevels the course; the new map's HUD enters
	// putting on that green. Course-agnostic -- works for any course we build with a green.geojson.
	void StartPuttingOnCourse(const FString& CourseId, int32 HoleRef);

	// GOL-117: true while URoundSubsystem reports an active single-player round. Range HUD uses
	// this to (a) skip the Tick respawn of its own pin, (b) early-return from ApplyPinDistance so
	// the spinner / console SetPin can't fight the URoundPinSubsystem-owned pin.
	bool RoundIsActive() const;

	// GOL-144: the in-round glass HUD (round panel + hole map). EnsureRoundHud lazily creates it;
	// UpdateInRoundHud (called from Tick) pushes live round + env data and toggles it vs the legacy panel.
	void EnsureRoundHud();
	void UpdateInRoundHud();

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
	// GOL-141 main-menu entry: open the round-setup wizard over the menu. The wizard reports back the
	// chosen course on Tee Off; HUD calls URoundSubsystem::StartRound + dismisses both.
	void OpenRoundSetup();
	// GOL-73 main-menu entry: open the practice-drill picker over the menu. Picking a drill + Start
	// dismisses the menu and enters that practice mode on the live range.
	void OpenPracticeSetup();
	// Tab cheat sheet (dev convenience; replaced when a real keybindings UI lands).
	void ToggleCheatSheet();

	// GOL-67: Game / Simulation mode. Game routes Space through the swing meter; Simulation
	// keeps the LM dropdown + the existing random-fire path. Default = Game (lower barrier).
	enum class EInputMode : uint8 { Game = 0, Simulation = 1 };
	void SetInputMode(EInputMode NewMode);

	// GOL-145: §6 launch-monitor gating. ApplyLaunchMonitorState derives the input mode from the
	// selected LM status (Online -> Simulation: the device owns the shot stream; everything else ->
	// Game: keyboard swing), repaints the control-bar status pill, and sets the telemetry primary-action
	// button label ("Sim shot" vs "Swing"). TriggerPrimaryAction routes that button by mode: Game ->
	// advance the swing meter (same as Space); Online -> ask the active driver to emit a shot.
	void ApplyLaunchMonitorState(ELaunchMonitorStatus Status, const FString& Name);
	// GOL-186: show/hide the "take your shot" ball-ready badge as the active LM reports armed/not.
	void ApplyLaunchMonitorReady(bool bReady);
	void TriggerPrimaryAction();

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
	bool InputGated() const { return bSettingsOpen || bMenuOpen || bHistoryOpen || bSessionsListOpen || bCheatOpen || bRoundSetupOpen || bPracticeSetupOpen || bScorecardOpen || bLeaveConfirmOpen; }

	// Follow camera: the "Camera" dropdown picks Tee (0, fixed pawn view) or Follow (1, chase cam).
	// SetCameraMode switches the view target; UpdateFollowCam (from Tick) chases the active ball and
	// parks on the resting ball until the next shot or a switch back to Tee.
	void SetCameraMode(int32 Index);
	// GOL-73: C key -> flip Tee <-> Follow (and sync the Camera dropdown). Gated like other gameplay keys.
	void ToggleCameraMode();
	void UpdateFollowCam(float DeltaSeconds);
	ACameraActor* GetOrSpawnFollowCam();

	// GOL-203 putt camera: a low, behind-the-ball framing down the putt line. Auto-engages whenever
	// a putt is addressed (course-green address + range re-putt standoffs) AND is pickable as the
	// dropdown's third "Putt" option; an explicit Tee/Follow pick (or C) wins until the next address
	// re-engages. Hides the first-person pawn model while active (it blocks the low view). Reuses
	// the find-or-spawned follow-cam actor.
	void EngagePuttCam(const FVector& BallWorld, const FVector& PinWorld);
	void ReleasePuttCam();        // restore the player's chosen camera mode (no-op if not engaged)
	void ClearPuttCamState();     // drop the state + un-hide the pawn (no camera blend)
	bool GetActivePinWorld(FVector& OutPin) const;   // round pin in a round, else the practice pin

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
	FGolfEventSubscription HoleCompleteSub;    // GOL-203: hole.complete -> score toast + cup SFX

	bool bManualOpen = false;            // is the manual-shot dialog showing (auto-fire panel hidden)
	bool bSettingsOpen = false;          // is the settings/credits modal showing (gameplay keys gated)
	bool bMenuOpen = false;              // is the startup main menu showing (gameplay keys gated)
	bool bHistoryOpen = false;           // GOL-65: shot-history table showing (gameplay keys gated)
	bool bHistoryFromList = false;       // GOL-65: opened from the previous-sessions list -> close returns to list
	bool bSessionsListOpen = false;      // GOL-65: previous-sessions list overlaying the main menu
	bool bRoundSetupOpen = false;        // GOL-141: round-setup wizard overlaying the main menu
	bool bPracticeSetupOpen = false;     // GOL-73: practice-drill picker overlaying the main menu
	bool bScorecardOpen = false;         // GOL-120: end-of-round scorecard modal
	bool bCheatOpen = false;             // Tab cheat sheet showing
	bool bLeaveConfirmOpen = false;      // GOL-147: leave/quit confirmation modal showing

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
	// Seconds the follow cam has sat parked on a settled ball with no orbit/new-shot. At
	// FollowIdleReturnSeconds the view snaps back to the Tee so the player is framed for the next shot.
	float FollowIdleSeconds = 0.0f;
	static constexpr float FollowIdleReturnSeconds = 3.0f;

	// GOL-203 putt-cam state. While active the follow-cam actor holds the low down-the-line pose
	// and Tick keeps the look-at tracking the rolling ball; any explicit camera pick clears it.
	bool bPuttCamActive = false;
	FVector PuttCamPinWorld = FVector::ZeroVector;

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
	UPROPERTY(Transient) TObjectPtr<URoundSetupWizard> RoundSetup;            // GOL-141
	UPROPERTY(Transient) TObjectPtr<UPracticeSetup> PracticeSetup;            // GOL-73 drill picker
	UPROPERTY(Transient) TObjectPtr<UScorecardPanel> Scorecard;               // GOL-120
	UPROPERTY(Transient) TObjectPtr<UCheatSheetPanel> CheatSheet;
	UPROPERTY(Transient) TObjectPtr<USwingMeterWidget> SwingMeter;            // GOL-67 (Game mode only)
	UPROPERTY(Transient) TObjectPtr<URoundHud> RoundHud;                      // GOL-144 in-round top HUD
	UPROPERTY(Transient) TObjectPtr<ULeaveConfirmDialog> LeaveDialog;         // GOL-147 leave/quit confirm
	UPROPERTY(Transient) TObjectPtr<UHoleOutToast> HoleOutToast;              // GOL-203 hole-out celebration
	void EnsureHoleOutToast();

	// GOL-209 hole-map caches. Texture + greens are per-course (null/empty results cached too, so a
	// course without minimap.png / green.geojson doesn't retry every hole); the static payload is
	// rebuilt when (CourseId, HoleIndex) changes -- which also covers the GOL-199 map-travel path.
	void PushHoleMapStatic(const GolfsimRound::FRoundState& S);
	UPROPERTY(Transient) TObjectPtr<UTexture2D> HoleMapTexture;
	FString HoleMapTextureCourseId;
	TArray<GolfsimRound::FGreenPolygon> HoleMapGreens;
	FString HoleMapGreensCourseId;
	FString HoleMapCourseId;                                                  // payload cache keys
	int32 HoleMapHoleIndex = INDEX_NONE;
	int32 HoleMapMatchedGreenIdx = INDEX_NONE;   // this hole's polygon in HoleMapGreens
	bool bHoleMapBallOnGreen = false;            // edge-tracked: drives the HOLE<->GREEN auto-tab
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
	double CurrentPinYd = 150.0;     // last applied; survives spinner -> SetPuttMode round-trips
	double CurrentPinSideYd = 0.0;   // GOL-73: active pin's lateral offset (yd, + = right); 0 = centerline

	// --- GOL-73 closest-to-pin practice mode -------------------------------------------------------
	// CtpMode is the active drill (CTP or, GOL-75, Putting). A practice shot pends scoring until the
	// ball settles (bCtpScorePending, resolved in Tick). After a scored attempt the pin holds ~2s
	// (bCtpAwaitingRespawn gates fires) then respawns. bCtpPutting marks an in-progress "play it out"
	// putt sequence (fires stay allowed). In CTP putts are NOT scored; in Putting mode every putt is a
	// counted stroke (PuttStrokeCount) and the attempt scores at hole-out. RNG + scoring live in
	// UPracticeModeSubsystem.
	GolfsimPractice::EPracticeMode CtpMode = GolfsimPractice::EPracticeMode::Free;
	bool bCtpScorePending = false;
	bool bCtpAwaitingRespawn = false;
	bool bCtpPutting = false;
	int32 PuttStrokeCount = 0;                               // GOL-75: putts taken on the current putting pin
	FTimerHandle CtpRespawnTimer;
	FTimerHandle SwingMeterCleanupTimer;   // GOL-209 polish: reset the meter to idle 2 s after the ball settles

	// GOL-199: course-green putting. When set, pins + the ball spawn ON a real green (PuttingTargetGreen,
	// world cm) instead of the flat range lane. Deferred entry: BeginPlay (post-OpenLevel) stashes the
	// target; DrawHUD fires EnterPuttingOnGreen once the pawn exists.
	bool bPuttingOnCourseGreen = false;
	GolfsimRound::FGreenPolygon PuttingTargetGreen;
	FRandomStream GreenStream;                               // green-point sampling RNG

	// GOL-199: cup capture -- a real-size cup is unputtable with settle-only holing, so a putt that
	// rolls over the cup slowly drops in. Tracked per putt from the live ball each Tick.
	bool bPuttCaptured = false;
	FVector PuttPrevBallLoc = FVector::ZeroVector;
	bool bPuttPrevValid = false;
	bool bPendingEnterGreen = false;
	FString PendingGreenCourseId;
	int32 PendingGreenHoleRef = 0;

	// GOL-203 in-world break grid: flowing-dot slope overlay on the active green. The slope grid +
	// corner heights are cached so the minimap (PushHoleMapStatic) and the overlay share one trace
	// burst; EnterPuttingOnGreen builds the same data for practice greens. G toggles (user override).
	TWeakObjectPtr<AGreenBreakGridActor> BreakGrid;
	GolfMap::FGreenSlopeGrid BreakGridSlope;
	TArray<double> BreakGridCorners;
	bool bBreakGridDataValid = false;
	bool bBreakGridUserHidden = false;
	bool BuildGreenSlopeGrid(const GolfsimRound::FGreenPolygon& Poly, GolfMap::FGreenSlopeGrid& OutGrid,
		TArray<double>& OutCornerHeightsCm) const;   // trace burst -> slope grid (shared minimap/overlay)
	void RebuildBreakGridActor();        // push the cached grid into the (find-or-spawned) actor
	void UpdateBreakGridVisibility();    // context show/hide: a green is active + not user-hidden
	void ToggleBreakGrid();              // G key flips the user-hidden override

	void EnterPuttingOnGreen(const FString& CourseId, int32 HoleRef);   // load green + start the drill on it
	void PlacePuttOnGreen();                                            // pick a new pin + address the ball on the green
	void AddressPuttAt(const FVector2D& BallXYcm, const FVector2D& PinXYcm);  // stand the pawn + ball AT the lie, facing the pin
	bool TraceGroundZ(double Xcm, double Ycm, double& OutZ) const;      // landscape Z under a world XY

	void SpawnNextCtpPin();                                  // pick + place the next pin via the subsystem
	void OnCtpShotSettled(AGolfBallActor* Ball);            // score the settled shot / drive the putt-out loop
	void OnPuttingShotSettled(AGolfBallActor* Ball);       // GOL-75: count the putt / score at hole-out / next pin
	void TeleportPawnForPutt(const FVector& BallWorld, const FVector& PinWorld);   // stand behind the lie, face the pin
	void EndCtpPuttSequence();                              // restore the tee pose after holing out
	void StartCtpRespawnTimer();                            // 2 s gap, then SpawnNextCtpPin
	void RefreshCtpScoreboard();                            // push session stats to the panel
};
