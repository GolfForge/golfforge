#include "SettingsMenu.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Kismet/KismetSystemLibrary.h"   // QuitGame

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"

void USettingsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

UButton* USettingsMenu::MakeButton(const TCHAR* Label)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Label));
	T->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	T->SetJustification(ETextJustify::Center);
	B->SetContent(T);
	return B;
}

void USettingsMenu::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Full-screen dim backdrop (also a modal scrim: absorbs clicks behind the card).
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	// Centered card.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(20.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("SETTINGS")));
	{ FSlateFontInfo F = Title->GetFont(); F.Size = 20; Title->SetFont(F); }
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.35f)));
	Col->AddChildToVerticalBox(Title);

	// Nav row: Display | Credits.
	UHorizontalBox* Nav = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* DisplayNav = MakeButton(TEXT("Display"));
	DisplayNav->OnClicked.AddDynamic(this, &USettingsMenu::HandleDisplayNavClicked);
	UButton* CreditsNav = MakeButton(TEXT("Credits"));
	CreditsNav->OnClicked.AddDynamic(this, &USettingsMenu::HandleCreditsNavClicked);
	Nav->AddChildToHorizontalBox(DisplayNav);
	Nav->AddChildToHorizontalBox(CreditsNav);
	UVerticalBoxSlot* NavSlot = Col->AddChildToVerticalBox(Nav);
	NavSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));

	// --- Display section ---
	DisplayBox = WidgetTree->ConstructWidget<UVerticalBox>();
	auto AddLabeledCombo = [&](const TCHAR* Label) -> UComboBoxString*
	{
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>();
		Lbl->SetText(FText::FromString(Label));
		DisplayBox->AddChildToVerticalBox(Lbl);
		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
		FTableRowStyle ItemStyle = Combo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		Combo->SetItemStyle(ItemStyle);
		DisplayBox->AddChildToVerticalBox(Combo);
		return Combo;
	};
	ResCombo = AddLabeledCombo(TEXT("Resolution"));
	WindowCombo = AddLabeledCombo(TEXT("Window mode"));
	WindowCombo->AddOption(TEXT("Windowed"));
	WindowCombo->AddOption(TEXT("Borderless"));
	WindowCombo->AddOption(TEXT("Fullscreen"));
	WindowCombo->OnSelectionChanged.AddDynamic(this, &USettingsMenu::HandleWindowModeChanged);
	QualityCombo = AddLabeledCombo(TEXT("Quality"));
	for (const TCHAR* Q : { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") }) { QualityCombo->AddOption(Q); }
	UpscalerCombo = AddLabeledCombo(TEXT("Upscaler"));   // options filled later by SetUpscalerOptions
	UpscalerCombo->OnSelectionChanged.AddDynamic(this, &USettingsMenu::HandleUpscalerChanged);

	UpscaleModeCombo = AddLabeledCombo(TEXT("Upscale Mode"));   // per-upscaler quality tiers (drive screen %)
	RepopulateModeCombo(0, 100.f);   // default to TSR/Native until SetUpscalerOptions + SetCurrent run

	UButton* ApplyBtn = MakeButton(TEXT("Apply"));
	ApplyBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleApplyClicked);
	UVerticalBoxSlot* ApplySlot = DisplayBox->AddChildToVerticalBox(ApplyBtn);
	ApplySlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	Col->AddChildToVerticalBox(DisplayBox);

	// --- Credits section ---
	CreditsScroll = WidgetTree->ConstructWidget<UScrollBox>();
	CreditsBody = WidgetTree->ConstructWidget<UTextBlock>();
	CreditsBody->SetText(FText::FromString(TEXT("")));
	CreditsScroll->AddChild(CreditsBody);
	Col->AddChildToVerticalBox(CreditsScroll);

	// Close (always visible at the bottom).
	UButton* CloseBtn = MakeButton(TEXT("Close"));
	CloseBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleCloseClicked);
	UVerticalBoxSlot* CloseSlot = Col->AddChildToVerticalBox(CloseBtn);
	CloseSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	UButton* QuitBtn = MakeButton(TEXT("Quit Game"));
	QuitBtn->OnClicked.AddDynamic(this, &USettingsMenu::HandleQuitClicked);
	UVerticalBoxSlot* QuitSlot = Col->AddChildToVerticalBox(QuitBtn);
	QuitSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	ShowSection(0);   // Display first
}

