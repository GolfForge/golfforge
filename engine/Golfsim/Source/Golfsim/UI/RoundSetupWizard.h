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
#include "RoundSetupWizard.generated.h"

class UButton;
class UBorder;
class UTextBlock;
class UWidgetSwitcher;
class UHorizontalBox;
class UUniformGridPanel;
class UCourseCard;

UCLASS()
class GOLFSIM_API URoundSetupWizard : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCourses(const TArray<FGolfCourseInfo>& Courses);   // build the Course-step grid
	void SetResumeVisible(bool bVisible);                      // resume banner seam (hidden until backing lands)
	void ResetToFirstStep();                                  // call on open: back to Course, clear selection

	TFunction<void(const FString& /*CourseId*/)> OnTeeOff;   // final step -> HUD starts the round
	TFunction<void()> OnClose;                                // close-X / Esc on step 1 -> back to menu

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;

	UFUNCTION() void HandleNextClicked();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleStepperClicked();   // bound to every pill; resolves which via IsHovered()

private:
	void BuildTree();
	void BuildTopbar(UHorizontalBox* Bar);
	void BuildStepper(UHorizontalBox* Bar);
	UWidget* BuildCourseStep();
	UWidget* BuildStubStep(const FString& Eyebrow, const FString& Title, const FString& Desc, const FString& Soon);
	void BuildFooter(UHorizontalBox* Footer);

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

	int32 CurrentStep = 1;          // 1 Course / 2 Format / 3 Players
	FString SelectedCourseId;
	FString SelectedCourseName;
};
