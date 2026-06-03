#include "UI/RoundSetupWizard.h"
#include "UI/GolfUITheme.h"
#include "UI/CourseCard.h"
#include "UI/OptionCard.h"
#include "UI/SegmentedControl.h"
#include "GolfDisplaySettings.h"   // GOL-143: player name + handicap pre-fill

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"

namespace
{
	const TCHAR* StepNames[3] = { TEXT("Course"), TEXT("Format"), TEXT("Players") };
}

void URoundSetupWizard::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);   // so NativeOnKeyDown (Enter/Esc) fires when the HUD gives us focus
	BuildTree();
	ShowStep(1);
}

FReply URoundSetupWizard::NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Enter)
	{
		GoNext();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		GoBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geo, KeyEvent);
}

// ───────────────────────── build ─────────────────────────
void URoundSetupWizard::BuildTree()
{
	using namespace GolfUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	auto FullScreen = [&](UWidget* W, bool bHitTest)
	{
		UCanvasPanelSlot* S = Root->AddChildToCanvas(W);
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
		if (!bHitTest) { W->SetVisibility(ESlateVisibility::HitTestInvisible); }
		return S;
	};

	// base canvas + radial ambiance (bg-glow top-right, accent-soft bottom-left), per the design stage.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
	Bg->SetBrushColor(Color::Bg0());
	FullScreen(Bg, true);
	FullScreen(MakeRadialGradient(WidgetTree, Color::BgGlow(), FLinearColor(0, 0, 0, 0), FVector2D(0.85f, 0.0f), 0.55f), false);
	FullScreen(MakeRadialGradient(WidgetTree, Color::AccentSoft(), FLinearColor(0, 0, 0, 0), FVector2D(0.0f, 1.0f), 0.5f), false);

	// main column: topbar | flow | footer
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	FullScreen(Col, true);

	UHorizontalBox* Topbar = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(Topbar)) { TS->SetPadding(FMargin(40.f, 22.f)); }
	BuildTopbar(Topbar);

	// flow (scrolls); switcher holds the three step panels.
	UScrollBox* Flow = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* FS = Col->AddChildToVerticalBox(Flow)) { FS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	ContentSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UScrollBoxSlot* CS = Cast<UScrollBoxSlot>(Flow->AddChild(ContentSwitcher))) { CS->SetHorizontalAlignment(HAlign_Left); }
	ContentSwitcher->AddChild(BuildCourseStep());
	ContentSwitcher->AddChild(BuildFormatStep());
	ContentSwitcher->AddChild(BuildPlayersStep());

	// footer hairline + bar
	UBorder* Hair = WidgetTree->ConstructWidget<UBorder>();
	Hair->SetBrushColor(Color::Border());
	USizeBox* HairBox = WidgetTree->ConstructWidget<USizeBox>();
	HairBox->SetHeightOverride(1.f); HairBox->SetContent(Hair);
	Col->AddChildToVerticalBox(HairBox);

	UBorder* FootBg = WidgetTree->ConstructWidget<UBorder>();
	FootBg->SetBrushColor(FLinearColor(0.043f, 0.06f, 0.05f, 0.65f));
	Col->AddChildToVerticalBox(FootBg);
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	FootBg->SetPadding(FMargin(40.f, 16.f));
	FootBg->SetContent(Footer);
	BuildFooter(Footer);
}

void URoundSetupWizard::BuildTopbar(UHorizontalBox* Bar)
{
	using namespace GolfUI;

	// ── brand lockup (left) ──
	UHorizontalBox* Brand = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		USizeBox* MarkBox = WidgetTree->ConstructWidget<USizeBox>();
		MarkBox->SetWidthOverride(38.f); MarkBox->SetHeightOverride(38.f);
		UBorder* Mark = WidgetTree->ConstructWidget<UBorder>();
		Mark->SetBrush(RoundedBrush(Color::Accent(), Radius::Md));   // glyph lands with the GOL-151 icon font
		MarkBox->SetContent(Mark);
		if (UHorizontalBoxSlot* MS = Brand->AddChildToHorizontalBox(MarkBox)) { MS->SetVerticalAlignment(VAlign_Center); MS->SetPadding(FMargin(0, 0, 13.f, 0)); }

		UVerticalBox* Names = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* BrandName = WidgetTree->ConstructWidget<UTextBlock>();
		BrandName->SetText(FText::FromString(TEXT("GOLFFORGE")));
		{ FSlateFontInfo F = Display(19, FName(TEXT("Bold"))); F.LetterSpacing = 60; BrandName->SetFont(F); }
		BrandName->SetColorAndOpacity(FSlateColor(Color::Text()));
		Names->AddChildToVerticalBox(BrandName);
		if (UVerticalBoxSlot* ES = Names->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Round Setup")))) { ES->SetPadding(FMargin(0, 3.f, 0, 0)); }
		if (UHorizontalBoxSlot* NS = Brand->AddChildToHorizontalBox(Names)) { NS->SetVerticalAlignment(VAlign_Center); }
	}
	if (UHorizontalBoxSlot* BS = Bar->AddChildToHorizontalBox(Brand)) { BS->SetVerticalAlignment(VAlign_Center); }

	// spacer | stepper | spacer  -> keeps the stepper centered
	if (UHorizontalBoxSlot* L = Bar->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())) { L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	UHorizontalBox* Stepper = WidgetTree->ConstructWidget<UHorizontalBox>();
	BuildStepper(Stepper);
	if (UHorizontalBoxSlot* StS = Bar->AddChildToHorizontalBox(Stepper)) { StS->SetVerticalAlignment(VAlign_Center); }
	if (UHorizontalBoxSlot* R = Bar->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())) { R->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// ── close-X (right) ──
	USizeBox* XBox = WidgetTree->ConstructWidget<USizeBox>();
	XBox->SetWidthOverride(38.f); XBox->SetHeightOverride(38.f);
	UButton* CloseX = WidgetTree->ConstructWidget<UButton>();
	{
		FButtonStyle S;
		S.SetNormal(RoundedBrush(Color::Surface(), Radius::Md, Color::Border(), 1.f));
		S.SetHovered(RoundedBrush(Color::Surface2(), Radius::Md, Color::AccentLine(), 1.f));
		S.SetPressed(RoundedBrush(Color::Surface(), Radius::Md, Color::AccentLine(), 1.f));
		S.SetDisabled(RoundedBrush(Color::Surface(), Radius::Md));
		S.SetNormalPadding(FMargin(0.f)); S.SetPressedPadding(FMargin(0.f));
		CloseX->SetStyle(S);
	}
	CloseX->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleCloseClicked);
	UTextBlock* X = WidgetTree->ConstructWidget<UTextBlock>();
	X->SetText(FText::FromString(TEXT("X")));
	X->SetFont(Mono(13));
	X->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	X->SetJustification(ETextJustify::Center);
	CloseX->SetContent(X);
	XBox->SetContent(CloseX);
	if (UHorizontalBoxSlot* XS = Bar->AddChildToHorizontalBox(XBox)) { XS->SetVerticalAlignment(VAlign_Center); }
}

