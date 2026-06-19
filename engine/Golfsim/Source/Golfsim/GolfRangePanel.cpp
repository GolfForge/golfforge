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

	// GOL-149: one tower tile -- a faint eyebrow label over a mono value, added as a half-width cell so
	// two tiles split a row evenly (the 2-column tower grid). Returns the value text block; *OutCell (if
	// given) receives the cell container so the caller can collapse just that half.
	UTextBlock* BuildTowerTile(UWidgetTree* Tree, UHorizontalBox* Row, const TCHAR* Label,
		bool bAccent, TObjectPtr<UWidget>* OutCell = nullptr)
	{
		UVerticalBox* Cell = Tree->ConstructWidget<UVerticalBox>();
		Cell->AddChildToVerticalBox(MakeEyebrow(Tree, Label));
		UTextBlock* Val = Tree->ConstructWidget<UTextBlock>();
		Val->SetText(FText::FromString(TEXT("-")));
		Val->SetFont(Mono(20, FName(TEXT("Medium"))));
		Val->SetColorAndOpacity(FSlateColor(bAccent ? Color::Accent() : Color::Text()));
		if (UVerticalBoxSlot* VS = Cell->AddChildToVerticalBox(Val)) { VS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		if (UHorizontalBoxSlot* CS = Row->AddChildToHorizontalBox(Cell))
		{
			CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));   // two tiles split the row evenly
			CS->SetPadding(FMargin(0.f, 12.f, 8.f, 0.f));
		}
		if (OutCell) { *OutCell = Cell; }
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

	// ── Top-left Menu button (GOL-147) ───────────────────────────────────────────────────────────
	// Mirrors the in-round round-panel Menu button (same 28px top-left inset). Opens the settings menu.
	// The HUD hides it in a round (RoundHud provides the in-round one).
	// Filled glass chip (not a ghost button) so it reads as a solid control floating over the range
	// scene -- there's no panel behind it here, unlike the in-round round-panel Menu button.
	MenuButton = WidgetTree->ConstructWidget<UButton>();
	StyleButton(MenuButton, Color::GlassFill(), Radius::Sm, Color::Border(), 1.f);
	{
		UTextBlock* MenuLabel = WidgetTree->ConstructWidget<UTextBlock>();
		MenuLabel->SetText(FText::FromString(TEXT("Menu")));
		MenuLabel->SetFont(Body(14, FName(TEXT("SemiBold"))));
		MenuLabel->SetColorAndOpacity(FSlateColor(Color::Text()));
		MenuLabel->SetJustification(ETextJustify::Center);
		MenuButton->SetContent(MenuLabel);
	}
	MenuButton->OnClicked.AddDynamic(this, &UGolfRangePanel::HandleMenuClicked);
	if (UCanvasPanelSlot* MS = Root->AddChildToCanvas(MenuButton))
	{
		MS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		MS->SetAlignment(FVector2D(0.f, 0.f));
		MS->SetAutoSize(true);
		MS->SetOffsets(FMargin(28.f, 28.f, 0.f, 0.f));
	}

	// ── Top-right ball-ready badge (GOL-186) ───────────────────────────────────────────────────────
	// "Take your shot" chip, mirroring the top-left Menu (same 28px inset). Floats over the scene;
	// hidden (Collapsed) until a live launch monitor reports it's armed. The HUD toggles it.
	ReadyBadge = WidgetTree->ConstructWidget<UBorder>();
	ReadyBadge->SetPadding(FMargin(14.f, 8.f));
	{
		FLinearColor ReadyFill = Color::Accent(); ReadyFill.A = 0.18f;
		FLinearColor ReadyLine = Color::Accent(); ReadyLine.A = 0.55f;
		ReadyBadge->SetBrush(RoundedBrush(ReadyFill, Radius::Sm, ReadyLine, 1.f));
		UTextBlock* ReadyText = WidgetTree->ConstructWidget<UTextBlock>();
		ReadyText->SetText(FText::FromString(TEXT("● TAKE YOUR SHOT")));
		ReadyText->SetFont(Mono(14, FName(TEXT("SemiBold"))));
		ReadyText->SetColorAndOpacity(FSlateColor(Color::Accent()));
		ReadyBadge->SetContent(ReadyText);
	}
	ReadyBadge->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* RBS = Root->AddChildToCanvas(ReadyBadge))
	{
		RBS->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));   // top-right
		RBS->SetAlignment(FVector2D(1.f, 0.f));          // pivot on the chip's top-right corner
		RBS->SetAutoSize(true);
		RBS->SetOffsets(FMargin(-28.f, 28.f, 0.f, 0.f)); // 28px inset from the right + top
	}

	// ── Telemetry readout (bottom-left glass card) ───────────────────────────────────────────────
	UBorder* Readout = MakeGlassPanel(WidgetTree);
	TelemetryCard = Readout;   // GOL-149: density cycle toggles this whole card
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
	PinBox->OnValueCommitted.AddDynamic(this, &UGolfRangePanel::HandleSpinCommitted);   // GOL-203 focus-return
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

	// ── GOL-73 CTP settings cluster (hidden unless Closest-to-Pin mode) ──────────────────────────
	// A small label-maker for the cluster (mono faint, matches the PIN label above).
	auto MakeFaintLabel = [&](const TCHAR* Text) -> UTextBlock*
	{
		UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
		L->SetText(FText::FromString(Text));
		L->SetFont(Mono(11));
		L->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		return L;
	};
	auto MakeYdSpin = [&](float Min, float Max, float Delta, float Width) -> USpinBox*
	{
		USpinBox* S = WidgetTree->ConstructWidget<USpinBox>();
		S->SetMinValue(Min);   S->SetMaxValue(Max);
		S->SetMinSliderValue(Min); S->SetMaxSliderValue(Max);
		S->SetDelta(Delta);
		S->SetMinDesiredWidth(Width);
		S->SetMinFractionalDigits(0);
		S->SetMaxFractionalDigits(0);
		// Commit (Enter/Esc) hands focus back to the game -- see HandleSpinCommitted (GOL-203 polish).
		S->OnValueCommitted.AddDynamic(this, &UGolfRangePanel::HandleSpinCommitted);
		return S;
	};

	CtpControlsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* CCS = RCol->AddChildToVerticalBox(CtpControlsRow)) { CCS->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f)); }
	{
		auto AddCell = [&](UWidget* W, float RightPad)
		{
			if (UHorizontalBoxSlot* S = CtpControlsRow->AddChildToHorizontalBox(W))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(0.f, 0.f, RightPad, 0.f));
			}
		};

		AddCell(MakeFaintLabel(TEXT("MIN")), 6.f);
		CtpMinBox = MakeYdSpin(0.f, 400.f, 5.f, 70.f);
		CtpMinBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandleCtpMinChanged);
		AddCell(CtpMinBox, 14.f);

		AddCell(MakeFaintLabel(TEXT("MAX")), 6.f);
		CtpMaxBox = MakeYdSpin(0.f, 400.f, 5.f, 70.f);
		CtpMaxBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandleCtpMaxChanged);
		AddCell(CtpMaxBox, 18.f);

		CtpSideBox = WidgetTree->ConstructWidget<UCheckBox>();
		CtpSideBox->OnCheckStateChanged.AddDynamic(this, &UGolfRangePanel::HandleCtpSideChanged);
		AddCell(CtpSideBox, 6.f);
		{
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(TEXT("Side offset")));
			L->SetFont(Body(12));
			L->SetColorAndOpacity(FSlateColor(Color::TextDim()));
			AddCell(L, 18.f);
		}

		CtpPuttOutBox = WidgetTree->ConstructWidget<UCheckBox>();
		CtpPuttOutBox->OnCheckStateChanged.AddDynamic(this, &UGolfRangePanel::HandleCtpPuttOutChanged);
		AddCell(CtpPuttOutBox, 6.f);
		{
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(TEXT("Putt out within")));
			L->SetFont(Body(12));
			L->SetColorAndOpacity(FSlateColor(Color::TextDim()));
			AddCell(L, 6.f);
		}
		CtpWithinBox = MakeYdSpin(1.f, 50.f, 1.f, 56.f);
		CtpWithinBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandleCtpWithinChanged);
		AddCell(CtpWithinBox, 6.f);
		AddCell(MakeFaintLabel(TEXT("YD")), 0.f);
	}

	// GOL-75 putting controls: FEET min/max (5-ft steps) + scoring toggle + a disabled line-preview
	// seam. Reuses the CTP scoreboard row below; only the inputs differ. Hidden unless Putting mode.
	PuttControlsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* PCS = RCol->AddChildToVerticalBox(PuttControlsRow)) { PCS->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f)); }
	{
		auto AddCell = [&](UWidget* W, float RightPad)
		{
			if (UHorizontalBoxSlot* S = PuttControlsRow->AddChildToHorizontalBox(W))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(0.f, 0.f, RightPad, 0.f));
			}
		};
		auto AddCheck = [&](UCheckBox* Box, const TCHAR* LabelText, float RightPad)
		{
			AddCell(Box, 6.f);
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(LabelText));
			L->SetFont(Body(12));
			L->SetColorAndOpacity(FSlateColor(Color::TextDim()));
			AddCell(L, RightPad);
		};

		AddCell(MakeFaintLabel(TEXT("MIN")), 6.f);
		PuttMinBox = MakeYdSpin(5.f, 60.f, 5.f, 70.f);   // feet (5-ft steps); the YdSpin is a generic int spin
		PuttMinBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandlePuttMinChanged);
		AddCell(PuttMinBox, 14.f);

		AddCell(MakeFaintLabel(TEXT("MAX")), 6.f);
		PuttMaxBox = MakeYdSpin(5.f, 60.f, 5.f, 70.f);
		PuttMaxBox->OnValueChanged.AddDynamic(this, &UGolfRangePanel::HandlePuttMaxChanged);
		AddCell(PuttMaxBox, 6.f);
		AddCell(MakeFaintLabel(TEXT("FT")), 18.f);

		PuttHoleOutBox = WidgetTree->ConstructWidget<UCheckBox>();
		PuttHoleOutBox->OnCheckStateChanged.AddDynamic(this, &UGolfRangePanel::HandlePuttScoreChanged);
		AddCheck(PuttHoleOutBox, TEXT("Hole-out scoring"), 18.f);

		// Line preview is a GOL-75 follow-up (a break-aware ideal path needs an aim solver) -- show the
		// control as a disabled seam so the option is discoverable without promising behavior yet.
		PuttLinePreviewBox = WidgetTree->ConstructWidget<UCheckBox>();
		PuttLinePreviewBox->SetIsEnabled(false);
		PuttLinePreviewBox->SetToolTipText(FText::FromString(TEXT("Coming soon")));
		AddCheck(PuttLinePreviewBox, TEXT("Line preview"), 0.f);
	}

	// CTP scoreboard: This / Best / Avg / Shots (reuses the telemetry tile look).
	CtpScoreRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* CSS = RCol->AddChildToVerticalBox(CtpScoreRow)) { CSS->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f)); }
	CtpValPin   = BuildTile(WidgetTree, CtpScoreRow, TEXT("PIN"),   false, false);
	CtpValThis  = BuildTile(WidgetTree, CtpScoreRow, TEXT("THIS"),  true,  false);
	CtpValBest  = BuildTile(WidgetTree, CtpScoreRow, TEXT("BEST"),  false, false);
	CtpValAvg   = BuildTile(WidgetTree, CtpScoreRow, TEXT("AVG"),   false, false);
	CtpValShots = BuildTile(WidgetTree, CtpScoreRow, TEXT("SHOTS"), false, false);
	{
		// "End drill" -> back to the plain free-fire range (no mode dropdown anymore).
		EndPracticeBtn = WidgetTree->ConstructWidget<UButton>();
		StyleButton(EndPracticeBtn, Color::Surface2(), Radius::Sm, Color::Border(), 1.f);
		UTextBlock* EndLbl = WidgetTree->ConstructWidget<UTextBlock>();
		EndLbl->SetText(FText::FromString(TEXT("End drill")));
		EndLbl->SetFont(Body(13, FName(TEXT("SemiBold"))));
		EndLbl->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		EndLbl->SetJustification(ETextJustify::Center);
		EndPracticeBtn->SetContent(EndLbl);
		EndPracticeBtn->OnClicked.AddDynamic(this, &UGolfRangePanel::HandleEndPracticeClicked);
		if (UHorizontalBoxSlot* ES = CtpScoreRow->AddChildToHorizontalBox(EndPracticeBtn))
		{
			ES->SetVerticalAlignment(VAlign_Center);
			ES->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		}
	}

	// Hidden until the player switches into a practice drill.
	SetCtpControlsVisible(false);
	SetPuttingControlsVisible(false);

	// ── Control bar (full-width bottom glass bar) ────────────────────────────────────────────────
	// Flat edge-to-edge strip (no rounded corners / border) for a slicker look -- it spans the full
	// width at the very bottom, so a rounded card outline read as fussy.
	UBorder* Bar = WidgetTree->ConstructWidget<UBorder>();
	Bar->SetBrush(RoundedBrush(Color::GlassFill(), 0.f));
	Bar->SetPadding(FMargin(24.f, 12.f));
	ControlBar = Bar;   // GOL-149: density cycle toggles the whole control bar
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

	// GOL-149: the compact left-side metrics tower (hidden until the density cycle selects Compact).
	BuildTower(Root);
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
	if (ValClub)   { ValClub->SetText(FText::FromString(Club.ToUpper())); }
	if (TowerClub) { TowerClub->SetText(FText::FromString(Club.ToUpper())); }
}

