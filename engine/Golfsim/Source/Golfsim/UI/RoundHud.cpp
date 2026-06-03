#include "UI/RoundHud.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	FString HudInitials(const FString& Name)
	{
		TArray<FString> Parts;
		Name.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(" "), true);
		FString Out;
		if (Parts.Num() > 0 && Parts[0].Len() > 0) { Out += Parts[0].Left(1); }
		if (Parts.Num() > 1 && Parts.Last().Len() > 0) { Out += Parts.Last().Left(1); }
		return Out.IsEmpty() ? TEXT("?") : Out.ToUpper();
	}

	FString ScoreVsParText(int32 V)
	{
		if (V == 0) { return TEXT("E"); }
		return V > 0 ? FString::Printf(TEXT("+%d"), V) : FString::Printf(TEXT("%d"), V);
	}
}

void URoundHud::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // only the Menu button takes input
	BuildTree();
}

void URoundHud::BuildTree()
{
	using namespace GolfUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// top legibility gradient (dark at top -> transparent down) behind the panels
	UImage* Grad = MakeLinearGradient(WidgetTree, FLinearColor(0, 0, 0, 0), FLinearColor(0.035f, 0.05f, 0.04f, 0.78f));
	Grad->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* GS = Root->AddChildToCanvas(Grad))
	{
		GS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
		GS->SetOffsets(FMargin(0.f, 0.f, 0.f, 300.f));   // L,T,R = anchored; B = height
		GS->SetAlignment(FVector2D(0.f, 0.f));
	}

	// ───────────────── round panel (top-left) ─────────────────
	UBorder* RoundCard = MakeGlassPanel(WidgetTree);
	RoundCard->SetPadding(FMargin(16.f, 18.f));
	if (UCanvasPanelSlot* RS = Root->AddChildToCanvas(RoundCard))
	{
		RS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		RS->SetAlignment(FVector2D(0.f, 0.f));
		RS->SetAutoSize(true);
		RS->SetOffsets(FMargin(28.f, 28.f, 0.f, 0.f));
	}
	UVerticalBox* RoundCol = WidgetTree->ConstructWidget<UVerticalBox>();
	RoundCard->SetContent(RoundCol);

	auto AddHairline = [&](UVerticalBox* Col)
	{
		UBorder* Sep = WidgetTree->ConstructWidget<UBorder>();
		Sep->SetBrushColor(Color::Border());
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(1.f); Box->SetContent(Sep);
		if (UVerticalBoxSlot* SS = Col->AddChildToVerticalBox(Box)) { SS->SetPadding(FMargin(0, 12.f, 0, 12.f)); }
	};

	// header: avatar + name/meta + Menu
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* AvBox = WidgetTree->ConstructWidget<USizeBox>();
		AvBox->SetWidthOverride(36.f); AvBox->SetHeightOverride(36.f);
		AvatarFill = WidgetTree->ConstructWidget<UBorder>();
		AvatarFill->SetBrush(RoundedBrush(Color::Accent(), 999.f));
		AvatarFill->SetHorizontalAlignment(HAlign_Center);
		AvatarFill->SetVerticalAlignment(VAlign_Center);
		AvatarText = WidgetTree->ConstructWidget<UTextBlock>();
		AvatarText->SetText(FText::FromString(TEXT("?")));
		AvatarText->SetFont(Display(15, FName(TEXT("Bold"))));
		AvatarText->SetColorAndOpacity(FSlateColor(Color::AccentInk()));
		AvatarText->SetJustification(ETextJustify::Center);
		AvatarFill->SetContent(AvatarText);
		AvBox->SetContent(AvatarFill);
		if (UHorizontalBoxSlot* AS = Head->AddChildToHorizontalBox(AvBox)) { AS->SetVerticalAlignment(VAlign_Center); AS->SetPadding(FMargin(0, 0, 11.f, 0)); }

		UVerticalBox* NameCol = WidgetTree->ConstructWidget<UVerticalBox>();
		NameText = WidgetTree->ConstructWidget<UTextBlock>();
		NameText->SetText(FText::FromString(TEXT("Player")));
		{ FSlateFontInfo F = Display(16, FName(TEXT("SemiBold"))); F.LetterSpacing = 30; NameText->SetFont(F); }
		NameText->SetColorAndOpacity(FSlateColor(Color::Text()));
		NameCol->AddChildToVerticalBox(NameText);
		MetaText = WidgetTree->ConstructWidget<UTextBlock>();
		MetaText->SetText(FText::FromString(TEXT("")));
		{ FSlateFontInfo F = Mono(11); F.LetterSpacing = 40; MetaText->SetFont(F); }
		MetaText->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
		if (UVerticalBoxSlot* MS = NameCol->AddChildToVerticalBox(MetaText)) { MS->SetPadding(FMargin(0, 4.f, 0, 0)); }
		if (UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(NameCol)) { NS->SetVerticalAlignment(VAlign_Center); }

		if (UHorizontalBoxSlot* SpS = Head->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())) { SpS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

		UButton* MenuBtn = MakeGhostButton(WidgetTree, TEXT("Menu"));
		MenuBtn->OnClicked.AddDynamic(this, &URoundHud::HandleMenuClicked);
		if (UHorizontalBoxSlot* MBS = Head->AddChildToHorizontalBox(MenuBtn)) { MBS->SetVerticalAlignment(VAlign_Center); MBS->SetPadding(FMargin(16.f, 0, 0, 0)); }

		RoundCol->AddChildToVerticalBox(Head);
	}

	AddHairline(RoundCol);

	// hole-stat row
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

		auto AddVLine = [&]()
		{
			UBorder* V = WidgetTree->ConstructWidget<UBorder>();
			V->SetBrushColor(Color::Border());
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(1.f); Box->SetHeightOverride(40.f); Box->SetContent(V);
			if (UHorizontalBoxSlot* VS = Row->AddChildToHorizontalBox(Box)) { VS->SetVerticalAlignment(VAlign_Bottom); VS->SetPadding(FMargin(14.f, 0)); }
		};
		auto AddStat = [&](const TCHAR* Label, bool bAccent) -> UTextBlock*
		{
			UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
			Col->AddChildToVerticalBox(MakeEyebrow(WidgetTree, Label));
			UTextBlock* Val = WidgetTree->ConstructWidget<UTextBlock>();
			Val->SetText(FText::FromString(TEXT("-")));
			Val->SetFont(Mono(17));
			Val->SetColorAndOpacity(FSlateColor(bAccent ? Color::Accent() : Color::Text()));
			if (UVerticalBoxSlot* VS = Col->AddChildToVerticalBox(Val)) { VS->SetPadding(FMargin(0, 3.f, 0, 0)); }
			if (UHorizontalBoxSlot* CS = Row->AddChildToHorizontalBox(Col)) { CS->SetVerticalAlignment(VAlign_Bottom); CS->SetPadding(FMargin(0, 0, 16.f, 0)); }
			return Val;
		};

		// HOLE (big)
		UVerticalBox* HoleCol = WidgetTree->ConstructWidget<UVerticalBox>();
		HoleCol->AddChildToVerticalBox(MakeEyebrow(WidgetTree, TEXT("Hole")));
		HoleNumText = WidgetTree->ConstructWidget<UTextBlock>();
		HoleNumText->SetText(FText::FromString(TEXT("--")));
		HoleNumText->SetFont(Display(40, FName(TEXT("Bold"))));
		HoleNumText->SetColorAndOpacity(FSlateColor(Color::Text()));
		HoleCol->AddChildToVerticalBox(HoleNumText);
		if (UHorizontalBoxSlot* HCS = Row->AddChildToHorizontalBox(HoleCol)) { HCS->SetVerticalAlignment(VAlign_Bottom); }

		AddVLine();
		ParText     = AddStat(TEXT("Par"), false);
		HoleYdsText = AddStat(TEXT("Hole yds"), false);
		AddVLine();
		ShotText  = AddStat(TEXT("Shot"), true);
		ToPinText = AddStat(TEXT("To pin"), true);

		RoundCol->AddChildToVerticalBox(Row);
	}

	AddHairline(RoundCol);

	// conditions strip: wind/temp (seam "--") + sky/time (real)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto AddChip = [&](const FString& InitialVal, const TCHAR* Sub) -> UTextBlock*
		{
			UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
			USizeBox* IcBox = WidgetTree->ConstructWidget<USizeBox>();
			IcBox->SetWidthOverride(26.f); IcBox->SetHeightOverride(26.f);
			UBorder* Ic = WidgetTree->ConstructWidget<UBorder>();
			Ic->SetBrush(RoundedBrush(Color::Surface(), Radius::Sm));   // icon placeholder (GOL-151)
			IcBox->SetContent(Ic);
			if (UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcBox)) { IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0, 0, 8.f, 0)); }

			UVerticalBox* TV = WidgetTree->ConstructWidget<UVerticalBox>();
			UTextBlock* Val = WidgetTree->ConstructWidget<UTextBlock>();
			Val->SetText(FText::FromString(InitialVal));
			Val->SetFont(Mono(13));
			Val->SetColorAndOpacity(FSlateColor(Color::Text()));
			TV->AddChildToVerticalBox(Val);
			UTextBlock* SubT = WidgetTree->ConstructWidget<UTextBlock>();
			SubT->SetText(FText::FromString(FString(Sub).ToUpper()));
			{ FSlateFontInfo F = Mono(9); F.LetterSpacing = 100; SubT->SetFont(F); }
			SubT->SetColorAndOpacity(FSlateColor(Color::TextFaint()));
			if (UVerticalBoxSlot* SS = TV->AddChildToVerticalBox(SubT)) { SS->SetPadding(FMargin(0, 3.f, 0, 0)); }
			if (UHorizontalBoxSlot* TS = Chip->AddChildToHorizontalBox(TV)) { TS->SetVerticalAlignment(VAlign_Center); }

			if (UHorizontalBoxSlot* CS = Row->AddChildToHorizontalBox(Chip)) { CS->SetPadding(FMargin(0, 0, 16.f, 0)); }
			return Val;
		};
		AddChip(TEXT("—"), TEXT("Wind"));    // seam (GOL-154)
		AddChip(TEXT("—"), TEXT("Temp"));    // seam (GOL-154)
		SkyValText  = AddChip(TEXT("—"), TEXT("Sky"));
		TimeValText = AddChip(TEXT("—"), TEXT("Time"));
		RoundCol->AddChildToVerticalBox(Row);
	}

	// ───────────────── hole-map card (top-right) ─────────────────
	UBorder* MapCard = MakeGlassPanel(WidgetTree);
	MapCard->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* MS = Root->AddChildToCanvas(MapCard))
	{
		MS->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		MS->SetAlignment(FVector2D(1.f, 0.f));
		MS->SetAutoSize(true);
		MS->SetOffsets(FMargin(0.f, 28.f, 28.f, 0.f));
	}
	{
		USizeBox* MapBox = WidgetTree->ConstructWidget<USizeBox>();
		MapBox->SetWidthOverride(248.f);
		MapCard->SetContent(MapBox);
		UVerticalBox* MapCol = WidgetTree->ConstructWidget<UVerticalBox>();
		MapBox->SetContent(MapCol);

		// image area + pin tag
		USizeBox* ImgBox = WidgetTree->ConstructWidget<USizeBox>();
		ImgBox->SetHeightOverride(150.f);
		UOverlay* ImgOverlay = WidgetTree->ConstructWidget<UOverlay>();
		ImgBox->SetContent(ImgOverlay);
		UBorder* ImgFill = WidgetTree->ConstructWidget<UBorder>();
		ImgFill->SetBrush(RoundedBrush(Color::Surface2(), Radius::Sm));   // flyover placeholder
		if (UOverlaySlot* IFS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(ImgFill))) { IFS->SetHorizontalAlignment(HAlign_Fill); IFS->SetVerticalAlignment(VAlign_Fill); }
		UBorder* PinTag = WidgetTree->ConstructWidget<UBorder>();
		PinTag->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0.45f), Radius::Sm, Color::BorderStrong(), 1.f));
		PinTag->SetPadding(FMargin(8.f, 4.f));
		MapPinText = WidgetTree->ConstructWidget<UTextBlock>();
		MapPinText->SetText(FText::FromString(TEXT("PIN — YD")));
		{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 80; MapPinText->SetFont(F); }
		MapPinText->SetColorAndOpacity(FSlateColor(Color::Text()));
		PinTag->SetContent(MapPinText);
		if (UOverlaySlot* PTS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(PinTag))) { PTS->SetHorizontalAlignment(HAlign_Right); PTS->SetVerticalAlignment(VAlign_Top); PTS->SetPadding(FMargin(0, 11.f, 11.f, 0)); }
		MapCol->AddChildToVerticalBox(ImgBox);

		// footer
		UBorder* Foot = WidgetTree->ConstructWidget<UBorder>();
		Foot->SetBrushColor(FLinearColor(0, 0, 0, 0));
		Foot->SetPadding(FMargin(14.f, 11.f));
		UHorizontalBox* FootRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		Foot->SetContent(FootRow);
		MapTitleText = WidgetTree->ConstructWidget<UTextBlock>();
		MapTitleText->SetText(FText::FromString(TEXT("Hole —")));
		{ FSlateFontInfo F = Display(14, FName(TEXT("SemiBold"))); F.LetterSpacing = 30; MapTitleText->SetFont(F); }
		MapTitleText->SetColorAndOpacity(FSlateColor(Color::Text()));
		if (UHorizontalBoxSlot* MTS = FootRow->AddChildToHorizontalBox(MapTitleText)) { MTS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MTS->SetVerticalAlignment(VAlign_Center); }
		MapYdsText = WidgetTree->ConstructWidget<UTextBlock>();
		MapYdsText->SetText(FText::FromString(TEXT("— yd")));
		MapYdsText->SetFont(Mono(12));
		MapYdsText->SetColorAndOpacity(FSlateColor(Color::Accent()));
		if (UHorizontalBoxSlot* MYS = FootRow->AddChildToHorizontalBox(MapYdsText)) { MYS->SetVerticalAlignment(VAlign_Center); }
		MapCol->AddChildToVerticalBox(Foot);
	}
}