void USettingsMenu::ShowSection(int32 Index)
{
	if (DisplayBox)    { DisplayBox->SetVisibility(Index == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CreditsScroll) { CreditsScroll->SetVisibility(Index == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void USettingsMenu::HandleDisplayNavClicked() { ShowSection(0); }
void USettingsMenu::HandleCreditsNavClicked() { ShowSection(1); }

void USettingsMenu::RepopulateModeCombo(int32 UpscalerFixedIndex, float TargetPct)
{
	if (!UpscaleModeCombo) { return; }
	UpscaleModeCombo->ClearOptions();
	for (const FString& M : GolfDisplay::UpscaleModeNames(UpscalerFixedIndex)) { UpscaleModeCombo->AddOption(M); }
	UpscaleModeCombo->SetSelectedIndex(GolfDisplay::ModeForScreenPct(UpscalerFixedIndex, TargetPct));
	ModeComboUpscaler = UpscalerFixedIndex;
}

void USettingsMenu::HandleUpscalerChanged(FString, ESelectInfo::Type SelectionType)
{
	// Ignore programmatic selection (SetSelectedIndex re-broadcasts as Direct).
	if (SelectionType == ESelectInfo::Direct || !UpscalerCombo) { return; }
	const int32 Sel = UpscalerCombo->GetSelectedIndex();
	if (!UpscalerOptionIndices.IsValidIndex(Sel)) { return; }
	const int32 NewUpscaler = UpscalerOptionIndices[Sel];
	// Preserve the chosen quality (render scale) across the upscaler switch.
	const float CurPct = UpscaleModeCombo
		? GolfDisplay::ScreenPctForMode(ModeComboUpscaler, UpscaleModeCombo->GetSelectedIndex())
		: 100.f;
	RepopulateModeCombo(NewUpscaler, CurPct);
}

void USettingsMenu::HandleWindowModeChanged(FString, ESelectInfo::Type SelectionType)
{
	// Ignore programmatic selection (SetCurrent calls UpdateResolutionEnabledForMode itself).
	if (SelectionType == ESelectInfo::Direct || !WindowCombo) { return; }
	UpdateResolutionEnabledForMode(WindowCombo->GetSelectedIndex());
}

void USettingsMenu::UpdateResolutionEnabledForMode(int32 WindowModeIndex)
{
	// Borderless (1) always renders at the OS desktop resolution, so the picker can't take effect there
	// -- grey it out to avoid the "I picked 4K but it stayed at the desktop res" confusion.
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
	if (!UpscalerCombo) { return; }
	UpscalerCombo->ClearOptions();
	for (int32 Idx : Indices)
	{
		UpscalerCombo->AddOption(GolfDisplay::UpscalerName(Idx));
	}
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
	if (WindowCombo)  { WindowCombo->SetSelectedIndex(GolfDisplay::ClampWindowModeIndex(S.WindowModeIndex)); }
	UpdateResolutionEnabledForMode(GolfDisplay::ClampWindowModeIndex(S.WindowModeIndex));
	if (QualityCombo) { QualityCombo->SetSelectedIndex(GolfDisplay::ClampQualityLevel(S.QualityLevel)); }
	if (UpscalerCombo)
	{
		const int32 Sel = UpscalerOptionIndices.IndexOfByKey(S.UpscalerIndex);
		if (Sel != INDEX_NONE) { UpscalerCombo->SetSelectedIndex(Sel); }
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
	S.WindowModeIndex = WindowCombo ? WindowCombo->GetSelectedIndex() : 0;
	S.QualityLevel    = QualityCombo ? QualityCombo->GetSelectedIndex() : 3;
	if (UpscalerCombo && UpscalerOptionIndices.IsValidIndex(UpscalerCombo->GetSelectedIndex()))
	{
		S.UpscalerIndex = UpscalerOptionIndices[UpscalerCombo->GetSelectedIndex()];
	}
	S.ScreenPercentage = UpscaleModeCombo ? GolfDisplay::ScreenPctForMode(S.UpscalerIndex, UpscaleModeCombo->GetSelectedIndex()) : 100.f;
	if (OnApplyDisplay) { OnApplyDisplay(S); }
}

void USettingsMenu::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}

void USettingsMenu::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
