// Pre-round picker (GOL-121) -- the user-facing entry to a GOL-112 single-player round.
// Opens over the main menu when the user clicks "Play Course". Dumb view: collects
// {course id, difficulty, player name} and reports them up to the HUD via TFunctions; the HUD
// is what actually calls URoundSubsystem::StartRound and dismisses both modals.
//
// One sibling-pattern note: course names + difficulties are populated by the HUD via Set*Options
// so the widget never imports the round subsystem -- single direction of include dependency.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/GolfDifficulty.h"
#include "PreRoundPicker.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;

UCLASS()
class GOLFSIM_API UPreRoundPicker : public UUserWidget
{
	GENERATED_BODY()

public:
	/** HUD seeds the course dropdown. Display labels go on the combo; CourseIds is the parallel
	 *  array used to resolve back to URoundSubsystem's CourseId at Start time. */
	void SetCourses(const TArray<FString>& DisplayLabels, const TArray<FString>& InCourseIds);

	/** Prefill the player-name field from GolfDisplay::ReadPlayerName(). */
	void SetPlayerName(const FString& Name);

	/** Fired when "Start Round" is clicked. (CourseId, Difficulty, PlayerName) -> HUD. */
	TFunction<void(const FString& /*CourseId*/, EGolfDifficulty /*Difficulty*/, const FString& /*PlayerName*/)> OnStartRound;

	/** Fired by the Back button. HUD dismisses the picker and leaves the main menu visible. */
	TFunction<void()> OnBack;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleBackClicked();

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> NameField;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString>  CourseCombo;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString>  DifficultyCombo;

	// Parallel to CourseCombo's display options; index lookup resolves Display -> CourseId.
	TArray<FString> CourseIds;
};
