#include "SwingMeterWidget.h"

#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace GolfUI;

namespace
{
	// Bar geometry. The glass panel auto-sizes around the track width + 16px panel padding.
	constexpr float TrackWidthPx  = 404.f;
	constexpr float TrackHeightPx = 15.f;
	constexpr float MarkerWidthPx = 4.f;
	constexpr float MarkerOverhang = 3.f;   // marker sticks out top + bottom of the track
	constexpr float TrackRadiusPx = 7.f;    // ~half the track height -> pill ends (999 fails to render)

	// Inset track background -- light enough to read as a groove on the dark glass.
	FSlateBrush TrackBrush() { return RoundedBrush(Color::Surface3(), TrackRadiusPx, Color::BorderStrong(), 1.f); }

	// A "LABEL ............ value" row used by both bars.
	UTextBlock* MakeBarLabel(UWidgetTree* Tree, const TCHAR* Text)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(FString(Text).ToUpper()));
		FSlateFontInfo F = Display(13, FName(TEXT("SemiBold")));
		F.LetterSpacing = 60;   // ~0.06em
		T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(Color::TextDim()));
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

	// Glass card, bottom-centre. Lifted clear of GOL-145's full-width control bar (~74px tall at the
	// screen bottom) so the two never overlap. AutoSize hugs the column.
	UBorder* Card = MakeGlassPanel(WidgetTree);
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CardSlot->SetAlignment(FVector2D(0.5f, 1.f));
	CardSlot->SetAutoSize(true);
	// AutoSize canvas slots use Offset.Top as the Y position (Bottom is ignored); a negative Top
	// lifts a bottom-anchored panel up off the screen edge -- here clear of GOL-145's control bar.
	CardSlot->SetOffsets(FMargin(0.f, -110.f, 0.f, 0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Col);

	// --- header: eyebrow (left) + green "GAME MODE - KEYBOARD" pill (right) --------------------
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Eyebrow = MakeEyebrow(WidgetTree, TEXT("SWING METER"));
		if (UHorizontalBoxSlot* ES = Head->AddChildToHorizontalBox(Eyebrow)) { ES->SetVerticalAlignment(VAlign_Center); }

		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* GS = Head->AddChildToHorizontalBox(Gap)) { GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

		UBorder* Pill = WidgetTree->ConstructWidget<UBorder>();
		Pill->SetBrush(RoundedBrush(Color::AccentSoft(), Radius::Sm, Color::AccentLine(), 1.f));
		Pill->SetPadding(FMargin(9.f, 4.f));
		UTextBlock* PillText = WidgetTree->ConstructWidget<UTextBlock>();
		PillText->SetText(FText::FromString(TEXT("GAME MODE · KEYBOARD")));
		{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 120; PillText->SetFont(F); }
		PillText->SetColorAndOpacity(FSlateColor(Color::Accent()));
		Pill->SetContent(PillText);
		if (UHorizontalBoxSlot* PS = Head->AddChildToHorizontalBox(Pill)) { PS->SetVerticalAlignment(VAlign_Center); }

		if (UVerticalBoxSlot* HS = Col->AddChildToVerticalBox(Head)) { HS->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f)); }
	}

	// --- power group: "POWER ..... NN%" + fill bar --------------------------------------------
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Row->AddChildToHorizontalBox(MakeBarLabel(WidgetTree, TEXT("Power")));
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* GS = Row->AddChildToHorizontalBox(Gap)) { GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		PowerValue = WidgetTree->ConstructWidget<UTextBlock>();
		PowerValue->SetText(FText::FromString(TEXT("—")));
		PowerValue->SetFont(Mono(13));
		PowerValue->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* VS = Row->AddChildToHorizontalBox(PowerValue)) { VS->SetVerticalAlignment(VAlign_Bottom); }
		if (UVerticalBoxSlot* RS = Col->AddChildToVerticalBox(Row)) { RS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f)); }

		// Track (fixed footprint via SizeBox); a width-driven fill border grows left->right.
		USizeBox* PowerWrap = WidgetTree->ConstructWidget<USizeBox>();
		PowerWrap->SetWidthOverride(TrackWidthPx);
		PowerWrap->SetHeightOverride(TrackHeightPx);
		UCanvasPanel* PowerTrack = WidgetTree->ConstructWidget<UCanvasPanel>();
		PowerTrack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		PowerWrap->SetContent(PowerTrack);

		UBorder* PowerBg = WidgetTree->ConstructWidget<UBorder>();
		PowerBg->SetBrush(TrackBrush());
		if (UCanvasPanelSlot* BgS = PowerTrack->AddChildToCanvas(PowerBg))
		{
			BgS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			BgS->SetAlignment(FVector2D(0.f, 0.f));
			BgS->SetOffsets(FMargin(0.f, 0.f, TrackWidthPx, TrackHeightPx));
		}

		PowerFill = WidgetTree->ConstructWidget<UBorder>();
		PowerFill->SetBrush(RoundedBrush(Color::Accent(), TrackRadiusPx));
		if (UCanvasPanelSlot* FS = PowerTrack->AddChildToCanvas(PowerFill))
		{
			FS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			FS->SetAlignment(FVector2D(0.f, 0.f));
			FS->SetOffsets(FMargin(0.f, 0.f, 0.f, TrackHeightPx));   // width set by SetMeters
		}

		if (UVerticalBoxSlot* PSlot = Col->AddChildToVerticalBox(PowerWrap)) { PSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 11.f)); }
	}

	// --- accuracy group: "ACCURACY ..... result" + track (zone + marker) ----------------------
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Row->AddChildToHorizontalBox(MakeBarLabel(WidgetTree, TEXT("Accuracy")));
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
		if (UHorizontalBoxSlot* GS = Row->AddChildToHorizontalBox(Gap)) { GS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		AccuracyValue = WidgetTree->ConstructWidget<UTextBlock>();
		AccuracyValue->SetText(FText::FromString(TEXT("—")));
		AccuracyValue->SetFont(Mono(13));
		AccuracyValue->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UHorizontalBoxSlot* VS = Row->AddChildToHorizontalBox(AccuracyValue)) { VS->SetVerticalAlignment(VAlign_Bottom); }
		if (UVerticalBoxSlot* RS = Col->AddChildToVerticalBox(Row)) { RS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f)); }

		// Fixed footprint via a SizeBox; the canvas places the band + marker at absolute offsets.
		USizeBox* TrackWrap = WidgetTree->ConstructWidget<USizeBox>();
		TrackWrap->SetWidthOverride(TrackWidthPx);
		TrackWrap->SetHeightOverride(TrackHeightPx);
		UCanvasPanel* Track = WidgetTree->ConstructWidget<UCanvasPanel>();
		Track->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		TrackWrap->SetContent(Track);

		// Track background (rounded surface inset).
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
		Bg->SetBrush(TrackBrush());
		if (UCanvasPanelSlot* BgS = Track->AddChildToCanvas(Bg))
		{
			BgS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			BgS->SetAlignment(FVector2D(0.f, 0.f));
			BgS->SetOffsets(FMargin(0.f, 0.f, TrackWidthPx, TrackHeightPx));
		}

		// Always-visible green sweet zone (positioned by SetSweetSpot).
		FLinearColor Band = Color::Accent(); Band.A = 0.45f;
		SweetSpotBand = WidgetTree->ConstructWidget<UBorder>();
		SweetSpotBand->SetBrush(RoundedBrush(Band, 3.f, Color::AccentLine(), 1.f));
		if (UCanvasPanelSlot* ZS = Track->AddChildToCanvas(SweetSpotBand))
		{
			ZS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			ZS->SetAlignment(FVector2D(0.f, 0.f));
			ZS->SetOffsets(FMargin(TrackWidthPx * (float)SweetLow, 0.f,
				TrackWidthPx * (float)(SweetHigh - SweetLow), TrackHeightPx));
		}

		// Moving marker (white; turns accent/amber on result). Top-most child.
		AccuracyMarker = WidgetTree->ConstructWidget<UBorder>();
		AccuracyMarker->SetBrush(RoundedBrush(FLinearColor::White, 2.f));
		if (UCanvasPanelSlot* MS = Track->AddChildToCanvas(AccuracyMarker))
		{
			MS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			MS->SetAlignment(FVector2D(0.f, 0.f));
			MS->SetOffsets(FMargin(-MarkerWidthPx * 0.5f, -MarkerOverhang, MarkerWidthPx, TrackHeightPx + MarkerOverhang * 2.f));
		}

		if (UVerticalBoxSlot* TS = Col->AddChildToVerticalBox(TrackWrap)) { TS->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); }
	}

	// --- prompt line: [Space] keycap + hint text ----------------------------------------------
	{
		UHorizontalBox* Prompt = WidgetTree->ConstructWidget<UHorizontalBox>();
		PromptKey = MakeKbd(WidgetTree, TEXT("Space"));
		if (UHorizontalBoxSlot* KS = Prompt->AddChildToHorizontalBox(PromptKey))
		{
			KS->SetVerticalAlignment(VAlign_Center);
			KS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		HintText = WidgetTree->ConstructWidget<UTextBlock>();
		HintText->SetText(FText::FromString(TEXT("to start your swing")));
		HintText->SetFont(Display(14, FName(TEXT("SemiBold"))));
		HintText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
		if (UHorizontalBoxSlot* HS = Prompt->AddChildToHorizontalBox(HintText)) { HS->SetVerticalAlignment(VAlign_Center); }

		if (UVerticalBoxSlot* PS = Col->AddChildToVerticalBox(Prompt))
		{
			PS->SetHorizontalAlignment(HAlign_Center);
			PS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}
	}
}

