#include "SettingsMenu.h"
#include "UI/GolfUITheme.h"
#include "UI/SegmentedControl.h"
#include "UI/ToggleSwitch.h"

#include "Kismet/KismetSystemLibrary.h"   // QuitGame

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"

void USettingsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);   // so NativeOnKeyDown (Esc) fires when the HUD gives us focus
	BuildTree();
}

FReply USettingsMenu::NativeOnKeyDown(const FGeometry& Geo, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (OnClose) { OnClose(); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geo, KeyEvent);
}

// ───────────────────────── build ─────────────────────────
void USettingsMenu::BuildTree()
{
	using namespace GolfUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Dim scrim that also closes on click-outside (the card absorbs its own clicks).
	UButton* Scrim = WidgetTree->ConstructWidget<UButton>();
	{
		FButtonStyle S;
		const FSlateBrush Dim = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		S.SetNormal(Dim); S.SetHovered(Dim); S.SetPressed(Dim); S.SetDisabled(Dim);
		Scrim->SetStyle(S);
	}
	Scrim->OnClicked.AddDynamic(this, &USettingsMenu::HandleCloseClicked);
	{
		UCanvasPanelSlot* SS = Root->AddChildToCanvas(Scrim);
		SS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		SS->SetOffsets(FMargin(0.f));
	}

	// Centered glass card (fixed size).
	USizeBox* CardBox = WidgetTree->ConstructWidget<USizeBox>();
	CardBox->SetWidthOverride(880.f);
	CardBox->SetHeightOverride(560.f);
	{
		UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardBox);
		CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CS->SetAlignment(FVector2D(0.5f, 0.5f));
		CS->SetAutoSize(true);
	}
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(RoundedBrush(Color::Bg1(), Radius::Lg, Color::BorderStrong(), 1.f));
	Card->SetPadding(FMargin(0.f));
	CardBox->SetContent(Card);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// ── header: title + close-X ──
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(Header)) { HS->SetPadding(FMargin(24.f, 20.f)); }
	if (UHorizontalBoxSlot* TS = Header->AddChildToHorizontalBox(MakeTitle(WidgetTree, TEXT("Settings"), 22)))
	{
		TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	}
	{
		USizeBox* XBox = WidgetTree->ConstructWidget<USizeBox>();
		XBox->SetWidthOverride(34.f); XBox->SetHeightOverride(34.f);
		UButton* CloseX = WidgetTree->ConstructWidget<UButton>();
		{
			FButtonStyle S;
			S.SetNormal(RoundedBrush(Color::Surface(), Radius::Sm, Color::Border(), 1.f));
			S.SetHovered(RoundedBrush(Color::Surface2(), Radius::Sm, Color::AccentLine(), 1.f));
			S.SetPressed(RoundedBrush(Color::Surface(), Radius::Sm, Color::AccentLine(), 1.f));
			S.SetDisabled(RoundedBrush(Color::Surface(), Radius::Sm));
			S.SetNormalPadding(FMargin(0.f)); S.SetPressedPadding(FMargin(0.f));
			CloseX->SetStyle(S);
		}
		CloseX->OnClicked.AddDynamic(this, &USettingsMenu::HandleCloseClicked);
		UTextBlock* X = WidgetTree->ConstructWidget<UTextBlock>();
		X->SetText(FText::FromString(TEXT("X")));
		X->SetFont(Mono(13));
		X->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		X->SetJustification(ETextJustify::Center);
		CloseX->SetContent(X);
		XBox->SetContent(CloseX);
		if (UHorizontalBoxSlot* XS = Header->AddChildToHorizontalBox(XBox)) { XS->SetVerticalAlignment(VAlign_Center); }
	}

	// ── body: rail | content ──
	UHorizontalBox* BodyRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* BS = Col->AddChildToVerticalBox(BodyRow)) { BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// rail
	USizeBox* RailBox = WidgetTree->ConstructWidget<USizeBox>();
	RailBox->SetWidthOverride(208.f);
	if (UHorizontalBoxSlot* RS = BodyRow->AddChildToHorizontalBox(RailBox)) { RS->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f)); }
	UVerticalBox* Rail = WidgetTree->ConstructWidget<UVerticalBox>();
	RailBox->SetContent(Rail);
	const TCHAR* Tabs[] = { TEXT("Graphics"), TEXT("Audio"), TEXT("Gameplay"), TEXT("Controls"), TEXT("Credits") };
	for (const TCHAR* TabName : Tabs)
	{
		UButton* Tab = WidgetTree->ConstructWidget<UButton>();
		Tab->OnClicked.AddDynamic(this, &USettingsMenu::HandleRailClicked);
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(TabName));
		T->SetFont(Display(15, FName(TEXT("SemiBold"))));
		T->SetJustification(ETextJustify::Left);
		Tab->SetContent(T);
		if (UVerticalBoxSlot* TabSlot = Rail->AddChildToVerticalBox(Tab)) { TabSlot->SetPadding(FMargin(0.f, 2.f)); }
		RailButtons.Add(Tab);
	}

	// content switcher
	ContentSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UHorizontalBoxSlot* CSlot = BodyRow->AddChildToHorizontalBox(ContentSwitcher))
	{
		CSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CSlot->SetPadding(FMargin(22.f, 0.f, 24.f, 0.f));
	}
	ContentSwitcher->AddChild(BuildGraphicsTab());
	ContentSwitcher->AddChild(BuildDisabledTab({
		{ TEXT("Master volume"), TEXT("Overall output level"), TEXT("slider") },
		{ TEXT("Ambience"), TEXT("Birds, wind & crowd"), TEXT("slider") },
		{ TEXT("Impact sounds"), TEXT("Club & ball contact"), TEXT("slider") },
		{ TEXT("Caddie voice"), TEXT("Spoken yardage callouts"), TEXT("toggle") },
	}));
	ContentSwitcher->AddChild(BuildDisabledTab({
		{ TEXT("Units"), TEXT("Distance display"), TEXT("seg"), TEXT("Yards"), TEXT("Meters") },
		{ TEXT("Shot tracer"), TEXT("On-screen ball flight line"), TEXT("toggle") },
		{ TEXT("Wind helper"), TEXT("Show wind vector overlay"), TEXT("toggle") },
		{ TEXT("Auto-club select"), TEXT("Suggest club by distance"), TEXT("toggle") },
		{ TEXT("Gimme range"), TEXT("Auto-hole short putts"), TEXT("seg"), TEXT("Off"), TEXT("3ft"), TEXT("5ft") },
	}));
	ContentSwitcher->AddChild(BuildDisabledTab({
		{ TEXT("Swing input"), TEXT("How shots are triggered"), TEXT("seg"), TEXT("Mouse"), TEXT("Monitor") },
		{ TEXT("Camera sensitivity"), TEXT("Look speed"), TEXT("slider") },
		{ TEXT("Invert aim"), TEXT("Flip horizontal aiming"), TEXT("toggle") },
		{ TEXT("Haptics"), TEXT("Controller rumble on impact"), TEXT("toggle") },
	}));
	{
		UScrollBox* CreditsScroll = WidgetTree->ConstructWidget<UScrollBox>();
		CreditsBody = WidgetTree->ConstructWidget<UTextBlock>();
		CreditsBody->SetText(FText::FromString(TEXT("")));
		CreditsBody->SetFont(Body(13));
		CreditsBody->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		CreditsBody->SetAutoWrapText(true);
		CreditsScroll->AddChild(CreditsBody);
		ContentSwitcher->AddChild(CreditsScroll);
	}

	// ── footer: Apply + Main Menu + Quit ──
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* FS = Col->AddChildToVerticalBox(Footer)) { FS->SetPadding(FMargin(24.f, 16.f)); }
	{
		// Main Menu + Quit only when opened in-range/mid-round (hidden when opened from the main menu).
		MainMenuBtn = MakeGhostButton(WidgetTree, TEXT("Main Menu"));
		MainMenuBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleMainMenuClicked);
		if (UHorizontalBoxSlot* MS = Footer->AddChildToHorizontalBox(MainMenuBtn)) { MS->SetPadding(FMargin(0, 0, 8.f, 0)); MS->SetVerticalAlignment(VAlign_Center); }
		QuitBtn = MakeGhostButton(WidgetTree, TEXT("Quit"));
		QuitBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleQuitClicked);
		if (UHorizontalBoxSlot* QS = Footer->AddChildToHorizontalBox(QuitBtn)) { QS->SetVerticalAlignment(VAlign_Center); }
		// Spacer keeps Apply pinned right regardless of whether Main Menu/Quit are shown.
		if (UHorizontalBoxSlot* SpS = Footer->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())) { SpS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		UButton* ApplyBtn = MakeAccentButton(WidgetTree, TEXT("Apply"));
		ApplyBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleApplyClicked);
		if (UHorizontalBoxSlot* AS = Footer->AddChildToHorizontalBox(ApplyBtn)) { AS->SetVerticalAlignment(VAlign_Center); }
	}

	ShowSection(0);
}

