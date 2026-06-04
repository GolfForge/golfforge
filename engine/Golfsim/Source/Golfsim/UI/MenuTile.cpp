#include "UI/MenuTile.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Materials/MaterialInstanceDynamic.h"   // feed the wash material the tile size for its rounded mask
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UMenuTile::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);   // hit-test the whole tile for hover/click
	BuildTree();
}

void UMenuTile::BuildTree()
{
	using namespace GolfUI;

	// Root is a UOverlay (a layout panel that fills its slot and stretches Fill-aligned children) -- NOT
	// a UButton, which shrinks the card to its text content (that was the "tiles won't fill" bug).
	// Click/hover are handled on the widget itself (NativeOnMouseButtonDown / Enter / Leave).
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TileRoot"));
	WidgetTree->RootWidget = Root;

	auto FillSlot = [](UOverlaySlot* OS)
	{
		if (OS) { OS->SetHorizontalAlignment(HAlign_Fill); OS->SetVerticalAlignment(VAlign_Fill); }
	};

	// Background card + border, recolored on hover/selected.
	BgBorder = WidgetTree->ConstructWidget<UBorder>();
	BgBorder->SetBrush(RoundedBrush(Color::Bg1(), Radius::Lg, Color::Border(), 1.f));
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(BgBorder)));

	// Accent hover-wash: a vertical gradient (accent-soft at the bottom -> transparent up), full-bleed so
	// its left/right/top edges coincide with the tile edges (no inset "box"). It fades to transparent
	// well before the top, and accent-soft is faint, so the only un-rounded bit is the two bottom corners
	// over the dark stage -- negligible at this alpha (a rounded-box gradient material is the proper fix).
	AccentWash = MakeLinearGradient(WidgetTree, Color::AccentSoft(), FLinearColor(0.f, 0.f, 0.f, 0.f));
	AccentWash->SetVisibility(ESlateVisibility::Collapsed);
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(AccentWash)));

	// Padded body: panel-top pinned to the top, panel-bottom to the bottom (CSS space-between).
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>();
	Pad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	Pad->SetPadding(FMargin(24.f));
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(Pad)));

	UOverlay* BodyOverlay = WidgetTree->ConstructWidget<UOverlay>();
	Pad->SetContent(BodyOverlay);

	// ---- panel-top: index (left) + tag pill (right) ----
	UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UOverlaySlot* TS = Cast<UOverlaySlot>(BodyOverlay->AddChildToOverlay(Top));
		if (TS) { TS->SetHorizontalAlignment(HAlign_Fill); TS->SetVerticalAlignment(VAlign_Top); }
	}
	IndexText = WidgetTree->ConstructWidget<UTextBlock>();
	IndexText->SetText(FText::FromString(TEXT("01")));
	IndexText->SetFont(Mono(13));
	IndexText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	if (UHorizontalBoxSlot* IS = Top->AddChildToHorizontalBox(IndexText)) { IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
	TagPill->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0.4f), 999.f, Color::BorderStrong(), 1.f));
	TagPill->SetPadding(FMargin(10.f, 5.f));
	TagText = WidgetTree->ConstructWidget<UTextBlock>();
	TagText->SetText(FText::FromString(TEXT("")));
	{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 160; TagText->SetFont(F); }
	TagText->SetColorAndOpacity(FSlateColor(Color::Text()));
	TagPill->SetContent(TagText);
	if (UHorizontalBoxSlot* PS = Top->AddChildToHorizontalBox(TagPill)) { PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Top); }

	// ---- panel-bottom: resume pill (hero, hidden) + title + desc + cta ----
	UVerticalBox* Bottom = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		UOverlaySlot* BS = Cast<UOverlaySlot>(BodyOverlay->AddChildToOverlay(Bottom));
		if (BS) { BS->SetHorizontalAlignment(HAlign_Fill); BS->SetVerticalAlignment(VAlign_Bottom); }
	}

	ResumeBtn = WidgetTree->ConstructWidget<UButton>();
	StyleButton(ResumeBtn, Color::Accent(), 999.f);
	ResumeBtn->OnClicked.AddDynamic(this, &UMenuTile::HandleResumeClicked);
	ResumeText = WidgetTree->ConstructWidget<UTextBlock>();
	ResumeText->SetText(FText::FromString(TEXT("")));
	{ FSlateFontInfo F = Mono(12); ResumeText->SetFont(F); }
	ResumeText->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
	ResumeBtn->SetContent(ResumeText);
	ResumeBtn->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* RS = Bottom->AddChildToVerticalBox(ResumeBtn)) { RS->SetHorizontalAlignment(HAlign_Left); RS->SetPadding(FMargin(0, 0, 0, 10.f)); }

	TitleText = WidgetTree->ConstructWidget<UTextBlock>();
	TitleText->SetText(FText::FromString(TEXT("")));
	TitleText->SetFont(Display(34, FName(TEXT("Bold"))));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TitleText->SetAutoWrapText(true);
	Bottom->AddChildToVerticalBox(TitleText);

	DescText = WidgetTree->ConstructWidget<UTextBlock>();
	DescText->SetText(FText::FromString(TEXT("")));
	DescText->SetFont(Body(14));
	DescText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	DescText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Bottom->AddChildToVerticalBox(DescText)) { DS->SetPadding(FMargin(0, 8.f, 0, 0)); }

	UHorizontalBox* Cta = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* CS = Bottom->AddChildToVerticalBox(Cta)) { CS->SetPadding(FMargin(0, 12.f, 0, 0)); }
	CtaText = WidgetTree->ConstructWidget<UTextBlock>();
	CtaText->SetText(FText::FromString(TEXT("")));
	{ FSlateFontInfo F = Display(14, FName(TEXT("SemiBold"))); F.LetterSpacing = 80; CtaText->SetFont(F); }
	CtaText->SetColorAndOpacity(FSlateColor(Color::Accent()));
	Cta->AddChildToHorizontalBox(CtaText);
	CtaArrow = MakeIcon(WidgetTree, EIcon::ArrowRight, 14, Color::Accent());   // GOL-151 Lucide arrow-right
	if (UHorizontalBoxSlot* AS = Cta->AddChildToHorizontalBox(CtaArrow)) { AS->SetPadding(FMargin(8.f, 0, 0, 0)); AS->SetVerticalAlignment(VAlign_Center); }
}