void USwingMeterWidget::PlaceMarker(double Accuracy)
{
	if (!AccuracyMarker) { return; }
	if (UCanvasPanelSlot* MS = Cast<UCanvasPanelSlot>(AccuracyMarker->Slot))
	{
		const float X = TrackWidthPx * FMath::Clamp((float)Accuracy, 0.f, 1.f) - MarkerWidthPx * 0.5f;
		MS->SetOffsets(FMargin(X, -MarkerOverhang, MarkerWidthPx, TrackHeightPx + MarkerOverhang * 2.f));
	}
}

void USwingMeterWidget::SetMeters(double Power, double Accuracy)
{
	const float P = FMath::Clamp((float)Power, 0.f, 1.f);
	if (PowerFill)
	{
		if (UCanvasPanelSlot* FS = Cast<UCanvasPanelSlot>(PowerFill->Slot))
		{
			FS->SetOffsets(FMargin(0.f, 0.f, TrackWidthPx * P, TrackHeightPx));
		}
	}
	if (PowerValue)
	{
		PowerValue->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),
			FMath::RoundToInt(FMath::Clamp((float)Power, 0.f, 1.f) * 100.f))));
	}
	PlaceMarker(Accuracy);
}

void USwingMeterWidget::SetSweetSpot(double Low, double High)
{
	SweetLow = Low;
	SweetHigh = High;
	if (!SweetSpotBand) { return; }
	if (UCanvasPanelSlot* ZS = Cast<UCanvasPanelSlot>(SweetSpotBand->Slot))
	{
		ZS->SetOffsets(FMargin(TrackWidthPx * (float)SweetLow, 0.f,
			TrackWidthPx * (float)(SweetHigh - SweetLow), TrackHeightPx));
	}
}

