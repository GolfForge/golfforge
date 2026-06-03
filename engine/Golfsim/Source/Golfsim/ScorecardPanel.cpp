#include "ScorecardPanel.h"

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

using namespace GolfUI;

namespace
{
	constexpr float CardWidthPx = 540.f;

	// vs-par cell color, mapped to the GolfUITheme palette (GOL-148).
	FLinearColor ColorForVsPar(int32 VsPar)
	{
		if (VsPar <  0) { return Color::Accent(); }     // birdie / eagle
		if (VsPar == 0) { return Color::Text(); }       // par ("E")
		if (VsPar == 1) { return Color::Caution(); }    // bogey
		return Color::DangerText();                      // double bogey or worse
	}

	UTextBlock* Cell(UUserWidget* Owner, const FString& Text, const FLinearColor& Col,
		const FSlateFontInfo& Font, ETextJustify::Type Justify = ETextJustify::Center)
	{
		UTextBlock* T = Owner->WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetColorAndOpacity(FSlateColor(Col));
		T->SetFont(Font);
		T->SetJustification(Justify);
		return T;
	}

	// A 4-column row (Hole | Par | Strokes | +/-). The first three use NeutralCol; the +/- cell uses DColor.
	UHorizontalBox* MakeRow(UUserWidget* Owner, const FString& A, const FString& B, const FString& C,
		const FString& D, const FLinearColor& NeutralCol, const FLinearColor& DColor, const FSlateFontInfo& Font)
	{
		UHorizontalBox* Row = Owner->WidgetTree->ConstructWidget<UHorizontalBox>();
		const TArray<TPair<FString, FLinearColor>> Cells = {
			{ A, NeutralCol }, { B, NeutralCol }, { C, NeutralCol }, { D, DColor }
		};
		for (const TPair<FString, FLinearColor>& C2 : Cells)
		{
			UTextBlock* T = Cell(Owner, C2.Key, C2.Value, Font);
			if (UHorizontalBoxSlot* CS = Row->AddChildToHorizontalBox(T))
			{
				CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				CS->SetPadding(FMargin(8.f, 3.f));
			}
		}
		return Row;
	}

	// A 1px full-width hairline.
	UWidget* MakeDivider(UUserWidget* Owner)
	{
		USizeBox* Box = Owner->WidgetTree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(1.f);
		UBorder* Line = Owner->WidgetTree->ConstructWidget<UBorder>();
		Line->SetBrushColor(Color::Border());
		Box->SetContent(Line);
		return Box;
	}
}

void UScorecardPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UScorecardPanel::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Blurred, dimmed backdrop.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	if (UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(Blur))
	{
		BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BlurSlot->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.012f, 0.027f, 0.020f, 0.62f));
	Blur->SetContent(Dim);

	// Centered glass card, fixed width so the four columns line up.
	USizeBox* CardBox = WidgetTree->ConstructWidget<USizeBox>();
	CardBox->SetWidthOverride(CardWidthPx);
	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBox))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetAutoSize(true);
	}
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(CardBrush());   // Bg1 + hairline, Radius::Lg
	Card->SetPadding(FMargin(28.f));
	CardBox->SetContent(Card);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header: eyebrow + title (title text filled by SetScorecard).
	if (UVerticalBoxSlot* ES = Col->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("ROUND COMPLETE"))))
	{
		ES->SetHorizontalAlignment(HAlign_Center);
	}
	Header = MakeTitle(WidgetTree, TEXT("Scorecard"), 28);
	Header->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(Header))
	{
		HS->SetHorizontalAlignment(HAlign_Center);
		HS->SetPadding(FMargin(0.f, 2.f, 0.f, 16.f));
	}

	// Column header row: HOLE | PAR | STROKES | +/- (faint mono) + a hairline under it.
	{
		FSlateFontInfo HeadFont = Mono(12);
		HeadFont.LetterSpacing = 80;
		UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (const TCHAR* H : { TEXT("HOLE"), TEXT("PAR"), TEXT("STROKES"), TEXT("+/-") })
		{
			UTextBlock* T = Cell(this, H, Color::TextFaint(), HeadFont);
			if (UHorizontalBoxSlot* HSlot = HeadRow->AddChildToHorizontalBox(T))
			{
				HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 6.f));
			}
		}
		Col->AddChildToVerticalBox(HeadRow);
		if (UVerticalBoxSlot* DS = Col->AddChildToVerticalBox(MakeDivider(this)))
		{
			DS->SetHorizontalAlignment(HAlign_Fill);
			DS->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
	}

	// Rows container (filled by SetScorecard).
	RowsBox = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UVerticalBoxSlot* RowsSlot = Col->AddChildToVerticalBox(RowsBox))
	{
		RowsSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 16.f));
	}

	// Back button (accent CTA).
	BackButton = MakeAccentButton(WidgetTree, TEXT("Back to menu"));
	BackButton->OnClicked.AddDynamic(this, &UScorecardPanel::HandleBackClicked);
	if (UVerticalBoxSlot* BackSlot = Col->AddChildToVerticalBox(BackButton))
	{
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UScorecardPanel::SetScorecard(const FString& PlayerName, const TArray<int32>& Pars, const TArray<int32>& Strokes)
{
	if (Header)
	{
		Header->SetText(FText::FromString(PlayerName.IsEmpty() ? FString(TEXT("Scorecard")) : PlayerName));
	}
	RebuildRows(Pars, Strokes);
}

void UScorecardPanel::RebuildRows(const TArray<int32>& Pars, const TArray<int32>& Strokes)
{
	if (!RowsBox) { return; }
	RowsBox->ClearChildren();

	const FSlateFontInfo RowFont = Mono(14);
	const int32 N = FMath::Min(Pars.Num(), Strokes.Num());
	int32 TotalPar = 0;
	int32 TotalStrokes = 0;

	auto FormatVsPar = [](int32 V) -> FString
	{
		return V > 0 ? FString::Printf(TEXT("+%d"), V) : (V == 0 ? FString(TEXT("E")) : FString::Printf(TEXT("%d"), V));
	};

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Par   = Pars[i];
		const int32 Stk   = Strokes[i];
		const int32 VsPar = Stk - Par;
		TotalPar     += Par;
		TotalStrokes += Stk;

		UHorizontalBox* Row = MakeRow(this,
			FString::FromInt(i + 1), FString::FromInt(Par), FString::FromInt(Stk),
			FormatVsPar(VsPar), Color::TextDim(), ColorForVsPar(VsPar), RowFont);
		RowsBox->AddChildToVerticalBox(Row);
	}

	// Divider + emphasized total row.
	if (UVerticalBoxSlot* DS = RowsBox->AddChildToVerticalBox(MakeDivider(this)))
	{
		DS->SetHorizontalAlignment(HAlign_Fill);
		DS->SetPadding(FMargin(0.f, 8.f, 0.f, 4.f));
	}
	const int32 TotalVsPar = TotalStrokes - TotalPar;
	UHorizontalBox* Footer = MakeRow(this,
		TEXT("TOTAL"), FString::FromInt(TotalPar), FString::FromInt(TotalStrokes),
		FormatVsPar(TotalVsPar), Color::Text(), ColorForVsPar(TotalVsPar), Mono(16, FName(TEXT("Medium"))));
	RowsBox->AddChildToVerticalBox(Footer);
}

void UScorecardPanel::HandleBackClicked()
{
	if (OnBackToMenu) { OnBackToMenu(); }
}
