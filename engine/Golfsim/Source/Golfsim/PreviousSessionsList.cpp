#include "PreviousSessionsList.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Text, int32 Size, const FLinearColor& Color,
		ETextJustify::Type Just = ETextJustify::Left)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		{ FSlateFontInfo F = T->GetFont(); F.Size = Size; T->SetFont(F); }
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetJustification(Just);
		return T;
	}
}

void UPreviousSessionsList::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UPreviousSessionsList::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Full-screen blur + dim acts as a modal scrim over the main menu underneath.
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(3.5f);
	{
		UCanvasPanelSlot* S = Root->AddChildToCanvas(Blur);
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.30f));
	Blur->SetContent(Dim);

	// Centered card, narrower than the table view -- the picker is a short list.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f));
	Card->SetPadding(FMargin(24.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.25f, 0.12f, 0.75f, 0.88f));
	CardSlot->SetOffsets(FMargin(0.f));
	CardSlot->SetAlignment(FVector2D(0.f, 0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// Header row.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UTextBlock* Title = MakeLabel(WidgetTree, TEXT("PREVIOUS SESSIONS"), 22, FLinearColor(1.0f, 0.92f, 0.35f));
		UHorizontalBoxSlot* TS = Header->AddChildToHorizontalBox(Title);
		TS->SetVerticalAlignment(VAlign_Center);

		USpacer* Push = WidgetTree->ConstructWidget<USpacer>();
		UHorizontalBoxSlot* SS = Header->AddChildToHorizontalBox(Push);
		SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		CloseButton = WidgetTree->ConstructWidget<UButton>();
		{
			UTextBlock* Lbl = MakeLabel(WidgetTree, TEXT("Back"), 12, FLinearColor::Black, ETextJustify::Center);
			CloseButton->SetContent(Lbl);
		}
		CloseButton->OnClicked.AddDynamic(this, &UPreviousSessionsList::HandleCloseClicked);
		UHorizontalBoxSlot* CS = Header->AddChildToHorizontalBox(CloseButton);
		CS->SetVerticalAlignment(VAlign_Center);
	}
	Col->AddChildToVerticalBox(Header);

	Subtitle = MakeLabel(WidgetTree, TEXT("Pick a session"), 12, FLinearColor(0.7f, 0.7f, 0.7f));
	UVerticalBoxSlot* SubSlot = Col->AddChildToVerticalBox(Subtitle);
	SubSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 12.f));

	RowScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* ScrollSlot = Col->AddChildToVerticalBox(RowScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UPreviousSessionsList::SetSessions(const TArray<FPreviousSessionInfo>& InRows)
{
	Rows = InRows;
	if (Subtitle)
	{
		Subtitle->SetText(FText::FromString(FString::Printf(
			TEXT("%d session%s"), Rows.Num(), Rows.Num() == 1 ? TEXT("") : TEXT("s"))));
	}
	RebuildRows();
}

void UPreviousSessionsList::RebuildRows()
{
	if (!RowScroll)
	{
		return;
	}
	RowScroll->ClearChildren();

	if (Rows.Num() == 0)
	{
		UTextBlock* Empty = MakeLabel(WidgetTree,
			TEXT("(no past sessions yet -- fire a few shots and quit to record one)"),
			13, FLinearColor(0.7f, 0.7f, 0.7f));
		RowScroll->AddChild(Empty);
		return;
	}

	TWeakObjectPtr<UPreviousSessionsList> WeakSelf(this);
	for (const FPreviousSessionInfo& R : Rows)
	{
		UPreviousSessionRow* RowWidget = CreateWidget<UPreviousSessionRow>(this, UPreviousSessionRow::StaticClass());
		if (!RowWidget) { continue; }
		RowWidget->Init(R);
		RowWidget->OnSelected = [WeakSelf](const FString& SessionId)
		{
			if (UPreviousSessionsList* Self = WeakSelf.Get())
			{
				if (Self->OnSessionPicked) { Self->OnSessionPicked(SessionId); }
			}
		};
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(RowScroll->AddChild(RowWidget)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

// --- UPreviousSessionRow -----------------------------------------------------------------------

void UPreviousSessionRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// VerticalBox root: auto-sizes vertically from its button child, fills horizontally inside the
	// parent ScrollBox. (The previous CanvasPanel-with-0..1-anchors collapsed to 0 height inside
	// the ScrollBox so the buttons had no hit area -- clickable in theory, invisible in practice.)
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;

	Btn = WidgetTree->ConstructWidget<UButton>();
	Lbl = MakeLabel(WidgetTree, TEXT(""), 14, FLinearColor::Black, ETextJustify::Center);
	Btn->SetContent(Lbl);
	Btn->OnClicked.AddDynamic(this, &UPreviousSessionRow::HandleClicked);

	UVerticalBoxSlot* BtnSlot = Root->AddChildToVerticalBox(Btn);
	BtnSlot->SetPadding(FMargin(0.f, 4.f));
	BtnSlot->SetHorizontalAlignment(HAlign_Fill);
}

void UPreviousSessionRow::Init(const FPreviousSessionInfo& Row)
{
	CachedSessionId = Row.SessionId;
	if (Lbl)
	{
		Lbl->SetText(FText::FromString(Row.DisplayLabel));
	}
}

void UPreviousSessionRow::HandleClicked()
{
	if (OnSelected && !CachedSessionId.IsEmpty())
	{
		OnSelected(CachedSessionId);
	}
}

void UPreviousSessionsList::HandleCloseClicked()
{
	if (OnClose) { OnClose(); }
}
