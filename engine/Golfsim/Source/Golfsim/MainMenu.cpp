#include "MainMenu.h"
#include "UI/GolfUITheme.h"
#include "UI/MenuTile.h"

#include "Kismet/KismetSystemLibrary.h"   // QuitGame
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"          // ProjectVersion for the brand subtitle

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UMainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);   // so NativeOnKeyDown receives 1-4 / Enter / Esc once focused
	BuildTree();
}

void UMainMenu::NativeConstruct()
{
	Super::NativeConstruct();
	UpdateClock();
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(ClockTimer, this, &UMainMenu::UpdateClock, 1.0f, true);
	}
	// No default selection -- the bento starts with nothing highlighted; 1-4 select, Enter confirms.
}

void UMainMenu::NativeDestruct()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ClockTimer);
	}
	Super::NativeDestruct();
}

UMenuTile* UMainMenu::TileForIndex(int32 Index) const
{
	switch (Index)
	{
	case 0: return HeroTile;
	case 1: return RangeTile;
	case 2: return PracticeTile;
	case 3: return SettingsTile;
	default: return nullptr;
	}
}

void UMainMenu::BuildTree()
{
	using namespace GolfUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Opaque dark stage covering the range (the "Range" tile dismisses to reveal it).
	UBorder* Stage = WidgetTree->ConstructWidget<UBorder>();
	Stage->SetBrushColor(Color::Bg0());
	{
		UCanvasPanelSlot* SS = Root->AddChildToCanvas(Stage);
		SS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		SS->SetOffsets(FMargin(0.f));
	}
	// Subtle bg ambiance (GOL-150): two soft radial gradients behind the content, per theme.css .stage
	// (bg-glow top-right + accent-soft bottom-left). HitTestInvisible so they never eat clicks. Added
	// after the Stage + before the content Col, so they sit between the flat stage and the bento.
	auto AddRadial = [&](const FLinearColor& Inner, FVector2D Center, float Radius)
	{
		FLinearColor Outer = Inner; Outer.A = 0.f;
		UImage* G = MakeRadialGradient(WidgetTree, Inner, Outer, Center, Radius);
		G->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* GS = Root->AddChildToCanvas(G))
		{
			GS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			GS->SetOffsets(FMargin(0.f));
		}
	};
	{
		FLinearColor Glow = Color::BgGlow(); Glow.A = 0.5f;
		AddRadial(Glow, FVector2D(0.85f, -0.10f), 0.55f);                 // top-right warmth
		AddRadial(Color::AccentSoft(), FVector2D(-0.05f, 1.10f), 0.45f);  // bottom-left accent
	}

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		UCanvasPanelSlot* CS = Root->AddChildToCanvas(Col);
		CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CS->SetOffsets(FMargin(40.f));
	}

	// ───────────────────────── top bar ─────────────────────────
	UHorizontalBox* TopBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* TBS = Col->AddChildToVerticalBox(TopBar)) { TBS->SetPadding(FMargin(0, 0, 0, 24.f)); }

	// brand lockup
	UHorizontalBox* Brand = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UHorizontalBoxSlot* BS = TopBar->AddChildToHorizontalBox(Brand)) { BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetVerticalAlignment(VAlign_Center); }
	{
		USizeBox* Mark = WidgetTree->ConstructWidget<USizeBox>();
		Mark->SetWidthOverride(42.f); Mark->SetHeightOverride(42.f);
		UBorder* MarkBg = WidgetTree->ConstructWidget<UBorder>();
		MarkBg->SetBrush(RoundedBrush(Color::Accent(), 11.f));
		MarkBg->SetHorizontalAlignment(HAlign_Center);
		MarkBg->SetVerticalAlignment(VAlign_Center);
		MarkBg->SetContent(MakeIcon(WidgetTree, EIcon::FlagTriangleRight, 22, Color::AccentInk()));   // GOL-151 brand mark
		Mark->SetContent(MarkBg);
		if (UHorizontalBoxSlot* MS = Brand->AddChildToHorizontalBox(Mark)) { MS->SetPadding(FMargin(0, 0, 14.f, 0)); MS->SetVerticalAlignment(VAlign_Center); }

		UVerticalBox* BrandText = WidgetTree->ConstructWidget<UVerticalBox>();
		if (UHorizontalBoxSlot* BTS = Brand->AddChildToHorizontalBox(BrandText)) { BTS->SetVerticalAlignment(VAlign_Center); }
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>();
		Name->SetText(FText::FromString(TEXT("GOLFFORGE")));
		{ FSlateFontInfo F = Display(22, FName(TEXT("Bold"))); F.LetterSpacing = 60; Name->SetFont(F); }
		Name->SetColorAndOpacity(FSlateColor(Color::Text()));
		BrandText->AddChildToVerticalBox(Name);
		// Brand subtitle pulls the real ProjectVersion (DefaultGame.ini, bumped per cook) instead of a
		// hardcoded mock version. Falls back to no version string if unset.
		FString Version;
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
		const FString BrandSub = Version.IsEmpty()
			? FString(TEXT("Golf Simulator"))
			: FString::Printf(TEXT("Golf Simulator · v%s"), *Version);
		UTextBlock* Sub = MakeEyebrow(WidgetTree, BrandSub);
		if (UVerticalBoxSlot* SubS = BrandText->AddChildToVerticalBox(Sub)) { SubS->SetPadding(FMargin(0, 4.f, 0, 0)); }
	}

	// environment cluster (right)
	UHorizontalBox* Env = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UHorizontalBoxSlot* ES = TopBar->AddChildToHorizontalBox(Env)) { ES->SetHorizontalAlignment(HAlign_Right); ES->SetVerticalAlignment(VAlign_Center); }

	auto AddEnvCard = [&](EIcon Glyph, const FString& Label, const FString& Value) -> UTextBlock*
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(RoundedBrush(Color::Surface(), Radius::Md, Color::Border(), 1.f));
		Card->SetPadding(FMargin(14.f, 9.f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Card->SetContent(Row);
		if (UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(MakeIcon(WidgetTree, Glyph, 16, Color::TextDim())))
		{ IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0, 0, 9.f, 0)); }
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		Row->AddChildToHorizontalBox(V);
		V->AddChildToVerticalBox(MakeEyebrow(WidgetTree, Label));
		UTextBlock* Val = MakeMonoNumber(WidgetTree, Value, 14, Color::Text());
		V->AddChildToVerticalBox(Val);
		if (UHorizontalBoxSlot* CS = Env->AddChildToHorizontalBox(Card)) { CS->SetPadding(FMargin(0, 0, 14.f, 0)); CS->SetVerticalAlignment(VAlign_Center); }
		return Val;
	};
	ClockText = AddEnvCard(EIcon::Clock, TEXT("Local"), TEXT("--:--"));
	AddEnvCard(EIcon::Cloud, TEXT("Monterey"), TEXT("62°F · 6mph"));   // TODO(GOL-144): real weather (temp/wind not modeled yet)

	// player chip
	{
		UBorder* Chip = WidgetTree->ConstructWidget<UBorder>();
		Chip->SetBrush(RoundedBrush(Color::Surface2(), 999.f, Color::Border(), 1.f));
		Chip->SetPadding(FMargin(8.f, 7.f, 14.f, 7.f));
		UHorizontalBox* ChipRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		Chip->SetContent(ChipRow);

		USizeBox* AvBox = WidgetTree->ConstructWidget<USizeBox>();
		AvBox->SetWidthOverride(34.f); AvBox->SetHeightOverride(34.f);
		UBorder* Av = WidgetTree->ConstructWidget<UBorder>();
		Av->SetBrush(RoundedBrush(Color::Accent(), 999.f));
		Av->SetVerticalAlignment(VAlign_Center); Av->SetHorizontalAlignment(HAlign_Center);
		AvatarText = WidgetTree->ConstructWidget<UTextBlock>();
		AvatarText->SetText(FText::FromString(TEXT("")));
		AvatarText->SetFont(Display(15, FName(TEXT("Bold"))));
		AvatarText->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
		Av->SetContent(AvatarText);
		AvBox->SetContent(Av);
		if (UHorizontalBoxSlot* AS = ChipRow->AddChildToHorizontalBox(AvBox)) { AS->SetPadding(FMargin(0, 0, 11.f, 0)); AS->SetVerticalAlignment(VAlign_Center); }

		UVerticalBox* ChipText = WidgetTree->ConstructWidget<UVerticalBox>();
		if (UHorizontalBoxSlot* CTS = ChipRow->AddChildToHorizontalBox(ChipText)) { CTS->SetVerticalAlignment(VAlign_Center); }
		PlayerNameText = WidgetTree->ConstructWidget<UTextBlock>();
		PlayerNameText->SetText(FText::FromString(TEXT("Player")));
		{ FSlateFontInfo F = Display(15, FName(TEXT("SemiBold"))); F.LetterSpacing = 40; PlayerNameText->SetFont(F); }
		PlayerNameText->SetColorAndOpacity(FSlateColor(Color::Text()));
		ChipText->AddChildToVerticalBox(PlayerNameText);
		UTextBlock* Hcp = MakeMonoNumber(WidgetTree, TEXT("HCP 8.4"), 11, Color::Accent());   // TODO(GOL-143): real handicap
		if (UVerticalBoxSlot* HS = ChipText->AddChildToVerticalBox(Hcp)) { HS->SetPadding(FMargin(0, 3.f, 0, 0)); }

		if (UHorizontalBoxSlot* ChS = Env->AddChildToHorizontalBox(Chip)) { ChS->SetVerticalAlignment(VAlign_Center); ChS->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f)); }
	}

	// ───────────────────────── bento ─────────────────────────
	// Proportional canvas anchors (the UMG analogue of CSS fr columns: anchored slots fill their rect
	// regardless of content, and fractions => responsive across resolutions). Columns 1.45/1/1 ->
	// x-splits 0.420 & 0.710; rows split at 0.5. Each tile inset 8px all sides -> even 16px gaps.
	UCanvasPanel* Bento = WidgetTree->ConstructWidget<UCanvasPanel>();
	if (UVerticalBoxSlot* BES = Col->AddChildToVerticalBox(Bento)) { BES->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BES->SetHorizontalAlignment(HAlign_Fill); BES->SetVerticalAlignment(VAlign_Fill); }

	auto PlaceTile = [&](UMenuTile* Tile, float MinX, float MinY, float MaxX, float MaxY)
	{
		if (UCanvasPanelSlot* CS = Bento->AddChildToCanvas(Tile))
		{
			CS->SetAnchors(FAnchors(MinX, MinY, MaxX, MaxY));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(8.f));   // min!=max anchors -> offsets are insets (=> 16px gaps)
		}
	};

	TWeakObjectPtr<UMainMenu> WeakThis(this);

	HeroTile = CreateWidget<UMenuTile>(this);
	// Mode-honest copy: "Play Course" opens the setup wizard (course + difficulty pick). When a
	// resumable session exists (future, GOL-141/resume), the hero will instead show that course's
	// name / through-hole / score via a SetHeroCourse path + the Resume pill.
	HeroTile->Configure(TEXT("01"), TEXT("18 Holes · Par 72"), TEXT("Play Course"),
		TEXT("Pick a course, set your difficulty, and play a full round — live scoring, GPS yardages and a shot tracer."),
		TEXT("Tee off"), /*bHero*/ true);
	HeroTile->OnActivated = [WeakThis]() { if (UMainMenu* M = WeakThis.Get()) { if (M->OnPlayCourse) M->OnPlayCourse(); } };
	PlaceTile(HeroTile, 0.f, 0.f, 0.420f, 1.f);

	RangeTile = CreateWidget<UMenuTile>(this);
	RangeTile->Configure(TEXT("02"), TEXT("Warm-up"), TEXT("Range"),
		TEXT("Open driving range — dial in carry numbers and ball flight, no pressure."), TEXT("Open range"));
	RangeTile->OnActivated = [WeakThis]() { if (UMainMenu* M = WeakThis.Get()) { if (M->OnPlayRange) M->OnPlayRange(); } };
	PlaceTile(RangeTile, 0.420f, 0.f, 1.f, 0.5f);

	PracticeTile = CreateWidget<UMenuTile>(this);
	PracticeTile->Configure(TEXT("03"), TEXT("Drills"), TEXT("Practice"),
		TEXT("Closest-to-pin & target drills — pick a mode and play."), TEXT("Start drills"));
	PracticeTile->OnActivated = [WeakThis]() { if (UMainMenu* M = WeakThis.Get()) { if (M->OnPlayPractice) M->OnPlayPractice(); } };
	PlaceTile(PracticeTile, 0.420f, 0.5f, 0.710f, 1.f);

	SettingsTile = CreateWidget<UMenuTile>(this);
	SettingsTile->Configure(TEXT("04"), TEXT("System"), TEXT("Settings"),
		TEXT("Graphics, audio & launch monitor."), TEXT("Configure"));
	SettingsTile->OnActivated = [WeakThis]() { if (UMainMenu* M = WeakThis.Get()) { if (M->OnSettings) M->OnSettings(); } };
	PlaceTile(SettingsTile, 0.710f, 0.5f, 1.f, 1.f);

	// ───────────────────────── footer ─────────────────────────
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* FS = Col->AddChildToVerticalBox(Footer)) { FS->SetPadding(FMargin(0, 24.f, 0, 0)); }

	UHorizontalBox* Legend = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UHorizontalBoxSlot* LS = Footer->AddChildToHorizontalBox(Legend)) { LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center); }
	auto AddKbd = [&](const FString& Key) { if (UHorizontalBoxSlot* KS = Legend->AddChildToHorizontalBox(MakeKbd(WidgetTree, Key))) { KS->SetPadding(FMargin(0, 0, 4.f, 0)); KS->SetVerticalAlignment(VAlign_Center); } };
	auto AddKbdIcon = [&](EIcon Glyph) { if (UHorizontalBoxSlot* KS = Legend->AddChildToHorizontalBox(MakeKbd(WidgetTree, Glyph))) { KS->SetPadding(FMargin(0, 0, 4.f, 0)); KS->SetVerticalAlignment(VAlign_Center); } };
	auto AddLabel = [&](const FString& Txt)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Txt));
		T->SetFont(Mono(11));
		T->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* TS = Legend->AddChildToHorizontalBox(T)) { TS->SetPadding(FMargin(2.f, 0, 18.f, 0)); TS->SetVerticalAlignment(VAlign_Center); }
	};
	AddKbd(TEXT("1")); AddKbd(TEXT("2")); AddKbd(TEXT("3")); AddKbd(TEXT("4")); AddLabel(TEXT("Select"));
	AddKbdIcon(EIcon::CornerDownLeft); AddLabel(TEXT("Confirm"));   // GOL-151 Lucide ↵ (corner-down-left)
	AddKbd(TEXT("Esc")); AddLabel(TEXT("Quit"));

	// Previous Sessions footer link (reuses the existing callback; greyed when count == 0).
	PreviousSessionsBtn = WidgetTree->ConstructWidget<UButton>();
	{
		FButtonStyle S;
		const FSlateBrush Clear = RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Sm);
		S.SetNormal(Clear); S.SetHovered(RoundedBrush(Color::Surface(), Radius::Sm)); S.SetPressed(Clear); S.SetDisabled(Clear);
		PreviousSessionsBtn->SetStyle(S);
	}
	PreviousSessionsBtn->OnClicked.AddDynamic(this, &UMainMenu::HandlePreviousSessionsClicked);
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(TEXT("Previous Sessions")));
		T->SetFont(Mono(11));
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		PreviousSessionsBtn->SetContent(T);
	}
	if (UHorizontalBoxSlot* PSS = Legend->AddChildToHorizontalBox(PreviousSessionsBtn)) { PSS->SetVerticalAlignment(VAlign_Center); }

	// Exit (danger on hover)
	UButton* Exit = WidgetTree->ConstructWidget<UButton>();
	{
		FLinearColor DangerSoft = Color::DangerFill(); DangerSoft.A = 0.14f;
		FLinearColor DangerLine = Color::DangerFill(); DangerLine.A = 0.6f;
		FButtonStyle S;
		S.SetNormal(RoundedBrush(Color::Surface(), Radius::Md, Color::BorderStrong(), 1.f));
		S.SetHovered(RoundedBrush(DangerSoft, Radius::Md, DangerLine, 1.f));
		S.SetPressed(RoundedBrush(DangerSoft, Radius::Md, DangerLine, 1.f));
		Exit->SetStyle(S);
	}
	Exit->OnClicked.AddDynamic(this, &UMainMenu::HandleExitClicked);
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UHorizontalBoxSlot* PIS = Row->AddChildToHorizontalBox(MakeIcon(WidgetTree, EIcon::Power, 14, Color::TextDim())))
		{ PIS->SetVerticalAlignment(VAlign_Center); PIS->SetPadding(FMargin(0, 0, 7.f, 0)); }   // GOL-151 power icon
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(TEXT("EXIT")));
		{ FSlateFontInfo F = Display(15, FName(TEXT("SemiBold"))); F.LetterSpacing = 80; T->SetFont(F); }
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UHorizontalBoxSlot* TS = Row->AddChildToHorizontalBox(T)) { TS->SetVerticalAlignment(VAlign_Center); }
		Exit->SetContent(Row);
	}
	if (UHorizontalBoxSlot* XS = Footer->AddChildToHorizontalBox(Exit)) { XS->SetHorizontalAlignment(HAlign_Right); XS->SetVerticalAlignment(VAlign_Center); }
}

