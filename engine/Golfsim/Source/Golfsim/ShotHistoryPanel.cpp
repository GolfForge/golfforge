#include "ShotHistoryPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	// Range panel uses the same factor; centralize when GOL-66 lands.
	constexpr double YdPerMeter = 1.0936132983;

	FString FormatRow(const FShotHistoryEntry& E)
	{
		const double CarryYd = E.CarryM * YdPerMeter;
		const double TotalYd = E.TotalM * YdPerMeter;
		const double OfflineYd = E.LateralOffsetM * YdPerMeter;
		const TCHAR* Side = (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L");
		return FString::Printf(TEXT("#%d  %s    %.0f yd carry / %.0f yd total    %s %.0f yd"),
			E.ShotId,
			E.Club.IsEmpty() ? TEXT("-") : *E.Club,
			CarryYd, TotalYd,
			Side, FMath::Abs(OfflineYd));
	}

	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Text, int32 Size, const FLinearColor& Color,
		ETextJustify::Type Just = ETextJustify::Left)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		{ FSlateFontInfo F = T->GetFont(); F.Size = Size; T->SetFont(F); }
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetJustification(Just);
		return T;
	}
}

void UShotHistoryPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UShotHistoryPanel::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Full-screen scrim so clicks behind the modal can't leak through. Same idiom as the main menu
	// blur. Subtle blur keeps the range visible-ish; the dim layer absorbs the focus.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	{
		UCanvasPanelSlot* S = Root->AddChildToCanvas(Blur);
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.25f));
	Blur->SetContent(Dim);

	// Centered card spanning ~80% of the screen so a long session reads comfortably without paging.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(24.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.1f, 0.1f, 0.9f, 0.9f));
	CardSlot->SetOffsets(FMargin(0.f));
	CardSlot->SetAlignment(FVector2D(0.f, 0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header row: title on the left, Close on the right.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UTextBlock* Title = MakeLabel(WidgetTree, TEXT("HISTORY"), 22, FLinearColor(1.0f, 0.92f, 0.35f));
		UHorizontalBoxSlot* TS = Header->AddChildToHorizontalBox(Title);
		TS->SetVerticalAlignment(VAlign_Center);

		USpacer* Push = WidgetTree->ConstructWidget<USpacer>();
		UHorizontalBoxSlot* SS = Header->AddChildToHorizontalBox(Push);
		SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		CloseButton = WidgetTree->ConstructWidget<UButton>();
		{
			UTextBlock* Lbl = MakeLabel(WidgetTree, TEXT("Close"), 12, FLinearColor::Black, ETextJustify::Center);
			CloseButton->SetContent(Lbl);
		}
		CloseButton->OnClicked.AddDynamic(this, &UShotHistoryPanel::HandleCloseClicked);
		UHorizontalBoxSlot* CS = Header->AddChildToHorizontalBox(CloseButton);
		CS->SetVerticalAlignment(VAlign_Center);
	}
	Col->AddChildToVerticalBox(Header);

	// Subtitle: "<session label> · <N> shots".
	SubtitleText = MakeLabel(WidgetTree, TEXT("0 shots"), 12, FLinearColor(0.7f, 0.7f, 0.7f));
	UVerticalBoxSlot* SubSlot = Col->AddChildToVerticalBox(SubtitleText);
	SubSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 12.f));

	// Scrollable list of rows -- fills the remainder of the card.
	ListScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* ScrollSlot = Col->AddChildToVerticalBox(ListScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));   // grow to consume the card

	// Footer: disabled Export. Sits at the bottom, doesn't compete with the list.
	ExportButton = WidgetTree->ConstructWidget<UButton>();
	{
		UTextBlock* Lbl = MakeLabel(WidgetTree, TEXT("Export (coming soon)"), 12, FLinearColor::Black, ETextJustify::Center);
		ExportButton->SetContent(Lbl);
	}
	ExportButton->SetIsEnabled(false);
	UVerticalBoxSlot* ExpSlot = Col->AddChildToVerticalBox(ExportButton);
	ExpSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
}

void UShotHistoryPanel::SetSession(const FString& Label, const TArray<FShotHistoryEntry>& InEntries)
{
	if (SubtitleText)
	{
		SubtitleText->SetText(FText::FromString(FString::Printf(
			TEXT("%s   %d shots"), *Label, InEntries.Num())));
	}
	RebuildRows(InEntries);
}

void UShotHistoryPanel::RebuildRows(const TArray<FShotHistoryEntry>& InEntries)
{
	if (!ListScroll)
	{
		return;
	}
	ListScroll->ClearChildren();
	for (int32 i = InEntries.Num() - 1; i >= 0; --i)
	{
		const FShotHistoryEntry& E = InEntries[i];
		UTextBlock* Row = MakeLabel(WidgetTree, *FormatRow(E), 13, FLinearColor::White);
		ListScroll->AddChild(Row);
	}
}

void UShotHistoryPanel::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