UScrollBox* USettingsMenu::BuildGraphicsTab()
{
	using namespace GolfUI;
	UScrollBox* Tab = WidgetTree->ConstructWidget<UScrollBox>();

	ResCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	StyleComboBox(ResCombo);
	AddRow(Tab, TEXT("Resolution"), TEXT("Output resolution"), ResCombo, false);

	WindowSeg = CreateWidget<USegmentedControl>(this);
	WindowSeg->SetOptions({ TEXT("Windowed"), TEXT("Borderless"), TEXT("Fullscreen") });
	WindowSeg->OnChanged = [this](int32 Sel) { UpdateResolutionEnabledForMode(Sel); };
	AddRow(Tab, TEXT("Window mode"), TEXT("How the game fills the screen"), WindowSeg, false);

	QualitySeg = CreateWidget<USegmentedControl>(this);
	QualitySeg->SetOptions({ TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") });
	AddRow(Tab, TEXT("Quality"), TEXT("Overall fidelity preset"), QualitySeg, false);

	UpscalerSeg = CreateWidget<USegmentedControl>(this);   // options filled by SetUpscalerOptions
	UpscalerSeg->OnChanged = [this](int32 Sel)
	{
		if (!UpscalerOptionIndices.IsValidIndex(Sel)) { return; }
		const int32 NewUpscaler = UpscalerOptionIndices[Sel];
		const float CurPct = UpscaleModeCombo
			? GolfDisplay::ScreenPctForMode(ModeSegUpscaler, UpscaleModeCombo->GetSelectedIndex())
			: 100.f;
		RepopulateModeCombo(NewUpscaler, CurPct);   // preserve the chosen render scale across the switch
	};
	AddRow(Tab, TEXT("Upscaler"), TEXT("Temporal upscaling backend"), UpscalerSeg, false);

	// Render scale = a dropdown (5-7 tiers; too many for a segmented pill -> it overflowed the row).
	UpscaleModeCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	StyleComboBox(UpscaleModeCombo);
	RepopulateModeCombo(0, 100.f);   // TSR/Native until SetUpscalerOptions + SetCurrent run
	AddRow(Tab, TEXT("Render scale"), TEXT("Internal resolution / quality tier"), UpscaleModeCombo, false);

	return Tab;
}

UScrollBox* USettingsMenu::BuildDisabledTab(const TArray<TArray<FString>>& Rows)
{
	using namespace GolfUI;
	UScrollBox* Tab = WidgetTree->ConstructWidget<UScrollBox>();
	for (const TArray<FString>& R : Rows)
	{
		if (R.Num() < 3) { continue; }
		const FString& Label = R[0];
		const FString& Desc = R[1];
		const FString& Type = R[2];
		UWidget* Control = nullptr;
		if (Type == TEXT("seg"))
		{
			USegmentedControl* Seg = CreateWidget<USegmentedControl>(this);
			TArray<FString> Opts;
			for (int32 i = 3; i < R.Num(); ++i) { Opts.Add(R[i]); }
			Seg->SetOptions(Opts);
			Seg->SetControlEnabled(false);
			Control = Seg;
		}
		else if (Type == TEXT("toggle"))
		{
			UToggleSwitch* Tog = CreateWidget<UToggleSwitch>(this);
			Tog->SetControlEnabled(false);
			Control = Tog;
		}
		else // slider
		{
			USizeBox* SBox = WidgetTree->ConstructWidget<USizeBox>();
			SBox->SetWidthOverride(180.f);
			USlider* Sld = WidgetTree->ConstructWidget<USlider>();
			StyleSlider(Sld);
			Sld->SetValue(0.7f);
			Sld->SetIsEnabled(false);
			SBox->SetContent(Sld);
			Control = SBox;
		}
		AddRow(Tab, Label, Desc, Control, true);
	}
	return Tab;
}

void USettingsMenu::AddRow(UScrollBox* Tab, const FString& Label, const FString& Desc, UWidget* Control, bool bDisabled)
{
	using namespace GolfUI;
	if (!Tab) { return; }

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBox* LeftV = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
	Lbl->SetText(FText::FromString(Label));
	Lbl->SetFont(Body(16, FName(TEXT("SemiBold"))));
	Lbl->SetColorAndOpacity(FSlateColor(Color::Text()));
	LeftV->AddChildToVerticalBox(Lbl);
	if (!Desc.IsEmpty())
	{
		UTextBlock* D = WidgetTree->ConstructWidget<UTextBlock>();
		D->SetText(FText::FromString(bDisabled ? Desc + TEXT("  ·  Coming soon") : Desc));
		D->SetFont(Body(12));
		D->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UVerticalBoxSlot* DS = LeftV->AddChildToVerticalBox(D)) { DS->SetPadding(FMargin(0, 3.f, 0, 0)); }
	}
	if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(LeftV)) { LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center); }
	if (Control) { if (UHorizontalBoxSlot* RS = Row->AddChildToHorizontalBox(Control)) { RS->SetHorizontalAlignment(HAlign_Right); RS->SetVerticalAlignment(VAlign_Center); } }

	if (bDisabled) { Row->SetRenderOpacity(0.55f); }
	if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(Tab->AddChild(Row))) { RowSlot->SetPadding(FMargin(0.f, 15.f)); }

	// hairline separator
	UBorder* Sep = WidgetTree->ConstructWidget<UBorder>();
	Sep->SetBrushColor(Color::Border());
	USizeBox* SepBox = WidgetTree->ConstructWidget<USizeBox>();
	SepBox->SetHeightOverride(1.f);
	SepBox->SetContent(Sep);
	Tab->AddChild(SepBox);
}

