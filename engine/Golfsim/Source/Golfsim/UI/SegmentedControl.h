// Segmented control (GOL-140, epic GOL-137). A reusable pill of mutually-exclusive options (e.g.
// Low/Medium/High/Epic, Windowed/Borderless/Fullscreen) — the workhorse atom for the settings modal
// and the round-setup wizard (GOL-142/143). Pure-C++ UUserWidget built from GolfUITheme atoms; the
// selected option is accent-filled, others are ghost. Dumb view: reports the pick via OnChanged.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SegmentedControl.generated.h"

class UHorizontalBox;
class UButton;
class UTextBlock;
class UBorder;

UCLASS()
class GOLFSIM_API USegmentedControl : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replace the options (rebuilds the track). Selection resets to 0 (no broadcast). Optional
	 *  SubLabels render as a small dim suffix on each option (e.g. "Full 18  par 72"); pass {} for none. */
	void SetOptions(const TArray<FString>& Options, const TArray<FString>& SubLabels = TArray<FString>());
	/** Select an option. bBroadcast = also fire OnChanged (default false for programmatic seeding). */
	void SetSelectedIndex(int32 Index, bool bBroadcast = false);
	int32 GetSelectedIndex() const { return SelectedIndex; }
	/** Per-option disable: a single option reads dim + non-interactive while the rest stay live
	 *  (e.g. Hole-out's "Gimmes on"). Independent of SetControlEnabled (which dims the whole control). */
	void SetOptionDisabled(int32 Index, bool bDisabled);
	/** Disabled = dimmed + non-interactive (the "Coming soon" look). */
	void SetControlEnabled(bool bEnabled);

	TFunction<void(int32)> OnChanged;

protected:
	virtual void NativeOnInitialized() override;
	UFUNCTION() void HandleOptionClicked();   // bound to every option; resolves which via IsHovered()

private:
	void Rebuild();
	void RefreshVisual();

	UPROPERTY(Transient) TObjectPtr<UBorder> Track;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OptionButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> OptionTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> OptionSubTexts;   // dim suffix per option (may be null)

	TArray<FString> OptionLabels;
	TArray<FString> OptionSubLabels;
	TArray<bool> OptionDisabled;
	int32 SelectedIndex = 0;
	bool bControlEnabled = true;
};
