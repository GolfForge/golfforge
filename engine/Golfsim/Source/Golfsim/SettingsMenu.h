// Settings/Credits menu (UMG). Pure-C++ UUserWidget, no WBP -- same idiom as UGolfRangePanel. A
// centered modal with a dimmed backdrop, a Display | Credits nav, the display controls, and a
// credits scroll. Dumb view: reports the user's Apply/Close via TFunctions; AGolfRangeHUD owns logic.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "GolfDisplaySettings.h"
#include "SettingsMenu.generated.h"

class UButton;
class UComboBoxString;
class USlider;
class UTextBlock;
class UVerticalBox;
class UScrollBox;

UCLASS()
class GOLFSIM_API USettingsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetResolutionOptions(const TArray<FIntPoint>& Resolutions);
	void SetCurrent(const FGolfDisplaySettings& S);   // seed controls from current values
	void SetCreditsText(const FString& Text);
	void SetUpscalerOptions(const TArray<int32>& Indices);
	void ShowSection(int32 Index);                    // 0 = Display, 1 = Credits

	// Set by the owning HUD.
	TFunction<void(const FGolfDisplaySettings&)> OnApplyDisplay;
	TFunction<void()> OnClose;
	TFunction<void()> OnMainMenu;   // GOL-125: exit to main menu (loads PracticeRange + abandons round)

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleMainMenuClicked();
	UFUNCTION() void HandleQuitClicked();
	UFUNCTION() void HandleDisplayNavClicked();
	UFUNCTION() void HandleCreditsNavClicked();
	UFUNCTION() void HandleUpscalerChanged(FString SelectedItem, ESelectInfo::Type SelectionType);   // refill mode list
	UFUNCTION() void HandleWindowModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType); // borderless locks res to desktop

private:
	void BuildTree();
	UButton* MakeButton(const TCHAR* Label);          // light button + black centered label
	void RepopulateModeCombo(int32 UpscalerFixedIndex, float TargetPct);   // fill Upscale Mode for an upscaler
	void UpdateResolutionEnabledForMode(int32 WindowModeIndex);            // disable the picker in Borderless (desktop-locked)

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> WindowCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> UpscalerCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> UpscaleModeCombo;   // DLSS-style quality presets
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> DisplayBox;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CreditsScroll;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreditsBody;

	TArray<FIntPoint> ResOptions;   // index-aligned with ResCombo options
	TArray<int32> UpscalerOptionIndices;   // fixed upscaler index per UpscalerCombo option
	int32 ModeComboUpscaler = 0;           // which upscaler the Upscale Mode combo is populated for
};