void URoundSetupWizard::BuildStepper(UHorizontalBox* Stepper)
{
	using namespace GolfUI;
	StepPills.Reset(); StepNumTexts.Reset(); StepNameTexts.Reset();

	for (int32 i = 0; i < 3; ++i)
	{
		if (i > 0)
		{
			UBorder* Sep = WidgetTree->ConstructWidget<UBorder>();
			Sep->SetBrushColor(Color::BorderStrong());
			USizeBox* SepBox = WidgetTree->ConstructWidget<USizeBox>();
			SepBox->SetWidthOverride(22.f); SepBox->SetHeightOverride(1.f); SepBox->SetContent(Sep);
			if (UHorizontalBoxSlot* SS = Stepper->AddChildToHorizontalBox(SepBox)) { SS->SetVerticalAlignment(VAlign_Center); SS->SetPadding(FMargin(6.f, 0.f)); }
		}

		// Pill = a button whose own FButtonStyle is painted per state in RefreshStepper (the proven
		// SettingsMenu-rail mechanism). Content is just number + name text (colored per state).
		UButton* Pill = WidgetTree->ConstructWidget<UButton>();
		Pill->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleStepperClicked);
		UHorizontalBox* PillRow = WidgetTree->ConstructWidget<UHorizontalBox>();

		// Number + name share a baseline: same point size, both center-aligned in the row (mono vs
		// condensed have different line metrics, so matching the size keeps the digit on the name's line).
		UTextBlock* NumText = WidgetTree->ConstructWidget<UTextBlock>();
		NumText->SetText(FText::FromString(FString::FromInt(i + 1)));
		NumText->SetFont(Mono(14, FName(TEXT("Bold"))));
		NumText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		NumText->SetJustification(ETextJustify::Center);
		if (UHorizontalBoxSlot* NS = PillRow->AddChildToHorizontalBox(NumText)) { NS->SetVerticalAlignment(VAlign_Center); NS->SetPadding(FMargin(0, 0, 10.f, 0)); }

		UTextBlock* NameText = WidgetTree->ConstructWidget<UTextBlock>();
		NameText->SetText(FText::FromString(StepNames[i]));
		{ FSlateFontInfo F = Display(14, FName(TEXT("SemiBold"))); F.LetterSpacing = 40; NameText->SetFont(F); }
		NameText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* NmS = PillRow->AddChildToHorizontalBox(NameText)) { NmS->SetVerticalAlignment(VAlign_Center); }

		Pill->SetContent(PillRow);
		if (UHorizontalBoxSlot* PS = Stepper->AddChildToHorizontalBox(Pill)) { PS->SetVerticalAlignment(VAlign_Center); }

		StepPills.Add(Pill);
		StepNumTexts.Add(NumText);
		StepNameTexts.Add(NameText);
	}
}

UWidget* URoundSetupWizard::BuildCourseStep()
{
	using namespace GolfUI;
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();

	// step head
	UVerticalBox* Head = WidgetTree->ConstructWidget<UVerticalBox>();
	Head->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Step 01 · Where are we playing")));
	if (UVerticalBoxSlot* HS = Head->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Pick your course"), 40))) { HS->SetPadding(FMargin(0, 4.f, 0, 0)); }
	UTextBlock* Desc = WidgetTree->ConstructWidget<UTextBlock>();
	Desc->SetText(FText::FromString(TEXT("Jump back into your last session, or choose a new track to take on.")));
	Desc->SetFont(Body(15));
	Desc->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	Desc->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Head->AddChildToVerticalBox(Desc)) { DS->SetPadding(FMargin(0, 9.f, 0, 0)); }
	if (UVerticalBoxSlot* HeadSlot = Content->AddChildToVerticalBox(Head)) { HeadSlot->SetPadding(FMargin(0, 6.f, 0, 20.f)); }

	// resume banner -- built but hidden until the resume backing lands (ticket scope).
	ResumeBanner = WidgetTree->ConstructWidget<UBorder>();
	ResumeBanner->SetBrush(RoundedBrush(Color::GlassFill(), Radius::Lg, Color::AccentLine(), 1.f));
	ResumeBanner->SetPadding(FMargin(22.f, 20.f));
	{
		UVerticalBox* RB = WidgetTree->ConstructWidget<UVerticalBox>();
		RB->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Continue last session")));
		ResumeBanner->SetContent(RB);
	}
	ResumeBanner->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* RS = Content->AddChildToVerticalBox(ResumeBanner)) { RS->SetPadding(FMargin(0, 0, 0, 26.f)); }

	// "or start a new round" divider
	UHorizontalBox* Divider = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto AddRule = [&]()
	{
		UBorder* Rule = WidgetTree->ConstructWidget<UBorder>();
		Rule->SetBrushColor(Color::Border());
		USizeBox* RuleBox = WidgetTree->ConstructWidget<USizeBox>();
		RuleBox->SetHeightOverride(1.f); RuleBox->SetContent(Rule);
		if (UHorizontalBoxSlot* RS = Divider->AddChildToHorizontalBox(RuleBox)) { RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RS->SetVerticalAlignment(VAlign_Center); }
	};
	AddRule();
	{
		UTextBlock* OrText = WidgetTree->ConstructWidget<UTextBlock>();
		OrText->SetText(FText::FromString(TEXT("OR START A NEW ROUND")));
		{ FSlateFontInfo F = Mono(11); F.LetterSpacing = 160; OrText->SetFont(F); }
		OrText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* OS = Divider->AddChildToHorizontalBox(OrText)) { OS->SetVerticalAlignment(VAlign_Center); OS->SetPadding(FMargin(16.f, 0.f)); }
	}
	AddRule();
	if (UVerticalBoxSlot* DivSlot = Content->AddChildToVerticalBox(Divider)) { DivSlot->SetPadding(FMargin(0, 0, 0, 20.f)); }

	// course grid (3-up); cards added in SetCourses
	CourseGrid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	CourseGrid->SetSlotPadding(FMargin(8.f));
	Content->AddChildToVerticalBox(CourseGrid);

	// fixed-width left-aligned column (~1180), matching the design stage.
	USizeBox* ColBox = WidgetTree->ConstructWidget<USizeBox>();
	ColBox->SetWidthOverride(1180.f);
	ColBox->SetContent(Content);
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>();
	Pad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	Pad->SetPadding(FMargin(40.f, 4.f, 40.f, 20.f));
	Pad->SetHorizontalAlignment(HAlign_Left);
	Pad->SetVerticalAlignment(VAlign_Top);
	Pad->SetContent(ColBox);
	return Pad;
}

namespace
{
	struct FGameDef { ERoundGameType Game; const TCHAR* Name; const TCHAR* Desc; };
	const FGameDef GameDefs[] = {
		{ ERoundGameType::Stroke,     TEXT("Stroke Play"), TEXT("Total strokes count — lowest score wins.") },
		{ ERoundGameType::Stableford, TEXT("Stableford"),  TEXT("Points earned per hole vs par. Highest wins.") },
		{ ERoundGameType::Match,      TEXT("Match Play"),  TEXT("Win holes head-to-head. Most holes won.") },
		{ ERoundGameType::Skins,      TEXT("Skins"),       TEXT("Each hole is worth a skin. Win it outright.") },
	};
	struct FTurnDef { ETurnOrder Turn; const TCHAR* Name; const TCHAR* Desc; };
	const FTurnDef TurnDefs[] = {
		{ ETurnOrder::PlayItOut, TEXT("Play it out"),     TEXT("Each player finishes the entire hole before the next tees off.") },
		{ ETurnOrder::Rotate,    TEXT("Stroke by stroke"), TEXT("The group rotates every shot — the player away from the hole plays next.") },
	};
}

