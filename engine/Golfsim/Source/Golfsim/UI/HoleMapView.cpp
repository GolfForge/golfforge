#include "UI/HoleMapView.h"
#include "UI/GolfUITheme.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"   // FApp::GetCurrentTime for the green-tab ball-ring pulse
#include "Physics/CourseSurface.h"   // FCourseSurfaceSampler::HalfXYCm (shared georeference)
#include "Rendering/DrawElements.h"

namespace
{
	constexpr double CmPerYd = 91.44;
	constexpr double AimLineLengthCm = 60000.0;     // 600 m -- clipped by the view, just "long"
	constexpr double MaxZoom = 8.0;
	constexpr int32  ArcSegments = 96;
	constexpr double HeatFullSlopePct = 5.0;        // slope% that reads as max heat on a green

	// Downhill-arrow head: two strokes splayed off the shaft tip.
	const FVector2D RotateD(const FVector2D& V, double Rad)
	{
		const double C = FMath::Cos(Rad), S = FMath::Sin(Rad);
		return FVector2D(C * V.X - S * V.Y, S * V.X + C * V.Y);
	}
}

void UHoleMapView::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Transparent root so the widget has real geometry + participates in hit-testing
	// (RoundHud's root is SelfHitTestInvisible; this child opts back in, like the Menu button).
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MapRoot"));
	Root->SetBrushColor(FLinearColor(0, 0, 0, 0));
	WidgetTree->RootWidget = Root;
	SetVisibility(ESlateVisibility::Visible);

	using namespace GolfUI;
	// Square, not rounded: the map strip sits edge-to-edge between the tabs row and the footer,
	// so rounded corners on the fill read as a floating chip instead of a sandwiched map area.
	FallbackBrush = RoundedBrush(Color::Surface2(), 0.f);
	WhiteBrush = FSlateBrush();                                    // default brush draws solid white; tint per call
	PinDotBrush = RoundedBrush(Color::Accent(), 999.f);
	BallDotBrush = RoundedBrush(Color::Text(), 999.f);
	BallRimBrush = RoundedBrush(FLinearColor(0, 0, 0, 0.6f), 999.f);
	MapBrush.DrawAs = ESlateBrushDrawType::Image;
	MapBrush.Tiling = ESlateBrushTileType::NoTile;
}

void UHoleMapView::NativeConstruct()
{
	Super::NativeConstruct();
	// Live overlays (ball, aim line) move every frame; volatile opts out of Slate's cached
	// invalidation so NativePaint runs each frame even with global invalidation enabled.
	if (TSharedPtr<SWidget> W = GetCachedWidget())
	{
		W->ForceVolatile(true);
	}
}

void UHoleMapView::SetViewSize(const FVector2D& SizePx)
{
	ViewSizePx = SizePx;
	RebuildFramings();
}

void UHoleMapView::SetStaticData(const FHoleMapStaticData& Data)
{
	StaticData = Data;
	MapTexture = Data.MinimapTexture;
	MapBrush.SetResourceObject(MapTexture);
	RebuildFramings();
}

void UHoleMapView::SetLive(const FVector2D& BallWorldCm, float InAimYawDeg)
{
	BallCm = BallWorldCm;
	AimYawDeg = InAimYawDeg;
}

void UHoleMapView::RebuildFramings()
{
	HoleProj = GolfMap::MakeHoleFraming(StaticData.TeeCm, StaticData.PinCm, ViewSizePx);
	BaseHolePxPerCm = HoleProj.PxPerCm;

	const FVector2D GreenCenter = StaticData.GreenCm.IsZero() ? StaticData.PinCm : StaticData.GreenCm;
	GreenProj = GolfMap::MakeGreenFraming(StaticData.GreenVertsCm, GreenCenter, HoleProj.ThetaRad, ViewSizePx);
	BaseGreenPxPerCm = GreenProj.PxPerCm;
}

// ---------------------------------------------------------------- input

