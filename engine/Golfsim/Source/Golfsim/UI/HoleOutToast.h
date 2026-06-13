// Hole-out celebration toast (GOL-203). Pure C++ UUserWidget (no WBP, same idiom as
// UManualShotDialog): a centered GolfUITheme glass card that pops in when a putt drops,
// holds, and fades out. Replaces the GOL-199 file-static DrawHUD text banner. The widget
// animates itself from NativeTick (pop -> hold -> fade via render opacity/scale); the HUD
// just creates it once and calls Show().

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HoleOutToast.generated.h"

class UBorder;
class UTextBlock;

UCLASS()
class GOLFSIM_API UHoleOutToast : public UUserWidget
{
	GENERATED_BODY()

public:
	// Pop the toast: 1 putt -> "HOLED IT!", n putts -> "HOLED OUT". Re-showing mid-animation
	// restarts the timeline (back-to-back hole-outs read correctly).
	void Show(int32 Putts);

	// General form (round hole-outs, CTP putt-outs): explicit eyebrow / headline / detail.
	// Empty Detail collapses the detail line.
	void ShowText(const FString& Eyebrow, const FString& Title, const FString& Detail);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UBorder> Card;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EyebrowText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;

	// Timeline seconds since Show(); < 0 = idle (collapsed). Driven in NativeTick.
	float AnimSeconds = -1.f;
	static constexpr float PopSec  = 0.18f;   // scale/opacity pop-in
	static constexpr float HoldSec = 2.0f;    // fully visible
	static constexpr float FadeSec = 0.5f;    // fade-out
};