UWidget* URoundSetupWizard::BuildFormatStep()
{
	using namespace GolfUI;
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();

	// step head
	UVerticalBox* Head = WidgetTree->ConstructWidget<UVerticalBox>();
	Head->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Step 02 · How we're playing")));
	if (UVerticalBoxSlot* HS = Head->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Set the format"), 40))) { HS->SetPadding(FMargin(0, 4.f, 0, 0)); }
	UTextBlock* Desc = WidgetTree->ConstructWidget<UTextBlock>();
	Desc->SetText(FText::FromString(TEXT("Choose the holes, the scoring game, and whether everyone putts everything out.")));
	Desc->SetFont(Body(15));
	Desc->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	Desc->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Head->AddChildToVerticalBox(Desc)) { DS->SetPadding(FMargin(0, 9.f, 0, 0)); }
	if (UVerticalBoxSlot* HeadSlot = Content->AddChildToVerticalBox(Head)) { HeadSlot->SetPadding(FMargin(0, 6.f, 0, 8.f)); }

	// ── Holes ──
	AddSectionHeader(Content, TEXT("Holes"), TEXT("Which stretch of the course to play"));
	HolesSeg = CreateWidget<USegmentedControl>(this);
	HolesSeg->SetOptions(
		{ TEXT("Front 9"), TEXT("Back 9"), TEXT("Full 18"), TEXT("Custom") },
		{ TEXT("1–9"), TEXT("10–18"), TEXT("par 72"), TEXT("") });
	HolesSeg->SetSelectedIndex((int32)ERoundHolesMode::Full18, false);
	HolesSeg->OnChanged = [this](int32 Sel) { SetHolesMode((ERoundHolesMode)Sel); };
	if (UVerticalBoxSlot* SegSlot = Content->AddChildToVerticalBox(HolesSeg)) { SegSlot->SetHorizontalAlignment(HAlign_Left); }

	BuildHolePicker(Content);

	// ── Game type (4-up) ──
	AddSectionHeader(Content, TEXT("Game type"), TEXT("How scoring works"));
	UUniformGridPanel* GameGrid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	GameGrid->SetSlotPadding(FMargin(6.f));
	GameCards.Reset();
	for (int32 i = 0; i < UE_ARRAY_COUNT(GameDefs); ++i)
	{
		const FGameDef& G = GameDefs[i];
		UOptionCard* Card = CreateWidget<UOptionCard>(this);
		Card->Configure(G.Name, G.Desc);
		const bool bEnabled = (G.Game == ERoundGameType::Stroke);   // only Stroke live this milestone
		Card->SetDisabled(!bEnabled);
		Card->SetSelected(G.Game == RoundConfig.GameType);
		if (bEnabled)
		{
			const ERoundGameType Game = G.Game;
			Card->OnSelected = [this, Game]() { SelectGameType(Game); };
		}
		if (UUniformGridSlot* GS = GameGrid->AddChildToUniformGrid(Card, 0, i)) { GS->SetHorizontalAlignment(HAlign_Fill); GS->SetVerticalAlignment(VAlign_Fill); }
		GameCards.Add(Card);
	}
	Content->AddChildToVerticalBox(GameGrid);

	// ── Turn order (2-up, capped width) ──
	AddSectionHeader(Content, TEXT("Turn order"), TEXT("How the group works through each hole"));
	UUniformGridPanel* TurnGrid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	TurnGrid->SetSlotPadding(FMargin(6.f));
	TurnCards.Reset();
	for (int32 i = 0; i < UE_ARRAY_COUNT(TurnDefs); ++i)
	{
		const FTurnDef& Tn = TurnDefs[i];
		UOptionCard* Card = CreateWidget<UOptionCard>(this);
		Card->Configure(Tn.Name, Tn.Desc);
		const bool bEnabled = (Tn.Turn == ETurnOrder::PlayItOut);   // only Play-it-out live
		Card->SetDisabled(!bEnabled);
		Card->SetSelected(Tn.Turn == RoundConfig.TurnOrder);
		if (bEnabled)
		{
			const ETurnOrder Turn = Tn.Turn;
			Card->OnSelected = [this, Turn]() { SelectTurnOrder(Turn); };
		}
		if (UUniformGridSlot* GS = TurnGrid->AddChildToUniformGrid(Card, 0, i)) { GS->SetHorizontalAlignment(HAlign_Fill); GS->SetVerticalAlignment(VAlign_Fill); }
		TurnCards.Add(Card);
	}
	USizeBox* TurnBox = WidgetTree->ConstructWidget<USizeBox>();
	TurnBox->SetWidthOverride(720.f);
	TurnBox->SetContent(TurnGrid);
	if (UVerticalBoxSlot* TurnSlot = Content->AddChildToVerticalBox(TurnBox)) { TurnSlot->SetHorizontalAlignment(HAlign_Left); }

	// ── Hole-out rule ── ("Gimmes on" reveals a concede distance; the radius loosens the auto-hole
	// tolerance at round time -- see GolfsimRound::EffectiveGimmeRadiusFt).
	AddSectionHeader(Content, TEXT("Hole-out rule"), TEXT("Putting discipline for the group"));
	UHorizontalBox* HoleOutRow = WidgetTree->ConstructWidget<UHorizontalBox>();

	USegmentedControl* HoleOutSeg = CreateWidget<USegmentedControl>(this);
	HoleOutSeg->SetOptions({ TEXT("Everyone holes out"), TEXT("Gimmes on") });
	HoleOutSeg->SetSelectedIndex((int32)EHoleOutRule::HoleOut, false);
	HoleOutSeg->OnChanged = [this](int32 Sel)
	{
		RoundConfig.HoleOutRule = (EHoleOutRule)Sel;
		if (GimmeBlock) { GimmeBlock->SetVisibility(RoundConfig.HoleOutRule == EHoleOutRule::Gimme ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	};
	if (UHorizontalBoxSlot* HoSlot = HoleOutRow->AddChildToHorizontalBox(HoleOutSeg)) { HoSlot->SetVerticalAlignment(VAlign_Center); }

	// gimme concede-distance block (hidden unless Gimmes on)
	UHorizontalBox* Gimme = WidgetTree->ConstructWidget<UHorizontalBox>();
	UTextBlock* GimmeLbl = WidgetTree->ConstructWidget<UTextBlock>();
	GimmeLbl->SetText(FText::FromString(TEXT("Concede inside")));
	GimmeLbl->SetFont(Body(13));
	GimmeLbl->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	if (UHorizontalBoxSlot* GLS = Gimme->AddChildToHorizontalBox(GimmeLbl)) { GLS->SetVerticalAlignment(VAlign_Center); GLS->SetPadding(FMargin(0, 0, 12.f, 0)); }
	USegmentedControl* GimmeSeg = CreateWidget<USegmentedControl>(this);
	GimmeSeg->SetOptions({ TEXT("3 ft"), TEXT("5 ft"), TEXT("8 ft") });
	GimmeSeg->SetSelectedIndex(0, false);
	GimmeSeg->OnChanged = [this](int32 Sel)
	{
		static const int32 Feet[] = { 3, 5, 8 };
		RoundConfig.GimmeFeet = Feet[FMath::Clamp(Sel, 0, 2)];
	};
	if (UHorizontalBoxSlot* GSS = Gimme->AddChildToHorizontalBox(GimmeSeg)) { GSS->SetVerticalAlignment(VAlign_Center); }
	GimmeBlock = Gimme;
	GimmeBlock->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* GBSlot = HoleOutRow->AddChildToHorizontalBox(GimmeBlock)) { GBSlot->SetVerticalAlignment(VAlign_Center); GBSlot->SetPadding(FMargin(14.f, 0, 0, 0)); }

	if (UVerticalBoxSlot* HoSlot = Content->AddChildToVerticalBox(HoleOutRow)) { HoSlot->SetHorizontalAlignment(HAlign_Left); }

	// 1180-wide left-aligned column (same as the other steps)
	USizeBox* ColBox = WidgetTree->ConstructWidget<USizeBox>();
	ColBox->SetWidthOverride(1180.f);
	ColBox->SetContent(Content);
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>();
	Pad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	Pad->SetPadding(FMargin(40.f, 4.f, 40.f, 20.f));
	Pad->SetHorizontalAlignment(HAlign_Left);
	Pad->SetVerticalAlignment(VAlign_Top);
	Pad->SetContent(ColBox);
	return Pad;
}

void URoundSetupWizard::AddSectionHeader(UVerticalBox* Col, const FString& Label, const FString& InDesc)
{
	using namespace GolfUI;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
	Lbl->SetText(FText::FromString(Label));
	Lbl->SetFont(Display(17, FName(TEXT("SemiBold"))));
	Lbl->SetColorAndOpacity(FSlateColor(Color::Text()));
	if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(Lbl)) { LS->SetVerticalAlignment(VAlign_Bottom); }
	UTextBlock* D = WidgetTree->ConstructWidget<UTextBlock>();
	D->SetText(FText::FromString(InDesc));
	D->SetFont(Body(13));
	D->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	if (UHorizontalBoxSlot* DS = Row->AddChildToHorizontalBox(D)) { DS->SetVerticalAlignment(VAlign_Bottom); DS->SetPadding(FMargin(10.f, 0, 0, 0)); }
	if (UVerticalBoxSlot* RS = Col->AddChildToVerticalBox(Row)) { RS->SetPadding(FMargin(0, 22.f, 0, 12.f)); }
}