FReply UHoleMapView::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnAimAt && ActiveProj().IsValid())
	{
		const FVector2D LocalPx = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		OnAimAt(ActiveProj().WidgetToWorld(LocalPx));
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UHoleMapView::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	GolfMap::FHoleMapProjection& P = (ActiveTab == EHoleMapTab::Green) ? GreenProj : HoleProj;
	const double Base = (ActiveTab == EHoleMapTab::Green) ? BaseGreenPxPerCm : BaseHolePxPerCm;
	if (P.IsValid() && Base > 0.0)
	{
		const FVector2D LocalPx = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		P.ZoomAt(FMath::Pow(1.15, InMouseEvent.GetWheelDelta()), LocalPx, Base, Base * MaxZoom);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

// ---------------------------------------------------------------- paint

int32 UHoleMapView::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	if (!ActiveProj().IsValid())
	{
		// No hole data yet -- just the placeholder fill (same look as pre-GOL-209).
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer, AllottedGeometry.ToPaintGeometry(), &FallbackBrush);
		return Layer;
	}

	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));

	if (ActiveTab == EHoleMapTab::Green)
	{
		PaintGreenTab(AllottedGeometry, OutDrawElements, ++Layer);
	}
	else
	{
		PaintHoleTab(AllottedGeometry, OutDrawElements, ++Layer);
	}
	Layer += 5;
	PaintMarkers(AllottedGeometry, OutDrawElements, Layer, ActiveProj());
	Layer += 3;

	OutDrawElements.PopClip();
	return Layer;
}

void UHoleMapView::PaintTexture(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const
{
	// Base fill underneath: covers the no-texture fallback AND any sliver the texture quad
	// doesn't reach (it always does at legal zooms -- the course square contains the framing).
	FSlateDrawElement::MakeBox(Out, LayerId, Geo.ToPaintGeometry(), &FallbackBrush);
	if (!MapTexture)
	{
		return;
	}

	// The texture covers world [-H, +H]^2 cm (the splatmap georeference). Local space for the
	// quad = cm offsets from the (-H, -H) corner, so LocalSize = (2H, 2H) and the render
	// transform is exactly the projection affine: draw and click-inverse share one matrix.
	const double H = FCourseSurfaceSampler::HalfXYCm;
	const GolfMap::FHoleMapProjection& P = ActiveProj();
	double A, B, C, D, Tx, Ty;
	P.GetAffine(A, B, C, D, Tx, Ty);
	const FVector2D CornerPx = P.WorldToWidget(FVector2D(-H, -H));

	// Slate render transforms use the row-vector convention (out = v * M + T): TMatrix2x2's
	// ctor order is (m00, m01, m10, m11) with out.x = v.x*m00 + v.y*m10, so our column-convention
	// affine [A B; C D] lands as FMatrix2x2(A, C, B, D).
	const FSlateRenderTransform Xf(
		FMatrix2x2f(static_cast<float>(A), static_cast<float>(C), static_cast<float>(B), static_cast<float>(D)),
		FVector2f(static_cast<float>(CornerPx.X), static_cast<float>(CornerPx.Y)));
	const FGeometry MapGeo = Geo.MakeChild(
		FVector2f(static_cast<float>(2.0 * H), static_cast<float>(2.0 * H)),
		FSlateLayoutTransform(),
		Xf,
		FVector2f(0.f, 0.f));   // pivot at local origin: the transform is absolute

	FSlateDrawElement::MakeBox(Out, LayerId + 1, MapGeo.ToPaintGeometry(), &MapBrush);
}

void UHoleMapView::PaintHoleTab(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace GolfUI;
	const GolfMap::FHoleMapProjection& P = HoleProj;
	PaintTexture(Geo, Out, LayerId);

	// Yard arcs centered on the ball at 50-yd steps, labeled where they cross the view-up axis.
	// View-up in world space = R(-Theta) * (0,-1) = (-sin, -cos).
	const FVector2D UpWorld(-FMath::Sin(P.ThetaRad), -FMath::Cos(P.ThetaRad));
	for (int32 Yd = 50; Yd <= 300; Yd += 50)
	{
		const double RCm = Yd * CmPerYd;
		const double RPx = RCm * P.PxPerCm;
		if (RPx < 18.0 || RPx > 1.5 * ViewSizePx.GetMax())
		{
			continue;   // too tight around the ball dot, or fully outside the view
		}
		TArray<FVector2f> Pts;
		Pts.Reserve(ArcSegments + 1);
		for (int32 S = 0; S <= ArcSegments; ++S)
		{
			const double A = 2.0 * PI * S / ArcSegments;
			const FVector2D W = BallCm + FVector2D(FMath::Cos(A) * RCm, FMath::Sin(A) * RCm);
			Pts.Add(FVector2f(P.WorldToWidget(W)));
		}
		FSlateDrawElement::MakeLines(Out, LayerId + 2, Geo.ToPaintGeometry(), Pts,
			ESlateDrawEffect::None, Color::Border(), true, 1.0f);

		const FVector2D LabelPx = P.WorldToWidget(BallCm + UpWorld * RCm) + FVector2D(4.0, -12.0);
		if (LabelPx.X > 0.0 && LabelPx.X < ViewSizePx.X - 24.0 && LabelPx.Y > 0.0 && LabelPx.Y < ViewSizePx.Y - 12.0)
		{
			FSlateDrawElement::MakeText(Out, LayerId + 3,
				Geo.ToPaintGeometry(FVector2f(40.f, 12.f), FSlateLayoutTransform(FVector2f(LabelPx))),
				FString::FromInt(Yd), Mono(9), ESlateDrawEffect::None, Color::TextDim());
		}
	}

	// Aim line: ball -> long ray along the aim yaw (UE yaw 0 = +X world, CCW toward +Y).
	const double YawRad = FMath::DegreesToRadians(AimYawDeg);
	const FVector2D AimEnd = BallCm + FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad)) * AimLineLengthCm;
	const TArray<FVector2f> AimPts = { FVector2f(P.WorldToWidget(BallCm)), FVector2f(P.WorldToWidget(AimEnd)) };
	FSlateDrawElement::MakeLines(Out, LayerId + 4, Geo.ToPaintGeometry(), AimPts,
		ESlateDrawEffect::None, Color::Accent(), true, 1.5f);
}

