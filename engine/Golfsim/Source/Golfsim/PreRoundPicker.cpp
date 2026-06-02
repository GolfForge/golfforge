#include "PreRoundPicker.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UButton* MakeButton(UUserWidget* Owner, const TCHAR* Label)
	{
		UWidgetTree* Tree = Owner->WidgetTree;
		UButton* B = Tree->ConstructWidget<UButton>();
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Label));
		T->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		return B;
	}

	UTextBlock* MakeLabel(UUserWidget* Owner, const TCHAR* Text)
	{
		UTextBlock* L = Owner->WidgetTree->ConstructWidget<UTextBlock>();
		L->SetText(FText::FromString(Text));
		L->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)));
		return L;
	}
}

void UPreRoundPicker::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UPreRoundPicker::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Blurred + dimmed scrim, identical to MainMenu / PreviousSessionsList.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	UCanvasPanelSlot* BlurSlot = Root->AddChildToCanvas(Blur);
	BlurSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BlurSlot->SetOffsets(FMargin(0.f));

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));
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
	Title->SetText(FText::FromString(TEXT("Play Course")));
	{ FSlateFontInfo F = Title->GetFont(); F.Size = 24; Title->SetFont(F); }
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.35f)));
	Title->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));

	// Player name field.
	Col->AddChildToVerticalBox(MakeLabel(this, TEXT("Player name")));
	NameField = WidgetTree->ConstructWidget<UEditableTextBox>();
	NameField->SetHintText(FText::FromString(TEXT("Your name")));
	UVerticalBoxSlot* NameSlot = Col->AddChildToVerticalBox(NameField);
	NameSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 12.f));

	// Course dropdown.
	Col->AddChildToVerticalBox(MakeLabel(this, TEXT("Course")));
	CourseCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	{
		FTableRowStyle ItemStyle = CourseCombo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		CourseCombo->SetItemStyle(ItemStyle);
	}
	UVerticalBoxSlot* CourseSlot = Col->AddChildToVerticalBox(CourseCombo);
	CourseSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 12.f));

	// Difficulty dropdown.
	Col->AddChildToVerticalBox(MakeLabel(this, TEXT("Difficulty")));
	DifficultyCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	{
		FTableRowStyle ItemStyle = DifficultyCombo->GetItemStyle();
		ItemStyle.TextColor = FSlateColor(FLinearColor::White);
		DifficultyCombo->SetItemStyle(ItemStyle);
	}
	DifficultyCombo->AddOption(TEXT("Easy"));
	DifficultyCombo->AddOption(TEXT("Normal"));
	DifficultyCombo->AddOption(TEXT("Pro"));
	DifficultyCombo->SetSelectedIndex(0);
	UVerticalBoxSlot* DiffSlot = Col->AddChildToVerticalBox(DifficultyCombo);
	DiffSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 20.f));

	// Buttons row.
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* StartBtn = MakeButton(this, TEXT("Start Round"));
	StartBtn->OnClicked.AddDynamic(this, &UPreRoundPicker::HandleStartClicked);
	UHorizontalBoxSlot* StartSlot = Buttons->AddChildToHorizontalBox(StartBtn);
	StartSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	StartSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UButton* BackBtn = MakeButton(this, TEXT("Back"));
	BackBtn->OnClicked.AddDynamic(this, &UPreRoundPicker::HandleBackClicked);
	UHorizontalBoxSlot* BackSlot = Buttons->AddChildToHorizontalBox(BackBtn);
	BackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Col->AddChildToVerticalBox(Buttons);
}

void UPreRoundPicker::SetCourses(const TArray<FString>& DisplayLabels, const TArray<FString>& InCourseIds)
{
	if (!CourseCombo) { return; }
	CourseCombo->ClearOptions();
	for (const FString& Label : DisplayLabels)
	{
		CourseCombo->AddOption(Label);
	}
	CourseIds = InCourseIds;
	if (CourseCombo->GetOptionCount() > 0)
	{
		CourseCombo->SetSelectedIndex(0);
	}
}

void UPreRoundPicker::SetPlayerName(const FString& Name)
{
	if (NameField)
	{
		NameField->SetText(FText::FromString(Name));
	}
}

void UPreRoundPicker::HandleStartClicked()
{
	if (!OnStartRound) { return; }

	const int32 CourseIdx = CourseCombo ? CourseCombo->GetSelectedIndex() : INDEX_NONE;
	if (!CourseIds.IsValidIndex(CourseIdx))
	{
		// No course selected / mismatched dropdown. The HUD should have seeded at least one entry;
		// bail rather than firing a malformed round-start.
		return;
	}
	const FString CourseId = CourseIds[CourseIdx];

	const int32 DiffIdx = DifficultyCombo ? DifficultyCombo->GetSelectedIndex() : 0;
	const EGolfDifficulty Difficulty = static_cast<EGolfDifficulty>(FMath::Clamp(DiffIdx, 0, 2));

	const FString PlayerName = NameField ? NameField->GetText().ToString() : FString();
	OnStartRound(CourseId, Difficulty, PlayerName);
}

void UPreRoundPicker::HandleBackClicked()
{
	if (OnBack) { OnBack(); }
}
