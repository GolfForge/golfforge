// In-round HUD top panels (GOL-144, epic GOL-137) -- the glass round panel (top-left) + hole-map card
// (top-right) over the live viewport, replacing the old canvas-drawn round readout. Procedural
// UUserWidget built from GolfUITheme; SelfHitTestInvisible so only its Menu button takes input and the
// swing meter / control bar below stay interactive. Dumb view: the HUD pushes live data each frame via
// SetData and the Menu button reports up through OnMenu (the leave path; GOL-147 adds the confirm guard).
//
// Conditions strip: sky + time-of-day are bound to the real env director; wind + temp are seams shown as
// "--" until GOL-154 adds a weather source.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoundHud.generated.h"

class UButton;
class UBorder;
class UTextBlock;

// Live round/HUD values the HUD computes each frame and pushes to the widget.
struct FRoundHudData
{
	FString PlayerName;
	int32 Handicap = 0;
	int32 ScoreVsPar = 0;     // over completed holes
	int32 HolesThru = 0;      // completed holes
	int32 HoleNum = 0;        // current hole Ref
	int32 Par = 0;
	int32 HoleYds = 0;        // tee -> green
	int32 Shot = 1;           // strokes this hole + 1
	int32 ToPinYd = 0;        // live pawn -> pin
	FString SkyName;          // real (Clear/Cloudy/Overcast)
	FString TimeName;         // real (Dawn/Morning/...)
};

UCLASS()
class GOLFSIM_API URoundHud : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetData(const FRoundHudData& Data);

	TFunction<void()> OnMenu;   // Menu button -> HUD leave-to-menu path

protected:
	virtual void NativeOnInitialized() override;
	UFUNCTION() void HandleMenuClicked();

private:
	void BuildTree();

	// round panel
	UPROPERTY(Transient) TObjectPtr<UBorder>    AvatarFill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AvatarText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MetaText;     // "-2 THRU 6 · HCP 8"
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HoleNumText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ParText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HoleYdsText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ShotText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToPinText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SkyValText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeValText;

	// hole-map card
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapPinText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapTitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MapYdsText;
};
