#include "UI/LeaveConfirmDialog.h"

#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"   // FButtonStyle

using namespace GolfUI;

namespace
{
	FLinearColor A(FLinearColor C, float Alpha) { C.A = Alpha; return C; }
}

void ULeaveConfirmDialog::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);   // so NativeOnKeyDown (Esc / Enter) fires once the HUD gives us focus
	BuildTree();
}

FReply ULeaveConfirmDialog::NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		if (OnCancel) { OnCancel(); }
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		if (OnConfirm) { OnConfirm(); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geo, KeyEvent);
}

void ULeaveConfirmDialog::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// ── blurred dim backdrop (decorative; doesn't eat clicks) ──
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(4.f);
	Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
	{
		UBorder* Tint = WidgetTree->ConstructWidget<UBorder>();
		Tint->SetBrushColor(FLinearColor(0.012f, 0.027f, 0.020f, 0.62f));
		Tint->SetVisibility(ESlateVisibility::HitTestInvisible);
		Blur->SetContent(Tint);
	}
	if (UCanvasPanelSlot* BS = Root->AddChildToCanvas(Blur))
	{
		BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BS->SetOffsets(FMargin(0.f));
	}

	// ── transparent scrim button: click-outside cancels ──
	UButton* Scrim = WidgetTree->ConstructWidget<UButton>();
	{
		FButtonStyle S;
		const FSlateBrush Clear = RoundedBrush(FLinearColor(0.f, 0.f, 0.f, 0.f), 0.f);
		S.SetNormal(Clear); S.SetHovered(Clear); S.SetPressed(Clear); S.SetDisabled(Clear);
		Scrim->SetStyle(S);
	}
	Scrim->OnClicked.AddDynamic(this, &ULeaveConfirmDialog::HandleCancelClicked);
	if (UCanvasPanelSlot* SS = Root->AddChildToCanvas(Scrim))
	{
		SS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		SS->SetOffsets(FMargin(0.f));
	}

	// ── centered card ──
	USizeBox* CardBox = WidgetTree->ConstructWidget<USizeBox>();
	CardBox->SetWidthOverride(440.f);
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardBox))
	{
		CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CS->SetAlignment(FVector2D(0.5f, 0.5f));
		CS->SetAutoSize(true);
	}
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(RoundedBrush(Color::Bg1(), Radius::Lg, Color::BorderStrong(), 1.f));
	Card->SetPadding(FMargin(30.f, 30.f, 30.f, 22.f));
	CardBox->SetContent(Card);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// icon tile (tint set per-mode in Configure; default = course/accent)
	{
		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
		IconBox->SetWidthOverride(54.f);
		IconBox->SetHeightOverride(54.f);
		IconTile = WidgetTree->ConstructWidget<UBorder>();
		IconTile->SetBrush(RoundedBrush(Color::AccentSoft(), 14.f, Color::AccentLine(), 1.f));
		IconTile->SetHorizontalAlignment(HAlign_Center);
		IconTile->SetVerticalAlignment(VAlign_Center);
		IconGlyph = WidgetTree->ConstructWidget<UTextBlock>();
		IconGlyph->SetText(FText::FromString(TEXT("→")));   // placeholder; Lucide "log-out" lands in GOL-151
		IconGlyph->SetFont(Display(26, FName(TEXT("SemiBold"))));
		IconGlyph->SetColorAndOpacity(FSlateColor(Color::Accent()));
		IconTile->SetContent(IconGlyph);
		IconBox->SetContent(IconTile);
		if (UVerticalBoxSlot* IS = Col->AddChildToVerticalBox(IconBox))
		{
			IS->SetHorizontalAlignment(HAlign_Center);
			IS->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
		}
	}

	// title
	TitleText = MakeTitle(WidgetTree, TEXT("Leave this round?"), 26);
	TitleText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(TitleText)) { TS->SetHorizontalAlignment(HAlign_Center); }

	// description
	DescText = WidgetTree->ConstructWidget<UTextBlock>();
	DescText->SetFont(Body(14));
	DescText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	DescText->SetJustification(ETextJustify::Center);
	DescText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Col->AddChildToVerticalBox(DescText))
	{
		DS->SetHorizontalAlignment(HAlign_Fill);
		DS->SetPadding(FMargin(6.f, 10.f, 6.f, 0.f));
	}

	// actions: Keep (neutral) + Leave (destructive)
	{
		UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();

		UButton* Stay = WidgetTree->ConstructWidget<UButton>();
		StyleButton(Stay, Color::Surface2(), Radius::Md, Color::BorderStrong(), 1.f);
		Stay->OnClicked.AddDynamic(this, &ULeaveConfirmDialog::HandleCancelClicked);
		StayLabel = WidgetTree->ConstructWidget<UTextBlock>();
		StayLabel->SetFont(Display(15, FName(TEXT("SemiBold"))));
		StayLabel->SetColorAndOpacity(FSlateColor(Color::Text()));
		StayLabel->SetJustification(ETextJustify::Center);
		Stay->SetContent(StayLabel);
		if (UHorizontalBoxSlot* HS = Actions->AddChildToHorizontalBox(Stay))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
		}

		UButton* Leave = WidgetTree->ConstructWidget<UButton>();
		{
			const FLinearColor Red = Color::DangerFill();
			FButtonStyle S;
			S.SetNormal(RoundedBrush(FLinearColor(0.f, 0.f, 0.f, 0.f), Radius::Md, A(Red, 0.5f), 1.f));
			S.SetHovered(RoundedBrush(A(Red, 0.14f), Radius::Md, A(Red, 0.7f), 1.f));
			S.SetPressed(RoundedBrush(A(Red, 0.10f), Radius::Md, A(Red, 0.7f), 1.f));
			S.SetDisabled(RoundedBrush(FLinearColor(0.f, 0.f, 0.f, 0.f), Radius::Md, A(Red, 0.2f), 1.f));
			S.SetNormalPadding(FMargin(18.f, 12.f));
			S.SetPressedPadding(FMargin(18.f, 12.f));
			Leave->SetStyle(S);
		}
		Leave->OnClicked.AddDynamic(this, &ULeaveConfirmDialog::HandleConfirmClicked);
		ConfirmLabel = WidgetTree->ConstructWidget<UTextBlock>();
		ConfirmLabel->SetFont(Display(15, FName(TEXT("SemiBold"))));
		ConfirmLabel->SetColorAndOpacity(FSlateColor(Color::DangerText()));
		ConfirmLabel->SetJustification(ETextJustify::Center);
		Leave->SetContent(ConfirmLabel);
		if (UHorizontalBoxSlot* HS = Actions->AddChildToHorizontalBox(Leave))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
		}

		if (UVerticalBoxSlot* AS = Col->AddChildToVerticalBox(Actions))
		{
			AS->SetHorizontalAlignment(HAlign_Fill);
			AS->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
		}
	}

	// hint line: [Esc] to stay · [Enter] to leave
	{
		UHorizontalBox* Hint = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto AddText = [&](const TCHAR* T)
		{
			UTextBlock* B = WidgetTree->ConstructWidget<UTextBlock>();
			B->SetText(FText::FromString(T));
			B->SetFont(Mono(11));
			B->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
			if (UHorizontalBoxSlot* S = Hint->AddChildToHorizontalBox(B)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(4.f, 0.f)); }
		};
		auto AddKey = [&](const TCHAR* K)
		{
			if (UHorizontalBoxSlot* S = Hint->AddChildToHorizontalBox(MakeKbd(WidgetTree, K))) { S->SetVerticalAlignment(VAlign_Center); }
		};
		AddKey(TEXT("Esc"));
		AddText(TEXT("to stay  ·"));
		AddKey(TEXT("Enter"));
		AddText(TEXT("to leave"));
		if (UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(Hint))
		{
			HS->SetHorizontalAlignment(HAlign_Center);
			HS->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));
		}
	}
}