void USettingsMenu::ShowSection(int32 Index)
{
	CurrentSection = Index;
	if (ContentSwitcher) { ContentSwitcher->SetActiveWidgetIndex(Index); }
	RefreshRail();
}

void USettingsMenu::RefreshRail()
{
	using namespace GolfUI;
	for (int32 i = 0; i < RailButtons.Num(); ++i)
	{
		UButton* B = RailButtons[i];
		if (!B) { continue; }
		const bool bActive = (i == CurrentSection);
		FButtonStyle S;
		if (bActive)
		{
			S.SetNormal(RoundedBrush(Color::AccentSoft(), Radius::Sm, Color::AccentLine(), 1.f));
			S.SetHovered(RoundedBrush(Color::AccentSoft(), Radius::Sm, Color::AccentLine(), 1.f));
			S.SetPressed(RoundedBrush(Color::AccentSoft(), Radius::Sm, Color::AccentLine(), 1.f));
		}
		else
		{
			S.SetNormal(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Sm));
			S.SetHovered(RoundedBrush(Color::Surface2(), Radius::Sm));
			S.SetPressed(RoundedBrush(Color::Surface(), Radius::Sm));
		}
		S.SetDisabled(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Sm));
		S.SetNormalPadding(FMargin(13.f, 11.f));
		S.SetPressedPadding(FMargin(13.f, 11.f));
		B->SetStyle(S);
		if (UTextBlock* T = Cast<UTextBlock>(B->GetChildAt(0)))
		{
			T->SetColorAndOpacity(FSlateColor(bActive ? Color::Text() : Color::TextDim()));
		}
	}
}

