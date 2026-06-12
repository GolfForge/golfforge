#include "UI/HoleMapProjection.h"

namespace GolfMap
{

const double InvalidHeightCm = TNumericLimits<double>::Max();

FVector2D FHoleMapProjection::WorldToWidget(const FVector2D& WorldCm) const
{
	const double C = FMath::Cos(ThetaRad), S = FMath::Sin(ThetaRad);
	const FVector2D D = WorldCm - CenterWorldCm;
	return FVector2D(
		ViewSizePx.X * 0.5 + PxPerCm * (C * D.X - S * D.Y),
		ViewSizePx.Y * 0.5 + PxPerCm * (S * D.X + C * D.Y));
}

FVector2D FHoleMapProjection::WidgetToWorld(const FVector2D& WidgetPx) const
{
	if (!IsValid())
	{
		return CenterWorldCm;
	}
	const double C = FMath::Cos(ThetaRad), S = FMath::Sin(ThetaRad);
	const FVector2D Q = (WidgetPx - ViewSizePx * 0.5) / PxPerCm;
	// Inverse rotation R(-Theta).
	return CenterWorldCm + FVector2D(C * Q.X + S * Q.Y, -S * Q.X + C * Q.Y);
}

void FHoleMapProjection::GetAffine(double& A, double& B, double& C, double& D, double& Tx, double& Ty) const
{
	const double Cos = FMath::Cos(ThetaRad), Sin = FMath::Sin(ThetaRad);
	A = PxPerCm * Cos;  B = -PxPerCm * Sin;
	C = PxPerCm * Sin;  D = PxPerCm * Cos;
	// Translation so that WorldToWidget(Center) == ViewSize/2.
	Tx = ViewSizePx.X * 0.5 - (A * CenterWorldCm.X + B * CenterWorldCm.Y);
	Ty = ViewSizePx.Y * 0.5 - (C * CenterWorldCm.X + D * CenterWorldCm.Y);
}

void FHoleMapProjection::ZoomAt(double Factor, const FVector2D& AnchorPx, double MinPxPerCm, double MaxPxPerCm)
{
	if (!IsValid())
	{
		return;
	}
	const FVector2D AnchorWorld = WidgetToWorld(AnchorPx);
	PxPerCm = FMath::Clamp(PxPerCm * Factor, MinPxPerCm, MaxPxPerCm);
	// Re-center so AnchorWorld maps back onto AnchorPx: solve WorldToWidget(AnchorWorld) == AnchorPx
	// for CenterWorldCm. With Q = (AnchorPx - ViewSize/2) / PxPerCm: Center = AnchorWorld - R(-Theta) * Q.
	const double C = FMath::Cos(ThetaRad), S = FMath::Sin(ThetaRad);
	const FVector2D Q = (AnchorPx - ViewSizePx * 0.5) / PxPerCm;
	CenterWorldCm = AnchorWorld - FVector2D(C * Q.X + S * Q.Y, -S * Q.X + C * Q.Y);
}

FHoleMapProjection MakeHoleFraming(const FVector2D& TeeCm, const FVector2D& PinCm,
	const FVector2D& ViewSizePx, double MarginFrac, double MinSpanCm)
{
	FHoleMapProjection P;
	P.ViewSizePx = ViewSizePx;
	P.CenterWorldCm = (TeeCm + PinCm) * 0.5;

	const FVector2D Axis = PinCm - TeeCm;
	const double Len = Axis.Size();
	// Rotate so the tee->pin direction lands on widget "up" (0,-1): the rotated angle of the
	// axis must be -PI/2, and rotation adds angles, so Theta = -PI/2 - atan2(Ay, Ax).
	// Degenerate tee==pin: north-up (north = -Y world per the pipeline's lat flip; Theta = 0
	// maps -Y world to -y widget = up).
	P.ThetaRad = (Len < 100.0) ? 0.0 : (-PI * 0.5 - FMath::Atan2(Axis.Y, Axis.X));

	const double SpanCm = FMath::Max(Len * (1.0 + 2.0 * MarginFrac), MinSpanCm);
	P.PxPerCm = (SpanCm > 0.0 && ViewSizePx.Y > 0.0) ? ViewSizePx.Y / SpanCm : 0.0;
	return P;
}

FHoleMapProjection MakeGreenFraming(const TArray<FVector2D>& VertsCm,
	const FVector2D& FallbackCenterCm, double ThetaRad, const FVector2D& ViewSizePx,
	double MarginFrac)
{
	FHoleMapProjection P;
	P.ViewSizePx = ViewSizePx;
	P.ThetaRad = ThetaRad;

	if (VertsCm.Num() < 3)
	{
		P.CenterWorldCm = FallbackCenterCm;
		const double SpanCm = 4000.0;   // 40 m box around the fallback center
		P.PxPerCm = (ViewSizePx.GetMin() > 0.0) ? ViewSizePx.GetMin() / SpanCm : 0.0;
		return P;
	}

	FVector2D Sum = FVector2D::ZeroVector;
	for (const FVector2D& V : VertsCm) { Sum += V; }
	P.CenterWorldCm = Sum / VertsCm.Num();

	// Fit the verts' bbox in the ROTATED frame so the framed extent matches what's drawn.
	const double C = FMath::Cos(ThetaRad), S = FMath::Sin(ThetaRad);
	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FVector2D& V : VertsCm)
	{
		const FVector2D D = V - P.CenterWorldCm;
		const FVector2D R(C * D.X - S * D.Y, S * D.X + C * D.Y);
		Min.X = FMath::Min(Min.X, R.X); Min.Y = FMath::Min(Min.Y, R.Y);
		Max.X = FMath::Max(Max.X, R.X); Max.Y = FMath::Max(Max.Y, R.Y);
	}
	const double Pad = 1.0 + 2.0 * MarginFrac;
	const double SpanX = FMath::Max((Max.X - Min.X) * Pad, 1000.0);   // >= 10 m guards tiny/degenerate outlines
	const double SpanY = FMath::Max((Max.Y - Min.Y) * Pad, 1000.0);
	// The rotated bbox isn't necessarily centered on the centroid -- recenter on the bbox middle
	// (in world space) so the outline sits centered in the view.
	const FVector2D MidR = (Min + Max) * 0.5;
	P.CenterWorldCm += FVector2D(C * MidR.X + S * MidR.Y, -S * MidR.X + C * MidR.Y);   // R(-Theta) * MidR

