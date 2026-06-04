// Settings modal (GOL-140, epic GOL-137). Pure-C++ UUserWidget rebuilt as the GolfForge design: a
// glass card with a left rail (Graphics / Audio / Gameplay / Controls / Credits) and label+description+
// control rows, using the reusable USegmentedControl / UToggleSwitch atoms. Graphics drives the real
// FGolfDisplaySettings pipeline (resolution dropdown + segmented window/quality/upscaler/render-scale,
// with the upscaler->mode carry-over + borderless lock preserved); Audio/Gameplay/Controls are disabled
// "Coming soon" seams. Public API unchanged from the old version so AGolfRangeHUD's wiring is untouched.
// Dumb view: reports Apply/Close/MainMenu via TFunctions.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GolfDisplaySettings.h"
#include "SettingsMenu.generated.h"

class UButton;
class UComboBoxString;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class UWidgetSwitcher;
class USegmentedControl;

UCLASS()
class GOLFSIM_API USettingsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetResolutionOptions(const TArray<FIntPoint>& Resolutions);
	void SetCurrent(const FGolfDisplaySettings& S);   // seed controls from current values
	void SetCreditsText(const FString& Text);
	void SetUpscalerOptions(const TArray<int32>& Indices);
	void ShowSection(int32 Index);                    // 0 Graphics, 1 Audio, 2 Gameplay, 3 Controls, 4 Credits
	void SetActionButtonsVisible(bool bVisible);      // hide Main Menu + Quit when opened from the main menu

	// Set by the owning HUD.
	TFunction<void(const FGolfDisplaySettings&)> OnApplyDisplay;
	TFunction<void()> OnClose;
	TFunction<void()> OnMainMenu;

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;   // Esc -> close

	UFUNCTION() void HandleRailClicked();   // resolves which rail tab via IsHovered()
	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleMainMenuClicked();
	UFUNCTION() void HandleQuitClicked();

private:
	void BuildTree();
	UScrollBox* BuildGraphicsTab();
	UScrollBox* BuildDisabledTab(const TArray<TArray<FString>>& Rows);   // representative "Coming soon" rows
	void AddRow(UScrollBox* Tab, const FString& Label, const FString& Desc, UWidget* Control, bool bDisabled);

	void RepopulateModeCombo(int32 UpscalerFixedIndex, float TargetPct);   // fill render-scale tiers for an upscaler
	void UpdateResolutionEnabledForMode(int32 WindowModeIndex);          // Borderless = desktop-locked
	void RefreshRail();

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> ContentSwitcher;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> RailButtons;

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResCombo;
	UPROPERTY(Transient) TObjectPtr<USegmentedControl> WindowSeg;
	UPROPERTY(Transient) TObjectPtr<USegmentedControl> QualitySeg;
	UPROPERTY(Transient) TObjectPtr<USegmentedControl> GrassSeg;   // GOL-162 grass detail (Off/Low/High)
	UPROPERTY(Transient) TObjectPtr<USegmentedControl> UpscalerSeg;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> UpscaleModeCombo;   // render-scale tiers (dropdown; 5-7 options)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreditsBody;
	UPROPERTY(Transient) TObjectPtr<UButton> MainMenuBtn;
	UPROPERTY(Transient) TObjectPtr<UButton> QuitBtn;

	TArray<FIntPoint> ResOptions;          // index-aligned with ResCombo options
	TArray<int32> UpscalerOptionIndices;   // fixed upscaler index per UpscalerSeg option
	int32 ModeSegUpscaler = 0;             // which upscaler the render-scale seg is populated for
	int32 CurrentSection = 0;
};
