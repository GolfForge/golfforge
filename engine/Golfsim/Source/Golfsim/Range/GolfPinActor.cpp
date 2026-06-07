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

	// Collar/fringe ring: a darker textured disc UNDER the green, wider by CollarWidthUU all round,
	// so only that band shows past the green's edge. Sits just below the green so the green covers
	// its centre. Hole cup: a small dark disc just ABOVE the green at the pole base (exaggerated from
	// the real 10.8 cm hole so it still reads from a few yards back).
	constexpr float CollarWidthUU = 60.f;       // fringe band width (cm)
	constexpr float CollarLiftUU = 1.f;         // just under the green plane (GreenLiftUU = 2)
	constexpr float HoleCupDiameterUU = 30.f;   // cup disc diameter (cm)
	constexpr float HoleCupLiftUU = 3.f;        // just above the green plane

	// Collar disc XY scale for a given green diameter: green + a CollarWidthUU band on every side.
	float CollarScaleFor(double GreenDiameterM)
	{
		const double CollarDiameterCm = GreenDiameterM * 100.0 + 2.0 * CollarWidthUU;
		return static_cast<float>(CollarDiameterCm / PlaneSizeUU);
	}

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

	CollarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CollarMesh"));
	CollarMesh->SetupAttachment(Root);
	CollarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollarMesh->SetMobility(EComponentMobility::Movable);

	DiscMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiscMesh"));
	DiscMesh->SetupAttachment(Root);
	DiscMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DiscMesh->SetMobility(EComponentMobility::Movable);

	HoleCupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoleCupMesh"));
	HoleCupMesh->SetupAttachment(Root);
	HoleCupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoleCupMesh->SetMobility(EComponentMobility::Movable);

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
	// M_FlagPole: white/black banded flagstick. Built by engine/scripts/build_flagpole_material.py;
	// missing on a fresh clone -> falls back to the plain white BasicShapeMaterial pole.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlagPoleMat(TEXT("/Game/Materials/M_FlagPole.M_FlagPole"));

	if (PlaneMesh.Succeeded())
	{
		DiscMesh->SetStaticMesh(PlaneMesh.Object);   // flat ground decal -- no thickness, no side wall
		CollarMesh->SetStaticMesh(PlaneMesh.Object);
		HoleCupMesh->SetStaticMesh(PlaneMesh.Object);
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

	// Collar: wider disc just BELOW the green; only the CollarWidthUU band past the green's edge shows.
	const float CollarXYScale = CollarScaleFor(DefaultDiameterMeters);
	CollarMesh->SetRelativeScale3D(FVector(CollarXYScale, CollarXYScale, 1.f));
	CollarMesh->SetRelativeLocation(FVector(0.f, 0.f, CollarLiftUU));

	// Hole cup: small dark disc just ABOVE the green at the pole base (centre).
	const float CupXYScale = HoleCupDiameterUU / PlaneSizeUU;
	HoleCupMesh->SetRelativeScale3D(FVector(CupXYScale, CupXYScale, 1.f));
	HoleCupMesh->SetRelativeLocation(FVector(0.f, 0.f, HoleCupLiftUU));

	// Pole: 3 cm thick, 2.4 m tall. Cylinder pivot is mid-height, so base at Z=0 means center = +half.
	const float PoleZScale = PoleHeightUU / 100.f;
	PoleMesh->SetRelativeScale3D(FVector(PoleRadiusScale, PoleRadiusScale, PoleZScale));
	PoleMesh->SetRelativeLocation(FVector(0.f, 0.f, PoleHeightUU * 0.5f));

	// Flag: a subdivided banner in the YZ plane (X=0), pole-attached edge at local Y=0 extending +Y to
	// the free edge; M_FlagWind's WPO ripples it. Component at the pole top so the flag's top edge is
	// flush with the pole top (grid Z is centered, so origin sits half a flag-height below the top).
	FlagMesh->SetRelativeLocation(FVector(0.f, 0.f, PoleHeightUU - FlagHeightUU * 0.5f));
	BuildFlagGrid(FlagMesh, FlagWidthUU, FlagHeightUU);

	// Green: use the authored M_GolfGreen directly so its default Color IS the green color -- tune it
	// in build_golf_green_material.py (no C++ rebuild). On a fresh clone without it, fall back to a
	// tinted BasicShapeMaterial MID. GreenSrc (below) is the MID parent for the collar + cup.
	UMaterialInterface* GreenSrc = GolfGreenMat.Succeeded() ? GolfGreenMat.Object
	                              : (BasicMat.Succeeded() ? BasicMat.Object : nullptr);
	if (GolfGreenMat.Succeeded())
	{
		DiscMesh->SetMaterial(0, GolfGreenMat.Object);
	}
	else if (UMaterialInstanceDynamic* GreenMID = MakeColorMID(GreenSrc, this, FLinearColor(0.045f, 0.28f, 0.10f)))
	{
		DiscMesh->SetMaterial(0, GreenMID);
	}
	// Collar: same masked-disc material, darker than the green + duller + finer grain so the fringe
	// band reads as a different, longer cut. (Roughness/GrainTiling are no-ops on the BasicMat fallback.)
	if (UMaterialInstanceDynamic* CollarMID = MakeColorMID(GreenSrc, this, FLinearColor(0.03f, 0.18f, 0.07f)))
	{
		CollarMID->SetScalarParameterValue(TEXT("Roughness"), 0.95f);
		CollarMID->SetScalarParameterValue(TEXT("GrainTiling"), 36.f);
		CollarMesh->SetMaterial(0, CollarMID);
	}
	// Hole cup: same masked disc, near-black, so the flag reads as standing in a hole.
	if (UMaterialInstanceDynamic* CupMID = MakeColorMID(GreenSrc, this, FLinearColor(0.02f, 0.02f, 0.02f)))
	{
		HoleCupMesh->SetMaterial(0, CupMID);
	}
	// Flagstick: prefer the authored white/black banded M_FlagPole; fall back to a plain white
	// BasicShapeMaterial pole on a fresh clone (before build_flagpole_material.py has run).
	if (FlagPoleMat.Succeeded())
	{
		PoleMesh->SetMaterial(0, FlagPoleMat.Object);
	}
	else if (BasicMat.Succeeded())
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
	const double Clamped = FMath::Max(DiameterM, 0.5);
	if (DiscMesh)
	{
		const float XY = static_cast<float>(Clamped * 100.0 / PlaneSizeUU);
		DiscMesh->SetRelativeScale3D(FVector(XY, XY, 1.f));
	}
	if (CollarMesh)
	{
		const float CXY = CollarScaleFor(Clamped);
		CollarMesh->SetRelativeScale3D(FVector(CXY, CXY, 1.f));
	}
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
