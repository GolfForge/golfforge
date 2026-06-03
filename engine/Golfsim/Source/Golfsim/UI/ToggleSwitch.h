// Toggle switch (GOL-140, epic GOL-137). A reusable on/off pill (track + sliding knob), on = accent.
// Pure-C++ UUserWidget built from GolfUITheme atoms. Reused by the settings modal (+ the wizard later).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ToggleSwitch.generated.h"

class UBorder;
class UOverlaySlot;

UCLASS()
class GOLFSIM_API UToggleSwitch : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetOn(bool bOn, bool bBroadcast = false);
	bool IsOn() const { return bIsOn; }
	void SetControlEnabled(bool bEnabled);   // dimmed + non-interactive ("Coming soon")

	TFunction<void(bool)> OnChanged;

protected:
	virtual void NativeOnInitialized() override;
	UFUNCTION() void HandleClicked();

private:
	void RefreshVisual();

	UPROPERTY(Transient) TObjectPtr<UBorder> TrackBg;
	UPROPERTY(Transient) TObjectPtr<UBorder> Knob;
	UPROPERTY(Transient) TObjectPtr<UOverlaySlot> KnobSlot;   // slide left/right via its horizontal alignment

	bool bIsOn = false;
};