void USettingsMenu::HandleRailClicked()
{
	for (int32 i = 0; i < RailButtons.Num(); ++i)
	{
		if (RailButtons[i] && RailButtons[i]->IsHovered()) { ShowSection(i); return; }
	}
}

void USettingsMenu::RepopulateModeCombo(int32 UpscalerFixedIndex, float TargetPct)
{
	if (!UpscaleModeCombo) { return; }
	UpscaleModeCombo->ClearOptions();
	for (const FString& M : GolfDisplay::UpscaleModeNames(UpscalerFixedIndex)) { UpscaleModeCombo->AddOption(M); }
	UpscaleModeCombo->SetSelectedIndex(GolfDisplay::ModeForScreenPct(UpscalerFixedIndex, TargetPct));
	ModeSegUpscaler = UpscalerFixedIndex;
}

void USettingsMenu::UpdateResolutionEnabledForMode(int32 WindowModeIndex)
{
	// Borderless (1) renders at the OS desktop resolution, so the picker can't take effect -- grey it out.
	if (ResCombo) { ResCombo->SetIsEnabled(WindowModeIndex != 1); }
}

void USettingsMenu::SetResolutionOptions(const TArray<FIntPoint>& Resolutions)
{
	ResOptions = Resolutions;
	if (!ResCombo) { return; }
	ResCombo->ClearOptions();
	for (const FIntPoint& R : Resolutions)
	{
		ResCombo->AddOption(FString::Printf(TEXT("%dx%d"), R.X, R.Y));
	}
}