void UMainMenu::SetSelectedTile(int32 Index)
{
	UMenuTile* T = TileForIndex(Index);
	if (!T || T->IsDisabled()) { return; }
	SelectedIndex = Index;
	for (int32 i = 0; i < 4; ++i)
	{
		if (UMenuTile* Tile = TileForIndex(i)) { Tile->SetSelected(i == Index); }
	}
}

void UMainMenu::UpdateClock()
{
	if (!ClockText) { return; }
	const FDateTime Now = FDateTime::Now();
	int32 H = Now.GetHour();
	const TCHAR* AP = H >= 12 ? TEXT("PM") : TEXT("AM");
	H = H % 12; if (H == 0) { H = 12; }
	ClockText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d %s"), H, Now.GetMinute(), AP)));
}

FReply UMainMenu::NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent)
{
	const FKey K = KeyEvent.GetKey();
	if (K == EKeys::Escape) { Quit(); return FReply::Handled(); }
	if (K == EKeys::Enter)
	{
		if (UMenuTile* T = TileForIndex(SelectedIndex)) { T->Activate(); }
		return FReply::Handled();
	}
	int32 Idx = -1;
	if (K == EKeys::One) { Idx = 0; }
	else if (K == EKeys::Two) { Idx = 1; }
	else if (K == EKeys::Three) { Idx = 2; }
	else if (K == EKeys::Four) { Idx = 3; }
	if (Idx >= 0) { SetSelectedTile(Idx); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geo, KeyEvent);
}

void UMainMenu::SetPreviousSessionsCount(int32 Count)
{
	if (PreviousSessionsBtn) { PreviousSessionsBtn->SetIsEnabled(Count > 0); }
}

void UMainMenu::SetPlayerName(const FString& Name)
{
	if (PlayerNameText) { PlayerNameText->SetText(FText::FromString(Name)); }
	if (AvatarText)
	{
		// initials: first letter of up to two words, else first 1-2 chars.
		FString Initials;
		TArray<FString> Parts;
		Name.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 2) { Initials = Parts[0].Left(1) + Parts.Last().Left(1); }
		else if (Name.Len() > 0) { Initials = Name.Left(2); }
		AvatarText->SetText(FText::FromString(Initials.ToUpper()));
	}
}

void UMainMenu::Quit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenu::HandlePreviousSessionsClicked() { if (OnPreviousSessions) { OnPreviousSessions(); } }
void UMainMenu::HandleExitClicked()             { Quit(); }
