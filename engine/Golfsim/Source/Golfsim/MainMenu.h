// Startup main menu (GOL-139, epic GOL-137). Pure-C++ UUserWidget rebuilt as the GolfForge
// "bento" grid: a full dark stage with a top bar (brand + environment cluster), a 4-tile bento
// (Play Course hero / Range / Practice / Settings), and a footer (keyboard legend + Previous
// Sessions link + Exit). Hover lifts + accent-glows tiles; keys 1-4 select, Enter confirms, Esc
// quits. Reuses GolfUITheme atoms. Dumb view: reports intent via TFunction; the HUD owns the flow.
//
// Seams (per the epic's "build everything, disable what we don't have"): the Practice tile opens the
// drill picker (GOL-73); the hero Resume pill is hidden (no resume backing yet); the env cluster's
// weather + handicap are static placeholders (clock + player name are real). See TODO(GOL-144/143).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenu.generated.h"

class UButton;
class UTextBlock;
class UMenuTile;

UCLASS()
class GOLFSIM_API UMainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> OnPlayRange;          // dismiss the menu, hand control to the live range
	TFunction<void()> OnPlayCourse;         // overlay the pre-round picker (GOL-121)
	TFunction<void()> OnPlayPractice;       // GOL-73: overlay the practice-drill picker
	TFunction<void()> OnPreviousSessions;   // overlay the shot-history list (GOL-65) -- footer link
	TFunction<void()> OnSettings;           // open settings over the menu (GOL-139 -> OpenSettingsOverMenu)

	/** Greys the "Previous Sessions" footer link when 0; enables otherwise. Called by HUD on mount. */
	void SetPreviousSessionsCount(int32 Count);

	/** Player chip name (real, from GolfDisplay::ReadPlayerName); derives the avatar initials. */
	void SetPlayerName(const FString& Name);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;

	UFUNCTION() void HandlePreviousSessionsClicked();
	UFUNCTION() void HandleExitClicked();

private:
	void BuildTree();
	void Quit();
	void SetSelectedTile(int32 Index);   // keyboard selection ring; clamps + skips disabled
	void UpdateClock();

	UPROPERTY(Transient) TObjectPtr<UMenuTile> HeroTile;       // 0 Play Course
	UPROPERTY(Transient) TObjectPtr<UMenuTile> RangeTile;      // 1 Range
	UPROPERTY(Transient) TObjectPtr<UMenuTile> PracticeTile;   // 2 Practice (disabled)
	UPROPERTY(Transient) TObjectPtr<UMenuTile> SettingsTile;   // 3 Settings
	UPROPERTY(Transient) TObjectPtr<UButton>   PreviousSessionsBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClockText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PlayerNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AvatarText;

	int32 SelectedIndex = -1;   // nothing selected until the user presses 1-4 (no default highlight)
	FTimerHandle ClockTimer;

	UMenuTile* TileForIndex(int32 Index) const;
};
