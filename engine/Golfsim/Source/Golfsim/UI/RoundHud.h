// In-round HUD top panels (GOL-144, epic GOL-137) -- the glass round panel (top-left) + hole-map
// card (top-right) over the live viewport, replacing the old canvas-drawn round readout. Procedural
// UUserWidget built from GolfUITheme; SelfHitTestInvisible so only its interactive children (Menu
// button, hole-map card) take input and the swing meter / control bar below stay interactive. Dumb
// view: the HUD pushes live data each frame via SetData and reports up through TFunctions (OnMenu
// for the leave path; OnAimAt / OnMapExpandedChanged / OnMapTabChanged for the GOL-209 minimap).
//
// GOL-209: the hole-map card is a real minimap (UI/HoleMapView -- baked basemap, zoom, click-to-aim,
// HOLE/GREEN tabs) and collapses to a small chip; expanded/tab state persists via GolfDisplaySettings.
//
// Conditions strip: sky + time-of-day are bound to the real env director; wind + temp are seams shown
// as "--" until GOL-154 adds a weather source.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoundHud.generated.h"

class UButton;
class UBorder;
class UTextBlock;
class USegmentedControl;
class UHoleMapView;
struct FHoleMapStaticData;

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
	FVector2D BallWorldCm = FVector2D::ZeroVector;   // GOL-209: minimap ball dot (pawn XY, world cm)
	float AimYawDeg = 0.f;                           // GOL-209: minimap aim line (control-rotation yaw)
};

UCLASS()
class GOLFSIM_API URoundHud : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetData(const FRoundHudData& Data);

	// --- GOL-209 hole-map card ---
	/** Per-hole payload (tee/pin/green, basemap texture, green outline + slope grid). Disables the
	 *  GREEN tab when there's no green outline for the hole. */
	void SetHoleMapStatic(const FHoleMapStaticData& Data);
	/** Visual swap card <-> chip (no broadcast -- use for seeding from persisted settings). */
	void SetMapExpanded(bool bExpanded);
	/** Swap card <-> chip and report through OnMapExpandedChanged (hotkey / chip click path). */
	void ToggleMapExpanded();
	/** Select HOLE (0) / GREEN (1) (no broadcast -- seeding). Falls back to HOLE if GREEN is disabled. */
	void SetMapTab(int32 Index);

	TFunction<void()> OnMenu;                      // Menu button -> HUD leave-to-menu path
	TFunction<void(FVector2D)> OnAimAt;            // minimap click -> world XY cm (HUD owns the aim)
	TFunction<void(bool)> OnMapExpandedChanged;    // HUD persists
	TFunction<void(int32)> OnMapTabChanged;        // HUD persists

protected:
	virtual void NativeOnInitialized() override;
	UFUNCTION() void HandleMenuClicked();
	UFUNCTION() void HandleMapChipClicked();
	UFUNCTION() void HandleMapCollapseClicked();

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

	// hole-map card (GOL-209)
	UPROPERTY(Transient) TObjectPtr<UBorder>           MapCard;     // expanded card
	UPROPERTY(Transient) TObjectPtr<UButton>           MapChip;     // collapsed chip
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        MapChipText; // "H07 · 412 YD"
	UPROPERTY(Transient) TObjectPtr<USegmentedControl> MapTabs;     // HOLE / GREEN
	UPROPERTY(Transient) TObjectPtr<UHoleMapView>      MapView;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        MapPinText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        MapTitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>        MapYdsText;

	bool bMapExpanded = false;
};
