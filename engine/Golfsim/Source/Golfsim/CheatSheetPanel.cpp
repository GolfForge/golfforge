#include "CheatSheetPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	struct FCheatBinding
	{
		const TCHAR* Key;
		const TCHAR* Action;
	};

	// MIRROR of AGolfRangeHUD::EnsureInputBound. Keep in sync; this is dev UI, drift surfaces in PIE.
	static const FCheatBinding kBindings[] = {
		{ TEXT("Q / E"),     TEXT("Previous / next club") },
		{ TEXT("Space"),     TEXT("Game mode: 1 power -> 2 accuracy -> 3 fire (whiff < 10% power)") },
		{ TEXT("Space"),     TEXT("Simulation mode: fire a randomized shot from the bag") },
		{ TEXT("Arrow keys"),TEXT("Aim left / right") },
		{ TEXT("M"),         TEXT("Manual-shot dialog") },
		{ TEXT("H"),         TEXT("Session shot history (current)") },
		{ TEXT("Tab"),       TEXT("This cheat sheet") },
		{ TEXT("Esc"),       TEXT("Settings / credits") },
		{ TEXT("RMB drag"),  TEXT("Orbit follow-cam (Follow mode)") },
		{ TEXT("Panel: Mode"), TEXT("Game = swing meter (default); Simulation = LM dropdown") },
#if WITH_EDITOR
		{ TEXT("Z"),         TEXT("Main menu (PIE only)") },
#endif
	};

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

void UCheatSheetPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UCheatSheetPanel::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Light scrim -- the cheat sheet should feel like a transient peek, not a heavyweight modal.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(2.0f);
	{
		UCanvasPanelSlot* S = Root->AddChildToCanvas(Blur);
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.20f));
	Blur->SetContent(Dim);

	// Centered, narrow card. Autosize so the card snugs to the binding list.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(24.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header with title + Close.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UTextBlock* Title = MakeLabel(WidgetTree, TEXT("KEY BINDINGS"), 22, FLinearColor(1.0f, 0.92f, 0.35f));
		UHorizontalBoxSlot* TS = Header->AddChildToHorizontalBox(Title);
		TS->SetVerticalAlignment(VAlign_Center);

		USpacer* Push = WidgetTree->ConstructWidget<USpacer>();
		UHorizontalBoxSlot* SS = Header->AddChildToHorizontalBox(Push);
		SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UButton* CloseBtn = WidgetTree->ConstructWidget<UButton>();
		{
			UTextBlock* Lbl = MakeLabel(WidgetTree, TEXT("Close"), 12, FLinearColor::Black, ETextJustify::Center);
			CloseBtn->SetContent(Lbl);
		}
		CloseBtn->OnClicked.AddDynamic(this, &UCheatSheetPanel::HandleCloseClicked);
		UHorizontalBoxSlot* CS = Header->AddChildToHorizontalBox(CloseBtn);
		CS->SetVerticalAlignment(VAlign_Center);
	}
	Col->AddChildToVerticalBox(Header);

	// Two-column grid: key on the left (yellow), action on the right (white).
	UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
	for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kBindings); ++i)
	{
		const FCheatBinding& B = kBindings[i];
		UTextBlock* K = MakeLabel(WidgetTree, B.Key, 14, FLinearColor(1.0f, 0.85f, 0.30f));
		UGridSlot* KS = Grid->AddChildToGrid(K, i, 0);
		KS->SetPadding(FMargin(0.f, 4.f, 24.f, 4.f));

		UTextBlock* A = MakeLabel(WidgetTree, B.Action, 14, FLinearColor::White);
		UGridSlot* AS = Grid->AddChildToGrid(A, i, 1);
		AS->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
	}
	UVerticalBoxSlot* GS = Col->AddChildToVerticalBox(Grid);
	GS->SetPadding(FMargin(0.f, 12.f, 0.f, 12.f));

	// Footer hint.
	UTextBlock* Hint = MakeLabel(WidgetTree,
		TEXT("(Tab toggles this sheet. Customizable bindings coming later.)"),
		10, FLinearColor(0.6f, 0.6f, 0.6f));
	Col->AddChildToVerticalBox(Hint);
}

void UCheatSheetPanel::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
