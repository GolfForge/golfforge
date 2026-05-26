// Manual-shot dialog (UMG). Pure-C++ UUserWidget, no WBP -- same idiom as UGolfRangePanel. A small
// bottom-right form to type exact launch numbers and Fire. The dialog is a dumb input form: on Fire it
// hands the values back to AGolfRangeHUD (which owns the tee/aim launch transform and publishes the
// shot.taken envelope). The first deliberate EventBus producer + the fallback when no launch monitor
// is connected, and the manual tuning rig for the solver (GOL-8).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"   // ESelectInfo::Type for the club OnSelectionChanged handler
#include "ManualShotDialog.generated.h"

class UButton;
class UComboBoxString;
class USpinBox;
class UTextBlock;

/**
 * Values the dialog reports back to the HUD on Fire, in display units (mph / deg / rpm). The HUD
 * converts to SI before publishing the envelope. Plain C++ struct -- passed via TFunction, not
 * reflected. Sign conventions match the event protocol: Sidespin/Azimuth - = draw/left, + = fade/right.
 */
struct FManualShotValues
{
	double BallSpeedMph = 0.0;
	double LaunchDeg = 0.0;
	double BackspinRpm = 0.0;
	double SidespinRpm = 0.0;
	double AzimuthDeg = 0.0;
	FString Club;
};

UCLASS()
class GOLFSIM_API UManualShotDialog : public UUserWidget
{
	GENERATED_BODY()

public:
	// Populate the club dropdown. Owned by the HUD (single source of truth = its club bag), like the panel.
	void SetClubOptions(const TArray<FString>& Names);
	// Sync the club dropdown to an index without re-entering the selection callback.
	void SetSelectedClubIndex(int32 Index);
	// Push launch values into the spinners (club-preset autofill / open-on-active-club seed).
	void SetFields(double SpeedMph, double LaunchDeg, double BackspinRpm, double SidespinRpm, double AzimuthDeg);
	// Update the result line from the resolved shot (carry / offline in yards).
	void SetResult(double CarryYd, double OfflineYd);

	// Set by the owning HUD.
	TFunction<void(const FManualShotValues&)> OnFire;
	TFunction<void(int32)> OnClubChosen;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleFireClicked();
	UFUNCTION() void HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

private:
	void BuildTree();
	// Hand keyboard focus back to the game viewport so Space/M/1-6 reach gameplay after a pick/Fire.
	void ReturnFocusToGameViewport();

	UPROPERTY(Transient) TObjectPtr<USpinBox> SpeedBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> LaunchBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> BackspinBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> SidespinBox;
	UPROPERTY(Transient) TObjectPtr<USpinBox> AzimuthBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ClubCombo;
	UPROPERTY(Transient) TObjectPtr<UButton> FireButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;

	// True while we programmatically set the club selection, so the resulting OnSelectionChanged
	// broadcast doesn't loop back into the autofill/focus handling.
	bool bSuppressSelectionCallback = false;
};
