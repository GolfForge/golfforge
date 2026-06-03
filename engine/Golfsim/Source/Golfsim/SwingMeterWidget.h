// Game-mode swing meter (GOL-67, glass reskin GOL-146). Pure-C++ UUserWidget anchored bottom-center,
// matching Build/handoff/screens/06-hud-gamemode.png: a glass panel with a header (eyebrow + green
// "GAME MODE - KEYBOARD" pill), a Power fill bar + live percent, an Accuracy track carrying an
// always-visible green sweet zone and a moving marker, per-bar value readouts, and a prompt line.
//
// The HUD pushes the live Power/Accuracy values each Tick (SetMeters) and drives the lock/result
// feedback via OnPowerLocked / OnAccuracyResult / ResetMeter. All swing *logic* lives in
// GolfsimKeyboardSwing:: -- this widget never computes a shot; it only paints what it's told.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SwingMeterWidget.generated.h"

class UTextBlock;
class UBorder;

UCLASS()
class GOLFSIM_API USwingMeterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Live bar values (called from HUD::Tick). Each in [0,1]. Updates the power fill + percent
	    readout and slides the accuracy marker. */
	void SetMeters(double Power, double Accuracy);

	/** Position the always-visible sweet zone on the accuracy track (called once when the widget
	    mounts). Low/High in [0,1]. */
	void SetSweetSpot(double Low, double High);

	/** The prompt line under the bars. Plain text; the caller owns the copy. The leading [Space]
	    keycap chip stays visible unless a result is showing (see OnAccuracyResult / ResetMeter). */
	void SetHintText(const FString& Hint);

	/** Press 2 -- power locked: recolor the power value to the accent (locked) look. */
	void OnPowerLocked();

	/** Press 3 -- accuracy locked + shot resolved. bInZone -> "Pure" (accent); else a qualitative
	    "Push R" / "Pull L" (amber) from which side of the zone midpoint the marker stopped. Also
	    hides the prompt keycap and tints the prompt line. No yardage is fabricated -- the real
	    offline number comes from the physics sim and shows in the launch-monitor readout. */
	void OnAccuracyResult(bool bInZone, double Accuracy);

	/** Back to idle: dashes for values, marker home, neutral colors, keycap shown. */
	void ResetMeter();

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildTree();
	void PlaceMarker(double Accuracy);   // slide the marker to Accuracy within the track

	UPROPERTY(Transient) TObjectPtr<UBorder> PowerFill;   // width-driven fill on the power track
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PowerValue;
	UPROPERTY(Transient) TObjectPtr<UBorder> SweetSpotBand;    // static green zone on the accuracy track
	UPROPERTY(Transient) TObjectPtr<UBorder> AccuracyMarker;   // moving line on the accuracy track
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AccuracyValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UBorder> PromptKey;        // [Space] keycap chip beside the prompt

	double SweetLow = 0.80;
	double SweetHigh = 0.90;
};
