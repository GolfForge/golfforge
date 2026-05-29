// Startup main menu (UMG). Pure-C++ UUserWidget, same idiom as USettingsMenu. Shows over the
// already-loaded range behind a barely-blurred backdrop (so "Range" is an instant dismiss, not a
// level load). Range -> OnPlayRange (HUD dismisses + hands control to the range); Play Course is
// disabled with a "Coming soon" note; Exit quits. Dumb view: reports intent via TFunction.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenu.generated.h"

class UButton;

UCLASS()
class GOLFSIM_API UMainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> OnPlayRange;   // set by the HUD: dismiss the menu, hand control to the live range

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleRangeClicked();
	UFUNCTION() void HandleQuitClicked();

private:
	void BuildTree();
	UButton* MakeButton(const TCHAR* Label, bool bEnabled = true);   // light button + black centered label
};