void USwingMeterWidget::SetHintText(const FString& Hint)
{
	if (HintText) { HintText->SetText(FText::FromString(Hint)); }
}

void USwingMeterWidget::OnPowerLocked()
{
	if (PowerValue) { PowerValue->SetColorAndOpacity(FSlateColor(Color::Accent())); }
}

void USwingMeterWidget::OnAccuracyResult(bool bInZone, double Accuracy)
{
	PlaceMarker(Accuracy);

	const FLinearColor Tint = bInZone ? Color::Accent() : Color::Caution();
	if (AccuracyMarker) { AccuracyMarker->SetBrushColor(Tint); }
	if (AccuracyValue)
	{
		const double Mid = 0.5 * (SweetLow + SweetHigh);
		const FString Label = bInZone ? TEXT("Pure") : (Accuracy < Mid ? TEXT("Pull L") : TEXT("Push R"));
		AccuracyValue->SetText(FText::FromString(Label));
		AccuracyValue->SetColorAndOpacity(FSlateColor(Tint));
	}
	// Result copy stands on its own -- drop the keycap and tint the prompt to match the verdict.
	if (PromptKey) { PromptKey->SetVisibility(ESlateVisibility::Collapsed); }
	if (HintText) { HintText->SetColorAndOpacity(FSlateColor(Tint)); }
}

void USwingMeterWidget::ResetMeter()
{
	if (PowerFill)
	{
		if (UCanvasPanelSlot* FS = Cast<UCanvasPanelSlot>(PowerFill->Slot))
		{
			FS->SetOffsets(FMargin(0.f, 0.f, 0.f, TrackHeightPx));
		}
	}
	if (PowerValue)
	{
		PowerValue->SetText(FText::FromString(TEXT("—")));
		PowerValue->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	}
	PlaceMarker(0.0);
	if (AccuracyMarker) { AccuracyMarker->SetBrushColor(FLinearColor::White); }
	if (AccuracyValue)
	{
		AccuracyValue->SetText(FText::FromString(TEXT("—")));
		AccuracyValue->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
	}
	if (PromptKey) { PromptKey->SetVisibility(ESlateVisibility::Visible); }
	if (HintText)
	{
		HintText->SetText(FText::FromString(TEXT("to start your swing")));
		HintText->SetColorAndOpacity(FSlateColor(Color::TextDim()));
	}
}
