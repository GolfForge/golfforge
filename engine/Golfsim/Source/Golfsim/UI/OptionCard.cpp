#include "UI/OptionCard.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UOptionCard::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);   // hit-test the whole card
	BuildTree();
}

void UOptionCard::BuildTree()
{
	using namespace GolfUI;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("OptionRoot"));
	WidgetTree->RootWidget = Root;

	auto FillSlot = [](UOverlaySlot* OS)
	{
		if (OS) { OS->SetHorizontalAlignment(HAlign_Fill); OS->SetVerticalAlignment(VAlign_Fill); }
	};

	BgBorder = WidgetTree->ConstructWidget<UBorder>();
	BgBorder->SetBrush(RoundedBrush(Color::Surface(), Radius::Md, Color::Border(), 1.f));
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(BgBorder)));

	// padded body
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>();
	Pad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	Pad->SetPadding(FMargin(16.f));
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(Pad)));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Pad->SetContent(Col);

	// icon chip: a small left-aligned square holding the option's Lucide glyph (accent when selected)
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
	IconBox->SetWidthOverride(34.f); IconBox->SetHeightOverride(34.f);
	IconSquare = WidgetTree->ConstructWidget<UBorder>();
	IconSquare->SetBrush(RoundedBrush(Color::Surface2(), 9.f));
	IconSquare->SetHorizontalAlignment(HAlign_Center);
	IconSquare->SetVerticalAlignment(VAlign_Center);
	IconGlyphText = MakeIcon(WidgetTree, IconGlyph, 18, Color::TextDim());
	IconSquare->SetContent(IconGlyphText);
	IconBox->SetContent(IconSquare);
	if (UVerticalBoxSlot* IBS = Col->AddChildToVerticalBox(IconBox)) { IBS->SetHorizontalAlignment(HAlign_Left); }

	TitleText = WidgetTree->ConstructWidget<UTextBlock>();
	TitleText->SetText(FText::FromString(TEXT("")));
	TitleText->SetFont(Display(17, FName(TEXT("Bold"))));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(TitleText)) { TS->SetPadding(FMargin(0, 12.f, 0, 0)); }

	DescText = WidgetTree->ConstructWidget<UTextBlock>();
	DescText->SetText(FText::FromString(TEXT("")));
	DescText->SetFont(Body(12));
	DescText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	DescText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Col->AddChildToVerticalBox(DescText)) { DS->SetPadding(FMargin(0, 5.f, 0, 0)); }

	// check badge top-right, shown when selected
	USizeBox* CheckBox = WidgetTree->ConstructWidget<USizeBox>();
	CheckBox->SetWidthOverride(22.f); CheckBox->SetHeightOverride(22.f);
	CheckBadge = WidgetTree->ConstructWidget<UBorder>();
	CheckBadge->SetBrush(RoundedBrush(Color::Accent(), 999.f));
	CheckBadge->SetVisibility(ESlateVisibility::Collapsed);
	CheckBadge->SetHorizontalAlignment(HAlign_Center);
	CheckBadge->SetVerticalAlignment(VAlign_Center);
	CheckBadge->SetContent(MakeIcon(WidgetTree, EIcon::Check, 11, Color::AccentInk()));   // GOL-151 Lucide check
	CheckBox->SetContent(CheckBadge);
	if (UOverlaySlot* CS = Cast<UOverlaySlot>(Root->AddChildToOverlay(CheckBox)))
	{
		CS->SetHorizontalAlignment(HAlign_Right); CS->SetVerticalAlignment(VAlign_Top);
		CS->SetPadding(FMargin(0, 12.f, 12.f, 0));
	}
}

void UOptionCard::Configure(const FString& Title, const FString& Desc, GolfUI::EIcon Icon)
{
	IconGlyph = Icon;
	if (TitleText)     { TitleText->SetText(FText::FromString(Title)); }
	if (DescText)      { DescText->SetText(FText::FromString(Desc)); }
	if (IconGlyphText) { IconGlyphText->SetText(FText::FromString(FString::Chr(static_cast<TCHAR>(Icon)))); }
}

void UOptionCard::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;
	RefreshVisualState();
}

void UOptionCard::SetDisabled(bool bDisabled)
{
	bIsDisabled = bDisabled;
	SetRenderOpacity(bDisabled ? 0.45f : 1.f);
	RefreshVisualState();
}

FReply UOptionCard::NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (!bIsDisabled && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnSelected) { OnSelected(); }
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(Geo, Ev);
}

void UOptionCard::NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev)
{
	Super::NativeOnMouseEnter(Geo, Ev);
	if (!bIsDisabled) { bIsHovered = true; RefreshVisualState(); }
}

void UOptionCard::NativeOnMouseLeave(const FPointerEvent& Ev)
{
	Super::NativeOnMouseLeave(Ev);
	bIsHovered = false;
	RefreshVisualState();
}

void UOptionCard::RefreshVisualState()
{
	using namespace GolfUI;
	if (BgBorder)
	{
		if (bIsSelected && !bIsDisabled)
		{
			BgBorder->SetBrush(RoundedBrush(Color::AccentSoft(), Radius::Md, Color::AccentLine(), 1.5f));
		}
		else if (bIsHovered && !bIsDisabled)
		{
			BgBorder->SetBrush(RoundedBrush(Color::Surface2(), Radius::Md, Color::BorderStrong(), 1.f));
		}
		else
		{
			BgBorder->SetBrush(RoundedBrush(Color::Surface(), Radius::Md, Color::Border(), 1.f));
		}
	}
	const bool bSel = bIsSelected && !bIsDisabled;
	if (IconSquare)
	{
		IconSquare->SetBrush(RoundedBrush(bSel ? Color::Accent() : Color::Surface2(), 9.f));
	}
	if (IconGlyphText)
	{
		IconGlyphText->SetColorAndOpacity(FSlateColor(bSel ? Color::AccentInk() : Color::TextDim()));
	}
	if (CheckBadge)
	{
		CheckBadge->SetVisibility((bIsSelected && !bIsDisabled) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	SetHoverLift(this, bIsHovered && !bIsDisabled, -3.f);
}