void UGolfRangePanel::SetRangeControlsVisible(bool bVisible)
{
	if (RangeControlsRow)
	{
		RangeControlsRow->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UGolfRangePanel::SetCtpControlsVisible(bool bVisible)
{
	const ESlateVisibility V = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (CtpControlsRow) { CtpControlsRow->SetVisibility(V); }
	if (CtpScoreRow)    { CtpScoreRow->SetVisibility(V); }
}

void UGolfRangePanel::SetCtpConfigValues(double MinYd, double MaxYd, bool bSideOffset, bool bPuttOut, double WithinYd)
{
	bSuppressCtpCallback = true;
	if (CtpMinBox)     { CtpMinBox->SetValue((float)MinYd); }
	if (CtpMaxBox)     { CtpMaxBox->SetValue((float)MaxYd); }
	if (CtpSideBox)    { CtpSideBox->SetIsChecked(bSideOffset); }
	if (CtpPuttOutBox) { CtpPuttOutBox->SetIsChecked(bPuttOut); }
	if (CtpWithinBox)  { CtpWithinBox->SetValue((float)WithinYd); }
	bSuppressCtpCallback = false;
}

void UGolfRangePanel::SetCtpScore(const FString& ThisStr, const FString& BestStr, const FString& AvgStr, int32 Shots)
{
	if (CtpValThis)  { CtpValThis->SetText(FText::FromString(ThisStr)); }
	if (CtpValBest)  { CtpValBest->SetText(FText::FromString(BestStr)); }
	if (CtpValAvg)   { CtpValAvg->SetText(FText::FromString(AvgStr)); }
	if (CtpValShots) { CtpValShots->SetText(FText::FromString(FString::Printf(TEXT("%d"), Shots))); }
}

void UGolfRangePanel::SetCtpPinInfo(double Yd, double SideYd)
{
	if (!CtpValPin) { return; }
	FString Text = FString::Printf(TEXT("%.0f yd"), Yd);
	if (FMath::Abs(SideYd) >= 1.0)
	{
		Text += FString::Printf(TEXT("  %.0f%s"), FMath::Abs(SideYd), (SideYd >= 0.0) ? TEXT("R") : TEXT("L"));
	}
	CtpValPin->SetText(FText::FromString(Text));
}

void UGolfRangePanel::EmitCtpConfig()
{
	if (bSuppressCtpCallback || !OnCtpConfigChanged) { return; }
	const double MinYd    = CtpMinBox    ? CtpMinBox->GetValue()    : 50.0;
	const double MaxYd    = CtpMaxBox    ? CtpMaxBox->GetValue()    : 250.0;
	const bool   bSide    = CtpSideBox   ? CtpSideBox->IsChecked()  : false;
	const bool   bPuttOut = CtpPuttOutBox? CtpPuttOutBox->IsChecked() : false;
	const double WithinYd = CtpWithinBox ? CtpWithinBox->GetValue() : 10.0;
	OnCtpConfigChanged(MinYd, MaxYd, bSide, bPuttOut, WithinYd);
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::HandleCtpMinChanged(float)     { EmitCtpConfig(); }
void UGolfRangePanel::HandleCtpMaxChanged(float)     { EmitCtpConfig(); }
void UGolfRangePanel::HandleCtpSideChanged(bool)     { EmitCtpConfig(); }
void UGolfRangePanel::HandleCtpPuttOutChanged(bool)  { EmitCtpConfig(); }
void UGolfRangePanel::HandleCtpWithinChanged(float)  { EmitCtpConfig(); }

// --- GOL-75 putting controls (feet) -------------------------------------------------------------

void UGolfRangePanel::SetPuttingControlsVisible(bool bVisible)
{
	const ESlateVisibility V = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (PuttControlsRow) { PuttControlsRow->SetVisibility(V); }
	if (CtpScoreRow)     { CtpScoreRow->SetVisibility(V); }   // shared scoreboard
}

void UGolfRangePanel::SetPuttingConfigValues(double MinFt, double MaxFt, bool bHoleOut)
{
	bSuppressCtpCallback = true;   // same guard family -- CTP + putting are never active at once
	if (PuttMinBox)     { PuttMinBox->SetValue((float)MinFt); }
	if (PuttMaxBox)     { PuttMaxBox->SetValue((float)MaxFt); }
	if (PuttHoleOutBox) { PuttHoleOutBox->SetIsChecked(bHoleOut); }
	bSuppressCtpCallback = false;
}

void UGolfRangePanel::SetPuttingPinInfo(double Ft)
{
	if (CtpValPin) { CtpValPin->SetText(FText::FromString(FString::Printf(TEXT("%.0f ft"), Ft))); }
}

void UGolfRangePanel::EmitPuttingConfig()
{
	if (bSuppressCtpCallback || !OnPuttingConfigChanged) { return; }
	const double MinFt    = PuttMinBox     ? PuttMinBox->GetValue()      : 5.0;
	const double MaxFt    = PuttMaxBox     ? PuttMaxBox->GetValue()      : 30.0;
	const bool   bHoleOut = PuttHoleOutBox ? PuttHoleOutBox->IsChecked() : true;
	OnPuttingConfigChanged(MinFt, MaxFt, bHoleOut);
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::HandlePuttMinChanged(float)   { EmitPuttingConfig(); }
void UGolfRangePanel::HandlePuttMaxChanged(float)   { EmitPuttingConfig(); }
void UGolfRangePanel::HandlePuttScoreChanged(bool)  { EmitPuttingConfig(); }

void UGolfRangePanel::HandleEndPracticeClicked()
{
	if (OnEndPractice) { OnEndPractice(); }
	ReturnFocusToGameViewport();
}

void UGolfRangePanel::HandleSpinCommitted(float /*Value*/, ETextCommit::Type CommitMethod)
{
	// A click into a spinbox enters text-edit mode and traps keyboard focus (Space types a space
	// instead of swinging) -- Enter/Esc now hand focus back to the game. A commit caused by the
	// user clicking ANOTHER control (OnUserMovedFocus) is a deliberate move; leave that focus alone.
	if (CommitMethod != ETextCommit::OnUserMovedFocus)
	{
		ReturnFocusToGameViewport();
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

void UGolfRangePanel::HandleMenuClicked()
{
	if (OnMenu) { OnMenu(); }   // HUD opens the settings menu (which owns its own keyboard focus)
}

void UGolfRangePanel::SetMenuButtonVisible(bool bVisible)
{
	if (MenuButton)
	{
		MenuButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
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

void UGolfRangePanel::BuildTower(UCanvasPanel* Root)
{
	UBorder* Tower = MakeGlassPanel(WidgetTree);
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Tower->SetContent(Col);

	// Header: eyebrow + club headline (full width).
	Col->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("LAST SHOT")));
	TowerClub = WidgetTree->ConstructWidget<UTextBlock>();
	TowerClub->SetText(FText::FromString(TEXT("-")));
	TowerClub->SetFont(Display(28, FName(TEXT("SemiBold"))));
	TowerClub->SetColorAndOpacity(FSlateColor(Color::Text()));
	if (UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(TowerClub)) { HS->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f)); }

	// 2-column tile grid: two BuildTowerTile cells per row split the width evenly.
	auto NewRow = [&]() -> UHorizontalBox*
	{
		UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>();
		Col->AddChildToVerticalBox(R);
		return R;
	};

	UHorizontalBox* R = NewRow();
	TowerBall   = BuildTowerTile(WidgetTree, R, TEXT("BALL SPEED"), false);
	TowerLaunch = BuildTowerTile(WidgetTree, R, TEXT("LAUNCH"),     false);

	R = NewRow();
	TowerSpin  = BuildTowerTile(WidgetTree, R, TEXT("SPIN"),  false);
	TowerCarry = BuildTowerTile(WidgetTree, R, TEXT("CARRY"), true);

	R = NewRow();
	TowerTotal = BuildTowerTile(WidgetTree, R, TEXT("TOTAL"), false);
	TowerApex  = BuildTowerTile(WidgetTree, R, TEXT("APEX"),  false);

	R = NewRow();
	TowerDescent = BuildTowerTile(WidgetTree, R, TEXT("DESCENT"), false);
	TowerHang    = BuildTowerTile(WidgetTree, R, TEXT("HANG"),    false);

	// OFFLINE (always) pairs with CLUB SPEED (collapses to leave OFFLINE alone when no club data).
	R = NewRow();
	TowerOffline   = BuildTowerTile(WidgetTree, R, TEXT("OFFLINE"),    false);
	TowerClubSpeed = BuildTowerTile(WidgetTree, R, TEXT("CLUB SPEED"), false, &TowerClubSpeedRow);

	// Club-delivery rows -- collapsed as a unit unless the LM reports club data.
	R = NewRow();
	TowerSmashRow = R;
	TowerSmash  = BuildTowerTile(WidgetTree, R, TEXT("SMASH"),  false);
	TowerAttack = BuildTowerTile(WidgetTree, R, TEXT("ATTACK"), false);

	R = NewRow();
	TowerDeliveryRow = R;
	TowerPath = BuildTowerTile(WidgetTree, R, TEXT("PATH"), false);
	TowerFace = BuildTowerTile(WidgetTree, R, TEXT("FACE"), false);

	// Fixed width (room for two columns), anchored mid-left over the scene.
	USizeBox* Wrap = WidgetTree->ConstructWidget<USizeBox>();
	Wrap->SetWidthOverride(320.f);
	Wrap->SetContent(Tower);
	MetricsTower = Wrap;
	if (UCanvasPanelSlot* TS = Root->AddChildToCanvas(Wrap))
	{
		TS->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));   // mid-left
		TS->SetAlignment(FVector2D(0.f, 0.5f));
		TS->SetAutoSize(true);
		TS->SetOffsets(FMargin(28.f, 0.f, 0.f, 0.f));
	}
	SetTowerVisible(false);   // hidden until the density cycle selects Compact
}

void UGolfRangePanel::SetMetricsCardVisible(bool bVisible)
{
	if (TelemetryCard) { TelemetryCard->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void UGolfRangePanel::SetControlBarVisible(bool bVisible)
{
	if (ControlBar) { ControlBar->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void UGolfRangePanel::SetTowerVisible(bool bVisible)
{
	// Display-only: never eats clicks (HitTestInvisible), unlike the card/bar which hold controls.
	if (MetricsTower) { MetricsTower->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
}

void UGolfRangePanel::UpdateTowerExtras(double ApexFt, double DescentDeg, double HangS,
	double ClubSpeedMph, double Smash, double AttackDeg, double ClubPathDeg, double FaceDeg)
{
	if (TowerApex)    { TowerApex->SetText(FText::FromString(FString::Printf(TEXT("%.0f ft"), ApexFt))); }
	if (TowerDescent) { TowerDescent->SetText(FText::FromString(FString::Printf(TEXT("%.0f°"), DescentDeg))); }
	if (TowerHang)    { TowerHang->SetText(FText::FromString(FString::Printf(TEXT("%.1f s"), HangS))); }

	// Club-delivery rows appear only when the connector actually reported club data.
	const bool bHasClub = (ClubSpeedMph > 0.0);
	const ESlateVisibility ClubVis = bHasClub ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (TowerClubSpeedRow) { TowerClubSpeedRow->SetVisibility(ClubVis); }
	if (TowerSmashRow)     { TowerSmashRow->SetVisibility(ClubVis); }
	if (TowerDeliveryRow)  { TowerDeliveryRow->SetVisibility(ClubVis); }
	if (bHasClub)
	{
		if (TowerClubSpeed) { TowerClubSpeed->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), ClubSpeedMph))); }
		if (TowerSmash)     { TowerSmash->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Smash))); }
		if (TowerAttack)    { TowerAttack->SetText(FText::FromString(FString::Printf(TEXT("%+.1f°"), AttackDeg))); }
		if (TowerPath)      { TowerPath->SetText(FText::FromString(FString::Printf(TEXT("%+.1f°"), ClubPathDeg))); }
		if (TowerFace)      { TowerFace->SetText(FText::FromString(FString::Printf(TEXT("%+.1f°"), FaceDeg))); }
	}
}