void URoundHud::SetData(const FRoundHudData& Data)
{
	if (AvatarText) { AvatarText->SetText(FText::FromString(HudInitials(Data.PlayerName))); }
	if (NameText)   { NameText->SetText(FText::FromString(Data.PlayerName.IsEmpty() ? TEXT("Player") : Data.PlayerName)); }
	if (MetaText)
	{
		MetaText->SetText(FText::FromString(FString::Printf(TEXT("%s THRU %d · HCP %d"),
			*ScoreVsParText(Data.ScoreVsPar), Data.HolesThru, Data.Handicap)));
	}
	if (HoleNumText) { HoleNumText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Data.HoleNum))); }
	if (ParText)     { ParText->SetText(FText::FromString(FString::FromInt(Data.Par))); }
	if (HoleYdsText) { HoleYdsText->SetText(FText::FromString(FString::FromInt(Data.HoleYds))); }
	if (ShotText)    { ShotText->SetText(FText::FromString(FString::FromInt(Data.Shot))); }
	if (ToPinText)   { ToPinText->SetText(FText::FromString(FString::Printf(TEXT("%d yd"), Data.ToPinYd))); }
	if (SkyValText)  { SkyValText->SetText(FText::FromString(Data.SkyName.IsEmpty() ? TEXT("—") : Data.SkyName)); }
	if (TimeValText) { TimeValText->SetText(FText::FromString(Data.TimeName.IsEmpty() ? TEXT("—") : Data.TimeName)); }
	if (MapPinText)  { MapPinText->SetText(FText::FromString(FString::Printf(TEXT("PIN %d YD"), Data.ToPinYd))); }
	if (MapTitleText){ MapTitleText->SetText(FText::FromString(FString::Printf(TEXT("Hole %d · Par %d"), Data.HoleNum, Data.Par))); }
	if (MapYdsText)  { MapYdsText->SetText(FText::FromString(FString::Printf(TEXT("%d yd"), Data.HoleYds))); }
}

void URoundHud::HandleMenuClicked()
{
	if (OnMenu) { OnMenu(); }
}
