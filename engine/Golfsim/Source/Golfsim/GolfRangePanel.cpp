#include "GolfRangePanel.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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

	// Three labeled dropdowns (Club / Time / Sky). A code-only ComboBoxString renders fully styled in
	// 5.7 (its ctor defaults WidgetStyle/ItemStyle/Font from the style cache), so no style asset is
	// needed; we only lighten the row text so names read against the dark dropdown menu (the default
	// ItemStyle.TextColor is dark; SelectedTextColor stays default so the hovered row reads on its
	// light highlight). Options are supplied later by the HUD.
	auto AddLabeledCombo = [&](const TCHAR* Label) -> UComboBoxString*
	{
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
		Lbl->SetText(FText::FromString(Label));
		UVerticalBoxSlot* LblSlot = Col->AddChildToVerticalBox(Lbl);
		LblSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 1.f));

		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
		FTableRowStyle ItemStyle = Combo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		Combo->SetItemStyle(ItemStyle);
		Col->AddChildToVerticalBox(Combo);
		return Combo;
	};

	ClubCombo = AddLabeledCombo(TEXT("Club"));
	ClubCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleClubSelectionChanged);
	TimeCombo = AddLabeledCombo(TEXT("Time"));
	TimeCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleTimeSelectionChanged);
	SkyCombo = AddLabeledCombo(TEXT("Sky"));
	SkyCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleSkySelectionChanged);
	LMCombo = AddLabeledCombo(TEXT("Launch Monitor"));
	LMCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleLaunchMonitorSelectionChanged);

	// "Simulate Shot" button -- asks the connected device to emit a shot (OpenFlight mock mode ->
	// Socket.IO simulate_shot). Hidden until SetConnectionStatus reports a connection. Default UButton
	// is light, so the label is black.
	SimulateButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* SimLabel = WidgetTree->ConstructWidget<UTextBlock>();
	SimLabel->SetText(FText::FromString(TEXT("Simulate Shot")));
	SimLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	SimLabel->SetJustification(ETextJustify::Center);
	SimulateButton->SetContent(SimLabel);
	SimulateButton->OnClicked.AddDynamic(this, &UGolfRangePanel::HandleSimulateClicked);
	SimulateButton->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBoxSlot* SimSlot = Col->AddChildToVerticalBox(SimulateButton);
	SimSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	// Launch-monitor connection indicator (bottom of the panel). Gray until the HUD wires the active
	// driver's status; green/red thereafter.
	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FText::FromString(TEXT("● No launch monitor")));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
	{
		FSlateFontInfo F = StatusText->GetFont();
		F.Size = 10;
		StatusText->SetFont(F);
	}
	UVerticalBoxSlot* StatusSlot = Col->AddChildToVerticalBox(StatusText);
	StatusSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

namespace
{
	// Repopulate a dropdown's options from a name list.
	void FillCombo(UComboBoxString* Combo, const TArray<FString>& Names)
	{
		if (!Combo)
		{
			return;
		}
		Combo->ClearOptions();
		for (const FString& Name : Names)
		{
			Combo->AddOption(Name);
		}
	}
}

void UGolfRangePanel::SetClubOptions(const TArray<FString>& Names) { FillCombo(ClubCombo, Names); }
void UGolfRangePanel::SetTimeOptions(const TArray<FString>& Names) { FillCombo(TimeCombo, Names); }
void UGolfRangePanel::SetSkyOptions(const TArray<FString>& Names)  { FillCombo(SkyCombo, Names); }
void UGolfRangePanel::SetLaunchMonitorOptions(const TArray<FString>& Names) { FillCombo(LMCombo, Names); }

void UGolfRangePanel::SetComboIndexGuarded(UComboBoxString* Combo, int32 Index)
{
	if (!Combo || Combo->GetOptionCount() <= 0)
	{
		return;   // nothing to select yet (options not populated)
	}
	const int32 Clamped = FMath::Clamp(Index, 0, Combo->GetOptionCount() - 1);
	bSuppressSelectionCallback = true;
	Combo->SetSelectedIndex(Clamped);
	bSuppressSelectionCallback = false;
}

