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
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/GolfUITheme.h"   // GOL-155: reskin onto the shared design system

using namespace GolfUI;

namespace
{
	constexpr double YdPerMeter = 1.0936132983;
	constexpr double MphPerMps  = 2.2369362921;

	// Table columns (label + fixed pixel width), in display order. Mono cells so the numbers align.
	struct FCol { const TCHAR* Label; float Width; };
	const FCol Columns[] = {
		{ TEXT("#"),       46.f  },
		{ TEXT("CLUB"),    120.f },
		{ TEXT("BALL"),    96.f  },
		{ TEXT("LAUNCH"),  90.f  },
		{ TEXT("SPIN"),    110.f },
		{ TEXT("CARRY"),   90.f  },
		{ TEXT("TOTAL"),   90.f  },
		{ TEXT("OFFLINE"), 100.f },
	};

	// One fixed-width table cell.
	UWidget* MakeCell(UWidgetTree* Tree, const FString& Text, float Width, const FLinearColor& Col,
		const FSlateFontInfo& Font)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetFont(Font);
		T->SetColorAndOpacity(FSlateColor(Col));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Width);
		Box->SetContent(T);
		return Box;
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

	// Full-screen blur + dim scrim so the modal owns focus and clicks can't leak behind it.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Blur))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));
	Blur->SetContent(Dim);

	// Centered glass card (~80% of the screen) for a long session.
	UBorder* Card = MakeGlassPanel(WidgetTree);
	Card->SetPadding(FMargin(28.f));
	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card))
	{
		CardSlot->SetAnchors(FAnchors(0.1f, 0.1f, 0.9f, 0.9f));
		CardSlot->SetOffsets(FMargin(0.f));
		CardSlot->SetAlignment(FVector2D(0.f, 0.f));
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header: eyebrow + title on the left, Close on the right.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UVerticalBox* TitleCol = WidgetTree->ConstructWidget<UVerticalBox>();
		TitleCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("SESSION")));
		if (UVerticalBoxSlot* TS = TitleCol->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Shot history"), 26)))
		{ TS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		if (UHorizontalBoxSlot* HS = Header->AddChildToHorizontalBox(TitleCol)) { HS->SetVerticalAlignment(VAlign_Center); }

		USpacer* Push = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* SS = Header->AddChildToHorizontalBox(Push)) { SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

		CloseButton = MakeGhostButton(WidgetTree, TEXT("Close"));
		CloseButton->OnClicked.AddDynamic(this, &UShotHistoryPanel::HandleCloseClicked);
		if (UHorizontalBoxSlot* CS = Header->AddChildToHorizontalBox(CloseButton)) { CS->SetVerticalAlignment(VAlign_Center); }
	}
	Col->AddChildToVerticalBox(Header);

	// Subtitle: "<session label> · <N> shots".
	SubtitleText = WidgetTree->ConstructWidget<UTextBlock>();
	SubtitleText->SetText(FText::FromString(TEXT("0 shots")));
	SubtitleText->SetFont(Body(13));
	SubtitleText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	if (UVerticalBoxSlot* SubSlot = Col->AddChildToVerticalBox(SubtitleText)) { SubSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 14.f)); }

	// Column-header row (aligned to the row inset below).
	UBorder* HeaderWrap = WidgetTree->ConstructWidget<UBorder>();
	HeaderWrap->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Sm));
	HeaderWrap->SetPadding(FMargin(10.f, 0.f, 10.f, 6.f));
	{
		UHorizontalBox* HeaderCols = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (const FCol& C : Columns)
		{
			if (UHorizontalBoxSlot* HS = HeaderCols->AddChildToHorizontalBox(
				MakeCell(WidgetTree, FString(C.Label), C.Width, Color::TextFaint(), Mono(11))))
			{ HS->SetVerticalAlignment(VAlign_Center); }
		}
		HeaderWrap->SetContent(HeaderCols);
	}
	Col->AddChildToVerticalBox(HeaderWrap);

	// Scrollable list of rows -- fills the remainder of the card.
	ListScroll = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* ScrollSlot = Col->AddChildToVerticalBox(ListScroll)) { ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// Footer: disabled Export (coming soon).
	ExportButton = MakeGhostButton(WidgetTree, TEXT("Export (coming soon)"));
	ExportButton->SetIsEnabled(false);
	if (UVerticalBoxSlot* ExpSlot = Col->AddChildToVerticalBox(ExportButton)) { ExpSlot->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f)); }
}

void UShotHistoryPanel::SetSession(const FString& Label, const TArray<FShotHistoryEntry>& InEntries)
{
	if (SubtitleText)
	{
		SubtitleText->SetText(FText::FromString(FString::Printf(
			TEXT("%s   ·   %d shots"), *Label, InEntries.Num())));
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

	const FSlateFontInfo CellFont = Mono(14);
	int32 RowIdx = 0;
	for (int32 i = InEntries.Num() - 1; i >= 0; --i, ++RowIdx)
	{
		const FShotHistoryEntry& E = InEntries[i];
		const double CarryYd   = E.CarryM * YdPerMeter;
		const double TotalYd   = E.TotalM * YdPerMeter;
		const double OfflineYd = E.LateralOffsetM * YdPerMeter;
		const FString OfflineStr = (FMath::Abs(OfflineYd) < 0.5)
			? FString(TEXT("0"))
			: FString::Printf(TEXT("%.0f %s"), FMath::Abs(OfflineYd), (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L"));
		const FString SpinStr = E.bSpinEstimated
			? FString::Printf(TEXT("%.0f est"), E.BackspinRpm)
			: FString::Printf(TEXT("%.0f"), E.BackspinRpm);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto Add = [&](const FString& Str, int32 ColIdx, const FLinearColor& C)
		{
			if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(MakeCell(WidgetTree, Str, Columns[ColIdx].Width, C, CellFont)))
			{ HS->SetVerticalAlignment(VAlign_Center); }
		};
		Add(FString::Printf(TEXT("%d"), E.ShotId), 0, Color::TextFaint());
		Add(E.Club.IsEmpty() ? FString(TEXT("-")) : E.Club.ToUpper(), 1, Color::Text());
		Add(FString::Printf(TEXT("%.0f"), E.BallSpeedMps * MphPerMps), 2, Color::TextDim());
		Add(FString::Printf(TEXT("%.1f"), E.LaunchAngleDeg),           3, Color::TextDim());
		Add(SpinStr,                                                   4, Color::TextDim());
		Add(FString::Printf(TEXT("%.0f"), CarryYd),                    5, Color::Accent());
		Add(FString::Printf(TEXT("%.0f"), TotalYd),                    6, Color::Text());
		Add(OfflineStr,                                                7, Color::TextDim());

		// Subtle zebra striping for readability.
		UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>();
		RowBg->SetBrush(RoundedBrush((RowIdx % 2 == 0) ? Color::Surface() : FLinearColor(0, 0, 0, 0), Radius::Sm));
		RowBg->SetPadding(FMargin(10.f, 7.f));
		RowBg->SetContent(Row);
		ListScroll->AddChild(RowBg);
	}
}

void UShotHistoryPanel::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
