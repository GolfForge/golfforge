#include "Range/GreenBreakGridActor.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UI/GreenFlowTexture.h"

AGreenBreakGridActor::AGreenBreakGridActor()
{
	PrimaryActorTick.bCanEverTick = false;   // the material's Time node animates; no C++ per-frame work

	GridMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GridMesh"));
	RootComponent = GridMesh;
	GridMesh->SetMobility(EComponentMobility::Movable);
	GridMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // never eat placement/ground traces
	GridMesh->SetCastShadow(false);
}

void AGreenBreakGridActor::BuildFromGrid(const GolfMap::FGreenSlopeGrid& Grid,
	const TArray<double>& CornerHeightsCm)
{
	if (!GridMesh) { return; }
	GridMesh->ClearAllMeshSections();

	const int32 NX = Grid.NX, NY = Grid.NY;
	const int32 NumCells = NX * NY;
	const int32 NumCorners = (NX + 1) * (NY + 1);
	if (Grid.IsEmpty() || CornerHeightsCm.Num() != NumCorners || Grid.bInGreen.Num() != NumCells
		|| Grid.SlopePct.Num() != NumCells || Grid.FallDirWorld.Num() != NumCells)
	{
		return;
	}

	// The actor sits at the world origin and the mesh verts carry world coordinates directly --
	// course spans are ~±100k cm, well inside float precision, and it keeps the vertex math
	// 1:1 with the slope grid's world-cm contract.
	SetActorLocation(FVector::ZeroVector);

	// Fallback Z for trace-miss corners (their quads are skipped, but the verts still enter the
	// section bounds -- a 1e308 sentinel would wreck culling).
	double ZSum = 0.0; int32 ZCount = 0;
	for (const double H : CornerHeightsCm)
	{
		if (H != GolfMap::InvalidHeightCm) { ZSum += H; ++ZCount; }
	}
	if (ZCount == 0) { return; }   // nothing traced -- no grid
	const double FallbackZ = ZSum / ZCount;

	const auto CellOn = [&](int32 I, int32 J) -> bool
	{
		return I >= 0 && I < NX && J >= 0 && J < NY && Grid.bInGreen[J * NX + I];
	};
	// A quad survives if any cell in its 3x3 neighborhood touches the green: keeps the soft
	// feathered edge the bilinear alpha gives, drops the dead outer margin.
	const auto QuadNearGreen = [&](int32 I, int32 J) -> bool
	{
		for (int32 DJ = -1; DJ <= 1; ++DJ)
		{
			for (int32 DI = -1; DI <= 1; ++DI)
			{
				if (CellOn(I + DI, J + DJ)) { return true; }
			}
		}
		return false;
	};

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Verts.Reserve(NumCorners);

	for (int32 J = 0; J <= NY; ++J)
	{
		for (int32 I = 0; I <= NX; ++I)
		{
			const double H = CornerHeightsCm[J * (NX + 1) + I];
			const FVector2D XY = Grid.OriginCm + FVector2D((I - 0.5) * Grid.CellCm, (J - 0.5) * Grid.CellCm);
			Verts.Add(FVector(XY.X, XY.Y, (H != GolfMap::InvalidHeightCm ? H : FallbackZ) + GridLiftUU));
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D((double)I / NX, (double)J / NY));
			Tangents.Add(FProcMeshTangent(1, 0, 0));

			// Corner color = average of the adjacent cells' flow data (fallback material path:
			// RG = dir [-1,1]->[0,1], B = slope/max, A = on-green mask).
			FVector2D DirSum = FVector2D::ZeroVector;
			float MagSum = 0.f, MaskSum = 0.f;
			int32 N = 0;
			for (int32 DJ = -1; DJ <= 0; ++DJ)
			{
				for (int32 DI = -1; DI <= 0; ++DI)
				{
					const int32 CI = I + DI, CJ = J + DJ;
					if (CI < 0 || CI >= NX || CJ < 0 || CJ >= NY) { continue; }
					const int32 Idx = CJ * NX + CI;
					DirSum += Grid.FallDirWorld[Idx];
					MagSum += FMath::Clamp(Grid.SlopePct[Idx] / GolfsimGreenFlow::DefaultSlopeMaxPct, 0.f, 1.f);
					MaskSum += Grid.bInGreen[Idx] ? 1.f : 0.f;
					++N;
				}
			}
			if (N > 0)
			{
				const FVector2D Dir = DirSum.GetSafeNormal();
				Colors.Add(FLinearColor(
					(float)(Dir.X * 0.5 + 0.5), (float)(Dir.Y * 0.5 + 0.5), MagSum / N, MaskSum / N));
			}
			else
			{
				Colors.Add(FLinearColor(0.5f, 0.5f, 0.f, 0.f));
			}
		}
	}

	const auto CornerHasHeight = [&](int32 I, int32 J) -> bool
	{
		return CornerHeightsCm[J * (NX + 1) + I] != GolfMap::InvalidHeightCm;
	};
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			if (!QuadNearGreen(I, J)) { continue; }
			if (!CornerHasHeight(I, J) || !CornerHasHeight(I + 1, J)
				|| !CornerHasHeight(I, J + 1) || !CornerHasHeight(I + 1, J + 1))
			{
				continue;
			}
			const int32 C00 = J * (NX + 1) + I;
			const int32 C10 = C00 + 1;
			const int32 C01 = C00 + (NX + 1);
			const int32 C11 = C01 + 1;
			Tris.Add(C00); Tris.Add(C01); Tris.Add(C10);
			Tris.Add(C10); Tris.Add(C01); Tris.Add(C11);
		}
	}
	if (Tris.Num() == 0) { return; }

	GridMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents,
		/*bCreateCollision=*/false);

	// Flow texture + material. M_GreenFlow is authored by engine/scripts/build_green_flow_material
	// .py -- lazy-loaded so a fresh run picks it up without an editor restart; missing on a fresh
	// clone -> engine default material (visible but inert), nothing crashes.
	FlowTexture = GolfsimGreenFlow::CreateFlowTexture(Grid);
	if (!GridMID)
	{
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Materials/M_GreenFlow.M_GreenFlow")))
		{
			GridMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	if (GridMID)
	{
		if (FlowTexture)
		{
			GridMID->SetTextureParameterValue(TEXT("FlowTex"), FlowTexture);
		}
		// World XY -> flow-texture UV: UV = (World - GridMin) * InvSpan, with texel centers on
		// cell centers (grid min = OriginCm - CellCm/2; span = N * CellCm).
		const double MinX = Grid.OriginCm.X - 0.5 * Grid.CellCm;
		const double MinY = Grid.OriginCm.Y - 0.5 * Grid.CellCm;
		GridMID->SetVectorParameterValue(TEXT("GridWorldToUV"), FLinearColor(
			(float)MinX, (float)MinY,
			(float)(1.0 / (NX * Grid.CellCm)), (float)(1.0 / (NY * Grid.CellCm))));
		GridMesh->SetMaterial(0, GridMID);
	}
}

void AGreenBreakGridActor::SetGridVisible(bool bVisible)
{
	if (GridMesh) { GridMesh->SetVisibility(bVisible); }
}

bool AGreenBreakGridActor::IsGridVisible() const
{
	return GridMesh && GridMesh->IsVisible();
}
