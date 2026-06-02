// Practice-range launch-monitor panel (UMG). Pure C++ UUserWidget: the whole tree is
// built in NativeOnInitialized, no WBP asset. Top-right metrics grid refreshed after each
// shot (Club / Ball Speed mph / Launch deg / Spin rpm / Carry yd / Offline yd) plus a club
// ComboBox. AGolfRangeHUD owns the firing/bag logic and drives this widget; the widget only
// displays metrics and reports the user's club pick back via OnClubChosen.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"   // ESelectInfo::Type for the OnSelectionChanged handler
#include "GolfRangePanel.generated.h"

class UComboBoxString;
class UTextBlock;
class UButton;
class USpinBox;
class UCheckBox;

UCLASS()
class GOLFSIM_API UGolfRangePanel : public UUserWidget
{
	GENERATED_BODY()

public:
	// Populate the club dropdown. Owned by the HUD (single source of truth = its club bag) so the
	// names never drift from the firing presets. Call once after creation, before SetSelectedClubIndex.
	void SetClubOptions(const TArray<FString>& Names);

	// Refresh the metrics grid with the last shot's display-unit values. bSpinEstimated marks the
	// spin as computed (not measured by the LM) -- shown as "5600 est". TotalYd is carry + ground roll.
	void UpdateMetrics(const FString& Club, double SpeedMph, double LaunchDeg,
		double SpinRpm, double CarryYd, double TotalYd, double OfflineYd, bool bSpinEstimated = false);

	// Set the launch-monitor connection indicator (green/red dot + detail). Driven by the HUD from
	// the active driver's status.
	void SetConnectionStatus(bool bConnected, const FString& Detail);

	// Populate the Time-of-day / Sky dropdowns from the environment director's preset names.
	void SetTimeOptions(const TArray<FString>& Names);
	void SetSkyOptions(const TArray<FString>& Names);

	// Populate the Camera dropdown (e.g. "Tee" / "Follow"). The HUD owns the modes.
	void SetCameraOptions(const TArray<FString>& Names);

	// Populate the launch-monitor dropdown. The HUD passes "Off" first, then each available driver's
	// display name -- so index 0 = Off (disconnect), index i = the (i-1)th driver (select + connect).
	void SetLaunchMonitorOptions(const TArray<FString>& Names);

	// Sync a dropdown to the active index (e.g. after a 1-6 key press, or to the director's
	// startup defaults). Guarded so it does not re-enter the selection callback.
	void SetSelectedClubIndex(int32 Index);
	void SetSelectedTimeIndex(int32 Index);
	void SetSelectedSkyIndex(int32 Index);
	void SetSelectedLaunchMonitorIndex(int32 Index);
	void SetSelectedCameraIndex(int32 Index);

	// Range pin distance (GOL-29). SetPinValue updates the spinner (re-entrancy guarded so a console
	// or HUD push doesn't re-broadcast OnPinChanged); SetPinActualReadout shows the resolved
	// post-clamp distance next to the spinner. SetPuttMode flips the "Putt from green" checkbox.
	void SetPinValue(double Yards);
	void SetPinActualReadout(double Yards);
	void SetPuttMode(bool bChecked);

	// Input mode (GOL-67). Mode dropdown: 0 = Game, 1 = Simulation. Visibility of the LM combo +
	// Simulate button is gated on Mode == Simulation -- Game mode hides the LM row entirely so a
	// keyboard player isn't asked about a launch monitor they don't have.
	void SetModeOptions(const TArray<FString>& Names);
	void SetSelectedModeIndex(int32 Index);
	void SetLMControlsVisible(bool bVisible);

	// Set by the owning HUD; each ComboBox pushes the user's pick back through its delegate.
	TFunction<void(int32)> OnClubChosen;
	TFunction<void(int32)> OnTimeChosen;
	TFunction<void(int32)> OnSkyChosen;
	TFunction<void(int32)> OnLaunchMonitorChosen;
	TFunction<void(int32)> OnCameraChosen;
	TFunction<void(int32)> OnModeChosen;        // GOL-67: 0=Game, 1=Simulation
	TFunction<void(double)> OnPinChanged;       // user dragged/edited the Pin spinner
	TFunction<void(bool)>   OnPuttModeChanged;  // user toggled "Putt from green"

	// Fired by the "Simulate Shot" button (only shown while a launch monitor is connected). The HUD
	// asks the active driver to emit a shot (OpenFlight mock mode -> Socket.IO simulate_shot).
	TFunction<void()> OnSimulateShot;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTimeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleSkySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleLaunchMonitorSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleCameraSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleSimulateClicked();
	UFUNCTION() void HandlePinValueChanged(float Value);
	UFUNCTION() void HandlePuttModeChanged(bool bChecked);

private:
	void BuildTree();

	// Shared body for the three dropdowns: ignore programmatic re-broadcasts, report genuine picks
	// via OnChosen, then hand keyboard focus back to the game so Space/1-6/arrows still reach gameplay.
	void HandleComboPick(UComboBoxString* Combo, const TFunction<void(int32)>& OnChosen,
		ESelectInfo::Type SelectionType);
	void ReturnFocusToGameViewport();

	// Select an index without re-entering the selection callback (sets bSuppressSelectionCallback).
	void SetComboIndexGuarded(UComboBoxString* Combo, int32 Index);

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ClubCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> TimeCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> SkyCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> CameraCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> LMCombo;
	UPROPERTY(Transient) TObjectPtr<UButton> SimulateButton;   // shown only while an LM is connected
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ModeCombo;            // GOL-67: Game / Simulation
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LMLabel;                   // "Launch Monitor" header (hidden in Game)
	UPROPERTY(Transient) TObjectPtr<USpinBox> PinBox;          // pin distance, yards
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PinActualText; // resolved post-clamp distance next to PinBox
	UPROPERTY(Transient) TObjectPtr<UCheckBox> PuttModeBox;    // teleports the player onto the green
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValClub;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpeed;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValLaunch;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpin;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValCarry;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValTotal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValOffline;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;   // launch-monitor connection dot + detail

	// True while we programmatically set the ComboBox selection, so the resulting
	// OnSelectionChanged broadcast doesn't loop back into gameplay.
	bool bSuppressSelectionCallback = false;
	bool bSuppressPinCallback = false;     // same guard for the Pin spinner
	bool bSuppressPuttCallback = false;    // same guard for the Putt-mode checkbox
	bool bSuppressModeCallback = false;    // GOL-67: guard for the Mode combobox programmatic set
};
