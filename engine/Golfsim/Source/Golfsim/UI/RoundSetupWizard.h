// Round Setup wizard (GOL-141, epic GOL-137) -- the full-screen, GolfForge-styled entry to a round,
// replacing the old UPreRoundPicker combo-box modal. This ticket builds the wizard chrome (header
// brand + clickable stepper + close-X, footer live-summary + Back/Continue) and the Course step in
// full; the Format (GOL-142) and Players (GOL-143) steps are "Coming soon" stub panels that slot into
// the same UWidgetSwitcher later. Behavior ported from Build/handoff/source/setup.js; motion per
// BUILD_SPEC §7 (stepper toggles instant; card lift decorative). Built procedurally from GolfUITheme.
//
// Dumb view: the cooked course is selected here and reported via OnTeeOff; the HUD owns URoundSubsystem
// and starts the round (Normal difficulty this milestone -- difficulty/scoring moves to the Format step).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/CourseRegistry.h"
#include "Round/RoundConfig.h"
#include "RoundSetupWizard.generated.h"

class UButton;
class UBorder;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;
class UHorizontalBox;
class UVerticalBox;
class UUniformGridPanel;
class UEditableTextBox;
class UCourseCard;
class UOptionCard;
class USegmentedControl;

UCLASS()
class GOLFSIM_API URoundSetupWizard : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCourses(const TArray<FGolfCourseInfo>& Courses);   // build the Course-step grid
	void SetResumeVisible(bool bVisible);                      // resume banner seam (hidden until backing lands)
	void ResetToFirstStep();                                  // call on open: back to Course, clear selection

	// final step -> HUD starts the round with the collected Format config (GOL-142)
	TFunction<void(const FString& /*CourseId*/, const FRoundConfig& /*Config*/)> OnTeeOff;
	TFunction<void()> OnClose;                                // close-X / Esc on step 1 -> back to menu

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;

	UFUNCTION() void HandleNextClicked();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleStepperClicked();   // bound to every pill; resolves which via IsHovered()
	UFUNCTION() void HandleHoleChipClicked();  // bound to every custom-hole chip; resolves via IsHovered()
	UFUNCTION() void HandleHoleQuickClicked(); // bound to All/Front/Back/Clear; resolves via IsHovered()
	UFUNCTION() void HandlePlayerNameChanged(const FText& Text);
	UFUNCTION() void HandleTeeClicked();       // bound to every tee swatch; resolves via IsHovered()
	UFUNCTION() void HandleHandicapMinus();
	UFUNCTION() void HandleHandicapPlus();

private:
	void BuildTree();
	void BuildTopbar(UHorizontalBox* Bar);
	void BuildStepper(UHorizontalBox* Bar);
	UWidget* BuildCourseStep();
	UWidget* BuildFormatStep();
	UWidget* BuildPlayersStep();
	void BuildFooter(UHorizontalBox* Footer);

	// Players-step helpers
	void PrefillPlayer();          // seed Players[0] + the row from GolfDisplay (name + handicap)
	void RefreshPlayerAvatar();    // initials from the player-1 name
	void RefreshTeeSwatches();     // selected-ring on the chosen tee
	void RefreshHandicapText();
	void RefreshRoundSummary();    // fill the summary card from RoundConfig + selected course

	// Format-step helpers
	void AddSectionHeader(UVerticalBox* Col, const FString& Label, const FString& Desc);
	void BuildHolePicker(UVerticalBox* Col);   // the custom 1-18 chip grid + quick buttons (hidden unless Custom)
	void SetHolesMode(ERoundHolesMode Mode);   // segmented change -> config + custom-picker visibility
	void RefreshHoleChips();                   // restyle chips per RoundConfig.CustomHoles
	void SelectGameType(ERoundGameType Game);
	void SelectTurnOrder(ETurnOrder Turn);
	FString HolesSummaryLabel() const;
	FString GameSummaryLabel() const;

	void ShowStep(int32 Step);
	void RefreshStepper();
	void RefreshNav();
	void UpdateSummary();
	bool CanAdvance() const;
	void GoNext();
	void GoBack();
	void HandleCardSelected(const FString& CourseId);

	// chrome. Pills follow the SettingsMenu-rail pattern: the active look is painted via the button's
	// own FButtonStyle (SetStyle), and the number/name highlight via text color/weight (both repaint
	// reliably). Note: a rounded-box border nested inside the button content does NOT repaint here, so
	// the number badge is plain colored text for now -- see follow-up bug for the filled-circle badge.
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    StepPills;        // click target + painted bg per pill
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StepNumTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StepNameTexts;
	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher>    ContentSwitcher;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox>     FootSummary;
	UPROPERTY(Transient) TObjectPtr<UButton>            BackBtn;
	UPROPERTY(Transient) TObjectPtr<UButton>            NextBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>         NextLabel;

	// Course step
	UPROPERTY(Transient) TObjectPtr<UBorder>            ResumeBanner;
	UPROPERTY(Transient) TObjectPtr<UUniformGridPanel>  CourseGrid;
	UPROPERTY(Transient) TArray<TObjectPtr<UCourseCard>> Cards;

	// Format step
	UPROPERTY(Transient) TObjectPtr<USegmentedControl>  HolesSeg;
	UPROPERTY(Transient) TObjectPtr<UWidget>            CustomPicker;     // hole-chip block; shown only in Custom
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    HoleChips;        // 18 toggle chips (Ref = index+1)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HoleChipTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    HoleQuickButtons; // All / Front 9 / Back 9 / Clear
	UPROPERTY(Transient) TArray<TObjectPtr<UOptionCard>> GameCards;
	UPROPERTY(Transient) TArray<TObjectPtr<UOptionCard>> TurnCards;
	UPROPERTY(Transient) TObjectPtr<UWidget>            GimmeBlock;       // "Concede inside" + 3/5/8 ft; shown when Gimmes on

	// Players step (single row this milestone)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        PlayerAvatarText;  // initials
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox>  PlayerNameBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>   TeeButtons;        // Black/Blue/White/Red
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        HandicapText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryCourseName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryCourseLoc;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryHolesVal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryGameVal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryTurnVal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryHoleOutVal;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        SummaryPlayersVal;

	FRoundConfig RoundConfig;        // collected Format selections; handed to OnTeeOff

	int32 CurrentStep = 1;          // 1 Course / 2 Format / 3 Players
	FString SelectedCourseId;
	FString SelectedCourseName;
	FString SelectedCourseLoc;
};
