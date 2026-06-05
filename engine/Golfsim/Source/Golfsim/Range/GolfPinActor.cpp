#include "Range/GolfPinActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Engine basic shapes are 100 cm. Plane is 100x100 cm in its local XY with normal +Z (it lays
	// flat on the ground by default -- no thickness, reads as a painted-on green from any distance).
	constexpr float PlaneSizeUU = 100.f;
	constexpr float PoleHeightMeters = 2.4f;
	constexpr float PoleHeightUU = PoleHeightMeters * 100.f;
	constexpr float PoleRadiusScale = 0.03f;
	constexpr float FlagWidthUU = 40.f;
	constexpr float FlagHeightUU = 25.f;
	// Lift the green plane a couple of cm so it doesn't z-fight with the splatmap-painted turf on
	// the range's landscape. Pole + flag offsets compose on top of this.
	constexpr float GreenLiftUU = 2.f;
	// GOL-123: gimme ring sits just above the green plane. Same Z-fighting concern; bigger lift
	// since both planes are flat at near-identical heights.
	constexpr float GimmeRingLiftUU = 4.f;
	constexpr float CmPerFt = 30.48f;

	UMaterialInstanceDynamic* MakeColorMID(UMaterialInterface* Source, UObject* Outer, const FLinearColor& Color)
	{
		if (!Source) { return nullptr; }
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Source, Outer);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);   // BasicShapeMaterial param name
		}
		return MID;
	}

	// Build a subdivided flag banner in the component's local YZ plane (X=0): pole-attached edge at
	// local Y=0, free edge at Y=WidthY; height centered on Z. UVs: U=along-flag (0 at pole -> 1 free),
	// V=height. Enough verts for M_FlagWind's WPO to ripple it like cloth. No collision.
	void BuildFlagGrid(UProceduralMeshComponent* PMC, float WidthY, float HeightZ)
	{
		if (!PMC) { return; }
		constexpr int32 NX = 16;   // segments along the flag (U / wave travel direction)
		constexpr int32 NY = 8;    // segments up the height
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		const TArray<FLinearColor> NoColors;
		for (int32 j = 0; j <= NY; ++j)
		{
			for (int32 i = 0; i <= NX; ++i)
			{
				const float U = static_cast<float>(i) / NX;
				const float V = static_cast<float>(j) / NY;
				Verts.Add(FVector(0.f, U * WidthY, (V - 0.5f) * HeightZ));
				UVs.Add(FVector2D(U, V));
				Normals.Add(FVector(1.f, 0.f, 0.f));
				Tangents.Add(FProcMeshTangent(0.f, 1.f, 0.f));
			}
		}
		const int32 Stride = NX + 1;
		for (int32 j = 0; j < NY; ++j)
		{
			for (int32 i = 0; i < NX; ++i)
			{
				const int32 A = j * Stride + i;
				const int32 B = A + 1;
				const int32 C = A + Stride;
				const int32 D = C + 1;
				Tris.Add(A); Tris.Add(C); Tris.Add(B);
				Tris.Add(B); Tris.Add(C); Tris.Add(D);
			}
		}
		PMC->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, NoColors, Tangents, /*bCreateCollision=*/false);
	}
}