void USettingsMenu::SetUpscalerOptions(const TArray<int32>& Indices)
{
	UpscalerOptionIndices = Indices;
	if (!UpscalerSeg) { return; }
	TArray<FString> Names;
	for (int32 Idx : Indices) { Names.Add(GolfDisplay::UpscalerName(Idx)); }
	UpscalerSeg->SetOptions(Names);
}

void USettingsMenu::SetCreditsText(const FString& Text)
{
	if (CreditsBody) { CreditsBody->SetText(FText::FromString(Text)); }
}

void USettingsMenu::SetCurrent(const FGolfDisplaySettings& S)
{
	if (ResCombo)
	{
		const int32 Idx = ResOptions.IndexOfByPredicate([&](const FIntPoint& R){ return R == S.Resolution; });
		if (Idx != INDEX_NONE) { ResCombo->SetSelectedIndex(Idx); }
	}
	const int32 WinIdx = GolfDisplay::ClampWindowModeIndex(S.WindowModeIndex);
	if (WindowSeg)  { WindowSeg->SetSelectedIndex(WinIdx, false); }
	UpdateResolutionEnabledForMode(WinIdx);
	if (QualitySeg) { QualitySeg->SetSelectedIndex(GolfDisplay::ClampQualityLevel(S.QualityLevel), false); }
	if (UpscalerSeg)
	{
		const int32 Sel = UpscalerOptionIndices.IndexOfByKey(S.UpscalerIndex);
		if (Sel != INDEX_NONE) { UpscalerSeg->SetSelectedIndex(Sel, false); }
	}
	RepopulateModeCombo(S.UpscalerIndex, S.ScreenPercentage);
}

void USettingsMenu::HandleApplyClicked()
{
	FGolfDisplaySettings S;
	if (ResCombo && ResOptions.IsValidIndex(ResCombo->GetSelectedIndex()))
	{
		S.Resolution = ResOptions[ResCombo->GetSelectedIndex()];
	}
	S.WindowModeIndex = WindowSeg ? WindowSeg->GetSelectedIndex() : 0;
	S.QualityLevel    = QualitySeg ? QualitySeg->GetSelectedIndex() : 3;
	if (UpscalerSeg && UpscalerOptionIndices.IsValidIndex(UpscalerSeg->GetSelectedIndex()))
	{
		S.UpscalerIndex = UpscalerOptionIndices[UpscalerSeg->GetSelectedIndex()];
	}
	S.ScreenPercentage = UpscaleModeCombo ? GolfDisplay::ScreenPctForMode(S.UpscalerIndex, UpscaleModeCombo->GetSelectedIndex()) : 100.f;
	if (OnApplyDisplay) { OnApplyDisplay(S); }
}

void USettingsMenu::HandleCloseClicked()    { if (OnClose) { OnClose(); } }
void USettingsMenu::HandleMainMenuClicked() { if (OnMainMenu) { OnMainMenu(); } }
void USettingsMenu::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void USettingsMenu::SetActionButtonsVisible(bool bVisible)
{
	const ESlateVisibility V = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (MainMenuBtn) { MainMenuBtn->SetVisibility(V); }
	if (QuitBtn) { QuitBtn->SetVisibility(V); }
}
