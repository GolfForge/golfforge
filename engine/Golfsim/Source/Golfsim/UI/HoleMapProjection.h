// Minimap projection math (GOL-209). Pure C++ -- no UObject, no UWorld, no Slate -- so the
// world<->widget transform, the per-hole framing, and the green slope grid are all
// unit-testable headlessly (Tests/HoleMapProjectionTests.cpp). The widget (UI/HoleMapView)
// consumes these; nothing here knows about textures or paint.
//
// Frames: world is UE world XY in cm (the courses/<id>/ rasters cover [-HalfXYCm, +HalfXYCm],
// see FCourseSurfaceSampler::HalfXYCm). Widget is local pixels, +y down, origin top-left.
// The projection is a similarity transform: WidgetPx = ViewSizePx/2 + PxPerCm * R(Theta) *
// (WorldCm - CenterWorldCm). Hole views pick Theta so the tee->pin axis points up (tee at
// the bottom -- yardage-book orientation).

#pragma once

#include "CoreMinimal.h"

namespace GolfMap
{
	/** World-cm <-> widget-px similarity transform (uniform scale + rotation + translation). */
	struct GOLFSIM_API FHoleMapProjection
	{
		FVector2D CenterWorldCm = FVector2D::ZeroVector;   // world point at the view center
		double    ThetaRad      = 0.0;                     // rotation applied world -> widget
		double    PxPerCm       = 0.0;                     // scale; 0 = invalid/unset
		FVector2D ViewSizePx    = FVector2D::ZeroVector;

		bool IsValid() const { return PxPerCm > 0.0 && ViewSizePx.X > 0.0 && ViewSizePx.Y > 0.0; }

		FVector2D WorldToWidget(const FVector2D& WorldCm) const;
		FVector2D WidgetToWorld(const FVector2D& WidgetPx) const;

		/** The full affine as WidgetPx = [A B; C D] * WorldCm + (Tx, Ty) (column convention).
		 *  The widget builds its Slate render transform from this so the drawn texture and the
		 *  click inverse can never disagree. */
		void GetAffine(double& A, double& B, double& C, double& D, double& Tx, double& Ty) const;

		/** Anchor-preserving zoom: scales PxPerCm by Factor (clamped to [MinPxPerCm, MaxPxPerCm])
		 *  and re-centers so the world point under AnchorPx stays under AnchorPx. */
		void ZoomAt(double Factor, const FVector2D& AnchorPx, double MinPxPerCm, double MaxPxPerCm);
	};

	/** Frame a whole hole: tee->pin axis up (tee bottom-center, pin top-center), the axis padded
	 *  by MarginFrac each end and clamped to at least MinSpanCm (so par-3s don't over-zoom;
	 *  default 160 yd). Degenerate tee==pin (< 1 m apart) falls back to north-up (Theta = 0). */
	GOLFSIM_API FHoleMapProjection MakeHoleFraming(const FVector2D& TeeCm, const FVector2D& PinCm,
		const FVector2D& ViewSizePx, double MarginFrac = 0.15, double MinSpanCm = 14630.0);

	/** Frame a green: same Theta as the hole view (consistent orientation), fit all outline verts
	 *  + margin. Empty Verts falls back to a 40 m box around FallbackCenterCm. */
	GOLFSIM_API FHoleMapProjection MakeGreenFraming(const TArray<FVector2D>& VertsCm,
		const FVector2D& FallbackCenterCm, double ThetaRad, const FVector2D& ViewSizePx,
		double MarginFrac = 0.20);

	/** Corner-height sample that failed (trace miss): cells touching one are zeroed + excluded. */
	GOLFSIM_API extern const double InvalidHeightCm;

	/** Slope grid over a green for the break view. Cells are CellCm squares; cell (i,j) center =
	 *  OriginCm + (i, j) * CellCm, row-major indexing (j * NX + i). Corner heights are sampled by
	 *  the caller (landscape traces) at OriginCm + (i - 0.5, j - 0.5) * CellCm for i in [0, NX],
	 *  j in [0, NY]; ComputeSlopeGrid derives per-cell slope + downhill direction from them. */
	struct GOLFSIM_API FGreenSlopeGrid
	{
		FVector2D OriginCm = FVector2D::ZeroVector;
		double    CellCm   = 100.0;
		int32     NX = 0, NY = 0;
		TArray<float>     SlopePct;       // NX*NY, 100 * rise/run
		TArray<FVector2D> FallDirWorld;   // unit downhill world XY; ZeroVector where flat
		TArray<bool>      bInGreen;       // set by the caller (point-in-polygon); cleared on bad cells

		bool IsEmpty() const { return NX <= 0 || NY <= 0; }
	};

	/** Derive SlopePct + FallDirWorld from corner heights ((NX+1)*(NY+1), row-major). Cells with
	 *  any InvalidHeightCm corner get slope 0 and bInGreen cleared. SlopePct/FallDirWorld are
	 *  (re)sized here; bInGreen must already be NX*NY (it is preserved otherwise). */
	GOLFSIM_API void ComputeSlopeGrid(const TArray<double>& CornerHeightsCm, FGreenSlopeGrid& InOut);
}
