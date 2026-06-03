// Mode-aware leave/quit confirmation modal (GOL-147, epic GOL-137). A centered glass card behind a
// blurred dim scrim, shown in front of the HUD's leave-to-menu paths so a round / range session is
// never abandoned on a single accidental click. Matches Build/handoff/screens/07-hud-leave-dialog.png.
//
// Mode-aware copy + icon: Course (auto-saved, green) / Range + Practice (unsaved, amber). The owning
// HUD calls Configure() then shows it; OnConfirm routes to AGolfRangeHUD::ReturnToMainMenu(), OnCancel
// just dismisses. Esc = cancel, Enter = confirm, click-outside = cancel.
//
// No entrance animation by design (per BUILD_SPEC §7 + the codebase's all-instant UI grain): the
// resting state IS the visible state, so it can never be left half-shown.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LeaveConfirmDialog.generated.h"

class UBorder;
class UTextBlock;

UENUM()
enum class ELeaveMode : uint8
{
	Course,    // active round -- progress auto-saved (green icon)
	Range,     // driving range -- shot history cleared (amber icon)
	Practice,  // practice session -- not kept (amber icon); mode currently disabled, copy present
};

UCLASS()
class GOLFSIM_API ULeaveConfirmDialog : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set the mode-aware icon tint, title, body copy, and button labels. HoleNum is only used for
	    the Course copy ("You're on Hole N."). */
	void Configure(ELeaveMode Mode, int32 HoleNum);

	TFunction<void()> OnConfirm;   // "Leave" -> HUD::ReturnToMainMenu()
	TFunction<void()> OnCancel;    // "Keep playing" / Esc / click-outside -> dismiss

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent) override;

private:
	void BuildTree();

	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleCancelClicked();

	UPROPERTY(Transient) TObjectPtr<UBorder> IconTile;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IconGlyph;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StayLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmLabel;
};
