// Tab-toggled key-binding cheat sheet (GOL-65 sibling). Pure-C++ UUserWidget. A static
// two-column list of "Key  -> Action" rows so the dev / early user can see what's bound
// without grepping the code. Read-only; a future keybindings UI replaces this.
//
// All bindings live in the AGolfRangeHUD::EnsureInputBound table -- when that table changes,
// update kBindings below to match. Drift is fine to catch in PIE (it'll just read wrong).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CheatSheetPanel.generated.h"

class UButton;

UCLASS()
class GOLFSIM_API UCheatSheetPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> OnClose;   // Close button click

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleCloseClicked();

private:
	void BuildTree();
};
