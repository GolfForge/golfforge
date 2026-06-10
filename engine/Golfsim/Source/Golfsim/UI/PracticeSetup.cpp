#include "UI/PracticeSetup.h"
#include "UI/GolfUITheme.h"
#include "UI/OptionCard.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace GolfUI;

namespace
{
	// One drill row: title, "what it does", icon, and whether it's playable yet.
	struct FDrillDef { const TCHAR* Title; const TCHAR* Desc; EIcon Icon; bool bAvailable; };

	static const FDrillDef GDrills[] = {
		{ TEXT("Closest to Pin"),
		  TEXT("A pin spawns at a random distance in a range you set. Score by how close you finish — carry-only, or putt it out."),
		  EIcon::Target, true },
		{ TEXT("Islands"),
		  TEXT("Hold shrinking island greens, club by club. Coming soon."),
		  EIcon::Flag, false },
		{ TEXT("Putting"),
		  TEXT("Lag putting and green-reading drills on the practice green. Coming soon."),
		  EIcon::FlagTriangleRight, false },
	};
	constexpr int32 GDrillNum = sizeof(GDrills) / sizeof(GDrills[0]);
}

void UPracticeSetup::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);   // absorb input while the picker is up
	SetIsFocusable(true);   // so SetKeyboardFocus() works -> NativeOnKeyDown (Enter/Esc) fires
	BuildTree();
}

void UPracticeSetup::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Dim backdrop over the whole screen (also blocks clicks reaching the menu behind).
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	{
		FLinearColor Dim = Color::Bg0(); Dim.A = 0.86f;
		Backdrop->SetBrush(RoundedBrush(Dim, 0.f));
	}
	if (UCanvasPanelSlot* BS = Root->AddChildToCanvas(Backdrop))
	{
		BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BS->SetOffsets(FMargin(0.f));
	}

	// Centered glass panel.
	UBorder* Panel = MakeGlassPanel(WidgetTree);
	USizeBox* Wrap = WidgetTree->ConstructWidget<USizeBox>();
	Wrap->SetWidthOverride(640.f);
	Wrap->SetContent(Panel);
	if (UCanvasPanelSlot* PS = Root->AddChildToCanvas(Wrap))
	{
		PS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		PS->SetAlignment(FVector2D(0.5f, 0.5f));
		PS->SetAutoSize(true);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Col);

	Col->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("PRACTICE")));
	if (UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Choose a drill"), 30)))
	{
		TS->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
	}
	{
		UTextBlock* Sub = WidgetTree->ConstructWidget<UTextBlock>();
		Sub->SetText(FText::FromString(TEXT("Pick a drill to run on the range.")));
		Sub->SetFont(Body(13));
		Sub->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UVerticalBoxSlot* SS = Col->AddChildToVerticalBox(Sub)) { SS->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f)); }
	}

	// Drill cards.
	TWeakObjectPtr<UPracticeSetup> WeakThis(this);
	for (int32 i = 0; i < GDrillNum; ++i)
	{
		UOptionCard* Card = CreateWidget<UOptionCard>(this, UOptionCard::StaticClass());
		if (!Card) { continue; }
		Card->Configure(GDrills[i].Title, GDrills[i].Desc, GDrills[i].Icon);
		Card->SetDisabled(!GDrills[i].bAvailable);
		const int32 Index = i;
		Card->OnSelected = [WeakThis, Index]()
		{
			if (UPracticeSetup* S = WeakThis.Get()) { S->Select(Index); }
		};
		if (UVerticalBoxSlot* CS = Col->AddChildToVerticalBox(Card)) { CS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f)); }
		ModeCards.Add(Card);
	}

	// Footer: Back (ghost) ... Start (accent, disabled until a drill is chosen).
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* FS = Col->AddChildToVerticalBox(Footer)) { FS->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }

	UButton* Back = MakeGhostButton(WidgetTree, TEXT("Back"));
	Back->OnClicked.AddDynamic(this, &UPracticeSetup::HandleBackClicked);
	if (UHorizontalBoxSlot* BkS = Footer->AddChildToHorizontalBox(Back)) { BkS->SetVerticalAlignment(VAlign_Center); }

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
	if (UHorizontalBoxSlot* GS = Footer->AddChildToHorizontalBox(Gap)) { GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	StartBtn = MakeAccentButton(WidgetTree, TEXT("Start drill"));
	StartBtn->OnClicked.AddDynamic(this, &UPracticeSetup::HandleStartClicked);
	// Cache the label so RefreshStart can dim it; MakeAccentButton's content is a centered text block.
	StartLabel = Cast<UTextBlock>(StartBtn->GetChildAt(0));
	if (UHorizontalBoxSlot* StS = Footer->AddChildToHorizontalBox(StartBtn)) { StS->SetVerticalAlignment(VAlign_Center); }

	RefreshStart();
}

void UPracticeSetup::ResetSelection()
{
	SelectedIndex = -1;
	for (UOptionCard* Card : ModeCards) { if (Card) { Card->SetSelected(false); } }
	RefreshStart();
}

void UPracticeSetup::Select(int32 Index)
{
	if (!GDrills[Index].bAvailable) { return; }   // disabled cards don't fire OnSelected, but guard anyway
	SelectedIndex = Index;
	for (int32 i = 0; i < ModeCards.Num(); ++i)
	{
		if (ModeCards[i]) { ModeCards[i]->SetSelected(i == Index); }
	}
	RefreshStart();
}

void UPracticeSetup::RefreshStart()
{
	const bool bEnabled = SelectedIndex >= 0;
	if (StartBtn) { StartBtn->SetIsEnabled(bEnabled); }
	if (StartLabel)
	{
		StartLabel->SetColorAndOpacity(FSlateColor(bEnabled ? Color::AccentInk() : Color::TextFaint()));
	}
}

void UPracticeSetup::HandleBackClicked()
{
	if (OnClose) { OnClose(); }
}

void UPracticeSetup::HandleStartClicked()
{
	if (SelectedIndex < 0) { return; }
	// Only Closest to Pin is wired this milestone (index 0); the others are disabled seam cards.
	if (SelectedIndex == 0 && OnStartCtp) { OnStartCtp(); }
}

FReply UPracticeSetup::NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		HandleBackClicked();
		return FReply::Handled();
	}
	if ((Key == EKeys::Enter || Key == EKeys::SpaceBar) && SelectedIndex >= 0)
	{
		HandleStartClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geo, KeyEvent);
}
