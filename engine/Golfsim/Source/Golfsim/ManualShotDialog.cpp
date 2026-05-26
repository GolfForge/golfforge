#include "ManualShotDialog.h"

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
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"   // FTableRowStyle for the dropdown row text color

void UManualShotDialog::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UManualShotDialog::BuildTree()
{
	// Root canvas fills the screen (AddToViewport); a single bottom-right-anchored Border holds the
	// form, tucked into the corner so it doesn't block the view of the shot.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;   // without this the base RebuildWidget renders an empty SSpacer

	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
	Bg->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Bg->SetPadding(FMargin(16.f));
	UCanvasPanelSlot* BgSlot = Root->AddChildToCanvas(Bg);
	BgSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));   // bottom-right point anchor
	BgSlot->SetAlignment(FVector2D(1.f, 1.f));          // pivot at the widget's bottom-right corner
	BgSlot->SetAutoSize(true);
	BgSlot->SetOffsets(FMargin(0.f, 0.f, 28.f, 28.f));  // L,T,R,B; with autosize, B/R are the corner inset

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Bg->SetContent(Col);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("MANUAL SHOT")));
	{
		FSlateFontInfo F = Title->GetFont();   // engine default Roboto; no asset needed
		F.Size = 18;
		Title->SetFont(F);
	}
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.35f)));
	Col->AddChildToVerticalBox(Title);

	// Label/SpinBox grid: label in col 0, numeric spinner in col 1. SpinBox draws its own light
	// field background, so its text reads regardless of the dark panel behind it.
	UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
	auto AddSpinRow = [&](int32 RowIdx, const TCHAR* Label, float MinV, float MaxV, float Delta) -> USpinBox*
	{
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
		Lbl->SetText(FText::FromString(Label));
		UGridSlot* LS = Grid->AddChildToGrid(Lbl, RowIdx, 0);
		LS->SetPadding(FMargin(0.f, 3.f, 12.f, 3.f));
		LS->SetVerticalAlignment(VAlign_Center);

		USpinBox* Spin = WidgetTree->ConstructWidget<USpinBox>();
		Spin->SetMinValue(MinV);
		Spin->SetMaxValue(MaxV);
		Spin->SetMinSliderValue(MinV);
		Spin->SetMaxSliderValue(MaxV);
		Spin->SetDelta(Delta);
		// Fixed box width (fits the widest value, e.g. "12000"/"-3000") so the displayed number's
		// digit count doesn't change the spinner's size and shove the AutoSize panel around.
		Spin->SetMinDesiredWidth(96.f);
		// Always show exactly one decimal -- float drag imprecision otherwise spills extra digits
		// (e.g. 166.99998), overflowing the fixed box and nudging the panel. 1-decimal UI this pass.
		Spin->SetMinFractionalDigits(1);
		Spin->SetMaxFractionalDigits(1);
		UGridSlot* SS = Grid->AddChildToGrid(Spin, RowIdx, 1);
		SS->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
		return Spin;
	};
	SpeedBox    = AddSpinRow(0, TEXT("Ball Speed (mph)"),     0.f,   220.f,    1.f);
	LaunchBox   = AddSpinRow(1, TEXT("Launch (deg)"),         0.f,    60.f,    0.5f);
	BackspinBox = AddSpinRow(2, TEXT("Backspin (rpm)"),       0.f, 12000.f,  100.f);
	SidespinBox = AddSpinRow(3, TEXT("Sidespin (rpm)"),   -3000.f,  3000.f,  100.f);
	AzimuthBox  = AddSpinRow(4, TEXT("Azimuth (deg)"),      -30.f,    30.f,    0.5f);
	UVerticalBoxSlot* GridSlot = Col->AddChildToVerticalBox(Grid);
	GridSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));

	// Club dropdown (options supplied by the HUD). Lighten the menu row text like the panel does.
	UTextBlock* ClubLbl = WidgetTree->ConstructWidget<UTextBlock>();
	ClubLbl->SetText(FText::FromString(TEXT("Club")));
	UVerticalBoxSlot* ClubLblSlot = Col->AddChildToVerticalBox(ClubLbl);
	ClubLblSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));

	ClubCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	{
		FTableRowStyle ItemStyle = ClubCombo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		ClubCombo->SetItemStyle(ItemStyle);
	}
	ClubCombo->OnSelectionChanged.AddDynamic(this, &UManualShotDialog::HandleClubSelectionChanged);
	Col->AddChildToVerticalBox(ClubCombo);

	// Fire button (light default style, so its label is dark for contrast).
	FireButton = WidgetTree->ConstructWidget<UButton>();
	FireButton->OnClicked.AddDynamic(this, &UManualShotDialog::HandleFireClicked);
	UTextBlock* FireLbl = WidgetTree->ConstructWidget<UTextBlock>();
	FireLbl->SetText(FText::FromString(TEXT("Fire")));
	FireLbl->SetJustification(ETextJustify::Center);
	FireLbl->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FireButton->SetContent(FireLbl);
	UVerticalBoxSlot* FireSlot = Col->AddChildToVerticalBox(FireButton);
	FireSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 6.f));
	FireSlot->SetHorizontalAlignment(HAlign_Fill);

	// Result line, filled by the HUD when the shot's outcome comes back through the bus.
	ResultText = WidgetTree->ConstructWidget<UTextBlock>();
	ResultText->SetText(FText::FromString(TEXT("Carry -- yd   Offline -- yd")));
	Col->AddChildToVerticalBox(ResultText);
}

