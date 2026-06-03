#include "GolfRangePanel.h"
#include "UI/GolfUITheme.h"
#include "Drivers/LaunchMonitorManager.h"   // ELaunchMonitorStatus

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/SlateWrapperTypes.h"   // FSlateChildSize / ESlateSizeRule for fill spacers
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace GolfUI;

namespace
{
	// A telemetry tile: small eyebrow over a mono value, in a rounded inset. bAccent fills the cell
	// with the soft accent + tints the value green (CARRY). bBig bumps the value size for the primary row.
	UTextBlock* BuildTile(UWidgetTree* Tree, UHorizontalBox* Row, const TCHAR* Label,
		bool bAccent, bool bBig)
	{
		UBorder* Cell = Tree->ConstructWidget<UBorder>();
		Cell->SetBrush(RoundedBrush(bAccent ? Color::AccentSoft() : Color::Surface(), Radius::Md,
			bAccent ? Color::AccentLine() : Color::Border(), 1.f));
		Cell->SetPadding(FMargin(12.f, 8.f));

		UVerticalBox* Col = Tree->ConstructWidget<UVerticalBox>();
		Cell->SetContent(Col);
		Col->AddChildToVerticalBox(MakeEyebrow(Tree, Label));

		UTextBlock* Val = Tree->ConstructWidget<UTextBlock>();
		Val->SetText(FText::FromString(TEXT("-")));
		Val->SetFont(Mono(bBig ? 22 : 15, FName(TEXT("Medium"))));
		Val->SetColorAndOpacity(FSlateColor(bAccent ? Color::Accent() : Color::Text()));
		if (UVerticalBoxSlot* VS = Col->AddChildToVerticalBox(Val)) { VS->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f)); }

		if (UHorizontalBoxSlot* CS = Row->AddChildToHorizontalBox(Cell))
		{
			CS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
			CS->SetVerticalAlignment(VAlign_Fill);
		}
		return Val;
	}
}

void UGolfRangePanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UGolfRangePanel::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;   // without this the base RebuildWidget renders an empty SSpacer

	// Full-screen vertical stack: a fill spacer pushes the readout + control bar to the bottom. This
	// sidesteps fiddly mixed canvas anchors and gives the control bar a clean full-width bottom span.
	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>();
	Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* SS = Root->AddChildToCanvas(Stack))
	{
		SS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		SS->SetOffsets(FMargin(0.f));
	}

	USpacer* Fill = WidgetTree->ConstructWidget<USpacer>();
	Fill->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UVerticalBoxSlot* FS = Stack->AddChildToVerticalBox(Fill)) { FS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// ── Telemetry readout (bottom-left glass card) ───────────────────────────────────────────────
	UBorder* Readout = MakeGlassPanel(WidgetTree);
	if (UVerticalBoxSlot* RS = Stack->AddChildToVerticalBox(Readout))
	{
		RS->SetHorizontalAlignment(HAlign_Left);
		RS->SetPadding(FMargin(28.f, 0.f, 0.f, 14.f));
	}
	UVerticalBox* RCol = WidgetTree->ConstructWidget<UVerticalBox>();
	Readout->SetContent(RCol);

	// Header: eyebrow + club headline (left), primary-action button (right).
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	RCol->AddChildToVerticalBox(Header);
	{
		UVerticalBox* HL = WidgetTree->ConstructWidget<UVerticalBox>();
		HL->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("LAUNCH MONITOR · LAST SHOT")));
		ValClub = WidgetTree->ConstructWidget<UTextBlock>();
		ValClub->SetText(FText::FromString(TEXT("-")));
		ValClub->SetFont(Display(30, FName(TEXT("SemiBold"))));
		ValClub->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UVerticalBoxSlot* CS = HL->AddChildToVerticalBox(ValClub)) { CS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		if (UHorizontalBoxSlot* HLS = Header->AddChildToHorizontalBox(HL)) { HLS->SetVerticalAlignment(VAlign_Center); }

		USpacer* HGap = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* GS = Header->AddChildToHorizontalBox(HGap)) { GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

		// Primary action button: "Swing"/"Sim shot" + a [Space] keycap. Surface fill so it reads as an
		// elevated control over the glass (matches 06/08).
		PrimaryButton = WidgetTree->ConstructWidget<UButton>();
		StyleButton(PrimaryButton, Color::Surface2(), Radius::Md, Color::Border(), 1.f);
		UHorizontalBox* BtnRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		PrimaryButtonLabel = WidgetTree->ConstructWidget<UTextBlock>();
		PrimaryButtonLabel->SetText(FText::FromString(TEXT("Swing")));
		PrimaryButtonLabel->SetFont(Body(14, FName(TEXT("SemiBold"))));
		PrimaryButtonLabel->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UHorizontalBoxSlot* LS = BtnRow->AddChildToHorizontalBox(PrimaryButtonLabel))
		{
			LS->SetVerticalAlignment(VAlign_Center);
			LS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		if (UHorizontalBoxSlot* KS = BtnRow->AddChildToHorizontalBox(MakeKbd(WidgetTree, TEXT("Space"))))
		{
			KS->SetVerticalAlignment(VAlign_Center);
		}
		PrimaryButton->SetContent(BtnRow);
		PrimaryButton->OnClicked.AddDynamic(this, &UGolfRangePanel::HandlePrimaryActionClicked);
		if (UHorizontalBoxSlot* BS = Header->AddChildToHorizontalBox(PrimaryButton))
		{
			BS->SetVerticalAlignment(VAlign_Center);
			BS->SetPadding(FMargin(20.f, 0.f, 0.f, 0.f));
		}
	}

	// Secondary tiles: ball speed / launch / spin.
	UHorizontalBox* SecRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* SRS = RCol->AddChildToVerticalBox(SecRow)) { SRS->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f)); }
	ValSpeed  = BuildTile(WidgetTree, SecRow, TEXT("BALL SPEED"), false, false);
	ValLaunch = BuildTile(WidgetTree, SecRow, TEXT("LAUNCH"),     false, false);
	ValSpin   = BuildTile(WidgetTree, SecRow, TEXT("SPIN"),       false, false);

	// Primary tiles: carry (accent) / total / offline.
	UHorizontalBox* PriRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* PRS = RCol->AddChildToVerticalBox(PriRow)) { PRS->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f)); }
	ValCarry   = BuildTile(WidgetTree, PriRow, TEXT("CARRY"),   true,  true);
	ValTotal   = BuildTile(WidgetTree, PriRow, TEXT("TOTAL"),   false, true);
	ValOffline = BuildTile(WidgetTree, PriRow, TEXT("OFFLINE"), false, true);

	// Range-only dev controls: pin distance spinner + putt-from-green checkbox. Hidden in a round.
	RangeControlsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* RGS = RCol->AddChildToVerticalBox(RangeControlsRow)) { RGS->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f)); }
	{
		UTextBlock* PinLbl = WidgetTree->ConstructWidget<UTextBlock>();
		PinLbl->SetText(FText::FromString(TEXT("PIN")));
		PinLbl->SetFont(Mono(11));
		PinLbl->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* PLS = RangeControlsRow->AddChildToHorizontalBox(PinLbl))
		{
			PLS->SetVerticalAlignment(VAlign_Center);
			PLS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}

		PinBox = WidgetTree->ConstructWidget<USpinBox>();
		PinBox->SetMinValue(0.f);
		PinBox->SetMaxValue(400.f);
		PinBox->SetMinSliderValue(0.f);
		PinBox->SetMaxSliderValue(400.f);
		PinBox->SetDelta(5.f);
		PinBox->SetMinDesiredWidth(80.f);
		PinBox->SetMinFractionalDigits(0);
		PinBox->SetMaxFractionalDigits(0);
		PinBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandlePinValueChanged);
		if (UHorizontalBoxSlot* PBS = RangeControlsRow->AddChildToHorizontalBox(PinBox))
		{
			PBS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			PBS->SetVerticalAlignment(VAlign_Center);
		}

		PinActualText = WidgetTree->ConstructWidget<UTextBlock>();
		PinActualText->SetText(FText::FromString(TEXT("@ -- yd")));
		PinActualText->SetFont(Mono(12));
		PinActualText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UHorizontalBoxSlot* PAS = RangeControlsRow->AddChildToHorizontalBox(PinActualText))
		{
			PAS->SetVerticalAlignment(VAlign_Center);
			PAS->SetPadding(FMargin(0.f, 0.f, 18.f, 0.f));
		}

		PuttModeBox = WidgetTree->ConstructWidget<UCheckBox>();
		PuttModeBox->OnCheckStateChanged.AddDynamic(this, &UGolfRangePanel::HandlePuttModeChanged);
		if (UHorizontalBoxSlot* PMS = RangeControlsRow->AddChildToHorizontalBox(PuttModeBox))
		{
			PMS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			PMS->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* PuttLbl = WidgetTree->ConstructWidget<UTextBlock>();
		PuttLbl->SetText(FText::FromString(TEXT("Putt from green")));
		PuttLbl->SetFont(Body(12));
		PuttLbl->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UHorizontalBoxSlot* PLS2 = RangeControlsRow->AddChildToHorizontalBox(PuttLbl))
		{
			PLS2->SetVerticalAlignment(VAlign_Center);
		}
	}

	// ── Control bar (full-width bottom glass bar) ────────────────────────────────────────────────
	UBorder* Bar = WidgetTree->ConstructWidget<UBorder>();
	Bar->SetBrush(RoundedBrush(Color::GlassFill(), Radius::Lg, Color::Border(), 1.f));
	Bar->SetPadding(FMargin(24.f, 12.f));
	if (UVerticalBoxSlot* BarSlot = Stack->AddChildToVerticalBox(Bar)) { BarSlot->SetHorizontalAlignment(HAlign_Fill); }

	UHorizontalBox* BarRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Bar->SetContent(BarRow);

	// One labeled dropdown cell: eyebrow over a fixed-width styled combo. Options come from the HUD.
	auto AddBarCombo = [&](const TCHAR* Label, float Width) -> UComboBoxString*
	{
		UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>();
		Cell->AddChildToVerticalBox(MakeEyebrow(WidgetTree, Label));

		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
		StyleComboBox(Combo);
		USizeBox* Wrap = WidgetTree->ConstructWidget<USizeBox>();
		Wrap->SetWidthOverride(Width);
		Wrap->SetContent(Combo);
		if (UVerticalBoxSlot* WS = Cell->AddChildToVerticalBox(Wrap)) { WS->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f)); }

		if (UHorizontalBoxSlot* CS = BarRow->AddChildToHorizontalBox(Cell))
		{
			CS->SetVerticalAlignment(VAlign_Center);
			CS->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
		}
		return Combo;
	};

	ClubCombo = AddBarCombo(TEXT("CLUB"), 150.f);
	ClubCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleClubSelectionChanged);
	TimeCombo = AddBarCombo(TEXT("TIME OF DAY"), 150.f);
	TimeCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleTimeSelectionChanged);
	SkyCombo = AddBarCombo(TEXT("SKY / WEATHER"), 160.f);
	SkyCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleSkySelectionChanged);
	CameraCombo = AddBarCombo(TEXT("CAMERA"), 140.f);
	CameraCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleCameraSelectionChanged);
	LMCombo = AddBarCombo(TEXT("LAUNCH MONITOR"), 190.f);
	LMCombo->OnSelectionChanged.AddDynamic(this, &UGolfRangePanel::HandleLaunchMonitorSelectionChanged);

	USpacer* BarGap = WidgetTree->ConstructWidget<USpacer>();
	if (UHorizontalBoxSlot* BGS = BarRow->AddChildToHorizontalBox(BarGap)) { BGS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// LM status pill (recoloured by SetLaunchMonitorStatus; amber game-mode default).
	StatusPill = WidgetTree->ConstructWidget<UBorder>();
	StatusPill->SetPadding(FMargin(12.f, 7.f));
	UHorizontalBox* PillRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	StatusPill->SetContent(PillRow);
	{
		USizeBox* DotBox = WidgetTree->ConstructWidget<USizeBox>();
		DotBox->SetWidthOverride(9.f);
		DotBox->SetHeightOverride(9.f);
		StatusDot = WidgetTree->ConstructWidget<UBorder>();
		DotBox->SetContent(StatusDot);
		if (UHorizontalBoxSlot* DS = PillRow->AddChildToHorizontalBox(DotBox))
		{
			DS->SetVerticalAlignment(VAlign_Center);
			DS->SetPadding(FMargin(0.f, 0.f, 9.f, 0.f));
		}

		UVerticalBox* PillCol = WidgetTree->ConstructWidget<UVerticalBox>();
		StatusEyebrow = MakeEyebrow(WidgetTree, TEXT("MODE"));
		PillCol->AddChildToVerticalBox(StatusEyebrow);
		StatusValue = WidgetTree->ConstructWidget<UTextBlock>();
		StatusValue->SetText(FText::FromString(TEXT("Game · Keyboard")));
		StatusValue->SetFont(Mono(13, FName(TEXT("Medium"))));
		StatusValue->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UVerticalBoxSlot* PVS = PillCol->AddChildToVerticalBox(StatusValue)) { PVS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		PillRow->AddChildToHorizontalBox(PillCol);
	}
	if (UHorizontalBoxSlot* PS = BarRow->AddChildToHorizontalBox(StatusPill)) { PS->SetVerticalAlignment(VAlign_Center); }

	// Default look = Sim / game mode (the HUD re-applies this once the manager is wired).
	SetLaunchMonitorStatus(ELaunchMonitorStatus::Sim, TEXT("Simulated (no device)"));
}

