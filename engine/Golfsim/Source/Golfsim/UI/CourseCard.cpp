#include "UI/CourseCard.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UCourseCard::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);   // hit-test the whole card for hover/click
	BuildTree();
}

void UCourseCard::BuildTree()
{
	using namespace GolfUI;

	// Root overlay fills the grid cell; click/hover handled on the widget itself (not a UButton, which
	// would shrink the card to its content -- same reason as UMenuTile).
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CardRoot"));
	WidgetTree->RootWidget = Root;

	auto FillSlot = [](UOverlaySlot* OS)
	{
		if (OS) { OS->SetHorizontalAlignment(HAlign_Fill); OS->SetVerticalAlignment(VAlign_Fill); }
	};

	BgBorder = WidgetTree->ConstructWidget<UBorder>();
	BgBorder->SetBrush(RoundedBrush(Color::Bg1(), Radius::Lg, Color::Border(), 1.f));
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(BgBorder)));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	FillSlot(Cast<UOverlaySlot>(Root->AddChildToOverlay(Content)));

	// ── image slot (placeholder block) + flag pill + check badge ──
	USizeBox* ImgBox = WidgetTree->ConstructWidget<USizeBox>();
	ImgBox->SetHeightOverride(132.f);
	Content->AddChildToVerticalBox(ImgBox);

	UOverlay* ImgOverlay = WidgetTree->ConstructWidget<UOverlay>();
	ImgBox->SetContent(ImgOverlay);

	UBorder* ImgFill = WidgetTree->ConstructWidget<UBorder>();
	ImgFill->SetBrush(RoundedBrush(Color::Surface2(), Radius::Sm));
	FillSlot(Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(ImgFill)));

	// type flag pill, top-left
	UBorder* FlagPill = WidgetTree->ConstructWidget<UBorder>();
	FlagPill->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0.45f), Radius::Sm, Color::BorderStrong(), 1.f));
	FlagPill->SetPadding(FMargin(9.f, 4.f));
	FlagText = WidgetTree->ConstructWidget<UTextBlock>();
	FlagText->SetText(FText::FromString(TEXT("")));
	{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 100; FlagText->SetFont(F); }
	FlagText->SetColorAndOpacity(FSlateColor(Color::Text()));
	FlagPill->SetContent(FlagText);
	if (UOverlaySlot* FS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(FlagPill)))
	{
		FS->SetHorizontalAlignment(HAlign_Left); FS->SetVerticalAlignment(VAlign_Top);
		FS->SetPadding(FMargin(10.f, 10.f, 0.f, 0.f));
	}

	// check badge, top-right (accent circle), shown when selected
	USizeBox* CheckBox = WidgetTree->ConstructWidget<USizeBox>();
	CheckBox->SetWidthOverride(28.f); CheckBox->SetHeightOverride(28.f);
	CheckBadge = WidgetTree->ConstructWidget<UBorder>();
	CheckBadge->SetBrush(RoundedBrush(Color::Accent(), 999.f));
	CheckBadge->SetVisibility(ESlateVisibility::Collapsed);
	CheckBadge->SetHorizontalAlignment(HAlign_Center);
	CheckBadge->SetVerticalAlignment(VAlign_Center);
	CheckBadge->SetContent(MakeIcon(WidgetTree, EIcon::Check, 13, Color::AccentInk()));   // GOL-151 Lucide check
	CheckBox->SetContent(CheckBadge);
	if (UOverlaySlot* CS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(CheckBox)))
	{
		CS->SetHorizontalAlignment(HAlign_Right); CS->SetVerticalAlignment(VAlign_Top);
		CS->SetPadding(FMargin(0.f, 10.f, 10.f, 0.f));
	}

	// bottom scrim under the flag/name (subtle) -- a dark gradient up from the image bottom.
	UImage* Scrim = MakeLinearGradient(WidgetTree, FLinearColor(0.035f, 0.05f, 0.04f, 0.85f), FLinearColor(0, 0, 0, 0));
	Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
	FillSlot(Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(Scrim)));

	// ── body: name / location / stats ── (BodyPad, not "Body", to not shadow GolfUI::Body)
	UBorder* BodyPad = WidgetTree->ConstructWidget<UBorder>();
	BodyPad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	BodyPad->SetPadding(FMargin(15.f, 13.f, 15.f, 15.f));
	Content->AddChildToVerticalBox(BodyPad);

	UVerticalBox* BodyCol = WidgetTree->ConstructWidget<UVerticalBox>();
	BodyPad->SetContent(BodyCol);

	NameText = WidgetTree->ConstructWidget<UTextBlock>();
	NameText->SetText(FText::FromString(TEXT("")));
	NameText->SetFont(Display(19, FName(TEXT("Bold"))));
	NameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	BodyCol->AddChildToVerticalBox(NameText);

	LocText = WidgetTree->ConstructWidget<UTextBlock>();
	LocText->SetText(FText::FromString(TEXT("")));
	LocText->SetFont(Body(12));
	LocText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	if (UVerticalBoxSlot* LS = BodyCol->AddChildToVerticalBox(LocText)) { LS->SetPadding(FMargin(0, 4.f, 0, 0)); }

	StatsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* SS = BodyCol->AddChildToVerticalBox(StatsRow)) { SS->SetPadding(FMargin(0, 12.f, 0, 0)); }
}

