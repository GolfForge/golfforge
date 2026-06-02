#include "ScorecardPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	FLinearColor ColorForVsPar(int32 VsPar)
	{
		if (VsPar <  0) { return FLinearColor(0.30f, 0.95f, 0.45f); }   // birdie/eagle -- green
		if (VsPar == 0) { return FLinearColor(0.95f, 0.95f, 0.95f); }   // par -- white
		if (VsPar == 1) { return FLinearColor(1.00f, 0.85f, 0.30f); }   // bogey -- yellow
		return FLinearColor(0.95f, 0.30f, 0.30f);                       // double+ -- red
	}

	UTextBlock* MakeCell(UUserWidget* Owner, const FString& Text, const FLinearColor& Color,
		int32 FontSize = 14, ETextJustify::Type Justify = ETextJustify::Center)
	{
		UTextBlock* T = Owner->WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetJustification(Justify);
		{ FSlateFontInfo F = T->GetFont(); F.Size = FontSize; T->SetFont(F); }
		return T;
	}

	UHorizontalBox* MakeRow(UUserWidget* Owner, const FString& A, const FString& B, const FString& C,
		const FString& D, const FLinearColor& DColor, int32 FontSize = 14)
	{
		UHorizontalBox* Row = Owner->WidgetTree->ConstructWidget<UHorizontalBox>();
		const FLinearColor Neutral(0.85f, 0.85f, 0.85f);
		const TArray<TPair<FString, FLinearColor>> Cells = {
			{ A, Neutral }, { B, Neutral }, { C, Neutral }, { D, DColor }
		};
		for (const auto& Cell : Cells)
		{
			UTextBlock* T = MakeCell(Owner, Cell.Key, Cell.Value, FontSize);
			UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(T);
			CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CellSlot->SetPadding(FMargin(8.f, 2.f));
		}
		return Row;
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

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(Blur);
	BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BlurSlot->SetOffsets(FMargin(0.f));

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.40f));
	Blur->SetContent(Dim);

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.96f));
	Card->SetPadding(FMargin(28.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	Header = MakeCell(this, TEXT("Round complete"), FLinearColor(1.0f, 0.92f, 0.35f), 24, ETextJustify::Center);
	UVerticalBoxSlot* HeaderSlot = Col->AddChildToVerticalBox(Header);
	HeaderSlot->SetHorizontalAlignment(HAlign_Center);
	HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	// Column header row: Hole | Par | Strokes | +/-
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	const FLinearColor HeaderDim(0.7f, 0.7f, 0.7f);
	for (const TCHAR* H : { TEXT("Hole"), TEXT("Par"), TEXT("Strokes"), TEXT("+/-") })
	{
		UTextBlock* T = MakeCell(this, H, HeaderDim, 13);
		UHorizontalBoxSlot* HeadSlot = HeadRow->AddChildToHorizontalBox(T);
		HeadSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HeadSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 4.f));
	}
	Col->AddChildToVerticalBox(HeadRow);

	// Rows container (filled by SetScorecard).
	RowsBox = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBoxSlot* RowsSlot = Col->AddChildToVerticalBox(RowsBox);
	RowsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Back button.
	BackButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* BackLbl = WidgetTree->ConstructWidget<UTextBlock>();
	BackLbl->SetText(FText::FromString(TEXT("Back to Menu")));
	BackLbl->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	BackLbl->SetJustification(ETextJustify::Center);
	BackButton->SetContent(BackLbl);
	BackButton->OnClicked.AddDynamic(this, &UScorecardPanel::HandleBackClicked);

	UVerticalBoxSlot* BackSlot = Col->AddChildToVerticalBox(BackButton);
	BackSlot->SetHorizontalAlignment(HAlign_Center);
	BackSlot->SetPadding(FMargin(0.f, 4.f));
}

void UScorecardPanel::SetScorecard(const FString& PlayerName, const TArray<int32>& Pars, const TArray<int32>& Strokes)
{
	if (Header)
	{
		const FString Title = PlayerName.IsEmpty()
			? FString(TEXT("Round complete"))
			: FString::Printf(TEXT("Round complete -- %s"), *PlayerName);
		Header->SetText(FText::FromString(Title));
	}
	RebuildRows(Pars, Strokes);
}

void UScorecardPanel::RebuildRows(const TArray<int32>& Pars, const TArray<int32>& Strokes)
{
	if (!RowsBox) { return; }
	RowsBox->ClearChildren();

	const int32 N = FMath::Min(Pars.Num(), Strokes.Num());
	int32 TotalPar = 0;
	int32 TotalStrokes = 0;

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Par     = Pars[i];
		const int32 Stk     = Strokes[i];
		const int32 VsPar   = Stk - Par;
		TotalPar     += Par;
		TotalStrokes += Stk;

		UHorizontalBox* Row = MakeRow(this,
			FString::FromInt(i + 1),
			FString::FromInt(Par),
			FString::FromInt(Stk),
			(VsPar > 0 ? FString::Printf(TEXT("+%d"), VsPar)
			            : VsPar == 0 ? FString(TEXT("E"))
			                          : FString::Printf(TEXT("%d"), VsPar)),
			ColorForVsPar(VsPar));
		RowsBox->AddChildToVerticalBox(Row);
	}

	// Footer total row.
	const int32 TotalVsPar = TotalStrokes - TotalPar;
	UHorizontalBox* Footer = MakeRow(this,
		TEXT("Total"),
		FString::FromInt(TotalPar),
		FString::FromInt(TotalStrokes),
		(TotalVsPar > 0 ? FString::Printf(TEXT("+%d"), TotalVsPar)
		                : TotalVsPar == 0 ? FString(TEXT("E"))
		                                  : FString::Printf(TEXT("%d"), TotalVsPar)),
		ColorForVsPar(TotalVsPar),
		/*FontSize=*/16);
	UVerticalBoxSlot* FootSlot = RowsBox->AddChildToVerticalBox(Footer);
	FootSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

void UScorecardPanel::HandleBackClicked()
{
	if (OnBackToMenu) { OnBackToMenu(); }
}
