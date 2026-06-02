// Game-mode swing meter (GOL-67). Pure-C++ UUserWidget anchored bottom-center. Two stacked
// horizontal bars (power on top, accuracy below) + an always-visible sweet-spot band overlay
// on the accuracy bar + a hint line ("Space x3: Power -> Accuracy -> Fire"). The HUD pushes
// the current Power / Accuracy values each Tick; the widget paints them.
//
// State-machine logic lives in GolfsimKeyboardSwing:: -- this widget is dumb.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SwingMeterWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;

UCLASS()
class GOLFSIM_API USwingMeterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Update the two bar values (called from HUD::Tick). Each value is in [0, 1]. */
	void SetMeters(double Power, double Accuracy);

	/** Update the displayed sweet-spot zone (called once from HUD when the widget mounts). */
	void SetSweetSpot(double Low, double High);

	/** Update the hint line ("Press Space to start" / "Lock power" / "Lock accuracy"). */
	void SetHintText(const FString& Hint);

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UProgressBar> PowerBar;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> AccuracyBar;
	UPROPERTY(Transient) TObjectPtr<UBorder> SweetSpotBand;   // sized + offset onto the accuracy bar
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;

	double SweetLow = 0.80;
	double SweetHigh = 0.90;
};