namespace
{
	// Repopulate a dropdown's options from a name list.
	void FillCombo(UComboBoxString* Combo, const TArray<FString>& Names)
	{
		if (!Combo) { return; }
		Combo->ClearOptions();
		for (const FString& Name : Names) { Combo->AddOption(Name); }
	}
}

void UGolfRangePanel::SetClubOptions(const TArray<FString>& Names) { FillCombo(ClubCombo, Names); }
void UGolfRangePanel::SetTimeOptions(const TArray<FString>& Names) { FillCombo(TimeCombo, Names); }
void UGolfRangePanel::SetSkyOptions(const TArray<FString>& Names)  { FillCombo(SkyCombo, Names); }
void UGolfRangePanel::SetCameraOptions(const TArray<FString>& Names) { FillCombo(CameraCombo, Names); }
void UGolfRangePanel::SetLaunchMonitorOptions(const TArray<FString>& Names) { FillCombo(LMCombo, Names); }

void UGolfRangePanel::SetComboIndexGuarded(UComboBoxString* Combo, int32 Index)
{
	if (!Combo || Combo->GetOptionCount() <= 0) { return; }   // nothing to select yet
	const int32 Clamped = FMath::Clamp(Index, 0, Combo->GetOptionCount() - 1);
	bSuppressSelectionCallback = true;
	Combo->SetSelectedIndex(Clamped);
	bSuppressSelectionCallback = false;
}

