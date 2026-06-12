#include "UI/RoundHud.h"
#include "UI/GolfUITheme.h"
#include "UI/HoleMapView.h"
#include "UI/SegmentedControl.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
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
		auto AddChip = [&](EIcon Glyph, const FString& InitialVal, const TCHAR* Sub) -> UTextBlock*
		{
			UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
			USizeBox* IcBox = WidgetTree->ConstructWidget<USizeBox>();
			IcBox->SetWidthOverride(26.f); IcBox->SetHeightOverride(26.f);
			UBorder* Ic = WidgetTree->ConstructWidget<UBorder>();
			Ic->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0), Radius::Sm));   // transparent; centers the Lucide glyph (GOL-151)
			Ic->SetHorizontalAlignment(HAlign_Center);
			Ic->SetVerticalAlignment(VAlign_Center);
			Ic->SetContent(MakeIcon(WidgetTree, Glyph, 18, Color::TextDim()));
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
		AddChip(EIcon::Wind, TEXT("—"), TEXT("Wind"));          // seam (GOL-154)
		AddChip(EIcon::Thermometer, TEXT("—"), TEXT("Temp"));   // seam (GOL-154)
		SkyValText  = AddChip(EIcon::Cloud, TEXT("—"), TEXT("Sky"));
		TimeValText = AddChip(EIcon::Clock, TEXT("—"), TEXT("Time"));
		RoundCol->AddChildToVerticalBox(Row);
	}

	// ───────────────── hole-map card (top-right, GOL-209 minimap) ─────────────────
	MapCard = MakeGlassPanel(WidgetTree);
	MapCard->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* MS = Root->AddChildToCanvas(MapCard))
	{
		MS->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		MS->SetAlignment(FVector2D(1.f, 0.f));
		MS->SetAutoSize(true);
		MS->SetOffsets(FMargin(0.f, 28.f, 28.f, 0.f));
	}
	{
		MapWidthBox = WidgetTree->ConstructWidget<USizeBox>();
		MapWidthBox->SetWidthOverride(280.f);   // wide enough that tabs + the +/- buttons never collide
		MapCard->SetContent(MapWidthBox);
		UVerticalBox* MapCol = WidgetTree->ConstructWidget<UVerticalBox>();
		MapWidthBox->SetContent(MapCol);

		// tabs row: HOLE / GREEN segmented control + enlarge/collapse buttons. The ghost-button
		// style bakes 18x10 label padding -- far too wide for single glyphs next to the tabs --
		// so tighten the pair down to icon-button size.
		auto TightenButton = [](UButton* B)
		{
			FButtonStyle Sty = B->GetStyle();
			Sty.SetNormalPadding(FMargin(9.f, 4.f));
			Sty.SetPressedPadding(FMargin(9.f, 4.f));
			B->SetStyle(Sty);
		};
		UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		MapTabs = CreateWidget<USegmentedControl>(this);
		MapTabs->SetOptions({ TEXT("HOLE"), TEXT("GREEN") });
		MapTabs->OnChanged = [this](int32 Idx)
		{
			if (MapView) { MapView->SetTab(static_cast<EHoleMapTab>(Idx)); }
		};
		if (UHorizontalBoxSlot* TS = TabRow->AddChildToHorizontalBox(MapTabs)) { TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center); }
		MapEnlargeBtn = MakeGhostButton(WidgetTree, TEXT("+"));
		TightenButton(MapEnlargeBtn);
		MapEnlargeBtn->OnClicked.AddDynamic(this, &URoundHud::HandleMapEnlargeClicked);
		if (UHorizontalBoxSlot* ES = TabRow->AddChildToHorizontalBox(MapEnlargeBtn)) { ES->SetVerticalAlignment(VAlign_Center); ES->SetPadding(FMargin(8.f, 0, 0, 0)); }
		UButton* CollapseBtn = MakeGhostButton(WidgetTree, TEXT("-"));
		TightenButton(CollapseBtn);
		CollapseBtn->OnClicked.AddDynamic(this, &URoundHud::HandleMapCollapseClicked);
		if (UHorizontalBoxSlot* CS = TabRow->AddChildToHorizontalBox(CollapseBtn)) { CS->SetVerticalAlignment(VAlign_Center); CS->SetPadding(FMargin(4.f, 0, 0, 0)); }
		if (UVerticalBoxSlot* TRS = MapCol->AddChildToVerticalBox(TabRow)) { TRS->SetPadding(FMargin(10.f, 10.f, 10.f, 8.f)); }

		// map area (square so the projection math frames against a fixed view) + pin tag
		MapImgBox = WidgetTree->ConstructWidget<USizeBox>();
		MapImgBox->SetHeightOverride(280.f);
		UOverlay* ImgOverlay = WidgetTree->ConstructWidget<UOverlay>();
		MapImgBox->SetContent(ImgOverlay);
		MapView = CreateWidget<UHoleMapView>(this);
		MapView->SetViewSize(FVector2D(278.0, 280.0));   // width minus the 1 px side insets
		MapView->OnAimAt = [this](FVector2D WorldCm) { if (OnAimAt) { OnAimAt(WorldCm); } };
		if (UOverlaySlot* MVS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(MapView))) { MVS->SetHorizontalAlignment(HAlign_Fill); MVS->SetVerticalAlignment(VAlign_Fill); }
		UBorder* PinTag = WidgetTree->ConstructWidget<UBorder>();
		PinTag->SetBrush(RoundedBrush(FLinearColor(0, 0, 0, 0.45f), Radius::Sm, Color::BorderStrong(), 1.f));
		PinTag->SetPadding(FMargin(8.f, 4.f));
		PinTag->SetVisibility(ESlateVisibility::HitTestInvisible);   // never block map clicks
		MapPinText = WidgetTree->ConstructWidget<UTextBlock>();
		MapPinText->SetText(FText::FromString(TEXT("PIN — YD")));
		{ FSlateFontInfo F = Mono(10); F.LetterSpacing = 80; MapPinText->SetFont(F); }
		MapPinText->SetColorAndOpacity(FSlateColor(Color::Text()));
		PinTag->SetContent(MapPinText);
		if (UOverlaySlot* PTS = Cast<UOverlaySlot>(ImgOverlay->AddChildToOverlay(PinTag))) { PTS->SetHorizontalAlignment(HAlign_Right); PTS->SetVerticalAlignment(VAlign_Top); PTS->SetPadding(FMargin(0, 11.f, 11.f, 0)); }
		// 1 px side inset: the card border (MakeGlassPanel) draws a 1 px hairline at its edge, and an
		// edge-to-edge map overdraws it -- reading a couple px wider than the rows above/below.
		if (UVerticalBoxSlot* IBS = MapCol->AddChildToVerticalBox(MapImgBox)) { IBS->SetPadding(FMargin(1.f, 0.f)); }

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

	// ───────────────── collapsed map chip (same top-right anchor) ─────────────────
	MapChip = WidgetTree->ConstructWidget<UButton>();
	StyleButton(MapChip, Color::GlassFill(), Radius::Lg, Color::Border(), 1.f);   // style brings 18x10 padding
	MapChip->OnClicked.AddDynamic(this, &URoundHud::HandleMapChipClicked);
	MapChipText = WidgetTree->ConstructWidget<UTextBlock>();
	MapChipText->SetText(FText::FromString(TEXT("H— · — YD")));
	{ FSlateFontInfo F = Mono(12); F.LetterSpacing = 60; MapChipText->SetFont(F); }
	MapChipText->SetColorAndOpacity(FSlateColor(Color::Text()));
	MapChip->AddChild(MapChipText);
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(MapChip))
	{
		CS->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		CS->SetAlignment(FVector2D(1.f, 0.f));
		CS->SetAutoSize(true);
		CS->SetOffsets(FMargin(0.f, 28.f, 28.f, 0.f));
	}

	SetMapSize(MapSize);   // default chip; HUD seeds the persisted state after create
}

