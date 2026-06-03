// Bento tile for the main menu (GOL-139, epic GOL-137). One reusable cell — index + pill tag,
// Barlow-Condensed title, description, and an accent CTA with a nudging arrow — over a glass card.
// Pure-C++ UUserWidget built in NativeOnInitialized via WidgetTree (codebase idiom); consumes
// GolfUITheme atoms. Hover lifts + accent-glows the tile and slides the CTA arrow (BUILD_SPEC §7:
// the lift is decorative; the selected/disabled look never depends on an animation). Keyboard
// selection mirrors the hover highlight. Dumb view: reports intent via TFunction.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuTile.generated.h"

class UButton;
class UBorder;
class UTextBlock;
class UOverlay;
class UImage;

UCLASS()
class GOLFSIM_API UMenuTile : public UUserWidget
{
	GENERATED_BODY()

public:
	// Fill the tile. bHero = the large col-1 hero cell (bigger title). Call after CreateWidget.
	void Configure(const FString& Index, const FString& Tag, const FString& Title,
		const FString& Desc, const FString& Cta, bool bHero = false);

	void SetDisabled(bool bDisabled);                          // dim + "Coming soon" + no activate
	void SetSelected(bool bSelected);                          // keyboard-selection accent ring
	void SetResumePill(bool bVisible, const FString& Text);    // hero only; hidden by default
	bool IsDisabled() const { return bIsDisabled; }
	void Activate();                                           // keyboard Enter -> OnActivated (if enabled)

	TFunction<void()> OnActivated;   // tile click / Enter
	TFunction<void()> OnResume;      // resume pill (nested action; does not fire OnActivated)

protected:
	virtual void NativeOnInitialized() override;
	// Interaction lives on the widget itself (not a wrapping UButton, which would shrink the card to
	// its content). The root is a UOverlay that fills its cell; we hit-test the whole tile here.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Ev) override;

	UFUNCTION() void HandleResumeClicked();

private:
	void BuildTree();
	void RefreshVisualState();   // border + accent-wash + arrow per hovered|selected|disabled

	UPROPERTY(Transient) TObjectPtr<UBorder>    BgBorder;       // card fill + border (recolored on state)
	UPROPERTY(Transient) TObjectPtr<UImage>     AccentWash;     // accent-soft gradient sheen, shown when active
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IndexText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TagText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtaText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CtaArrow;       // slides right on hover
	UPROPERTY(Transient) TObjectPtr<UButton>    ResumeBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResumeText;

	bool bHeroTile = false;
	bool bIsDisabled = false;
	bool bIsSelected = false;
	bool bIsHovered = false;
};
