// End-of-round scorecard modal (GOL-120). Centered overlay shown when round.complete fires;
// 18-row table (Hole / Par / Strokes / +/-) with colored vs-par cell, footer totals row, and a
// "Back to Menu" button. Pure C++ -- same idiom as UShotHistoryPanel / UPreRoundPicker. Dumb view:
// the HUD populates rows via SetScorecard and listens for OnBackToMenu.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScorecardPanel.generated.h"

class UVerticalBox;
class UButton;
class UTextBlock;

UCLASS()
class GOLFSIM_API UScorecardPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replace the rows. Pars + Strokes are parallel arrays of length N (typically 18). PlayerName
	 *  goes into the header. */
	void SetScorecard(const FString& PlayerName, const TArray<int32>& Pars, const TArray<int32>& Strokes);

	/** Fired by the "Back to Menu" button. HUD loads the range + shows main menu. */
	TFunction<void()> OnBackToMenu;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleBackClicked();

private:
	void BuildTree();
	void RebuildRows(const TArray<int32>& Pars, const TArray<int32>& Strokes);

	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Header;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox>  RowsBox;     // holds the 18 hole rows + footer
	UPROPERTY(Transient) TObjectPtr<UButton>       BackButton;
};
