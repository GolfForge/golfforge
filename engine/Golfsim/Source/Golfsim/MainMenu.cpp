#include "MainMenu.h"

#include "Kismet/KismetSystemLibrary.h"   // QuitGame
#include "UI/GolfUITheme.h"   // GOL-138 smoke test: shared theme tokens + fonts

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UMainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

UButton* UMainMenu::MakeButton(const TCHAR* Label, bool bEnabled)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Label));
	T->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	T->SetJustification(ETextJustify::Center);
	B->SetContent(T);
	B->SetIsEnabled(bEnabled);
	return B;
}

void UMainMenu::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Barely-blurred full-screen view of the live range behind the menu. The blur's content (a faint
	// dim border) is hit-test visible, so it also acts as a modal scrim that absorbs clicks meant for
	// the range panel underneath.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(Blur);
	BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BlurSlot->SetOffsets(FMargin(0.f));

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.25f));
	Blur->SetContent(Dim);

	// Centered card.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(28.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("GolfForge")));
	// GOL-138 smoke test: drive the title from GolfUITheme. Renders Roboto fallback until the Barlow
	// Condensed font asset is imported, then this same call picks it up automatically.
	Title->SetFont(GolfUI::Display(28, FName(TEXT("Bold"))));
	Title->SetColorAndOpacity(FSlateColor(GolfUI::Color::Text()));
	Title->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

	UButton* RangeBtn = MakeButton(TEXT("Range"));
	RangeBtn->OnClicked.AddDynamic(this, &UMainMenu::HandleRangeClicked);
	UVerticalBoxSlot* RangeSlot = Col->AddChildToVerticalBox(RangeBtn);
	RangeSlot->SetPadding(FMargin(0.f, 4.f));

	// Previous Sessions (GOL-65): overlay the shot-history panel over this menu (which stays mounted
	// underneath). HUD passes the count via SetPreviousSessionsCount; greyed when 0.
	PreviousSessionsBtn = MakeButton(TEXT("Previous Sessions"), /*bEnabled*/ false);
	PreviousSessionsBtn->OnClicked.AddDynamic(this, &UMainMenu::HandlePreviousSessionsClicked);
	UVerticalBoxSlot* PrevSlot = Col->AddChildToVerticalBox(PreviousSessionsBtn);
	PrevSlot->SetPadding(FMargin(0.f, 4.f));

	// GOL-121: Play Course is now wired through the HUD to UPreRoundPicker -> URoundSubsystem::StartRound.
	UButton* CourseBtn = MakeButton(TEXT("Play Course"));
	CourseBtn->OnClicked.AddDynamic(this, &UMainMenu::HandlePlayCourseClicked);
	UVerticalBoxSlot* CourseSlot = Col->AddChildToVerticalBox(CourseBtn);
	CourseSlot->SetPadding(FMargin(0.f, 4.f));

	UButton* ExitBtn = MakeButton(TEXT("Exit"));
	ExitBtn->OnClicked.AddDynamic(this, &UMainMenu::HandleQuitClicked);
	UVerticalBoxSlot* ExitSlot = Col->AddChildToVerticalBox(ExitBtn);
	ExitSlot->SetPadding(FMargin(0.f, 4.f));
}

void UMainMenu::HandleRangeClicked()
{
	if (OnPlayRange) { OnPlayRange(); }
}

void UMainMenu::HandlePlayCourseClicked()
{
	if (OnPlayCourse) { OnPlayCourse(); }
}

void UMainMenu::HandlePreviousSessionsClicked()
{
	if (OnPreviousSessions) { OnPreviousSessions(); }
}

void UMainMenu::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenu::SetPreviousSessionsCount(int32 Count)
{
	if (PreviousSessionsBtn)
	{
		PreviousSessionsBtn->SetIsEnabled(Count > 0);
	}
}
