// In-round / range bottom HUD (UMG). Pure C++ UUserWidget: the whole tree is built in
// NativeOnInitialized, no WBP asset. GOL-145 rebuilt this from a top-right metrics grid into the
// spec's bottom layout (Build/handoff/screens/06 + 08): a telemetry readout (bottom-left -- club +
// ball/launch/spin + carry/total/offline, refreshed after each shot) and a full-width control bar
// (Club / Time / Sky / Camera / Launch Monitor dropdowns + an LM status pill). Shown on both the
// practice range and in a round (RoundHud's top panels layer on top in-round). AGolfRangeHUD owns
// the firing/bag/launch-monitor logic and drives this widget; the widget only displays state and
// reports the user's picks + the primary (Swing / Sim shot) action back via TFunction callbacks.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"   // ESelectInfo::Type for the OnSelectionChanged handlers
#include "GolfRangePanel.generated.h"

class UComboBoxString;
class UTextBlock;
class UButton;
class UBorder;
class USpinBox;
class UCheckBox;
class UHorizontalBox;
enum class ELaunchMonitorStatus : uint8;   // Drivers/LaunchMonitorManager.h

UCLASS()
class GOLFSIM_API UGolfRangePanel : public UUserWidget
{
	GENERATED_BODY()

public:
	// Populate the club dropdown. Owned by the HUD (single source of truth = its club bag) so the
	// names never drift from the firing presets. Call once after creation, before SetSelectedClubIndex.
	void SetClubOptions(const TArray<FString>& Names);

	// Refresh the telemetry readout with the last shot's display-unit values. bSpinEstimated marks the
	// spin as computed (not measured by the LM) -- shown as "5600 rpm est". TotalYd is carry + ground roll.
	void UpdateMetrics(const FString& Club, double SpeedMph, double LaunchDeg,
		double SpinRpm, double CarryYd, double TotalYd, double OfflineYd, bool bSpinEstimated = false);

	// §6 launch-monitor status pill (GOL-145). Online -> green "Monitor / {Name} · Live"; Sim -> amber
	// "Mode / Game · Keyboard"; Pairing/Off -> amber "Mode / {Name} · Pairing… | Offline". Driven by the
	// HUD from the active driver's status (which also flips the input mode + swing-meter visibility).
	void SetLaunchMonitorStatus(ELaunchMonitorStatus Status, const FString& Name);

	// GOL-186: show/hide the "take your shot" ball-ready badge in the control bar. The HUD toggles it
	// from the active launch monitor's armed state (shown only while a device is Online + armed).
	void SetLaunchMonitorReady(bool bReady);

	// Label of the primary-action button in the telemetry readout: "Swing" (game mode) / "Sim shot"
	// (a real LM owns the stream). The HUD sets it alongside SetLaunchMonitorStatus.
	void SetPrimaryActionLabel(const FString& Label);

	// GOL-73: push the CTP settings into the spinboxes/checkboxes (defaults at startup, or after a
	// console change). Values are in display units (yards). Suppress-guarded so it won't re-emit.
	void SetCtpConfigValues(double MinYd, double MaxYd, bool bSideOffset, bool bPuttOut, double WithinYd);

	// GOL-73: repaint the CTP scoreboard. The HUD formats per mode (yards for carry-only, strokes for
	// putt-out), so the panel just displays the strings + the shot count.
	void SetCtpScore(const FString& ThisStr, const FString& BestStr, const FString& AvgStr, int32 Shots);

	// GOL-73: show the active pin's distance (+ optional side offset) so the player knows the target.
	// SideYd: + = right of the centerline, - = left; |SideYd| < 1 shows distance only.
	void SetCtpPinInfo(double Yd, double SideYd);

	// GOL-73: show/hide the CTP settings cluster + scoreboard (visible only in Closest-to-Pin mode,
	// and never in a round). Mirrors SetRangeControlsVisible.
	void SetCtpControlsVisible(bool bVisible);

	// GOL-75: putting drill. Same scoreboard row as CTP (reused), a separate FEET min/max + scoring
	// toggle control row, and a feet pin readout. The HUD pushes config in feet; suppress-guarded.
	void SetPuttingControlsVisible(bool bVisible);
	void SetPuttingConfigValues(double MinFt, double MaxFt, bool bHoleOut);
	void SetPuttingPinInfo(double Ft);