void UGolfRangePanel::UpdateMetrics(const FString& Club, double SpeedMph, double LaunchDeg,
	double SpinRpm, double CarryYd, double TotalYd, double OfflineYd, bool bSpinEstimated)
{
	const FString ClubStr   = Club.ToUpper();
	const FString SpeedStr  = FString::Printf(TEXT("%.0f mph"), SpeedMph);
	const FString LaunchStr = FString::Printf(TEXT("%.1f°"), LaunchDeg);
	// Mark estimated spin so it's clearly computed, not measured by the LM.
	const FString SpinStr   = bSpinEstimated
		? FString::Printf(TEXT("%.0f rpm est"), SpinRpm)
		: FString::Printf(TEXT("%.0f rpm"), SpinRpm);
	const FString CarryStr  = FString::Printf(TEXT("%.0f yd"), CarryYd);
	const FString TotalStr  = FString::Printf(TEXT("%.0f yd"), TotalYd);
	const FString OfflineStr = (FMath::Abs(OfflineYd) < 0.5)
		? FString(TEXT("0 yd"))
		: FString::Printf(TEXT("%.0f yd %s"), FMath::Abs(OfflineYd), (OfflineYd >= 0.0) ? TEXT("R") : TEXT("L"));

	// Bottom-left card.
	if (ValClub)    { ValClub->SetText(FText::FromString(ClubStr)); }
	if (ValSpeed)   { ValSpeed->SetText(FText::FromString(SpeedStr)); }
	if (ValLaunch)  { ValLaunch->SetText(FText::FromString(LaunchStr)); }
	if (ValSpin)    { ValSpin->SetText(FText::FromString(SpinStr)); }
	if (ValCarry)   { ValCarry->SetText(FText::FromString(CarryStr)); }
	if (ValTotal)   { ValTotal->SetText(FText::FromString(TotalStr)); }
	if (ValOffline) { ValOffline->SetText(FText::FromString(OfflineStr)); }

	// GOL-149 compact tower (shares the animating values; non-animating extras via UpdateTowerExtras).
	if (TowerClub)    { TowerClub->SetText(FText::FromString(ClubStr)); }
	if (TowerBall)    { TowerBall->SetText(FText::FromString(SpeedStr)); }
	if (TowerLaunch)  { TowerLaunch->SetText(FText::FromString(LaunchStr)); }
	if (TowerSpin)    { TowerSpin->SetText(FText::FromString(SpinStr)); }
	if (TowerCarry)   { TowerCarry->SetText(FText::FromString(CarryStr)); }
	if (TowerTotal)   { TowerTotal->SetText(FText::FromString(TotalStr)); }
	if (TowerOffline) { TowerOffline->SetText(FText::FromString(OfflineStr)); }
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

void UGolfRangePanel::SetLaunchMonitorReady(bool bReady)
{
	if (ReadyBadge)
	{
		ReadyBadge->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
