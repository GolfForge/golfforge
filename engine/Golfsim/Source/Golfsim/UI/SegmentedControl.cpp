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

void USegmentedControl::SetOptions(const TArray<FString>& Options)
{
	OptionLabels = Options;
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, FMath::Max(0, OptionLabels.Num() - 1));
	Rebuild();
}

void USegmentedControl::Rebuild()
{
	using namespace GolfUI;
	OptionButtons.Reset();
	OptionTexts.Reset();
	if (!Track) { return; }

	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>();
	Track->SetContent(Box);

	for (const FString& Label : OptionLabels)
	{
		UButton* B = WidgetTree->ConstructWidget<UButton>();
		B->OnClicked.AddDynamic(this, &USegmentedControl::HandleOptionClicked);
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Label));
		T->SetFont(Mono(12));
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		if (UHorizontalBoxSlot* BS = Box->AddChildToHorizontalBox(B))
		{
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		OptionButtons.Add(B);
		OptionTexts.Add(T);
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
		if (!B) { continue; }
		const bool bSel = (i == SelectedIndex);

		FButtonStyle S;
		if (bSel)
		{
			const FLinearColor Hover = FMath::Lerp(Color::Accent(), FLinearColor::White, 0.10f);
			S.SetNormal(RoundedBrush(Color::Accent(), 6.f));
			S.SetHovered(RoundedBrush(Hover, 6.f));
			S.SetPressed(RoundedBrush(Color::Accent(), 6.f));
		}
		else
		{
			S.SetNormal(RoundedBrush(FLinearColor(0, 0, 0, 0), 6.f));
			S.SetHovered(RoundedBrush(Color::Surface2(), 6.f));
			S.SetPressed(RoundedBrush(Color::Surface(), 6.f));
		}
		S.SetDisabled(RoundedBrush(FLinearColor(0, 0, 0, 0), 6.f));
		S.SetNormalPadding(FMargin(12.f, 6.f));
		S.SetPressedPadding(FMargin(12.f, 6.f));
		B->SetStyle(S);

		if (T) { T->SetColorAndOpacity(FSlateColor(bSel ? Color::AccentInk() : Color::TextDim())); }
	}
}

void USegmentedControl::HandleOptionClicked()
{
	// Dynamic delegates don't pass the sender; the clicked button is the hovered one at click time.
	for (int32 i = 0; i < OptionButtons.Num(); ++i)
	{
		if (OptionButtons[i] && OptionButtons[i]->IsHovered())
		{
			SetSelectedIndex(i, /*bBroadcast*/ true);
			return;
		}
	}
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