	// Populate the Time-of-day / Sky / Camera / Launch-monitor dropdowns. The HUD owns the option lists.
	void SetTimeOptions(const TArray<FString>& Names);
	void SetSkyOptions(const TArray<FString>& Names);
	void SetCameraOptions(const TArray<FString>& Names);
	// Launch-monitor dropdown: the HUD passes "Simulated (no device)" first (index 0 = keyboard/game),
	// then each available driver's display name (index i = the (i-1)th driver -> select + connect).
	void SetLaunchMonitorOptions(const TArray<FString>& Names);

	// Sync a dropdown to the active index (e.g. after a 1-6 key press, or to startup defaults).
	// Guarded so it does not re-enter the selection callback.
	void SetSelectedClubIndex(int32 Index);
	void SetSelectedTimeIndex(int32 Index);
	void SetSelectedSkyIndex(int32 Index);
	void SetSelectedLaunchMonitorIndex(int32 Index);
	void SetSelectedCameraIndex(int32 Index);

	// GOL-123: refresh just the readout's club headline (called by ApplyClubSelection so it stays in
	// sync with the dropdown when an auto-swap fires before a shot lands).
	void SetMetricClubName(const FString& Club);

	// Range pin distance (GOL-29). SetPinValue updates the spinner (re-entrancy guarded); SetPinActualReadout
	// shows the resolved post-clamp distance; SetPuttMode flips the "Putt from green" checkbox.
	void SetPinValue(double Yards);
	void SetPinActualReadout(double Yards);
	void SetPuttMode(bool bChecked);

	// The pin spinner + putt checkbox are range-only dev controls (they no-op in a round). The HUD hides
	// the cluster while a round is active so the in-round readout stays clean.
	void SetRangeControlsVisible(bool bVisible);

	// GOL-147: top-left "Menu" button (mirrors the in-round round-panel Menu). Opens the settings menu
	// (same as Esc). The HUD hides it in a round, where RoundHud provides its own Menu button.
	void SetMenuButtonVisible(bool bVisible);

	// Set by the owning HUD; each control pushes the user's pick/action back through its delegate.
	TFunction<void(int32)> OnClubChosen;
	TFunction<void(int32)> OnTimeChosen;
	TFunction<void(int32)> OnSkyChosen;
	TFunction<void(int32)> OnLaunchMonitorChosen;
	TFunction<void(int32)> OnCameraChosen;
	TFunction<void(double)> OnPinChanged;       // user dragged/edited the Pin spinner
	TFunction<void(bool)>   OnPuttModeChanged;  // user toggled "Putt from green"

	// GOL-73: user changed any CTP setting; all values reported together (display units = yards).
	TFunction<void(double /*MinYd*/, double /*MaxYd*/, bool /*bSideOffset*/, bool /*bPuttOut*/, double /*WithinYd*/)> OnCtpConfigChanged;
	// GOL-75: user changed any putting setting; min/max in FEET + hole-out-vs-distance scoring toggle.
	TFunction<void(double /*MinFt*/, double /*MaxFt*/, bool /*bHoleOut*/)> OnPuttingConfigChanged;
	// GOL-73: user clicked "End drill" in the CTP cluster -> HUD returns to plain free-fire range.
	TFunction<void()> OnEndPractice;

	// The telemetry readout's primary button (Swing / Sim shot). The HUD routes it by mode: game ->
	// advance the swing meter; a connected LM -> ask the active driver to emit a shot.
	TFunction<void()> OnPrimaryAction;

	// GOL-147: top-left Menu button -> HUD opens the settings menu.
	TFunction<void()> OnMenu;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTimeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleSkySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleLaunchMonitorSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleCameraSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandlePrimaryActionClicked();
	UFUNCTION() void HandleMenuClicked();
	UFUNCTION() void HandlePinValueChanged(float Value);
	UFUNCTION() void HandlePuttModeChanged(bool bChecked);

	// GOL-73 CTP control handlers. The spin/check handlers all funnel through EmitCtpConfig so a single
	// OnCtpConfigChanged carries the whole config every time.
	UFUNCTION() void HandleCtpMinChanged(float Value);
	UFUNCTION() void HandleCtpMaxChanged(float Value);
	UFUNCTION() void HandleCtpSideChanged(bool bChecked);
	UFUNCTION() void HandleCtpPuttOutChanged(bool bChecked);
	UFUNCTION() void HandleCtpWithinChanged(float Value);
	UFUNCTION() void HandleEndPracticeClicked();

