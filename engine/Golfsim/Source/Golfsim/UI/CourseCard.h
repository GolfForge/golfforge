// Course card (GOL-141, epic GOL-137) -- one cell in the Round Setup wizard's 3-up course grid.
// Image-slot placeholder + type flag, name, location, holes/par/yards, a 5-dot difficulty meter, and
// a check badge that shows when selected. Interaction mirrors UMenuTile: the root is a UOverlay that
// hit-tests the whole card (a UButton would shrink to its content), hover lifts + accent-borders the
// card, and -- per BUILD_SPEC §7 -- the selected look (accent ring + check) is the base style, never
// gated on an animation. Dumb view: reports selection via OnSelected. Disabled = unbuilt placeholder.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/CourseRegistry.h"
#include "CourseCard.generated.h"

class UBorder;
class UTextBlock;
class UOverlay;
class UHorizontalBox;

UCLASS()
class GOLFSIM_API UCourseCard : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(const FGolfCourseInfo& Info);   // fill content; sets the unbuilt-placeholder look
	void SetSelected(bool bSelected);              // accent ring + check badge
	bool IsDisabled() const { return bIsDisabled; }
	const FString& GetCourseId() const { return CourseId; }

	TFunction<void()> OnSelected;   // card click (only when enabled)

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Ev) override;

private:
	void BuildTree();
	void RefreshVisualState();   // border ring + hover lift per hovered|selected|disabled

	UPROPERTY(Transient) TObjectPtr<UBorder>        BgBorder;     // card fill + border (recolored on state)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>     FlagText;     // type pill label
	UPROPERTY(Transient) TObjectPtr<UTextBlock>     NameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>     LocText;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> StatsRow;     // holes / par / yards + dot meter
	UPROPERTY(Transient) TObjectPtr<UBorder>        CheckBadge;   // accent circle, shown when selected

	FString CourseId;
	bool bIsDisabled = false;
	bool bIsSelected = false;
	bool bIsHovered = false;
};