AGolfPinActor::AGolfPinActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Bare scene-component root so the three meshes attach as siblings; no parent-scale inheritance.
	// Actor origin = ground level: the HUD's placement sets the actor Z to the line-trace hit.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);

	DiscMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiscMesh"));
	DiscMesh->SetupAttachment(Root);
	DiscMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DiscMesh->SetMobility(EComponentMobility::Movable);

	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	PoleMesh->SetupAttachment(Root);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoleMesh->SetMobility(EComponentMobility::Movable);

	FlagMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FlagMesh"));
	FlagMesh->SetupAttachment(Root);
	FlagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlagMesh->SetMobility(EComponentMobility::Movable);

	// GOL-123 gimme ring: same Plane mesh + M_GolfGreen material idiom as the green disc, just
	// a different colour. Hidden by default (SetGimmeRadiusFt(0) on construction); the round
	// subsystem reveals + sizes it after the player picks a difficulty.
	GimmeRingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GimmeRingMesh"));
	GimmeRingMesh->SetupAttachment(Root);
	GimmeRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GimmeRingMesh->SetMobility(EComponentMobility::Movable);
	GimmeRingMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// M_GolfGreen is a Masked material with a circular alpha (centered unit disc) and a "Color"
	// vector parameter. Authored by engine/scripts/build_golf_green_material.py; missing the first
	// time anyone clones the repo, so the loader degrades to BasicShapeMaterial (square tint).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GolfGreenMat(TEXT("/Game/Materials/M_GolfGreen.M_GolfGreen"));
	// M_FlagWind: two-sided WPO flutter material, branded GolfForge flag texture (GOL-165). Built by
	// engine/scripts/build_flag_wind_material.py; missing on a fresh clone -> falls back to red BasicMat.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlagWindMat(TEXT("/Game/Materials/M_FlagWind.M_FlagWind"));

	if (PlaneMesh.Succeeded())
	{
		DiscMesh->SetStaticMesh(PlaneMesh.Object);   // flat ground decal -- no thickness, no side wall
		GimmeRingMesh->SetStaticMesh(PlaneMesh.Object);   // FlagMesh is a procedural grid, built below
	}
	if (CylinderMesh.Succeeded())
	{
		PoleMesh->SetStaticMesh(CylinderMesh.Object);   // 3D pole only
	}

	// Green plane: flat, lifted a couple cm above the landscape to dodge z-fighting with the splatmap.
	const float DiscXYScale = static_cast<float>(DefaultDiameterMeters * 100.0 / PlaneSizeUU);
	DiscMesh->SetRelativeScale3D(FVector(DiscXYScale, DiscXYScale, 1.f));
	DiscMesh->SetRelativeLocation(FVector(0.f, 0.f, GreenLiftUU));

	// Pole: 3 cm thick, 2.4 m tall. Cylinder pivot is mid-height, so base at Z=0 means center = +half.
	const float PoleZScale = PoleHeightUU / 100.f;
	PoleMesh->SetRelativeScale3D(FVector(PoleRadiusScale, PoleRadiusScale, PoleZScale));
	PoleMesh->SetRelativeLocation(FVector(0.f, 0.f, PoleHeightUU * 0.5f));

	// Flag: a subdivided banner in the YZ plane (X=0), pole-attached edge at local Y=0 extending +Y to
	// the free edge; M_FlagWind's WPO ripples it. Component at the pole top so the flag's top edge is
	// flush with the pole top (grid Z is centered, so origin sits half a flag-height below the top).
	FlagMesh->SetRelativeLocation(FVector(0.f, 0.f, PoleHeightUU - FlagHeightUU * 0.5f));
	BuildFlagGrid(FlagMesh, FlagWidthUU, FlagHeightUU);

	// Green: prefer the authored M_GolfGreen (round, matte), fall back to a tinted BasicShapeMaterial
	// (square) so a fresh clone still builds + spawns a usable, if visibly placeholder, target.
	UMaterialInterface* GreenSrc = GolfGreenMat.Succeeded() ? GolfGreenMat.Object
	                              : (BasicMat.Succeeded() ? BasicMat.Object : nullptr);
	if (UMaterialInstanceDynamic* GreenMID = MakeColorMID(GreenSrc, this, FLinearColor(0.10f, 0.55f, 0.18f)))
	{
		DiscMesh->SetMaterial(0, GreenMID);
	}
	if (BasicMat.Succeeded())
	{
		if (UMaterialInstanceDynamic* WhiteMID = MakeColorMID(BasicMat.Object, this, FLinearColor(0.95f, 0.95f, 0.95f)))
		{
			PoleMesh->SetMaterial(0, WhiteMID);
		}
	}

	// Flag: prefer the authored M_FlagWind (WPO flutter, branded GolfForge texture baked in -- the
	// "Color" set below is a harmless no-op on it). Fall back to a tinted BasicShapeMaterial (static
	// red, where "Color" does apply) so a fresh clone still spawns a usable, if frozen, flag.
	UMaterialInterface* FlagSrc = FlagWindMat.Succeeded() ? FlagWindMat.Object
	                            : (BasicMat.Succeeded() ? BasicMat.Object : nullptr);
	if (UMaterialInstanceDynamic* FlagMID = MakeColorMID(FlagSrc, this, FLinearColor(0.85f, 0.10f, 0.10f)))
	{
		FlagMesh->SetMaterial(0, FlagMID);
	}

	// Gimme ring: same M_GolfGreen masked disc, white tint so it reads as a halo against the
	// green. Square placeholder via BasicMat for fresh clones (same fallback as DiscMesh).
	UMaterialInterface* GimmeSrc = GolfGreenMat.Succeeded() ? GolfGreenMat.Object
	                              : (BasicMat.Succeeded() ? BasicMat.Object : nullptr);
	if (UMaterialInstanceDynamic* GimmeMID = MakeColorMID(GimmeSrc, this, FLinearColor(0.98f, 0.95f, 0.40f)))
	{
		GimmeRingMesh->SetMaterial(0, GimmeMID);
	}
}

void AGolfPinActor::SetGreenDiameterMeters(double DiameterM)
{
	if (!DiscMesh) { return; }
	const float XY = static_cast<float>(FMath::Max(DiameterM, 0.5) * 100.0 / PlaneSizeUU);
	DiscMesh->SetRelativeScale3D(FVector(XY, XY, 1.f));
}

void AGolfPinActor::SetGimmeRadiusFt(double RadiusFt)
{
	if (!GimmeRingMesh) { return; }
	if (RadiusFt <= 0.0)
	{
		GimmeRingMesh->SetVisibility(false);
		return;
	}
	// Plane mesh is 100 cm in its local XY; scale = diameter in cm / 100. Diameter = 2 * radius.
	const float DiameterCm = static_cast<float>(2.0 * RadiusFt * CmPerFt);
	const float XY = DiameterCm / PlaneSizeUU;
	GimmeRingMesh->SetRelativeScale3D(FVector(XY, XY, 1.f));
	GimmeRingMesh->SetRelativeLocation(FVector(0.f, 0.f, GimmeRingLiftUU));
	GimmeRingMesh->SetVisibility(true);
}