	// GOL-75 putting control handlers -- all funnel through EmitPuttingConfig (one callback per change).
	UFUNCTION() void HandlePuttMinChanged(float Value);
	UFUNCTION() void HandlePuttMaxChanged(float Value);
	UFUNCTION() void HandlePuttScoreChanged(bool bChecked);

	// GOL-203 polish: a bare click on a spinbox enters its text-edit mode and traps keyboard focus
	// (the next Space types into the box instead of swinging) -- and no OnValueChanged fires until
	// the value actually changes, so the Emit* focus-return never runs. Every spinbox binds its
	// commit (Enter / Esc / focus-out) here to hand focus back to the game; deliberate moves to
	// another control (OnUserMovedFocus) are left alone.
	UFUNCTION() void HandleSpinCommitted(float Value, ETextCommit::Type CommitMethod);

private:
	void BuildTree();
	void EmitCtpConfig();   // read all CTP controls -> OnCtpConfigChanged (no-op while suppressed)
	void EmitPuttingConfig();   // read the putting controls -> OnPuttingConfigChanged (no-op while suppressed)

	// Shared body for the dropdowns: ignore programmatic re-broadcasts, report genuine picks via
	// OnChosen, then hand keyboard focus back to the game so Space/1-6/arrows still reach gameplay.
	void HandleComboPick(UComboBoxString* Combo, const TFunction<void(int32)>& OnChosen,
		ESelectInfo::Type SelectionType);
	void ReturnFocusToGameViewport();

	// Select an index without re-entering the selection callback (sets bSuppressSelectionCallback).
	void SetComboIndexGuarded(UComboBoxString* Combo, int32 Index);

	// Control-bar dropdowns.
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ClubCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> TimeCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> SkyCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> CameraCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> LMCombo;

	// GOL-147: top-left Menu button (range only; hidden in a round).
	UPROPERTY(Transient) TObjectPtr<UButton> MenuButton;

	// LM status pill (control bar, right).
	UPROPERTY(Transient) TObjectPtr<UBorder> StatusPill;
	UPROPERTY(Transient) TObjectPtr<UBorder> StatusDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusEyebrow;   // "MODE" / "MONITOR"
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusValue;     // "Game · Keyboard" / "{Name} · Live"
	UPROPERTY(Transient) TObjectPtr<UBorder> ReadyBadge;        // GOL-186 "take your shot" badge (hidden by default)

	// Telemetry readout (bottom-left).
	UPROPERTY(Transient) TObjectPtr<UButton> PrimaryButton;        // Swing / Sim shot
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PrimaryButtonLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValClub;          // club headline (upper)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpeed;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValLaunch;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpin;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValCarry;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValTotal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValOffline;

	// Range-only dev controls (pin distance + putt-from-green), hidden in a round.
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> RangeControlsRow;
	UPROPERTY(Transient) TObjectPtr<USpinBox> PinBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PinActualText;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> PuttModeBox;

	// GOL-73 CTP settings cluster (hidden unless Closest-to-Pin mode is active).
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> CtpControlsRow;
	UPROPERTY(Transient) TObjectPtr<USpinBox> CtpMinBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> CtpMaxBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> CtpSideBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> CtpPuttOutBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> CtpWithinBox;
	// GOL-73 CTP scoreboard (Pin / This / Best / Avg / Shots).
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> CtpScoreRow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtpValPin;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtpValThis;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtpValBest;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtpValAvg;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtpValShots;
	UPROPERTY(Transient) TObjectPtr<UButton> EndPracticeBtn;   // GOL-73 "End drill" -> back to free range

	// GOL-75 putting settings cluster (hidden unless Putting mode is active). Reuses CtpScoreRow for
	// the scoreboard; this is just the distinct input row (feet min/max + scoring + line-preview seam).
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PuttControlsRow;
	UPROPERTY(Transient) TObjectPtr<USpinBox> PuttMinBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> PuttMaxBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> PuttHoleOutBox;        // checked = hole-out; unchecked = distance-to-pin
	UPROPERTY(Transient) TObjectPtr<UCheckBox> PuttLinePreviewBox;    // disabled seam (GOL-75 follow-up)

	// True while we programmatically set a ComboBox selection, so the resulting OnSelectionChanged
	// broadcast doesn't loop back into gameplay.
	bool bSuppressSelectionCallback = false;
	bool bSuppressPinCallback = false;     // same guard for the Pin spinner
	bool bSuppressPuttCallback = false;    // same guard for the Putt-mode checkbox
	bool bSuppressCtpCallback = false;     // GOL-73: guard for programmatic CTP control writes
};
