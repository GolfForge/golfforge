#include "UI/RoundSetupWizard.h"
#include "UI/GolfUITheme.h"
#include "UI/CourseCard.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
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
	ContentSwitcher->AddChild(BuildStubStep(TEXT("Step 02 · How we're playing"), TEXT("Set the format"),
		TEXT("Choose the holes, the scoring game, and whether everyone putts everything out."),
		TEXT("Coming soon — GOL-142")));
	ContentSwitcher->AddChild(BuildStubStep(TEXT("Step 03 · Who's teeing it up"), TEXT("Add your players"),
		TEXT("Up to four. Set names, tee boxes and handicaps — then tee off."),
		TEXT("Coming soon — GOL-143")));

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

UWidget* URoundSetupWizard::BuildStubStep(const FString& Eyebrow, const FString& Title, const FString& Desc, const FString& Soon)
{
	using namespace GolfUI;
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();

	UVerticalBox* Head = WidgetTree->ConstructWidget<UVerticalBox>();
	Head->AddChildToVerticalBox(MakeEyebrow(WidgetTree, Eyebrow));
	if (UVerticalBoxSlot* HS = Head->AddChildToVerticalBox(MakeTitle(WidgetTree, Title, 40))) { HS->SetPadding(FMargin(0, 4.f, 0, 0)); }
	UTextBlock* D = WidgetTree->ConstructWidget<UTextBlock>();
	D->SetText(FText::FromString(Desc));
	D->SetFont(Body(15));
	D->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	D->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DS = Head->AddChildToVerticalBox(D)) { DS->SetPadding(FMargin(0, 9.f, 0, 0)); }
	if (UVerticalBoxSlot* HeadSlot = Content->AddChildToVerticalBox(Head)) { HeadSlot->SetPadding(FMargin(0, 6.f, 0, 24.f)); }

	UBorder* Note = WidgetTree->ConstructWidget<UBorder>();
	Note->SetBrush(RoundedBrush(Color::Surface(), Radius::Lg, Color::Border(), 1.f));
	Note->SetPadding(FMargin(28.f, 40.f));
	UTextBlock* SoonText = WidgetTree->ConstructWidget<UTextBlock>();
	SoonText->SetText(FText::FromString(Soon));
	SoonText->SetFont(Display(18, FName(TEXT("SemiBold"))));
	SoonText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	SoonText->SetJustification(ETextJustify::Center);
	Note->SetContent(SoonText);
	Content->AddChildToVerticalBox(Note);

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
			Card->OnSelected = [this, Id, Name]()
			{
				SelectedCourseName = Name;
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
	for (UCourseCard* Card : Cards) { if (Card) { Card->SetSelected(false); } }
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

	// live summary chips (left of the footer). GOL-141 shows the chosen course; later steps append more.
	if (!SelectedCourseName.IsEmpty())
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* DotBox = WidgetTree->ConstructWidget<USizeBox>();
		DotBox->SetWidthOverride(8.f); DotBox->SetHeightOverride(8.f);
		DotBox->SetContent(MakeStatusDot(WidgetTree, Color::Accent()));
		if (UHorizontalBoxSlot* DS = Chip->AddChildToHorizontalBox(DotBox)) { DS->SetVerticalAlignment(VAlign_Center); DS->SetPadding(FMargin(0, 0, 8.f, 0)); }
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(SelectedCourseName));
		T->SetFont(Mono(12));
		T->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UHorizontalBoxSlot* TS = Chip->AddChildToHorizontalBox(T)) { TS->SetVerticalAlignment(VAlign_Center); }
		FootSummary->AddChildToHorizontalBox(Chip);
	}
}

bool URoundSetupWizard::CanAdvance() const
{
	if (CurrentStep == 1) { return !SelectedCourseId.IsEmpty(); }
	return true;   // steps 2/3 are stubs this milestone
}

void URoundSetupWizard::GoNext()
{
	if (!CanAdvance()) { return; }
	if (CurrentStep < 3) { ShowStep(CurrentStep + 1); }
	else if (OnTeeOff) { OnTeeOff(SelectedCourseId); }
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
