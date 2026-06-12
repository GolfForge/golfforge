#include "CheatSheetPanel.h"
#include "UI/GolfUITheme.h"

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
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace GolfUI;

namespace
{
	struct FCheatBinding
	{
		const TCHAR* Key;
		const TCHAR* Action;
	};

	// MIRROR of AGolfRangeHUD::EnsureInputBound. Keep in sync; this is dev/reference UI, drift surfaces in PIE.
	static const FCheatBinding kBindings[] = {
		{ TEXT("Q / E"),      TEXT("Previous / next club") },
		{ TEXT("Space"),      TEXT("Swing (Game mode: power -> accuracy) / fire (Simulation)") },
		{ TEXT("← →"), TEXT("Aim left / right") },
		{ TEXT("C"),          TEXT("Toggle camera (Tee / Follow)") },
		{ TEXT("RMB drag"),   TEXT("Orbit the follow camera") },
		{ TEXT("M"),          TEXT("Hole map (in round): chip / card / large") },
		{ TEXT("N"),          TEXT("Manual-shot dialog") },
		{ TEXT("H"),          TEXT("Session shot history") },
		{ TEXT("Tab"),        TEXT("This cheat sheet") },
		{ TEXT("Esc"),        TEXT("Settings / credits") },
#if WITH_EDITOR
		{ TEXT("Z"),          TEXT("Main menu (PIE only)") },
#endif
	};
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

	// Light blurred scrim -- a transient peek, not a heavyweight modal.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.0f);
	if (UCanvasPanelSlot* BS = Root->AddChildToCanvas(Blur))
	{
		BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BS->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	{
		FLinearColor Scrim = Color::Bg0(); Scrim.A = 0.55f;
		Dim->SetBrush(RoundedBrush(Scrim, 0.f));
	}
	Blur->SetContent(Dim);

	// Centered glass card.
	UBorder* Card = MakeGlassPanel(WidgetTree);
	USizeBox* Wrap = WidgetTree->ConstructWidget<USizeBox>();
	Wrap->SetWidthOverride(560.f);
	Wrap->SetContent(Card);
	if (UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Wrap))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetAutoSize(true);
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header: eyebrow + title (left), Close (right).
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Col->AddChildToVerticalBox(Header);
	{
		UVerticalBox* HL = WidgetTree->ConstructWidget<UVerticalBox>();
		HL->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("REFERENCE")));
		HL->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Key bindings"), 26));
		if (UHorizontalBoxSlot* HLS = Header->AddChildToHorizontalBox(HL)) { HLS->SetVerticalAlignment(VAlign_Center); }

		USpacer* Push = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* SS = Header->AddChildToHorizontalBox(Push)) { SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

		UButton* CloseBtn = MakeGhostButton(WidgetTree, TEXT("Close"));
		CloseBtn->OnClicked.AddDynamic(this, &UCheatSheetPanel::HandleCloseClicked);
		if (UHorizontalBoxSlot* CS = Header->AddChildToHorizontalBox(CloseBtn)) { CS->SetVerticalAlignment(VAlign_Center); }
	}

	// Two-column grid: keycap chip (left), action (right).
	UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
	for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kBindings); ++i)
	{
		const FCheatBinding& B = kBindings[i];

		UBorder* Kbd = MakeKbd(WidgetTree, FString(B.Key));
		if (UGridSlot* KS = Grid->AddChildToGrid(Kbd, i, 0))
		{
			KS->SetPadding(FMargin(0.f, 5.f, 18.f, 5.f));
			KS->SetHorizontalAlignment(HAlign_Left);
			KS->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* A = WidgetTree->ConstructWidget<UTextBlock>();
		A->SetText(FText::FromString(B.Action));
		A->SetFont(Body(14));
		A->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UGridSlot* AS = Grid->AddChildToGrid(A, i, 1))
		{
			AS->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
			AS->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (UVerticalBoxSlot* GS = Col->AddChildToVerticalBox(Grid)) { GS->SetPadding(FMargin(0.f, 16.f, 0.f, 14.f)); }

	// Footer hint.
	UTextBlock* Hint = WidgetTree->ConstructWidget<UTextBlock>();
	Hint->SetText(FText::FromString(TEXT("Tab toggles this sheet  ·  customizable bindings coming later")));
	Hint->SetFont(Mono(11));
	Hint->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	Col->AddChildToVerticalBox(Hint);
}

void UCheatSheetPanel::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
