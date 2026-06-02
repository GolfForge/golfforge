#include "SwingMeterWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float MeterWidthPx  = 520.f;
	constexpr float MeterHeightPx = 22.f;

	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Text, int32 Size, const FLinearColor& Color,
		ETextJustify::Type Just = ETextJustify::Center)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Text));
		{ FSlateFontInfo F = T->GetFont(); F.Size = Size; T->SetFont(F); }
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetJustification(Just);
		return T;
	}
}

void USwingMeterWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void USwingMeterWidget::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Bottom-center anchored card. AutoSize hugs the column; offset lifts it off the screen edge.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	Card->SetPadding(FMargin(14.f));
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CardSlot->SetAlignment(FVector2D(0.5f, 1.f));
	CardSlot->SetAutoSize(true);
	CardSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 32.f));   // 32 px above the bottom edge

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	UTextBlock* Title = MakeLabel(WidgetTree, TEXT("SWING METER"), 12, FLinearColor(1.0f, 0.92f, 0.35f));
	UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(Title);
	TS->SetHorizontalAlignment(HAlign_Center);
	TS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Power label + bar.
	UTextBlock* PLbl = MakeLabel(WidgetTree, TEXT("Power"), 10, FLinearColor(0.85f, 0.85f, 0.85f), ETextJustify::Left);
	Col->AddChildToVerticalBox(PLbl);

	PowerBar = WidgetTree->ConstructWidget<UProgressBar>();
	PowerBar->SetPercent(0.f);
	PowerBar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.65f, 0.15f));   // amber
	// Wrap in a SizeBox so the bar has a fixed footprint -- otherwise UVerticalBox would let the
	// progress bar collapse to its zero intrinsic size.
	{
		USizeBox* PowerWrap = WidgetTree->ConstructWidget<USizeBox>();
		PowerWrap->SetWidthOverride(MeterWidthPx);
		PowerWrap->SetHeightOverride(MeterHeightPx);
		PowerWrap->SetContent(PowerBar);
		UVerticalBoxSlot* PSlot = Col->AddChildToVerticalBox(PowerWrap);
		PSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 8.f));
	}

	// Accuracy label + bar with sweet-spot overlay.
	UTextBlock* ALbl = MakeLabel(WidgetTree, TEXT("Accuracy"), 10, FLinearColor(0.85f, 0.85f, 0.85f), ETextJustify::Left);
	Col->AddChildToVerticalBox(ALbl);

	// Wrap the accuracy bar in a CanvasPanel so we can overlay the sweet-spot band on top of it
	// at an absolute x-range. Set the canvas's MinDesired to the meter size so it occupies space.
	UCanvasPanel* AccCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	{
		UVerticalBoxSlot* AS = Col->AddChildToVerticalBox(AccCanvas);
		AS->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));
		AS->SetHorizontalAlignment(HAlign_Fill);
	}
	// Force a known footprint by anchoring children explicitly + sized -- VerticalBox would
	// otherwise collapse the canvas to zero (it has no intrinsic size).
	AccCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	AccuracyBar = WidgetTree->ConstructWidget<UProgressBar>();
	AccuracyBar->SetPercent(0.f);
	AccuracyBar->SetFillColorAndOpacity(FLinearColor(0.20f, 0.85f, 0.40f));   // green
	UCanvasPanelSlot* ABarSlot = AccCanvas->AddChildToCanvas(AccuracyBar);
	ABarSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	ABarSlot->SetAlignment(FVector2D(0.f, 0.f));
	ABarSlot->SetOffsets(FMargin(0.f, 0.f, MeterWidthPx, MeterHeightPx));   // L,T,W,H absolute

	SweetSpotBand = WidgetTree->ConstructWidget<UBorder>();
	SweetSpotBand->SetBrushColor(FLinearColor(0.20f, 1.0f, 0.40f, 0.45f));   // translucent bright green
	SweetSpotBand->SetPadding(FMargin(0.f));
	UCanvasPanelSlot* BandSlot = AccCanvas->AddChildToCanvas(SweetSpotBand);
	BandSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	BandSlot->SetAlignment(FVector2D(0.f, 0.f));
	BandSlot->SetOffsets(FMargin(MeterWidthPx * (float)SweetLow, 0.f,
		MeterWidthPx * (float)(SweetHigh - SweetLow), MeterHeightPx));

	HintText = MakeLabel(WidgetTree, TEXT("Press Space to start your swing"), 11,
		FLinearColor(0.85f, 0.85f, 0.85f), ETextJustify::Center);
	UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(HintText);
	HS->SetHorizontalAlignment(HAlign_Center);
	HS->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
}

void USwingMeterWidget::SetMeters(double Power, double Accuracy)
{
	if (PowerBar)    { PowerBar->SetPercent(FMath::Clamp((float)Power, 0.f, 1.f)); }
	if (AccuracyBar) { AccuracyBar->SetPercent(FMath::Clamp((float)Accuracy, 0.f, 1.f)); }
}

void USwingMeterWidget::SetSweetSpot(double Low, double High)
{
	SweetLow = Low;
	SweetHigh = High;
	if (!SweetSpotBand) { return; }
	if (UCanvasPanelSlot* BandSlot = Cast<UCanvasPanelSlot>(SweetSpotBand->Slot))
	{
		BandSlot->SetOffsets(FMargin(MeterWidthPx * (float)SweetLow, 0.f,
			MeterWidthPx * (float)(SweetHigh - SweetLow), MeterHeightPx));
	}
}

void USwingMeterWidget::SetHintText(const FString& Hint)
{
	if (HintText) { HintText->SetText(FText::FromString(Hint)); }
}
