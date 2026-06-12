// Hole minimap view (GOL-209, epic GOL-137) -- the drawable heart of the round HUD's hole-map
// card. Two tabs: HOLE draws the pipeline-baked courses/<id>/minimap.png rotated tee-up with
// ball / pin / aim-line / yard-arc overlays; GREEN draws the green outline with a slope heatmap
// + downhill break arrows from the per-hole trace grid. Mouse wheel zooms (anchor under cursor),
// left-click reports a world XY up through OnAimAt (dumb view: the widget never touches the
// PlayerController -- AGolfRangeHUD owns the aim, same pattern as RoundHud's OnMenu).
//
// This is the repo's first NativePaint widget: every drawn element goes through one
// GolfMap::FHoleMapProjection per tab (UI/HoleMapProjection.h), and the click inverse uses the
// same projection, so the map and the aim can never disagree.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "UI/HoleMapProjection.h"
#include "HoleMapView.generated.h"

class UTexture2D;

enum class EHoleMapTab : uint8 { Hole = 0, Green = 1 };

/** Per-hole payload, built HUD-side once per (course, hole) and pushed via SetStaticData. */
struct FHoleMapStaticData
{
	FVector2D TeeCm = FVector2D::ZeroVector;     // world XY cm
	FVector2D PinCm = FVector2D::ZeroVector;
	FVector2D GreenCm = FVector2D::ZeroVector;   // green center (framing fallback)
	UTexture2D* MinimapTexture = nullptr;        // null = flat fallback fill (markers still draw)
	TArray<FVector2D> GreenVertsCm;              // outline; empty = GREEN tab has no outline
	GolfMap::FGreenSlopeGrid Slope;              // empty = no heatmap/arrows
};

UCLASS()
class GOLFSIM_API UHoleMapView : public UUserWidget
{
	GENERATED_BODY()

public:
	/** New hole: rebuilds both framings and resets the user zoom. */
	void SetStaticData(const FHoleMapStaticData& Data);

	/** Per-tick live state (ball world XY cm + aim yaw in UE degrees). */
	void SetLive(const FVector2D& BallWorldCm, float InAimYawDeg);

	void SetTab(EHoleMapTab Tab) { ActiveTab = Tab; }
	EHoleMapTab GetTab() const { return ActiveTab; }
	bool HasGreenData() const { return StaticData.GreenVertsCm.Num() >= 3; }

	/** The fixed view size the projections frame against (the card sets this once; it must match
	 *  the SizeBox the widget lives in). */
	void SetViewSize(const FVector2D& SizePx);

	TFunction<void(FVector2D /*WorldCm*/)> OnAimAt;   // left-click -> world XY (either tab)

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void RebuildFramings();
	const GolfMap::FHoleMapProjection& ActiveProj() const
	{
		return ActiveTab == EHoleMapTab::Green ? GreenProj : HoleProj;
	}

	// paint helpers (const -- paint never mutates state)
	void PaintHoleTab(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintGreenTab(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintTexture(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const;
	void PaintMarkers(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId,
		const GolfMap::FHoleMapProjection& P) const;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapTexture;   // GC anchor for the transient texture

	// Member brushes: Slate draw elements can reference brush data beyond the paint call,
	// so none of these may be stack locals inside NativePaint.
	FSlateBrush MapBrush;        // backed by MapTexture
	FSlateBrush FallbackBrush;   // the old flat placeholder fill (no minimap.png)
	FSlateBrush WhiteBrush;      // tintable solid for heat cells
	FSlateBrush PinDotBrush;     // accent dot at the exact pin point
	FSlateBrush BallDotBrush;    // white ball dot
	FSlateBrush BallRimBrush;    // dark rim under the ball dot

	FHoleMapStaticData StaticData;
	GolfMap::FHoleMapProjection HoleProj, GreenProj;
	double BaseHolePxPerCm = 0.0, BaseGreenPxPerCm = 0.0;   // zoom-out floor per tab (8x ceiling)

	FVector2D ViewSizePx = FVector2D(280.0, 280.0);
	EHoleMapTab ActiveTab = EHoleMapTab::Hole;
	FVector2D BallCm = FVector2D::ZeroVector;
	float AimYawDeg = 0.f;
};