void URoundSetupWizard::BuildHolePicker(UVerticalBox* Col)
{
	using namespace GolfUI;
	// default the custom set to all 18 (matches the prototype) so switching to Custom starts full.
	RoundConfig.CustomHoles.Reset();
	for (int32 n = 1; n <= 18; ++n) { RoundConfig.CustomHoles.Add(n); }

	UVerticalBox* Picker = WidgetTree->ConstructWidget<UVerticalBox>();

	// quick buttons: Select all / Front 9 / Back 9 / Clear
	HoleQuickButtons.Reset();
	UHorizontalBox* Quick = WidgetTree->ConstructWidget<UHorizontalBox>();
	const TCHAR* QuickLabels[] = { TEXT("Select all"), TEXT("Front 9"), TEXT("Back 9"), TEXT("Clear") };
	for (const TCHAR* QLabel : QuickLabels)
	{
		UButton* B = WidgetTree->ConstructWidget<UButton>();
		StyleButton(B, Color::Surface(), Radius::Sm, Color::Border(), 1.f);
		B->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleHoleQuickClicked);
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(QLabel));
		{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 60; T->SetFont(F); }
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		B->SetContent(T);
		if (UHorizontalBoxSlot* QS = Quick->AddChildToHorizontalBox(B)) { QS->SetPadding(FMargin(0, 0, 8.f, 0)); }
		HoleQuickButtons.Add(B);
	}
	if (UVerticalBoxSlot* QRow = Picker->AddChildToVerticalBox(Quick)) { QRow->SetPadding(FMargin(0, 0, 0, 10.f)); }

	// 1-18 chip grid (9 cols x 2 rows)
	HoleChips.Reset();
	HoleChipTexts.Reset();
	UUniformGridPanel* Chips = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Chips->SetSlotPadding(FMargin(4.f));
	for (int32 i = 0; i < 18; ++i)
	{
		UButton* Chip = WidgetTree->ConstructWidget<UButton>();
		Chip->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleHoleChipClicked);
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(FString::FromInt(i + 1)));
		T->SetFont(Mono(13));
		T->SetJustification(ETextJustify::Center);
		Chip->SetContent(T);
		if (UUniformGridSlot* CS = Chips->AddChildToUniformGrid(Chip, i / 9, i % 9)) { CS->SetHorizontalAlignment(HAlign_Fill); CS->SetVerticalAlignment(VAlign_Fill); }
		HoleChips.Add(Chip);
		HoleChipTexts.Add(T);
	}
	USizeBox* ChipBox = WidgetTree->ConstructWidget<USizeBox>();
	ChipBox->SetWidthOverride(560.f);
	ChipBox->SetContent(Chips);
	Picker->AddChildToVerticalBox(ChipBox);

	CustomPicker = Picker;
	CustomPicker->SetVisibility(ESlateVisibility::Collapsed);   // shown only when Holes = Custom
	if (UVerticalBoxSlot* PS = Col->AddChildToVerticalBox(CustomPicker)) { PS->SetPadding(FMargin(0, 14.f, 0, 0)); }
	RefreshHoleChips();
}

void URoundSetupWizard::RefreshHoleChips()
{
	using namespace GolfUI;
	const TSet<int32> On(RoundConfig.CustomHoles);
	for (int32 i = 0; i < HoleChips.Num(); ++i)
	{
		const bool bOn = On.Contains(i + 1);
		if (UButton* Chip = HoleChips[i])
		{
			FButtonStyle S;
			S.SetNormal(RoundedBrush(bOn ? Color::Accent() : Color::Surface(), Radius::Sm, bOn ? Color::Accent() : Color::Border(), 1.f));
			S.SetHovered(RoundedBrush(bOn ? Color::Accent() : Color::Surface2(), Radius::Sm, bOn ? Color::Accent() : Color::BorderStrong(), 1.f));
			S.SetPressed(RoundedBrush(bOn ? Color::Accent() : Color::Surface(), Radius::Sm));
			S.SetDisabled(RoundedBrush(Color::Surface(), Radius::Sm));
			S.SetNormalPadding(FMargin(0, 8.f));
			S.SetPressedPadding(FMargin(0, 8.f));
			Chip->SetStyle(S);
		}
		if (HoleChipTexts.IsValidIndex(i) && HoleChipTexts[i])
		{
			HoleChipTexts[i]->SetColorAndOpacity(FSlateColor(bOn ? Color::AccentInk() : Color::TextDim()));
		}
	}
}

