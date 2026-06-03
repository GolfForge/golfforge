#include "UI/SegmentedControl.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

void USegmentedControl::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	using namespace GolfUI;
	// Rounded surface track; options are built into it by SetOptions/Rebuild.
	Track = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Track"));
	Track->SetBrush(RoundedBrush(Color::Surface(), Radius::Sm, Color::Border(), 1.f));
	Track->SetPadding(FMargin(3.f));
	WidgetTree->RootWidget = Track;
}

void USegmentedControl::SetOptions(const TArray<FString>& Options, const TArray<FString>& SubLabels)
{
	OptionLabels = Options;
	OptionSubLabels = SubLabels;
	OptionDisabled.Init(false, Options.Num());
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, FMath::Max(0, OptionLabels.Num() - 1));
	Rebuild();
}

void USegmentedControl::Rebuild()
{
	using namespace GolfUI;
	OptionButtons.Reset();
	OptionTexts.Reset();
	OptionSubTexts.Reset();
	if (!Track) { return; }

	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>();
	Track->SetContent(Box);

	for (int32 i = 0; i < OptionLabels.Num(); ++i)
	{
		UButton* B = WidgetTree->ConstructWidget<UButton>();
		B->OnClicked.AddDynamic(this, &USegmentedControl::HandleOptionClicked);

		// Option content = main label, plus an optional small dim sub-label suffix.
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(OptionLabels[i]));
		T->SetFont(Mono(12));
		T->SetJustification(ETextJustify::Center);
		if (UHorizontalBoxSlot* TS = Row->AddChildToHorizontalBox(T)) { TS->SetVerticalAlignment(VAlign_Center); }

		UTextBlock* Sub = nullptr;
		const FString SubLabel = OptionSubLabels.IsValidIndex(i) ? OptionSubLabels[i] : FString();
		if (!SubLabel.IsEmpty())
		{
			Sub = WidgetTree->ConstructWidget<UTextBlock>();
			Sub->SetText(FText::FromString(SubLabel));
			Sub->SetFont(Mono(9));
			Sub->SetJustification(ETextJustify::Center);
			if (UHorizontalBoxSlot* SS = Row->AddChildToHorizontalBox(Sub)) { SS->SetVerticalAlignment(VAlign_Center); SS->SetPadding(FMargin(6.f, 0, 0, 0)); }
		}

		B->SetContent(Row);
		// Auto-size each option to its content (matches the design). Fill-equal width clipped long
		// labels like "Everyone holes out" down to a short sibling's width.
		Box->AddChildToHorizontalBox(B);
		OptionButtons.Add(B);
		OptionTexts.Add(T);
		OptionSubTexts.Add(Sub);
	}
	RefreshVisual();
}

void USegmentedControl::RefreshVisual()
{
	using namespace GolfUI;
	for (int32 i = 0; i < OptionButtons.Num(); ++i)
	{
		UButton* B = OptionButtons[i];
		UTextBlock* T = OptionTexts.IsValidIndex(i) ? OptionTexts[i] : nullptr;
		UTextBlock* Sub = OptionSubTexts.IsValidIndex(i) ? OptionSubTexts[i] : nullptr;
		if (!B) { continue; }
		const bool bSel = (i == SelectedIndex);
		const bool bDisabled = OptionDisabled.IsValidIndex(i) && OptionDisabled[i];

		FButtonStyle S;
		if (bSel && !bDisabled)
		{
			const FLinearColor Hover = FMath::Lerp(Color::Accent(), FLinearColor::White, 0.10f);
			S.SetNormal(RoundedBrush(Color::Accent(), 6.f));
			S.SetHovered(RoundedBrush(Hover, 6.f));
			S.SetPressed(RoundedBrush(Color::Accent(), 6.f));
		}
		else
		{
			S.SetNormal(RoundedBrush(FLinearColor(0, 0, 0, 0), 6.f));
			// Disabled options don't react to hover/press.
			S.SetHovered(RoundedBrush(bDisabled ? FLinearColor(0, 0, 0, 0) : Color::Surface2(), 6.f));
			S.SetPressed(RoundedBrush(bDisabled ? FLinearColor(0, 0, 0, 0) : Color::Surface(), 6.f));
		}
		S.SetDisabled(RoundedBrush(FLinearColor(0, 0, 0, 0), 6.f));
		S.SetNormalPadding(FMargin(16.f, 7.f));
		S.SetPressedPadding(FMargin(16.f, 7.f));
		B->SetStyle(S);

		// Selected = ink on the accent fill; disabled = faint; otherwise dim.
		const FLinearColor MainCol = bDisabled ? Color::TextFaint() : (bSel ? Color::AccentInk() : Color::TextDim());
		if (T) { T->SetColorAndOpacity(FSlateColor(MainCol)); }
		if (Sub) { Sub->SetColorAndOpacity(FSlateColor(bDisabled ? Color::TextFaint() : (bSel ? Color::AccentInk() : Color::TextFaint()))); }
	}
}

void USegmentedControl::HandleOptionClicked()
{
	// Dynamic delegates don't pass the sender; the clicked button is the hovered one at click time.
	for (int32 i = 0; i < OptionButtons.Num(); ++i)
	{
		if (OptionButtons[i] && OptionButtons[i]->IsHovered())
		{
			if (OptionDisabled.IsValidIndex(i) && OptionDisabled[i]) { return; }   // ignore disabled option
			SetSelectedIndex(i, /*bBroadcast*/ true);
			return;
		}
	}
}

void USegmentedControl::SetOptionDisabled(int32 Index, bool bDisabled)
{
	if (!OptionDisabled.IsValidIndex(Index)) { return; }
	OptionDisabled[Index] = bDisabled;
	RefreshVisual();
}

void USegmentedControl::SetSelectedIndex(int32 Index, bool bBroadcast)
{
	if (!OptionLabels.IsValidIndex(Index)) { return; }
	SelectedIndex = Index;
	RefreshVisual();
	if (bBroadcast && OnChanged) { OnChanged(Index); }
}

void USegmentedControl::SetControlEnabled(bool bEnabled)
{
	bControlEnabled = bEnabled;
	SetIsEnabled(bEnabled);
	SetRenderOpacity(bEnabled ? 1.f : 0.5f);
}
