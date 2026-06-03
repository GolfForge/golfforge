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

	// Label of the primary-action button in the telemetry readout: "Swing" (game mode) / "Sim shot"
	// (a real LM owns the stream). The HUD sets it alongside SetLaunchMonitorStatus.
	void SetPrimaryActionLabel(const FString& Label);

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

	// Set by the owning HUD; each control pushes the user's pick/action back through its delegate.
	TFunction<void(int32)> OnClubChosen;
	TFunction<void(int32)> OnTimeChosen;
	TFunction<void(int32)> OnSkyChosen;
	TFunction<void(int32)> OnLaunchMonitorChosen;
	TFunction<void(int32)> OnCameraChosen;
	TFunction<void(double)> OnPinChanged;       // user dragged/edited the Pin spinner
	TFunction<void(bool)>   OnPuttModeChanged;  // user toggled "Putt from green"

	// The telemetry readout's primary button (Swing / Sim shot). The HUD routes it by mode: game ->
	// advance the swing meter; a connected LM -> ask the active driver to emit a shot.
	TFunction<void()> OnPrimaryAction;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTimeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleSkySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleLaunchMonitorSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleCameraSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandlePrimaryActionClicked();
	UFUNCTION() void HandlePinValueChanged(float Value);
	UFUNCTION() void HandlePuttModeChanged(bool bChecked);

private:
	void BuildTree();

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

	// LM status pill (control bar, right).
	UPROPERTY(Transient) TObjectPtr<UBorder> StatusPill;
	UPROPERTY(Transient) TObjectPtr<UBorder> StatusDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusEyebrow;   // "MODE" / "MONITOR"
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusValue;     // "Game · Keyboard" / "{Name} · Live"

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

	// True while we programmatically set a ComboBox selection, so the resulting OnSelectionChanged
	// broadcast doesn't loop back into gameplay.
	bool bSuppressSelectionCallback = false;
	bool bSuppressPinCallback = false;     // same guard for the Pin spinner
	bool bSuppressPuttCallback = false;    // same guard for the Putt-mode checkbox
};