void URoundSetupWizard::SetHolesMode(ERoundHolesMode Mode)
{
	RoundConfig.HolesMode = Mode;
	if (CustomPicker) { CustomPicker->SetVisibility(Mode == ERoundHolesMode::Custom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	UpdateSummary();
	RefreshNav();
}

void URoundSetupWizard::SelectGameType(ERoundGameType Game)
{
	RoundConfig.GameType = Game;
	for (int32 i = 0; i < GameCards.Num(); ++i) { if (GameCards[i]) { GameCards[i]->SetSelected((ERoundGameType)i == Game); } }
	UpdateSummary();
}

void URoundSetupWizard::SelectTurnOrder(ETurnOrder Turn)
{
	RoundConfig.TurnOrder = Turn;
	for (int32 i = 0; i < TurnCards.Num(); ++i) { if (TurnCards[i]) { TurnCards[i]->SetSelected((ETurnOrder)i == Turn); } }
}

FString URoundSetupWizard::HolesSummaryLabel() const
{
	switch (RoundConfig.HolesMode)
	{
		case ERoundHolesMode::Front9: return TEXT("Front 9");
		case ERoundHolesMode::Back9:  return TEXT("Back 9");
		case ERoundHolesMode::Custom: return FString::Printf(TEXT("%d hole%s"), RoundConfig.CustomHoles.Num(), RoundConfig.CustomHoles.Num() == 1 ? TEXT("") : TEXT("s"));
		case ERoundHolesMode::Full18:
		default: return TEXT("Full 18");
	}
}

FString URoundSetupWizard::GameSummaryLabel() const
{
	for (const FGameDef& G : GameDefs) { if (G.Game == RoundConfig.GameType) { return G.Name; } }
	return TEXT("Stroke Play");
}

void URoundSetupWizard::HandleHoleChipClicked()
{
	for (int32 i = 0; i < HoleChips.Num(); ++i)
	{
		if (HoleChips[i] && HoleChips[i]->IsHovered())
		{
			const int32 Ref = i + 1;
			if (RoundConfig.CustomHoles.Contains(Ref)) { RoundConfig.CustomHoles.Remove(Ref); }
			else { RoundConfig.CustomHoles.Add(Ref); }
			RefreshHoleChips();
			UpdateSummary();
			RefreshNav();
			return;
		}
	}
}

void URoundSetupWizard::HandleHoleQuickClicked()
{
	for (int32 q = 0; q < HoleQuickButtons.Num(); ++q)
	{
		if (HoleQuickButtons[q] && HoleQuickButtons[q]->IsHovered())
		{
			RoundConfig.CustomHoles.Reset();
			if (q == 0) { for (int32 n = 1; n <= 18; ++n) { RoundConfig.CustomHoles.Add(n); } }       // Select all
			else if (q == 1) { for (int32 n = 1; n <= 9; ++n) { RoundConfig.CustomHoles.Add(n); } }    // Front 9
			else if (q == 2) { for (int32 n = 10; n <= 18; ++n) { RoundConfig.CustomHoles.Add(n); } }  // Back 9
			// q == 3 Clear -> leave empty
			RefreshHoleChips();
			UpdateSummary();
			RefreshNav();
			return;
		}
	}
}

void URoundSetupWizard::HandlePlayerNameChanged(const FText& Text)
{
	if (RoundConfig.Players.Num() == 0) { RoundConfig.Players.AddDefaulted(); }
	RoundConfig.Players[0].Name = Text.ToString();
	RefreshPlayerAvatar();
}

void URoundSetupWizard::HandleTeeClicked()
{
	if (RoundConfig.Players.Num() == 0) { RoundConfig.Players.AddDefaulted(); }
	for (int32 i = 0; i < TeeButtons.Num(); ++i)
	{
		if (TeeButtons[i] && TeeButtons[i]->IsHovered())
		{
			RoundConfig.Players[0].TeeIndex = i;
			RefreshTeeSwatches();
			return;
		}
	}
}

void URoundSetupWizard::HandleHandicapMinus()
{
	if (RoundConfig.Players.Num() == 0) { RoundConfig.Players.AddDefaulted(); }
	RoundConfig.Players[0].Handicap = FMath::Clamp(RoundConfig.Players[0].Handicap - 1, 0, 54);
	RefreshHandicapText();
}

void URoundSetupWizard::HandleHandicapPlus()
{
	if (RoundConfig.Players.Num() == 0) { RoundConfig.Players.AddDefaulted(); }
	RoundConfig.Players[0].Handicap = FMath::Clamp(RoundConfig.Players[0].Handicap + 1, 0, 54);
	RefreshHandicapText();
}

namespace
{
	// Tee swatch colours (sRGB), per setup.js TEES. Index 0=Black 1=Blue 2=White 3=Red.
	const FLinearColor TeeColors[] = {
		FLinearColor(FColor::FromHex(TEXT("1C1C1C"))),
		FLinearColor(FColor::FromHex(TEXT("2F6FD0"))),
		FLinearColor(FColor::FromHex(TEXT("E8E8E8"))),
		FLinearColor(FColor::FromHex(TEXT("D24B4B"))),
	};

	FString InitialsFromName(const FString& Name)
	{
		TArray<FString> Parts;
		Name.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(" "), true);
		FString Out;
		if (Parts.Num() > 0 && Parts[0].Len() > 0) { Out += Parts[0].Left(1); }
		if (Parts.Num() > 1 && Parts.Last().Len() > 0) { Out += Parts.Last().Left(1); }
		return Out.IsEmpty() ? TEXT("?") : Out.ToUpper();
	}
}

UWidget* URoundSetupWizard::BuildPlayersStep()
{
	using namespace GolfUI;
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();

	// step head
	UVerticalBox* Head = WidgetTree->ConstructWidget<UVerticalBox>();
	Head->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Step 03 · Who's teeing it up")));
	if (UVerticalBoxSlot* HS = Head->AddChildToVerticalBox(MakeTitle(WidgetTree, TEXT("Add your players"), 40))) { HS->SetPadding(FMargin(0, 4.f, 0, 0)); }
	UTextBlock* D = WidgetTree->ConstructWidget<UTextBlock>();
	D->SetText(FText::FromString(TEXT("Up to four. Set names, tee boxes and handicaps — then tee off.")));
	D->SetFont(Body(15));
	D->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	D->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Head->AddChildToVerticalBox(D)) { DS->SetPadding(FMargin(0, 9.f, 0, 0)); }
	if (UVerticalBoxSlot* HeadSlot = Content->AddChildToVerticalBox(Head)) { HeadSlot->SetPadding(FMargin(0, 6.f, 0, 8.f)); }

	// ── Player count (1 live; 2-4 disabled = single-player only) ──
	AddSectionHeader(Content, TEXT("Players"), TEXT("How many in the group"));
	USegmentedControl* CountSeg = CreateWidget<USegmentedControl>(this);
	CountSeg->SetOptions({ TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") });
	CountSeg->SetSelectedIndex(0, false);
	for (int32 i = 1; i <= 3; ++i) { CountSeg->SetOptionDisabled(i, true); }
	CountSeg->OnChanged = [this](int32 Sel) { RoundConfig.PlayerCount = Sel + 1; RefreshRoundSummary(); UpdateSummary(); };
	if (UVerticalBoxSlot* CSeg = Content->AddChildToVerticalBox(CountSeg)) { CSeg->SetHorizontalAlignment(HAlign_Left); CSeg->SetPadding(FMargin(0, 0, 0, 4.f)); }

	// ── body: player list (left) + round summary card (right) ── (BodyRow, not "Body", to not shadow GolfUI::Body)
	UHorizontalBox* BodyRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* BodySlot = Content->AddChildToVerticalBox(BodyRow)) { BodySlot->SetPadding(FMargin(0, 18.f, 0, 0)); }

	// --- player 1 row (single row this milestone) ---
	UBorder* Row = MakeCard(WidgetTree);
	Row->SetPadding(FMargin(16.f, 14.f));
	UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
	Row->SetContent(RowBox);

	// avatar
	USizeBox* AvBox = WidgetTree->ConstructWidget<USizeBox>();
	AvBox->SetWidthOverride(44.f); AvBox->SetHeightOverride(44.f);
	UBorder* Avatar = WidgetTree->ConstructWidget<UBorder>();
	Avatar->SetBrush(RoundedBrush(Color::Accent(), 999.f));
	Avatar->SetHorizontalAlignment(HAlign_Center);   // UBorder content defaults to Fill -> the
	Avatar->SetVerticalAlignment(VAlign_Center);      // initial would sit top-left and read dim.
	PlayerAvatarText = WidgetTree->ConstructWidget<UTextBlock>();
	PlayerAvatarText->SetText(FText::FromString(TEXT("?")));
	PlayerAvatarText->SetFont(Display(18, FName(TEXT("Bold"))));
	PlayerAvatarText->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
	PlayerAvatarText->SetJustification(ETextJustify::Center);
	Avatar->SetContent(PlayerAvatarText);
	AvBox->SetContent(Avatar);
	if (UHorizontalBoxSlot* AS = RowBox->AddChildToHorizontalBox(AvBox)) { AS->SetVerticalAlignment(VAlign_Center); AS->SetPadding(FMargin(0, 0, 14.f, 0)); }

	// name field (PLAYER 1 eyebrow + editable box)
	UVerticalBox* NameCol = WidgetTree->ConstructWidget<UVerticalBox>();
	NameCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Player 1")));
	PlayerNameBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	{
		FEditableTextBoxStyle TBStyle;
		TBStyle.SetBackgroundImageNormal(RoundedBrush(Color::Bg0(), Radius::Sm, Color::Border(), 1.f));
		TBStyle.SetBackgroundImageHovered(RoundedBrush(Color::Bg0(), Radius::Sm, Color::BorderStrong(), 1.f));
		TBStyle.SetBackgroundImageFocused(RoundedBrush(Color::Bg0(), Radius::Sm, Color::AccentLine(), 1.f));
		TBStyle.SetForegroundColor(Color::Text());
		TBStyle.SetFont(Body(15));
		TBStyle.SetPadding(FMargin(11.f, 9.f));
		PlayerNameBox->WidgetStyle = TBStyle;
	}
	PlayerNameBox->SetHintText(FText::FromString(TEXT("Name")));
	PlayerNameBox->OnTextChanged.AddDynamic(this, &URoundSetupWizard::HandlePlayerNameChanged);
	if (UVerticalBoxSlot* NBS = NameCol->AddChildToVerticalBox(PlayerNameBox)) { NBS->SetPadding(FMargin(0, 5.f, 0, 0)); }
	if (UHorizontalBoxSlot* NCS = RowBox->AddChildToHorizontalBox(NameCol)) { NCS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NCS->SetVerticalAlignment(VAlign_Center); }

	// tee box swatches
	UVerticalBox* TeeCol = WidgetTree->ConstructWidget<UVerticalBox>();
	TeeCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Tee box")));
	UHorizontalBox* TeeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TeeButtons.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		USizeBox* SwBox = WidgetTree->ConstructWidget<USizeBox>();
		SwBox->SetWidthOverride(34.f); SwBox->SetHeightOverride(30.f);
		UButton* Sw = WidgetTree->ConstructWidget<UButton>();
		Sw->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleTeeClicked);
		SwBox->SetContent(Sw);
		if (UHorizontalBoxSlot* SwS = TeeRow->AddChildToHorizontalBox(SwBox)) { SwS->SetPadding(FMargin(0, 0, 4.f, 0)); }
		TeeButtons.Add(Sw);
	}
	if (UVerticalBoxSlot* TRS = TeeCol->AddChildToVerticalBox(TeeRow)) { TRS->SetPadding(FMargin(0, 5.f, 0, 0)); }
	if (UHorizontalBoxSlot* TCS = RowBox->AddChildToHorizontalBox(TeeCol)) { TCS->SetVerticalAlignment(VAlign_Center); TCS->SetPadding(FMargin(14.f, 0, 0, 0)); }

	// handicap stepper
	UVerticalBox* HcpCol = WidgetTree->ConstructWidget<UVerticalBox>();
	HcpCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Handicap")));
	UHorizontalBox* HcpRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakeStepBtn = [&](const TCHAR* Glyph, bool bMinus)
	{
		USizeBox* BBox = WidgetTree->ConstructWidget<USizeBox>();
		BBox->SetWidthOverride(30.f); BBox->SetHeightOverride(30.f);
		UButton* B = WidgetTree->ConstructWidget<UButton>();
		{
			// Tight icon button: zero padding so the glyph centers in the 30x30 box (StyleButton's
			// default 18px label padding is wider than the button and shoves the glyph off-center).
			FButtonStyle S;
			S.SetNormal(RoundedBrush(Color::Surface(), Radius::Sm, Color::Border(), 1.f));
			S.SetHovered(RoundedBrush(Color::Surface2(), Radius::Sm, Color::BorderStrong(), 1.f));
			S.SetPressed(RoundedBrush(Color::Surface(), Radius::Sm, Color::Border(), 1.f));
			S.SetDisabled(RoundedBrush(Color::Surface(), Radius::Sm));
			S.SetNormalPadding(FMargin(0.f)); S.SetPressedPadding(FMargin(0.f));
			B->SetStyle(S);
		}
		if (bMinus) { B->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleHandicapMinus); }
		else        { B->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleHandicapPlus); }
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Glyph));
		T->SetFont(Mono(15));
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		BBox->SetContent(B);
		return BBox;
	};
	if (UHorizontalBoxSlot* MS = HcpRow->AddChildToHorizontalBox(MakeStepBtn(TEXT("−"), true))) { MS->SetVerticalAlignment(VAlign_Center); }
	HandicapText = WidgetTree->ConstructWidget<UTextBlock>();
	HandicapText->SetText(FText::FromString(TEXT("0")));
	HandicapText->SetFont(Mono(16));
	HandicapText->SetColorAndOpacity(FSlateColor(Color::Text()));
	HandicapText->SetJustification(ETextJustify::Center);
	if (UHorizontalBoxSlot* HVS = HcpRow->AddChildToHorizontalBox(HandicapText)) { HVS->SetVerticalAlignment(VAlign_Center); HVS->SetPadding(FMargin(10.f, 0)); }
	if (UHorizontalBoxSlot* PS = HcpRow->AddChildToHorizontalBox(MakeStepBtn(TEXT("+"), false))) { PS->SetVerticalAlignment(VAlign_Center); }
	if (UVerticalBoxSlot* HRS = HcpCol->AddChildToVerticalBox(HcpRow)) { HRS->SetPadding(FMargin(0, 5.f, 0, 0)); }
	if (UHorizontalBoxSlot* HCS = RowBox->AddChildToHorizontalBox(HcpCol)) { HCS->SetVerticalAlignment(VAlign_Center); HCS->SetPadding(FMargin(20.f, 0, 0, 0)); }

	// player-list column wraps the single row
	UVerticalBox* PlayerList = WidgetTree->ConstructWidget<UVerticalBox>();
	PlayerList->AddChildToVerticalBox(Row);
	if (UHorizontalBoxSlot* PLS = BodyRow->AddChildToHorizontalBox(PlayerList)) { PLS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PLS->SetVerticalAlignment(VAlign_Top); }

	// --- round summary card (right) ---
	USizeBox* SummaryBox = WidgetTree->ConstructWidget<USizeBox>();
	SummaryBox->SetWidthOverride(320.f);
	UBorder* SummaryCard = MakeGlassPanel(WidgetTree);
	UVerticalBox* SummaryCol = WidgetTree->ConstructWidget<UVerticalBox>();
	SummaryCard->SetContent(SummaryCol);
	{
		UBorder* HeadPad = WidgetTree->ConstructWidget<UBorder>();
		HeadPad->SetBrushColor(FLinearColor(0, 0, 0, 0));
		HeadPad->SetPadding(FMargin(18.f, 16.f, 18.f, 14.f));
		UVerticalBox* HeadCol = WidgetTree->ConstructWidget<UVerticalBox>();
		HeadCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Round summary")));
		SummaryCourseName = WidgetTree->ConstructWidget<UTextBlock>();
		SummaryCourseName->SetFont(Display(22, FName(TEXT("Bold"))));
		SummaryCourseName->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UVerticalBoxSlot* SNS = HeadCol->AddChildToVerticalBox(SummaryCourseName)) { SNS->SetPadding(FMargin(0, 6.f, 0, 0)); }
		SummaryCourseLoc = WidgetTree->ConstructWidget<UTextBlock>();
		SummaryCourseLoc->SetFont(Body(12));
		SummaryCourseLoc->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UVerticalBoxSlot* SLS = HeadCol->AddChildToVerticalBox(SummaryCourseLoc)) { SLS->SetPadding(FMargin(0, 3.f, 0, 0)); }
		HeadPad->SetContent(HeadCol);
		SummaryCol->AddChildToVerticalBox(HeadPad);
	}
	auto AddSummaryRow = [&](const FString& Key) -> UTextBlock*
	{
		UHorizontalBox* SRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* K = WidgetTree->ConstructWidget<UTextBlock>();
		K->SetText(FText::FromString(Key.ToUpper()));
		{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 120; K->SetFont(F); }
		K->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* KS = SRow->AddChildToHorizontalBox(K)) { KS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); KS->SetVerticalAlignment(VAlign_Center); }
		UTextBlock* Val = WidgetTree->ConstructWidget<UTextBlock>();
		Val->SetFont(Display(15, FName(TEXT("SemiBold"))));
		Val->SetColorAndOpacity(FSlateColor(Color::Text()));
		Val->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* VS = SRow->AddChildToHorizontalBox(Val)) { VS->SetVerticalAlignment(VAlign_Center); }
		UBorder* RowPad = WidgetTree->ConstructWidget<UBorder>();
		RowPad->SetBrushColor(FLinearColor(0, 0, 0, 0));
		RowPad->SetPadding(FMargin(18.f, 9.f));
		RowPad->SetContent(SRow);
		SummaryCol->AddChildToVerticalBox(RowPad);
		return Val;
	};
	SummaryHolesVal   = AddSummaryRow(TEXT("Holes"));
	SummaryGameVal    = AddSummaryRow(TEXT("Game"));
	SummaryTurnVal    = AddSummaryRow(TEXT("Turn order"));
	SummaryHoleOutVal = AddSummaryRow(TEXT("Hole-out"));
	SummaryPlayersVal = AddSummaryRow(TEXT("Players"));
	SummaryBox->SetContent(SummaryCard);
	if (UHorizontalBoxSlot* SBS = BodyRow->AddChildToHorizontalBox(SummaryBox)) { SBS->SetVerticalAlignment(VAlign_Top); SBS->SetPadding(FMargin(22.f, 0, 0, 0)); }

	// seed player 0 + the row from persisted settings
	PrefillPlayer();

	// 1180-wide left-aligned column
	USizeBox* ColBox = WidgetTree->ConstructWidget<USizeBox>();
	ColBox->SetWidthOverride(1180.f);
	ColBox->SetContent(Content);
	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>();
	Pad->SetBrushColor(FLinearColor(0, 0, 0, 0));
	Pad->SetPadding(FMargin(40.f, 4.f, 40.f, 20.f));
	Pad->SetHorizontalAlignment(HAlign_Left);
	Pad->SetVerticalAlignment(VAlign_Top);
	Pad->SetContent(ColBox);
	return Pad;
}

