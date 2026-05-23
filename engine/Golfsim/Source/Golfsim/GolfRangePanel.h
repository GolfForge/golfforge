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

UCLASS()
class GOLFSIM_API UGolfRangePanel : public UUserWidget
{
	GENERATED_BODY()

public:
	// Populate the club dropdown. Owned by the HUD (single source of truth = its club bag) so the
	// names never drift from the firing presets. Call once after creation, before SetSelectedClubIndex.
	void SetClubOptions(const TArray<FString>& Names);

	// Refresh the metrics grid with the last shot's display-unit values.
	void UpdateMetrics(const FString& Club, double SpeedMph, double LaunchDeg,
		double SpinRpm, double CarryYd, double OfflineYd);

	// Sync the dropdown to the active club (e.g. after a 1-6 key press). Guarded so it does
	// not re-enter the selection callback.
	void SetSelectedClubIndex(int32 Index);

	// Set by the owning HUD; the ComboBox pushes the user's pick back through this.
	TFunction<void(int32)> OnClubChosen;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION()
	void HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ClubCombo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValClub;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpeed;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValLaunch;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValSpin;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValCarry;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValOffline;

	// True while we programmatically set the ComboBox selection, so the resulting
	// OnSelectionChanged broadcast doesn't loop back into gameplay.
	bool bSuppressSelectionCallback = false;
};