void UHoleMapView::PaintGreenTab(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace GolfUI;
	const GolfMap::FHoleMapProjection& P = GreenProj;
	PaintTexture(Geo, Out, LayerId);

	// Slope heatmap: per-cell tint, accent (flat) -> danger red (HeatFullSlopePct+).
	const GolfMap::FGreenSlopeGrid& G = StaticData.Slope;
	if (!G.IsEmpty() && G.SlopePct.Num() == G.NX * G.NY && G.bInGreen.Num() == G.NX * G.NY)
	{
		FLinearColor HeatLow = Color::Accent();   HeatLow.A = 0.10f;
		FLinearColor HeatHigh = Color::DangerFill(); HeatHigh.A = 0.45f;
		const float CellPx = static_cast<float>(G.CellCm * P.PxPerCm);

		for (int32 J = 0; J < G.NY; ++J)
		{
			for (int32 I = 0; I < G.NX; ++I)
			{
				const int32 Cell = J * G.NX + I;
				if (!G.bInGreen[Cell])
				{
					continue;
				}
				const FVector2D CenterCm = G.OriginCm + FVector2D(I * G.CellCm, J * G.CellCm);
				const FVector2D Px = P.WorldToWidget(CenterCm);
				const float T = FMath::Clamp(G.SlopePct[Cell] / static_cast<float>(HeatFullSlopePct), 0.f, 1.f);
				FSlateDrawElement::MakeBox(Out, LayerId + 2,
					Geo.ToPaintGeometry(FVector2f(CellPx, CellPx),
						FSlateLayoutTransform(FVector2f(Px - FVector2D(CellPx * 0.5, CellPx * 0.5)))),
					&WhiteBrush, ESlateDrawEffect::None, FMath::Lerp(HeatLow, HeatHigh, T));
			}
		}

		// Downhill break arrows on every 2nd cell each axis (cells are 1 m -- full density is noise).
		const FLinearColor ArrowCol = Color::Text().CopyWithNewOpacity(0.75f);
		for (int32 J = 0; J < G.NY; J += 2)
		{
			for (int32 I = 0; I < G.NX; I += 2)
			{
				const int32 Cell = J * G.NX + I;
				if (!G.bInGreen[Cell] || G.FallDirWorld[Cell].IsZero())
				{
					continue;
				}
				const FVector2D CenterCm = G.OriginCm + FVector2D(I * G.CellCm, J * G.CellCm);
				const FVector2D Dir = G.FallDirWorld[Cell];
				const FVector2D TailCm = CenterCm - Dir * (G.CellCm * 0.55);
				const FVector2D TipCm = CenterCm + Dir * (G.CellCm * 0.55);
				const FVector2f TailPx(P.WorldToWidget(TailCm));
				const FVector2f TipPx(P.WorldToWidget(TipCm));

				const TArray<FVector2f> Shaft = { TailPx, TipPx };
				FSlateDrawElement::MakeLines(Out, LayerId + 3, Geo.ToPaintGeometry(), Shaft,
					ESlateDrawEffect::None, ArrowCol, true, 1.0f);
				for (const double HeadRad : { 2.62, -2.62 })   // +/-150 deg off the fall direction
				{
					const FVector2D HeadCm = TipCm + RotateD(Dir, HeadRad) * (G.CellCm * 0.3);
					const TArray<FVector2f> Head = { TipPx, FVector2f(P.WorldToWidget(HeadCm)) };
					FSlateDrawElement::MakeLines(Out, LayerId + 3, Geo.ToPaintGeometry(), Head,
						ESlateDrawEffect::None, ArrowCol, true, 1.0f);
				}
			}
		}
	}

	// Green outline on top of the heat cells.
	if (StaticData.GreenVertsCm.Num() >= 3)
	{
		TArray<FVector2f> Outline;
		Outline.Reserve(StaticData.GreenVertsCm.Num() + 1);
		for (const FVector2D& V : StaticData.GreenVertsCm)
		{
			Outline.Add(FVector2f(P.WorldToWidget(V)));
		}
		const FVector2f First = Outline[0];   // copy: Add(Outline[0]) trips UE's aliased-element assert
		Outline.Add(First);                   // close the ring
		FSlateDrawElement::MakeLines(Out, LayerId + 4, Geo.ToPaintGeometry(), Outline,
			ESlateDrawEffect::None, Color::Text().CopyWithNewOpacity(0.9f), true, 1.5f);
	}

	// Aim line here too: reading a putt = your line vs the break arrows. Same ray as the HOLE
	// tab (the ball dot comes from PaintMarkers); a ball just off the framed green still shows
	// its line crossing the view.
	const double YawRad = FMath::DegreesToRadians(AimYawDeg);
	const FVector2D AimEnd = BallCm + FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad)) * AimLineLengthCm;
	const TArray<FVector2f> AimPts = { FVector2f(P.WorldToWidget(BallCm)), FVector2f(P.WorldToWidget(AimEnd)) };
	FSlateDrawElement::MakeLines(Out, LayerId + 4, Geo.ToPaintGeometry(), AimPts,
		ESlateDrawEffect::None, Color::Accent(), true, 1.5f);
}