	P.PxPerCm = FMath::Min(
		ViewSizePx.X > 0.0 ? ViewSizePx.X / SpanX : 0.0,
		ViewSizePx.Y > 0.0 ? ViewSizePx.Y / SpanY : 0.0);
	return P;
}

void ComputeSlopeGrid(const TArray<double>& CornerHeightsCm, FGreenSlopeGrid& InOut)
{
	const int32 NX = InOut.NX, NY = InOut.NY;
	if (NX <= 0 || NY <= 0 || CornerHeightsCm.Num() != (NX + 1) * (NY + 1))
	{
		InOut.SlopePct.Empty();
		InOut.FallDirWorld.Empty();
		return;
	}
	InOut.SlopePct.SetNumZeroed(NX * NY);
	InOut.FallDirWorld.SetNumZeroed(NX * NY);
	if (InOut.bInGreen.Num() != NX * NY)
	{
		InOut.bInGreen.SetNumZeroed(NX * NY);
	}

	const auto Corner = [&](int32 I, int32 J) { return CornerHeightsCm[J * (NX + 1) + I]; };
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 Cell = J * NX + I;
			const double H00 = Corner(I, J),     H10 = Corner(I + 1, J);
			const double H01 = Corner(I, J + 1), H11 = Corner(I + 1, J + 1);
			if (H00 >= InvalidHeightCm || H10 >= InvalidHeightCm ||
				H01 >= InvalidHeightCm || H11 >= InvalidHeightCm)
			{
				InOut.bInGreen[Cell] = false;   // trace miss: exclude rather than show a fake flat cell
				continue;
			}
			// Central differences across the cell's corner pairs (run = CellCm per axis).
			const double DzDx = ((H10 + H11) - (H00 + H01)) / (2.0 * InOut.CellCm);
			const double DzDy = ((H01 + H11) - (H00 + H10)) / (2.0 * InOut.CellCm);
			const double Mag = FMath::Sqrt(DzDx * DzDx + DzDy * DzDy);
			InOut.SlopePct[Cell] = static_cast<float>(100.0 * Mag);
			if (Mag > 0.001)   // < 0.1% grade reads as flat -- no arrow
			{
				InOut.FallDirWorld[Cell] = FVector2D(-DzDx / Mag, -DzDy / Mag);   // downhill = -gradient
			}
		}
	}
}

}   // namespace GolfMap
