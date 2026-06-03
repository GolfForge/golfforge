#include "UI/ToggleSwitch.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"

void UToggleSwitch::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	using namespace GolfUI;

	// Root is a transparent button so the whole pill is one click target.
	UButton* Root = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ToggleRoot"));
	{
		FButtonStyle S;
		const FSlateBrush Clear = RoundedBrush(FLinearColor(0, 0, 0, 0), 999.f);
		S.SetNormal(Clear); S.SetHovered(Clear); S.SetPressed(Clear); S.SetDisabled(Clear);
		S.SetNormalPadding(FMargin(0)); S.SetPressedPadding(FMargin(0));
		Root->SetStyle(S);
	}
	Root->OnClicked.AddDynamic(this, &UToggleSwitch::HandleClicked);
	WidgetTree->RootWidget = Root;

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(46.f);
	Box->SetHeightOverride(26.f);
	Root->SetContent(Box);

	UOverlay* Over = WidgetTree->ConstructWidget<UOverlay>();
	Box->SetContent(Over);

	TrackBg = WidgetTree->ConstructWidget<UBorder>();
	if (UOverlaySlot* TS = Cast<UOverlaySlot>(Over->AddChildToOverlay(TrackBg)))
	{
		TS->SetHorizontalAlignment(HAlign_Fill);
		TS->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* KnobBox = WidgetTree->ConstructWidget<USizeBox>();
	KnobBox->SetWidthOverride(20.f);
	KnobBox->SetHeightOverride(20.f);
	Knob = WidgetTree->ConstructWidget<UBorder>();
	KnobBox->SetContent(Knob);
	KnobSlot = Cast<UOverlaySlot>(Over->AddChildToOverlay(KnobBox));
	if (KnobSlot)
	{
		KnobSlot->SetVerticalAlignment(VAlign_Center);
		KnobSlot->SetPadding(FMargin(3.f, 0.f));
	}

	RefreshVisual();
}

void UToggleSwitch::RefreshVisual()
{
	using namespace GolfUI;
	if (TrackBg)
	{
		TrackBg->SetBrush(RoundedBrush(bIsOn ? Color::Accent() : Color::Surface3(), 999.f, Color::Border(), 1.f));
	}
	if (Knob)
	{
		Knob->SetBrush(RoundedBrush(bIsOn ? Color::AccentInk() : Color::TextDim(), 999.f));
	}
	// Slide the knob: left when off, right when on (its overlay slot's horizontal alignment).
	if (KnobSlot)
	{
		KnobSlot->SetHorizontalAlignment(bIsOn ? HAlign_Right : HAlign_Left);
	}
}

void UToggleSwitch::HandleClicked()
{
	SetOn(!bIsOn, /*bBroadcast*/ true);
}

void UToggleSwitch::SetOn(bool bOn, bool bBroadcast)
{
	bIsOn = bOn;
	RefreshVisual();
	if (bBroadcast && OnChanged) { OnChanged(bOn); }
}

void UToggleSwitch::SetControlEnabled(bool bEnabled)
{
	SetIsEnabled(bEnabled);
	SetRenderOpacity(bEnabled ? 1.f : 0.5f);
}