void URoundSetupWizard::PrefillPlayer()
{
	if (RoundConfig.Players.Num() == 0) { RoundConfig.Players.AddDefaulted(); }
	FRoundPlayer& P = RoundConfig.Players[0];
	P.Name = GolfDisplay::ReadPlayerName();
	P.Handicap = GolfDisplay::ReadHandicap();
	if (PlayerNameBox) { PlayerNameBox->SetText(FText::FromString(P.Name)); }
	RefreshPlayerAvatar();
	RefreshTeeSwatches();
	RefreshHandicapText();
}

void URoundSetupWizard::RefreshPlayerAvatar()
{
	if (PlayerAvatarText && RoundConfig.Players.Num() > 0)
	{
		PlayerAvatarText->SetText(FText::FromString(InitialsFromName(RoundConfig.Players[0].Name)));
	}
}

void URoundSetupWizard::RefreshTeeSwatches()
{
	using namespace GolfUI;
	const int32 Sel = RoundConfig.Players.Num() > 0 ? RoundConfig.Players[0].TeeIndex : 1;
	for (int32 i = 0; i < TeeButtons.Num(); ++i)
	{
		if (!TeeButtons[i]) { continue; }
		const FLinearColor Col = TeeColors[FMath::Clamp(i, 0, 3)];
		const bool bSel = (i == Sel);
		FButtonStyle S;
		// selected = white 2px ring; otherwise a thin border.
		S.SetNormal(RoundedBrush(Col, Radius::Sm, bSel ? FLinearColor::White : Color::Border(), bSel ? 2.f : 1.f));
		S.SetHovered(RoundedBrush(Col, Radius::Sm, bSel ? FLinearColor::White : Color::BorderStrong(), bSel ? 2.f : 1.f));
		S.SetPressed(RoundedBrush(Col, Radius::Sm, FLinearColor::White, 2.f));
		S.SetDisabled(RoundedBrush(Col, Radius::Sm));
		S.SetNormalPadding(FMargin(0.f)); S.SetPressedPadding(FMargin(0.f));
		TeeButtons[i]->SetStyle(S);
	}
}