void UManualShotDialog::SetClubOptions(const TArray<FString>& Names)
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

void UManualShotDialog::SetSelectedClubIndex(int32 Index)
{
	if (!ClubCombo || ClubCombo->GetOptionCount() <= 0)
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(Index, 0, ClubCombo->GetOptionCount() - 1);
	bSuppressSelectionCallback = true;
	ClubCombo->SetSelectedIndex(Clamped);
	bSuppressSelectionCallback = false;
}

void UManualShotDialog::SetFields(double SpeedMph, double LaunchDeg, double BackspinRpm,
	double SidespinRpm, double AzimuthDeg)
{
	if (SpeedBox)    { SpeedBox->SetValue((float)SpeedMph); }
	if (LaunchBox)   { LaunchBox->SetValue((float)LaunchDeg); }
	if (BackspinBox) { BackspinBox->SetValue((float)BackspinRpm); }
	if (SidespinBox) { SidespinBox->SetValue((float)SidespinRpm); }
	if (AzimuthBox)  { AzimuthBox->SetValue((float)AzimuthDeg); }
}

void UManualShotDialog::SetResult(double CarryYd, double OfflineYd)
{
	if (!ResultText)
	{
		return;
	}
	const TCHAR* Side = (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L");
	ResultText->SetText(FText::FromString(FString::Printf(
		TEXT("Carry %.0f yd   Offline %s %.0f yd"), CarryYd, Side, FMath::Abs(OfflineYd))));
}

void UManualShotDialog::HandleFireClicked()
{
	if (OnFire)
	{
		FManualShotValues V;
		V.BallSpeedMph = SpeedBox    ? SpeedBox->GetValue()    : 0.0;
		V.LaunchDeg    = LaunchBox   ? LaunchBox->GetValue()   : 0.0;
		V.BackspinRpm  = BackspinBox ? BackspinBox->GetValue() : 0.0;
		V.SidespinRpm  = SidespinBox ? SidespinBox->GetValue() : 0.0;
		V.AzimuthDeg   = AzimuthBox  ? AzimuthBox->GetValue()  : 0.0;
		V.Club         = ClubCombo   ? ClubCombo->GetSelectedOption() : FString();
		OnFire(V);
	}
	ReturnFocusToGameViewport();   // so Space/M reach gameplay instead of the focused button/spinner
}

void UManualShotDialog::HandleClubSelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	// Programmatic selection (SetSelectedIndex) re-broadcasts with ESelectInfo::Direct; only act on
	// genuine user picks (same guard as UGolfRangePanel).
	if (bSuppressSelectionCallback || SelectionType == ESelectInfo::Direct || !ClubCombo)
	{
		return;
	}
	const int32 Idx = ClubCombo->GetSelectedIndex();
	if (Idx >= 0 && OnClubChosen)
	{
		OnClubChosen(Idx);
	}
	ReturnFocusToGameViewport();
}

void UManualShotDialog::ReturnFocusToGameViewport()
{
	// Deferred a tick so it runs after the combobox/button finish their own post-click focus handling.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UManualShotDialog> WeakSelf(this);
		World->GetTimerManager().SetTimerForNextTick([WeakSelf]()
		{
			if (WeakSelf.IsValid() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		});
	}
}