void UCourseCard::Configure(const FGolfCourseInfo& Info)
{
	using namespace GolfUI;
	CourseId = Info.Id;
	if (FlagText) { FlagText->SetText(FText::FromString(Info.Type.ToUpper())); }
	if (NameText) { NameText->SetText(FText::FromString(Info.Name)); }
	if (LocText)  { LocText->SetText(FText::FromString(Info.Location)); }

	// ── (re)build the stats row: value+label pairs in mono, then a 5-dot difficulty meter ──
	if (StatsRow)
	{
		StatsRow->ClearChildren();

		// emphasized value + dim label, e.g. "18 holes" / "Par 72" / "7,040 yd"
		auto AddStat = [&](const FString& Lead, const FString& Strong, const FString& Trail)
		{
			UHorizontalBox* Stat = WidgetTree->ConstructWidget<UHorizontalBox>();
			auto AddPart = [&](const FString& Txt, const FLinearColor& Col)
			{
				if (Txt.IsEmpty()) { return; }
				UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
				T->SetText(FText::FromString(Txt));
				T->SetFont(Mono(12));
				T->SetColorAndOpacity(FSlateColor(Col));
				if (UHorizontalBoxSlot* PS = Stat->AddChildToHorizontalBox(T)) { PS->SetVerticalAlignment(VAlign_Center); }
			};
			AddPart(Lead, Color::TextDim());
			AddPart(Strong, Color::Text());
			AddPart(Trail, Color::TextDim());
			if (UHorizontalBoxSlot* StatSlot = StatsRow->AddChildToHorizontalBox(Stat)) { StatSlot->SetPadding(FMargin(0, 0, 14.f, 0)); }
		};
		AddStat(FString(), FString::Printf(TEXT("%d "), Info.Holes), TEXT("holes"));
		AddStat(TEXT("Par "), FString::Printf(TEXT("%d"), Info.Par), FString());
		AddStat(FString(), Info.Yards + TEXT(" "), TEXT("yd"));

		// difficulty dot meter (right-aligned): filled = accent, empty = surface3
		UHorizontalBox* Dots = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 5; ++i)
		{
			USizeBox* DotBox = WidgetTree->ConstructWidget<USizeBox>();
			DotBox->SetWidthOverride(6.f); DotBox->SetHeightOverride(6.f);
			UBorder* Dot = WidgetTree->ConstructWidget<UBorder>();
			Dot->SetBrush(RoundedBrush(i < Info.Difficulty ? Color::Accent() : Color::Surface3(), 999.f));
			DotBox->SetContent(Dot);
			if (UHorizontalBoxSlot* DS = Dots->AddChildToHorizontalBox(DotBox)) { DS->SetPadding(FMargin(0, 0, 3.f, 0)); DS->SetVerticalAlignment(VAlign_Center); }
		}
		if (UHorizontalBoxSlot* DotsSlot = StatsRow->AddChildToHorizontalBox(Dots))
		{
			DotsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			DotsSlot->SetHorizontalAlignment(HAlign_Right);
			DotsSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	bIsDisabled = !Info.bAvailable;
	SetRenderOpacity(bIsDisabled ? 0.45f : 1.f);
	RefreshVisualState();
}

void UCourseCard::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;
	RefreshVisualState();
}

FReply UCourseCard::NativeOnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (!bIsDisabled && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnSelected) { OnSelected(); }
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(Geo, Ev);
}

void UCourseCard::NativeOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Ev)
{
	Super::NativeOnMouseEnter(Geo, Ev);
	if (!bIsDisabled) { bIsHovered = true; RefreshVisualState(); }
}

void UCourseCard::NativeOnMouseLeave(const FPointerEvent& Ev)
{
	Super::NativeOnMouseLeave(Ev);
	bIsHovered = false;
	RefreshVisualState();
}

void UCourseCard::RefreshVisualState()
{
	using namespace GolfUI;
	if (BgBorder)
	{
		FLinearColor BorderCol = Color::Border();
		float Width = 1.f;
		if (!bIsDisabled)
		{
			if (bIsSelected)      { BorderCol = Color::Accent();     Width = 2.f; }   // selected ring (base style)
			else if (bIsHovered)  { BorderCol = Color::AccentLine(); Width = 1.5f; }
		}
		BgBorder->SetBrush(RoundedBrush(Color::Bg1(), Radius::Lg, BorderCol, Width));
	}
	if (CheckBadge)
	{
		CheckBadge->SetVisibility((bIsSelected && !bIsDisabled) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// Lift only on hover (decorative); the selected resting state never depends on it (§7).
	SetHoverLift(this, bIsHovered && !bIsDisabled, -3.f);
}