void URoundSetupWizard::RefreshHandicapText()
{
	if (HandicapText && RoundConfig.Players.Num() > 0)
	{
		HandicapText->SetText(FText::FromString(FString::FromInt(RoundConfig.Players[0].Handicap)));
	}
}

void URoundSetupWizard::RefreshRoundSummary()
{
	if (SummaryCourseName) { SummaryCourseName->SetText(FText::FromString(SelectedCourseName.IsEmpty() ? TEXT("—") : SelectedCourseName)); }
	if (SummaryCourseLoc)  { SummaryCourseLoc->SetText(FText::FromString(SelectedCourseLoc)); }
	if (SummaryHolesVal)   { SummaryHolesVal->SetText(FText::FromString(HolesSummaryLabel())); }
	if (SummaryGameVal)    { SummaryGameVal->SetText(FText::FromString(GameSummaryLabel())); }
	if (SummaryTurnVal)    { SummaryTurnVal->SetText(FText::FromString(RoundConfig.TurnOrder == ETurnOrder::PlayItOut ? TEXT("Play it out") : TEXT("Stroke by stroke"))); }
	if (SummaryHoleOutVal)
	{
		SummaryHoleOutVal->SetText(FText::FromString(RoundConfig.HoleOutRule == EHoleOutRule::Gimme
			? FString::Printf(TEXT("Gimmes %d ft"), RoundConfig.GimmeFeet) : TEXT("Everyone holes out")));
	}
	if (SummaryPlayersVal) { SummaryPlayersVal->SetText(FText::FromString(FString::FromInt(RoundConfig.PlayerCount))); }
}

void URoundSetupWizard::BuildFooter(UHorizontalBox* Footer)
{
	using namespace GolfUI;

	FootSummary = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UHorizontalBoxSlot* SS = Footer->AddChildToHorizontalBox(FootSummary)) { SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SS->SetVerticalAlignment(VAlign_Center); }

	BackBtn = MakeGhostButton(WidgetTree, TEXT("Back"));
	BackBtn->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleBackClicked);
	if (UHorizontalBoxSlot* BS = Footer->AddChildToHorizontalBox(BackBtn)) { BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(0, 0, 10.f, 0)); }

	NextBtn = WidgetTree->ConstructWidget<UButton>();
	StyleButton(NextBtn, Color::Accent());
	NextLabel = WidgetTree->ConstructWidget<UTextBlock>();
	NextLabel->SetText(FText::FromString(TEXT("Continue")));
	{ FSlateFontInfo F = Display(15, FName(TEXT("Bold"))); F.LetterSpacing = 60; NextLabel->SetFont(F); }
	NextLabel->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
	NextLabel->SetJustification(ETextJustify::Center);
	NextBtn->SetContent(NextLabel);
	NextBtn->OnClicked.AddDynamic(this, &URoundSetupWizard::HandleNextClicked);
	if (UHorizontalBoxSlot* NS = Footer->AddChildToHorizontalBox(NextBtn)) { NS->SetVerticalAlignment(VAlign_Center); }
}

// ───────────────────────── data / state ─────────────────────────
void URoundSetupWizard::SetCourses(const TArray<FGolfCourseInfo>& Courses)
{
	if (!CourseGrid) { return; }
	CourseGrid->ClearChildren();
	Cards.Reset();

	for (int32 i = 0; i < Courses.Num(); ++i)
	{
		const FGolfCourseInfo& Info = Courses[i];
		UCourseCard* Card = CreateWidget<UCourseCard>(this);
		if (!Card) { continue; }
		Card->Configure(Info);
		if (Info.bAvailable)
		{
			const FString Id = Info.Id;
			const FString Name = Info.Name;
			const FString Loc = Info.Location;
			Card->OnSelected = [this, Id, Name, Loc]()
			{
				SelectedCourseName = Name;
				SelectedCourseLoc = Loc;
				HandleCardSelected(Id);
			};
		}
		if (UUniformGridSlot* GridSlot = CourseGrid->AddChildToUniformGrid(Card, i / 3, i % 3))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
		Cards.Add(Card);
	}
}

