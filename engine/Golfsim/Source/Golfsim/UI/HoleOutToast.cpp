#include "UI/HoleOutToast.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/GolfUITheme.h"

void UHoleOutToast::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
	SetVisibility(ESlateVisibility::Collapsed);   // idle until the first Show()
}

void UHoleOutToast::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Centered card in the upper third (same band the old DrawHUD banner used) so it celebrates
	// without covering the green or the bottom telemetry/control bar.
	Card = GolfUI::MakeGlassPanel(WidgetTree);
	Card->SetPadding(FMargin(36.f, 22.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 0.24f, 0.5f, 0.24f));
	CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CardSlot->SetAutoSize(true);
	// Scale from the card's own center so the pop-in grows in place.
	Card->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	EyebrowText = GolfUI::MakeEyebrow(WidgetTree, TEXT("PUTTING"));
	EyebrowText->SetJustification(ETextJustify::Center);
	Col->AddChildToVerticalBox(EyebrowText);

	TitleText = GolfUI::MakeTitle(WidgetTree, TEXT("HOLED IT!"), 40);
	TitleText->SetColorAndOpacity(FSlateColor(GolfUI::Color::Accent()));
	TitleText->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Col->AddChildToVerticalBox(TitleText);
	TitleSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
	TitleSlot->SetHorizontalAlignment(HAlign_Center);

	DetailText = GolfUI::MakeMonoNumber(WidgetTree, TEXT("1 PUTT"), 14, GolfUI::Color::TextDim());
	DetailText->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* DetailSlot = Col->AddChildToVerticalBox(DetailText);
	DetailSlot->SetHorizontalAlignment(HAlign_Center);
}

void UHoleOutToast::Show(int32 Putts)
{
	ShowText(TEXT("PUTTING"),
		Putts == 1 ? TEXT("HOLED IT!") : TEXT("HOLED OUT"),
		Putts == 1 ? FString(TEXT("1 PUTT")) : FString::Printf(TEXT("%d PUTTS"), Putts));
}

void UHoleOutToast::ShowText(const FString& Eyebrow, const FString& Title, const FString& Detail)
{
	if (EyebrowText) { EyebrowText->SetText(FText::FromString(Eyebrow)); }
	if (TitleText)   { TitleText->SetText(FText::FromString(Title)); }
	if (DetailText)
	{
		DetailText->SetText(FText::FromString(Detail));
		DetailText->SetVisibility(Detail.IsEmpty() ? ESlateVisibility::Collapsed
		                                           : ESlateVisibility::HitTestInvisible);
	}
	AnimSeconds = 0.f;
	SetVisibility(ESlateVisibility::HitTestInvisible);   // celebratory overlay -- never eats clicks
	SetRenderOpacity(0.f);
}

void UHoleOutToast::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (AnimSeconds < 0.f)
	{
		return;
	}
	AnimSeconds += InDeltaTime;

	float Alpha = 1.f;
	float Scale = 1.f;
	if (AnimSeconds < PopSec)
	{
		// Pop-in: ease-out opacity + a slight grow from 92% so it lands with a little snap.
		const float T = AnimSeconds / PopSec;
		Alpha = FMath::Sin(T * UE_HALF_PI);
		Scale = FMath::Lerp(0.92f, 1.f, Alpha);
	}
	else if (AnimSeconds < PopSec + HoldSec)
	{
		// Hold at full.
	}
	else if (AnimSeconds < PopSec + HoldSec + FadeSec)
	{
		Alpha = 1.f - (AnimSeconds - PopSec - HoldSec) / FadeSec;
	}
	else
	{
		AnimSeconds = -1.f;
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	SetRenderOpacity(Alpha);
	if (Card)
	{
		Card->SetRenderScale(FVector2D(Scale, Scale));
	}
}