void UGolfRangePanel::SetSelectedClubIndex(int32 Index) { SetComboIndexGuarded(ClubCombo, Index); }
void UGolfRangePanel::SetSelectedTimeIndex(int32 Index) { SetComboIndexGuarded(TimeCombo, Index); }
void UGolfRangePanel::SetSelectedSkyIndex(int32 Index)  { SetComboIndexGuarded(SkyCombo, Index); }
void UGolfRangePanel::SetSelectedCameraIndex(int32 Index) { SetComboIndexGuarded(CameraCombo, Index); }
void UGolfRangePanel::SetSelectedLaunchMonitorIndex(int32 Index) { SetComboIndexGuarded(LMCombo, Index); }

void UGolfRangePanel::SetMetricClubName(const FString& Club)
{
	if (ValClub) { ValClub->SetText(FText::FromString(Club.ToUpper())); }
}

void UGolfRangePanel::SetRangeControlsVisible(bool bVisible)
{
	if (RangeControlsRow)
	{
		RangeControlsRow->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

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

void UGolfRangePanel::HandleCameraSelectionChanged(FString, ESelectInfo::Type SelectionType)
{
	HandleComboPick(CameraCombo, OnCameraChosen, SelectionType);
}

void UGolfRangePanel::HandlePrimaryActionClicked()
{
	if (OnPrimaryAction) { OnPrimaryAction(); }
	ReturnFocusToGameViewport();   // so Space/1-6/arrows still reach gameplay after the click
}

void UGolfRangePanel::HandlePinValueChanged(float Value)
{
	// Programmatic SetValue() should not loop back into gameplay (HUD writes the value at start-up).
	if (bSuppressPinCallback) { return; }
	if (OnPinChanged) { OnPinChanged(static_cast<double>(Value)); }
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::HandlePuttModeChanged(bool bChecked)
{
	if (bSuppressPuttCallback) { return; }
	if (OnPuttModeChanged) { OnPuttModeChanged(bChecked); }
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::SetPinValue(double Yards)
{
	if (!PinBox) { return; }
	const float Clamped = FMath::Clamp(static_cast<float>(Yards), 0.f, 400.f);
	bSuppressPinCallback = true;
	PinBox->SetValue(Clamped);
	bSuppressPinCallback = false;
}

void UGolfRangePanel::SetPinActualReadout(double Yards)
{
	if (PinActualText) { PinActualText->SetText(FText::FromString(FString::Printf(TEXT("@ %.0f yd"), Yards))); }
}

void UGolfRangePanel::SetPuttMode(bool bChecked)
{
	if (!PuttModeBox) { return; }
	bSuppressPuttCallback = true;
	PuttModeBox->SetIsChecked(bChecked);
	bSuppressPuttCallback = false;
}

void UGolfRangePanel::HandleComboPick(UComboBoxString* Combo, const TFunction<void(int32)>& OnChosen,
	ESelectInfo::Type SelectionType)
{
	// Programmatic selection (SetSelectedIndex) re-broadcasts with ESelectInfo::Direct; only act on
	// genuine user picks. The bool guard is belt-and-suspenders for the same reentrancy.
	if (bSuppressSelectionCallback || SelectionType == ESelectInfo::Direct || !Combo) { return; }
	const int32 Idx = Combo->GetSelectedIndex();
	if (Idx >= 0 && OnChosen) { OnChosen(Idx); }
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::ReturnFocusToGameViewport()
{
	// Hand keyboard focus back to the game viewport so Space/1-6/arrows reach gameplay instead of the
	// focused combobox. Deferred a tick so it runs after the combobox's own post-selection focus.
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
	double SpinRpm, double CarryYd, double TotalYd, double OfflineYd, bool bSpinEstimated)
{
	if (ValClub)   { ValClub->SetText(FText::FromString(Club.ToUpper())); }
	if (ValSpeed)  { ValSpeed->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), SpeedMph))); }
	if (ValLaunch) { ValLaunch->SetText(FText::FromString(FString::Printf(TEXT("%.1f°"), LaunchDeg))); }
	if (ValSpin)
	{
		// Mark estimated spin so it's clearly computed, not measured by the LM.
		ValSpin->SetText(FText::FromString(bSpinEstimated
			? FString::Printf(TEXT("%.0f rpm est"), SpinRpm)
			: FString::Printf(TEXT("%.0f rpm"), SpinRpm)));
	}
	if (ValCarry)  { ValCarry->SetText(FText::FromString(FString::Printf(TEXT("%.0f yd"), CarryYd))); }
	if (ValTotal)  { ValTotal->SetText(FText::FromString(FString::Printf(TEXT("%.0f yd"), TotalYd))); }
	if (ValOffline)
	{
		ValOffline->SetText(FText::FromString(FMath::Abs(OfflineYd) < 0.5
			? FString(TEXT("0 yd"))
			: FString::Printf(TEXT("%.0f yd %s"), FMath::Abs(OfflineYd), (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L"))));
	}
}

void UGolfRangePanel::SetPrimaryActionLabel(const FString& Label)
{
	if (PrimaryButtonLabel) { PrimaryButtonLabel->SetText(FText::FromString(Label)); }
}

void UGolfRangePanel::SetLaunchMonitorStatus(ELaunchMonitorStatus Status, const FString& Name)
{
	const bool bOnline = (Status == ELaunchMonitorStatus::Online);
	const FLinearColor Tint = bOnline ? Color::Accent() : Color::Caution();

	if (StatusDot)  { StatusDot->SetBrush(RoundedBrush(Tint, 999.f)); }
	if (StatusPill)
	{
		FLinearColor Fill = Tint; Fill.A = 0.14f;
		FLinearColor Line = Tint; Line.A = 0.42f;
		StatusPill->SetBrush(RoundedBrush(Fill, Radius::Sm, Line, 1.f));
	}
	if (StatusEyebrow) { StatusEyebrow->SetText(FText::FromString(bOnline ? TEXT("MONITOR") : TEXT("MODE"))); }
	if (StatusValue)
	{
		// Truncate long device names for the pill (matches hud.js: >18 chars -> first 16 + ellipsis).
		FString Short = Name;
		if (Short.Len() > 18) { Short = Short.Left(16) + TEXT("…"); }
		FString Val;
		switch (Status)
		{
			case ELaunchMonitorStatus::Online:  Val = Name + TEXT(" · Live"); break;
			case ELaunchMonitorStatus::Pairing: Val = Short + TEXT(" · Pairing…"); break;
			case ELaunchMonitorStatus::Off:     Val = Short + TEXT(" · Offline"); break;
			case ELaunchMonitorStatus::Sim:
			default:                            Val = TEXT("Game · Keyboard"); break;
		}
		StatusValue->SetText(FText::FromString(Val));
		StatusValue->SetColorAndOpacity(FSlateColor(bOnline ? Tint : Color::Text()));
	}
}
