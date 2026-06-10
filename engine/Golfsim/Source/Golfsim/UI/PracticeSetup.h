// Practice setup picker (GOL-73). The Practice tile on the main menu opens this instead of dropping
// the player straight into a CTP-rigged range. A full-screen GolfForge overlay (dim backdrop + a
// centered glass panel) that presents the available drills as selectable cards with a one-line "what
// it does", mirroring the Round Setup wizard's card pattern (reuses UI/OptionCard). Pick a drill +
// Start -> the HUD enters that practice mode on the live range. Closest-to-Pin and Putting (GOL-75)
// are wired; Islands stays a seam card ("Coming soon") until GOL-74 lands.
//
// Dumb view: reports the chosen drill via OnStartCtp / OnStartPutting; the HUD owns the flow (dismiss
// the menu, enter the mode). Esc / Back -> OnClose returns to the menu.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PracticeSetup.generated.h"

class UButton;
class UTextBlock;
class UOptionCard;

UCLASS()
class GOLFSIM_API UPracticeSetup : public UUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> OnStartCtp;       // user chose Closest to Pin + Start
	TFunction<void()> OnStartPutting;   // GOL-75: user chose Putting + Start
	TFunction<void()> OnClose;          // Back / Esc -> return to the main menu

	void ResetSelection();          // call on open: clear the highlighted card + disable Start

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;

	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleStartClicked();

private:
	void BuildTree();
	void Select(int32 Index);       // highlight one card, deselect the rest, enable Start
	void RefreshStart();            // Start enabled only when an available drill is selected

	UPROPERTY(Transient) TArray<TObjectPtr<UOptionCard>> ModeCards;   // index 0 = CTP (available)
	UPROPERTY(Transient) TObjectPtr<UButton> StartBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StartLabel;

	int32 SelectedIndex = -1;       // -1 = nothing chosen yet
};
