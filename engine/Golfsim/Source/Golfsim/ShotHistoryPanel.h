// Session shot-history table (GOL-65). Pure-C++ UUserWidget, no WBP. Centered modal that fills
// most of the screen since a long range session can hold a hundred-plus shots.
//
// Single-session view: the caller (AGolfRangeHUD) hands in a fixed label + entry array via
// SetSession. There is no session picker inside this widget -- past sessions are reached via the
// main-menu UPreviousSessionsList; the in-range H key opens this with the live session only.
//
// Close affordance: a top-right Close button fires OnClose. The HUD decides what to show next
// (gameplay vs the previous-sessions list).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Session/ShotHistorySubsystem.h"   // FShotHistoryEntry
#include "ShotHistoryPanel.generated.h"

class UScrollBox;
class UVerticalBox;
class UTextBlock;
class UButton;

UCLASS()
class GOLFSIM_API UShotHistoryPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replace the panel contents: subtitle = Label + " · N shots", rows = the entries (newest-first). */
	void SetSession(const FString& Label, const TArray<FShotHistoryEntry>& InEntries);

	/** Fired by the Close button. The HUD chooses what to show next. */
	TFunction<void()> OnClose;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleCloseClicked();

private:
	void BuildTree();
	void RebuildRows(const TArray<FShotHistoryEntry>& InEntries);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> ListScroll;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ExportButton;   // "Export (coming soon)", disabled
};