void URoundHud::SetHoleMapStatic(const FHoleMapStaticData& Data)
{
	if (!MapView)
	{
		return;
	}
	MapView->SetStaticData(Data);
	const bool bHasGreen = MapView->HasGreenData();
	if (MapTabs)
	{
		MapTabs->SetOptionDisabled(1, !bHasGreen);
	}
	if (!bHasGreen && MapView->GetTab() == EHoleMapTab::Green)
	{
		SetMapTab(0);
	}
}

void URoundHud::SetMapSize(int32 Size)
{
	MapSize = FMath::Clamp(Size, 0, 2);
	const bool bExpanded = MapSize > 0;
	if (MapCard) { MapCard->SetVisibility(bExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (MapChip) { MapChip->SetVisibility(bExpanded ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
	if (!bExpanded)
	{
		return;
	}
	// Card (1) vs large (2). The large view exists to read greens/breaks; SetViewSize re-frames
	// the projections (and resets the wheel zoom) so the map fills the new area exactly.
	const float Px = (MapSize == 2) ? 480.f : 280.f;
	if (MapWidthBox) { MapWidthBox->SetWidthOverride(Px); }
	if (MapImgBox)   { MapImgBox->SetHeightOverride(Px); }
	if (MapView)     { MapView->SetViewSize(FVector2D(Px - 2.0, Px)); }   // width minus the 1 px side insets
	if (MapEnlargeBtn) { MapEnlargeBtn->SetVisibility(MapSize == 2 ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
}

void URoundHud::CycleMapSize()
{
	SetMapSize((MapSize + 1) % 3);
	if (OnMapSizeChanged)
	{
		OnMapSizeChanged(MapSize);
	}
}

void URoundHud::SetMapTab(int32 Index)
{
	// No HasGreenData gate here: the persisted tab is seeded BEFORE the first hole's data arrives.
	// SetHoleMapStatic forces HOLE when a hole genuinely has no green outline.
	Index = FMath::Clamp(Index, 0, 1);
	if (MapTabs) { MapTabs->SetSelectedIndex(Index, /*bBroadcast=*/false); }
	if (MapView) { MapView->SetTab(static_cast<EHoleMapTab>(Index)); }
}

void URoundHud::HandleMapChipClicked()
{
	SetMapSize(1);   // chip -> card
	if (OnMapSizeChanged) { OnMapSizeChanged(MapSize); }
}

void URoundHud::HandleMapCollapseClicked()
{
	SetMapSize(MapSize - 1);   // large -> card -> chip
	if (OnMapSizeChanged) { OnMapSizeChanged(MapSize); }
}

void URoundHud::HandleMapEnlargeClicked()
{
	SetMapSize(2);   // card -> large
	if (OnMapSizeChanged) { OnMapSizeChanged(MapSize); }
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
	if (MapChipText) { MapChipText->SetText(FText::FromString(FString::Printf(TEXT("H%02d · %d YD"), Data.HoleNum, Data.HoleYds))); }
	if (MapView)     { MapView->SetLive(Data.BallWorldCm, Data.AimYawDeg); }
}

void URoundHud::HandleMenuClicked()
{
	if (OnMenu) { OnMenu(); }
}