void UMenuTile::Configure(const FString& Index, const FString& Tag, const FString& Title,
	const FString& Desc, const FString& Cta, bool bHero)
{
	bHeroTile = bHero;
	if (IndexText) { IndexText->SetText(FText::FromString(Index)); }
	if (TagText)   { TagText->SetText(FText::FromString(Tag.ToUpper())); }
	if (TitleText)
	{
		TitleText->SetText(FText::FromString(Title.ToUpper()));
		TitleText->SetFont(GolfUI::Display(bHero ? 64 : 34, FName(TEXT("Bold"))));
	}
	if (DescText) { DescText->SetText(FText::FromString(Desc)); }
	if (CtaText)  { CtaText->SetText(FText::FromString(Cta.ToUpper())); }
}

void UMenuTile::SetDisabled(bool bDisabled)
{
	bIsDisabled = bDisabled;
	SetRenderOpacity(bDisabled ? 0.45f : 1.f);
	if (bDisabled && TagText) { TagText->SetText(FText::FromString(TEXT("COMING SOON"))); }
	RefreshVisualState();
}

void UMenuTile::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;
	RefreshVisualState();
}

void UMenuTile::SetResumePill(bool bVisible, const FString& Text)
{
	if (!ResumeBtn) { return; }
	ResumeBtn->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (ResumeText) { ResumeText->SetText(FText::FromString(Text)); }
}

void UMenuTile::Activate()
{
	if (!bIsDisabled && OnActivated) { OnActivated(); }
}

FReply UMenuTile::NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (!bIsDisabled && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Activate();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(Geo, Ev);
}

void UMenuTile::NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev)
{
	Super::NativeOnMouseEnter(Geo, Ev);
	if (!bIsDisabled) { bIsHovered = true; RefreshVisualState(); }
}

void UMenuTile::NativeOnMouseLeave(const FPointerEvent& Ev)
{
	Super::NativeOnMouseLeave(Ev);
	bIsHovered = false;
	RefreshVisualState();
}

void UMenuTile::HandleResumeClicked() { if (OnResume) { OnResume(); } }

void UMenuTile::RefreshVisualState()
{
	const bool bActive = (bIsHovered || bIsSelected) && !bIsDisabled;
	if (BgBorder)
	{
		const FLinearColor BorderCol = bActive ? GolfUI::Color::AccentLine() : GolfUI::Color::Border();
		BgBorder->SetBrush(GolfUI::RoundedBrush(GolfUI::Color::Bg1(), GolfUI::Radius::Lg, BorderCol, bActive ? 1.5f : 1.f));
	}
	if (AccentWash)
	{
		AccentWash->SetVisibility(bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bActive)
		{
			// Feed the wash material the tile's pixel size so its rounded-box mask matches the card's
			// corners (the gradient is a flat rectangle otherwise; the mask rounds it to the card shape).
			if (UMaterialInstanceDynamic* MID = AccentWash->GetDynamicMaterial())
			{
				const FVector2D Sz = GetCachedGeometry().GetLocalSize();
				MID->SetScalarParameterValue(TEXT("SizeX"), (float)Sz.X);
				MID->SetScalarParameterValue(TEXT("SizeY"), (float)Sz.Y);
				MID->SetScalarParameterValue(TEXT("Radius"), (float)GolfUI::Radius::Lg);
			}
		}
	}
	if (CtaArrow)
	{
		CtaArrow->SetRenderTranslation(FVector2D(bActive ? 6.f : 0.f, 0.f));
	}
	GolfUI::SetHoverLift(this, bActive);
}
