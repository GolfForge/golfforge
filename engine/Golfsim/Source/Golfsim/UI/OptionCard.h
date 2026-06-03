// Option card (GOL-142) -- a selectable card for the Round Setup wizard's Format step (game type +
// turn order): an icon square, a title, and a one-line rule, with a check badge when selected. Same
// interaction model as UI/CourseCard (UOverlay root hit-tests the whole card, hover lift + accent
// border, selected = accent ring + check, disabled = dim + non-interactive) -- and it lives in the
// flow scrollbox, where rounded-box brushes repaint fine (unlike the topbar; see GOL-153).
//
// The icon is a placeholder accent/surface square for now (no glyph); the real Lucide icon lands with
// GOL-151. Dumb view: reports selection via OnSelected.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OptionCard.generated.h"

class UBorder;
class UTextBlock;
class UOverlay;

UCLASS()
class GOLFSIM_API UOptionCard : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(const FString& Title, const FString& Desc);
	void SetSelected(bool bSelected);
	void SetDisabled(bool bDisabled);
	bool IsDisabled() const { return bIsDisabled; }

	TFunction<void()> OnSelected;   // card click (only when enabled)

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Ev) override;

private:
	void BuildTree();
	void RefreshVisualState();

	UPROPERTY(Transient) TObjectPtr<UBorder>    BgBorder;     // card fill + border (recolored on state)
	UPROPERTY(Transient) TObjectPtr<UBorder>    IconSquare;   // placeholder icon (accent when selected)
	UPROPERTY(Transient) TObjectPtr<UBorder>    CheckBadge;   // accent check, shown when selected
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescText;

	bool bIsDisabled = false;
	bool bIsSelected = false;
	bool bIsHovered = false;
};