void ULeaveConfirmDialog::Configure(ELeaveMode Mode, int32 HoleNum)
{
	const bool bUnsaved = (Mode != ELeaveMode::Course);

	// icon tint: green when progress is kept (course), amber otherwise
	const FLinearColor Tint = bUnsaved ? Color::Caution() : Color::Accent();
	if (IconTile)  { IconTile->SetBrush(RoundedBrush(A(Tint, 0.16f), 14.f, A(Tint, 0.45f), 1.f)); }
	if (IconGlyph) { IconGlyph->SetColorAndOpacity(FSlateColor(Tint)); }

	FString Title, Desc, Stay, Confirm;
	switch (Mode)
	{
		case ELeaveMode::Range:
			Title   = TEXT("Leave the range?");
			Desc    = TEXT("Range sessions aren't saved — your shot history will be cleared.");
			Stay    = TEXT("Keep hitting");
			Confirm = TEXT("Leave the range");
			break;
		case ELeaveMode::Practice:
			Title   = TEXT("End practice session?");
			Desc    = TEXT("Practice sessions aren't saved — this session won't be kept.");
			Stay    = TEXT("Keep practicing");
			Confirm = TEXT("End session");
			break;
		case ELeaveMode::Course:
		default:
			Title   = TEXT("Leave this round?");
			Desc    = FString::Printf(TEXT("You're on Hole %d. Your progress is auto-saved — you can pick this round back up from the menu."), HoleNum);
			Stay    = TEXT("Keep playing");
			Confirm = TEXT("Leave to menu");
			break;
	}

	if (TitleText)    { TitleText->SetText(FText::FromString(Title)); }
	if (DescText)     { DescText->SetText(FText::FromString(Desc)); }
	if (StayLabel)    { StayLabel->SetText(FText::FromString(Stay.ToUpper())); }
	if (ConfirmLabel) { ConfirmLabel->SetText(FText::FromString(Confirm.ToUpper() + TEXT("  →"))); }
}

void ULeaveConfirmDialog::HandleConfirmClicked()
{
	if (OnConfirm) { OnConfirm(); }
}

void ULeaveConfirmDialog::HandleCancelClicked()
{
	if (OnCancel) { OnCancel(); }
}
