#include "GolfRangePanel.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"   // FTableRowStyle for the dropdown row text color

void UGolfRangePanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UGolfRangePanel::BuildTree()
{
	// Root canvas fills the screen (AddToViewport); a single corner-anchored Border holds the panel.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;   // without this the base RebuildWidget renders an empty SSpacer

	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
	Bg->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.5f));
	Bg->SetPadding(FMargin(12.f));
	UCanvasPanelSlot* BgSlot = Root->AddChildToCanvas(Bg);
	BgSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));   // top-right point anchor
	BgSlot->SetAlignment(FVector2D(1.f, 0.f));          // pivot at the widget's top-right corner
	BgSlot->SetAutoSize(true);
	BgSlot->SetOffsets(FMargin(0.f, 28.f, 28.f, 0.f));  // L,T,R,B; with autosize, T/R are the corner inset

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Bg->SetContent(Col);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("RANGE")));
	{
		FSlateFontInfo F = Title->GetFont();   // engine default Roboto; no asset needed
		F.Size = 18;
		Title->SetFont(F);
	}
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.35f)));
	Col->AddChildToVerticalBox(Title);

	// Label/value grid: label in col 0, right-aligned value in col 1.
	UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
	auto AddRow = [&](int32 RowIdx, const TCHAR* Label) -> UTextBlock*
	{
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
		Lbl->SetText(FText::FromString(Label));
		UGridSlot* LS = Grid->AddChildToGrid(Lbl, RowIdx, 0);
		LS->SetPadding(FMargin(0.f, 2.f, 12.f, 2.f));

		UTextBlock* Val = WidgetTree->ConstructWidget<UTextBlock>();
		Val->SetText(FText::FromString(TEXT("-")));
		UGridSlot* VS = Grid->AddChildToGrid(Val, RowIdx, 1);
		VS->SetHorizontalAlignment(HAlign_Right);
		VS->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		return Val;
	};
	ValClub    = AddRow(0, TEXT("Club"));
	ValSpeed   = AddRow(1, TEXT("Ball Speed (mph)"));
	ValLaunch  = AddRow(2, TEXT("Launch (deg)"));
	ValSpin    = AddRow(3, TEXT("Spin (rpm)"));
	ValCarry   = AddRow(4, TEXT("Carry (yd)"));
	ValOffline = AddRow(5, TEXT("Offline (yd)"));
	UVerticalBoxSlot* GridSlot = Col->AddChildToVerticalBox(Grid);
	GridSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 6.f));

	// Club dropdown. A code-only ComboBoxString renders fully styled in 5.7 (its ctor defaults
	// WidgetStyle/ItemStyle/Font from the style cache), so no style asset is needed. Options are
	// supplied later by the HUD via SetClubOptions.
	ClubCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	// Light row text so the club names read against the dark dropdown menu (the default TextColor is
	// dark). SelectedTextColor is left at its default so the hovered row stays readable on its
	// light highlight.
	{
		FTableRowStyle ItemStyle = ClubCombo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		ClubCombo->SetItemStyle(ItemStyle);
	}
	ClubCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleClubSelectionChanged);
	Col->AddChildToVerticalBox(ClubCombo);
}

void UGolfRangePanel::SetClubOptions(const TArray<FString>& Names)
{
	if (!ClubCombo)
	{
		return;
	}
	ClubCombo->ClearOptions();
	for (const FString& Name : Names)
	{
		ClubCombo->AddOption(Name);
	}
}

void UGolfRangePanel::SetSelectedClubIndex(int32 Index)
{
	if (!ClubCombo || ClubCombo->GetOptionCount() <= 0)
	{
		return;   // nothing to select yet (options not populated)
	}
	const int32 Clamped = FMath::Clamp(Index, 0, ClubCombo->GetOptionCount() - 1);
	bSuppressSelectionCallback = true;
	ClubCombo->SetSelectedIndex(Clamped);
	bSuppressSelectionCallback = false;
}

void UGolfRangePanel::HandleClubSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Programmatic selection (SetSelectedIndex) re-broadcasts with ESelectInfo::Direct; only act
	// on genuine user picks. The bool guard is belt-and-suspenders for the same reentrancy.
	if (bSuppressSelectionCallback || SelectionType == ESelectInfo::Direct || !ClubCombo)
	{
		return;
	}
	const int32 Idx = ClubCombo->GetSelectedIndex();
	if (Idx >= 0 && OnClubChosen)
	{
		OnClubChosen(Idx);
	}
	// Hand keyboard focus back to the game viewport so Space/1-6/arrows reach gameplay instead of
	// the focused combobox (otherwise Space toggles the dropdown). Deferred a tick so it runs after
	// the combobox finishes its own post-selection focus handling.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UGolfRangePanel> WeakSelf(this);
		World->GetTimerManager().SetTimerForNextTick([WeakSelf]()
		{
			if (WeakSelf.IsValid() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		});
	}
}

void UGolfRangePanel::UpdateMetrics(const FString& Club, double SpeedMph, double LaunchDeg,
	double SpinRpm, double CarryYd, double OfflineYd)
{
	if (ValClub)   { ValClub->SetText(FText::FromString(Club)); }
	if (ValSpeed)  { ValSpeed->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), SpeedMph))); }
	if (ValLaunch) { ValLaunch->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), LaunchDeg))); }
	if (ValSpin)   { ValSpin->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), SpinRpm))); }
	if (ValCarry)  { ValCarry->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), CarryYd))); }
	if (ValOffline)
	{
		const TCHAR* Side = (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L");
		ValOffline->SetText(FText::FromString(FString::Printf(TEXT("%s %.0f"), Side, FMath::Abs(OfflineYd))));
	}
}