void URoundSetupWizard::SetResumeVisible(bool bVisible)
{
	if (ResumeBanner) { ResumeBanner->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void URoundSetupWizard::ResetToFirstStep()
{
	SelectedCourseId.Empty();
	SelectedCourseName.Empty();
	SelectedCourseLoc.Empty();
	for (UCourseCard* Card : Cards) { if (Card) { Card->SetSelected(false); } }
	PrefillPlayer();   // re-read name/handicap in case they changed since last open
	ShowStep(1);
}

void URoundSetupWizard::HandleCardSelected(const FString& CourseId)
{
	SelectedCourseId = CourseId;
	for (UCourseCard* Card : Cards)
	{
		if (Card) { Card->SetSelected(Card->GetCourseId() == CourseId); }
	}
	RefreshNav();
	UpdateSummary();
}

void URoundSetupWizard::ShowStep(int32 Step)
{
	CurrentStep = FMath::Clamp(Step, 1, 3);
	if (ContentSwitcher) { ContentSwitcher->SetActiveWidgetIndex(CurrentStep - 1); }
	RefreshStepper();
	RefreshNav();
	UpdateSummary();
	RefreshRoundSummary();
}

void URoundSetupWizard::RefreshStepper()
{
	using namespace GolfUI;
	const bool bHaveCourse = !SelectedCourseId.IsEmpty();
	for (int32 i = 0; i < StepPills.Num(); ++i)
	{
		const int32 StepNum = i + 1;
		const bool bActive = (StepNum == CurrentStep);
		const bool bDone = (StepNum < CurrentStep);
		const bool bAccessible = (StepNum == 1) || bHaveCourse;   // steps 2/3 unlock once a course is picked
		const bool bLocked = !bActive && !bDone && !bAccessible;

		const bool bGreen = bActive || bDone;   // active + completed steps read "green"

		// Paint the pill via its own button style -- the same mechanism the SettingsMenu rail uses.
		if (UButton* Pill = StepPills[i])
		{
			FButtonStyle S;
			if (bActive)
			{
				S.SetNormal(RoundedBrush(Color::AccentSoft(), 999.f, Color::Accent(), 1.5f));
				S.SetHovered(RoundedBrush(Color::AccentSoft(), 999.f, Color::Accent(), 1.5f));
				S.SetPressed(RoundedBrush(Color::AccentSoft(), 999.f, Color::Accent(), 1.5f));
			}
			else
			{
				S.SetNormal(RoundedBrush(Color::Surface(), 999.f, Color::Border(), 1.f));
				S.SetHovered(RoundedBrush(Color::Surface2(), 999.f, Color::BorderStrong(), 1.f));
				S.SetPressed(RoundedBrush(Color::Surface(), 999.f, Color::Border(), 1.f));
			}
			S.SetDisabled(RoundedBrush(Color::Surface(), 999.f, Color::Border(), 1.f));
			S.SetNormalPadding(FMargin(13.f, 8.f));
			S.SetPressedPadding(FMargin(13.f, 8.f));
			Pill->SetStyle(S);
			Pill->SetRenderOpacity(bLocked ? 0.45f : 1.f);
		}
		// Highlight the number via a bright, visible text color (NOT AccentInk, which is dark and only
		// reads on top of a green fill): active/done = fairway green, otherwise dim.
		if (UTextBlock* NumT = StepNumTexts[i])
		{
			NumT->SetColorAndOpacity(FSlateColor(bGreen ? Color::Accent() : Color::TextFaint()));
		}
		if (UTextBlock* NameT = StepNameTexts[i])
		{
			FSlateFontInfo NameFont = Display(14, FName(bActive ? TEXT("Bold") : TEXT("SemiBold")));
			NameFont.LetterSpacing = 40;
			NameT->SetFont(NameFont);
			NameT->SetColorAndOpacity(FSlateColor(bActive ? Color::Text() : (bDone ? Color::TextDim() : Color::TextFaint())));
		}
	}
}

void URoundSetupWizard::RefreshNav()
{
	if (NextLabel) { NextLabel->SetText(FText::FromString(CurrentStep == 3 ? TEXT("Tee Off") : TEXT("Continue"))); }
	if (NextBtn)
	{
		const bool bCan = CanAdvance();
		NextBtn->SetIsEnabled(bCan);
		NextBtn->SetRenderOpacity(bCan ? 1.f : 0.4f);
	}
	if (BackBtn) { BackBtn->SetVisibility(CurrentStep > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void URoundSetupWizard::UpdateSummary()
{
	using namespace GolfUI;
	if (!FootSummary) { return; }
	FootSummary->ClearChildren();

	// live summary chips (left of the footer): course, then holes + game once past the Course step.
	auto AddChip = [&](const FString& Text)
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* DotBox = WidgetTree->ConstructWidget<USizeBox>();
		DotBox->SetWidthOverride(8.f); DotBox->SetHeightOverride(8.f);
		DotBox->SetContent(MakeStatusDot(WidgetTree, Color::Accent()));
		if (UHorizontalBoxSlot* DS = Chip->AddChildToHorizontalBox(DotBox)) { DS->SetVerticalAlignment(VAlign_Center); DS->SetPadding(FMargin(0, 0, 8.f, 0)); }
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		T->SetFont(Mono(12));
		T->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UHorizontalBoxSlot* TS = Chip->AddChildToHorizontalBox(T)) { TS->SetVerticalAlignment(VAlign_Center); }
		if (UHorizontalBoxSlot* CS = FootSummary->AddChildToHorizontalBox(Chip)) { CS->SetPadding(FMargin(0, 0, 18.f, 0)); }
	};

	if (!SelectedCourseName.IsEmpty()) { AddChip(SelectedCourseName); }
	if (CurrentStep >= 2)
	{
		AddChip(HolesSummaryLabel());
		AddChip(GameSummaryLabel());
	}
	if (CurrentStep >= 3)
	{
		AddChip(FString::Printf(TEXT("%d player%s"), RoundConfig.PlayerCount, RoundConfig.PlayerCount == 1 ? TEXT("") : TEXT("s")));
	}
}

bool URoundSetupWizard::CanAdvance() const
{
	if (CurrentStep == 1) { return !SelectedCourseId.IsEmpty(); }
	// Step 2: a Custom hole selection must have at least one hole.
	if (CurrentStep == 2) { return RoundConfig.HolesMode != ERoundHolesMode::Custom || RoundConfig.CustomHoles.Num() > 0; }
	return true;   // step 3 is a stub this milestone
}

void URoundSetupWizard::GoNext()
{
	if (!CanAdvance()) { return; }
	if (CurrentStep < 3) { ShowStep(CurrentStep + 1); }
	else if (OnTeeOff) { OnTeeOff(SelectedCourseId, RoundConfig); }
}

void URoundSetupWizard::GoBack()
{
	if (CurrentStep > 1) { ShowStep(CurrentStep - 1); }
	else if (OnClose) { OnClose(); }
}

// ───────────────────────── handlers ─────────────────────────
void URoundSetupWizard::HandleNextClicked() { GoNext(); }
void URoundSetupWizard::HandleBackClicked() { GoBack(); }
void URoundSetupWizard::HandleCloseClicked() { if (OnClose) { OnClose(); } }

void URoundSetupWizard::HandleStepperClicked()
{
	const bool bHaveCourse = !SelectedCourseId.IsEmpty();
	for (int32 i = 0; i < StepPills.Num(); ++i)
	{
		if (StepPills[i] && StepPills[i]->IsHovered())
		{
			const int32 StepNum = i + 1;
			const bool bAccessible = (StepNum == 1) || bHaveCourse;
			if (bAccessible) { ShowStep(StepNum); }
			return;
		}
	}
}
