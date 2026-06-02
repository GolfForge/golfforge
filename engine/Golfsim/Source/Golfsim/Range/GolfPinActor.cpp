#include "Range/GolfPinActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	FlagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagMesh"));
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

	if (PlaneMesh.Succeeded())
	{
		DiscMesh->SetStaticMesh(PlaneMesh.Object);   // flat ground decal -- no thickness, no side wall
		FlagMesh->SetStaticMesh(PlaneMesh.Object);
		GimmeRingMesh->SetStaticMesh(PlaneMesh.Object);
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

	// Flag: rectangle near the top of the pole, sticking out along +Y. Plane is 100x100 cm, normal +Z;
	// roll 90 deg so it stands up like a banner facing -Y.
	FlagMesh->SetRelativeScale3D(FVector(FlagWidthUU / PlaneSizeUU, FlagHeightUU / PlaneSizeUU, 1.f));
	FlagMesh->SetRelativeLocation(FVector(0.f, FlagWidthUU * 0.5f, PoleHeightUU - FlagHeightUU * 0.5f));
	FlagMesh->SetRelativeRotation(FRotator(/*Pitch=*/0.f, /*Yaw=*/0.f, /*Roll=*/90.f));

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
		if (UMaterialInstanceDynamic* RedMID = MakeColorMID(BasicMat.Object, this, FLinearColor(0.85f, 0.10f, 0.10f)))
		{
			FlagMesh->SetMaterial(0, RedMID);
		}
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