void UGolfRangePanel::SetSelectedClubIndex(int32 Index) { SetComboIndexGuarded(ClubCombo, Index); }
void UGolfRangePanel::SetSelectedTimeIndex(int32 Index) { SetComboIndexGuarded(TimeCombo, Index); }
void UGolfRangePanel::SetSelectedSkyIndex(int32 Index)  { SetComboIndexGuarded(SkyCombo, Index); }
void UGolfRangePanel::SetSelectedLaunchMonitorIndex(int32 Index) { SetComboIndexGuarded(LMCombo, Index); }

void UGolfRangePanel::HandleClubSelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	HandleComboPick(ClubCombo, OnClubChosen, SelectionType);
}

void UGolfRangePanel::HandleTimeSelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	HandleComboPick(TimeCombo, OnTimeChosen, SelectionType);
}

void UGolfRangePanel::HandleSkySelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	HandleComboPick(SkyCombo, OnSkyChosen, SelectionType);
}

void UGolfRangePanel::HandleLaunchMonitorSelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	HandleComboPick(LMCombo, OnLaunchMonitorChosen, SelectionType);
}

void UGolfRangePanel::HandleSimulateClicked()
{
	if (OnSimulateShot)
	{
		OnSimulateShot();
	}
	ReturnFocusToGameViewport();   // so Space/1-6/arrows still reach gameplay after the click
}

void UGolfRangePanel::HandleComboPick(UComboBoxString* Combo, const TFunction<void(int32)>& OnChosen,
	ESelectInfo::Type SelectionType)
{
	// Programmatic selection (SetSelectedIndex) re-broadcasts with ESelectInfo::Direct; only act on
	// genuine user picks. The bool guard is belt-and-suspenders for the same reentrancy.
	if (bSuppressSelectionCallback || SelectionType == ESelectInfo::Direct || !Combo)
	{
		return;
	}
	const int32 Idx = Combo->GetSelectedIndex();
	if (Idx >= 0 && OnChosen)
	{
		OnChosen(Idx);
	}
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::ReturnFocusToGameViewport()
{
	// Hand keyboard focus back to the game viewport so Space/1-6/arrows reach gameplay instead of the
	// focused combobox (otherwise Space toggles the dropdown). Deferred a tick so it runs after the
	// combobox finishes its own post-selection focus handling.
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
	double SpinRpm, double CarryYd, double OfflineYd, bool bSpinEstimated)
{
	if (ValClub)   { ValClub->SetText(FText::FromString(Club)); }
	if (ValSpeed)  { ValSpeed->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), SpeedMph))); }
	if (ValLaunch) { ValLaunch->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), LaunchDeg))); }
	if (ValSpin)
	{
		// Mark estimated spin so it's clearly computed, not measured by the LM.
		ValSpin->SetText(FText::FromString(bSpinEstimated
			? FString::Printf(TEXT("%.0f est"), SpinRpm)
			: FString::Printf(TEXT("%.0f"), SpinRpm)));
	}
	if (ValCarry)  { ValCarry->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), CarryYd))); }
	if (ValOffline)
	{
		const TCHAR* Side = (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L");
		ValOffline->SetText(FText::FromString(FString::Printf(TEXT("%s %.0f"), Side, FMath::Abs(OfflineYd))));
	}
}

void UGolfRangePanel::SetConnectionStatus(bool bConnected, const FString& Detail)
{
	// The Simulate Shot button is only useful while connected (it triggers a server-side mock shot).
	if (SimulateButton)
	{
		SimulateButton->SetVisibility(bConnected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!StatusText)
	{
		return;
	}
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("● %s"), *Detail)));
	StatusText->SetColorAndOpacity(FSlateColor(bConnected
		? FLinearColor(0.2f, 0.85f, 0.2f)    // green
		: FLinearColor(0.85f, 0.25f, 0.25f))); // red
}