void UHoleMapView::PaintMarkers(const FGeometry& Geo, FSlateWindowElementList& Out, int32 LayerId,
	const GolfMap::FHoleMapProjection& P) const
{
	using namespace GolfUI;

	// Pin: exact-point accent dot + the Lucide flag glyph planted on it.
	const FVector2D PinPx = P.WorldToWidget(StaticData.PinCm);
	FSlateDrawElement::MakeBox(Out, LayerId,
		Geo.ToPaintGeometry(FVector2f(5.f, 5.f), FSlateLayoutTransform(FVector2f(PinPx - FVector2D(2.5, 2.5)))),
		&PinDotBrush);
	FString Flag;
	Flag.AppendChar(static_cast<TCHAR>(EIcon::FlagTriangleRight));
	FSlateDrawElement::MakeText(Out, LayerId + 1,
		Geo.ToPaintGeometry(FVector2f(16.f, 16.f), FSlateLayoutTransform(FVector2f(PinPx + FVector2D(2.0, -15.0)))),
		Flag, Icon(13), ESlateDrawEffect::None, Color::Text());

	// Ball: white dot with a dark rim so it reads on any surface.
	const FVector2D BallPx = P.WorldToWidget(BallCm);
	FSlateDrawElement::MakeBox(Out, LayerId,
		Geo.ToPaintGeometry(FVector2f(9.f, 9.f), FSlateLayoutTransform(FVector2f(BallPx - FVector2D(4.5, 4.5)))),
		&BallRimBrush);
	FSlateDrawElement::MakeBox(Out, LayerId + 1,
		Geo.ToPaintGeometry(FVector2f(6.f, 6.f), FSlateLayoutTransform(FVector2f(BallPx - FVector2D(3.0, 3.0)))),
		&BallDotBrush);

	// GREEN tab "you are here": a slow-pulsing accent ring around the ball, so your spot reads
	// instantly over the heat cells + arrows. The widget is ForceVolatile, so paint runs every
	// frame and the pulse animates for free.
	if (ActiveTab == EHoleMapTab::Green)
	{
		const double T = FApp::GetCurrentTime();
		const float RingR = 9.f + 2.5f * static_cast<float>(FMath::Sin(T * 2.0 * PI / 1.2));
		TArray<FVector2f> Ring;
		Ring.Reserve(25);
		for (int32 S = 0; S <= 24; ++S)
		{
			const double A = 2.0 * PI * S / 24;
			Ring.Add(FVector2f(BallPx + FVector2D(FMath::Cos(A) * RingR, FMath::Sin(A) * RingR)));
		}
		FSlateDrawElement::MakeLines(Out, LayerId + 1, Geo.ToPaintGeometry(), Ring,
			ESlateDrawEffect::None, GolfUI::Color::Accent(), true, 1.5f);
	}
}
