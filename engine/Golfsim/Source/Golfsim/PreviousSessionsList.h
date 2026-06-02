// Previous-sessions picker (GOL-65). Opened from the main menu "Previous Sessions" button.
// A centered, scrollable list of past sessions; clicking one opens the full UShotHistoryPanel
// for that session. Closing returns to the main menu.
//
// AGolfRangeHUD owns mount/unmount + visibility. This widget is a dumb view: it gets a list of
// (label, sessionId) pairs and reports clicks back via OnSessionPicked.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PreviousSessionsList.generated.h"

class UScrollBox;
class UButton;
class UTextBlock;

USTRUCT()
struct FPreviousSessionInfo
{
	GENERATED_BODY()

	UPROPERTY() FString SessionId;    // raw id (the .jsonl basename)
	UPROPERTY() FString DisplayLabel; // e.g. "2026-06-01T15-04-22   (17 shots)"
};

/**
 * Per-row child widget. One button + a session id; click fires OnSelected with the id.
 * Lives here rather than in its own file because it's an internal helper for the list.
 */
UCLASS()
class GOLFSIM_API UPreviousSessionRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void Init(const FPreviousSessionInfo& Row);
	TFunction<void(const FString& /*SessionId*/)> OnSelected;

protected:
	virtual void NativeOnInitialized() override;
	UFUNCTION() void HandleClicked();

private:
	UPROPERTY(Transient) TObjectPtr<UButton> Btn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl;
	FString CachedSessionId;
};

UCLASS()
class GOLFSIM_API UPreviousSessionsList : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replace the row list. Empty array shows an "(no past sessions yet)" placeholder line. */
	void SetSessions(const TArray<FPreviousSessionInfo>& InRows);

	/** Fired with the raw SessionId of the clicked row. */
	TFunction<void(const FString& /*SessionId*/)> OnSessionPicked;

	/** Fired by the Close/Back button. */
	TFunction<void()> OnClose;

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleCloseClicked();

private:
	void BuildTree();
	void RebuildRows();

	UPROPERTY(Transient) TObjectPtr<UScrollBox> RowScroll;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Subtitle;

	TArray<FPreviousSessionInfo> Rows;
};
