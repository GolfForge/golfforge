#include "Range/GolfPinActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"               // GOL-123: gimme-ring terrain trace
#include "CollisionQueryParams.h"
#include "Components/TextRenderComponent.h"   // GOL-123: flat "GIMME" label in the ring gap

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
	// GOL-211: the cup is now draped per-vertex onto the terrain, so it only needs a hair of lift to
	// dodge z-fighting with the landscape -- a bigger lift reads as a floating coin (and a shadow).
	constexpr float HoleCupLiftUU = 0.5f;

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

	// GOL-211: procedural so BuildHoleCup can drape it over the terrain (per-vertex ground traces),
	// rather than a flat Plane that clips/floats on sloped greens. Built in local space at the actor
	// origin (= ground), so RelativeLocation stays zero.
	HoleCupMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HoleCupMesh"));
	HoleCupMesh->SetupAttachment(Root);
	HoleCupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoleCupMesh->SetMobility(EComponentMobility::Movable);
	HoleCupMesh->SetCastShadow(false);   // flat ground decal -- it must not drop a shadow on the turf

	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	PoleMesh->SetupAttachment(Root);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoleMesh->SetMobility(EComponentMobility::Movable);

	FlagMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FlagMesh"));
	FlagMesh->SetupAttachment(Root);
	FlagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlagMesh->SetMobility(EComponentMobility::Movable);

	// GOL-123 gimme ring: a thin procedural annulus built + terrain-conformed by SetGimmeRadiusFt.
	// Hidden by default; the round subsystem reveals + sizes it after the player picks a difficulty.
	GimmeRingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GimmeRingMesh"));
	GimmeRingMesh->SetupAttachment(Root);
	GimmeRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GimmeRingMesh->SetMobility(EComponentMobility::Movable);
	GimmeRingMesh->SetVisibility(false);

	// Flat "GIMME" label that fills one gap of the dashed ring. Unlit default TextRender material =
	// full-bright in every Time preset; warm-yellow to match the ring. Centered, no collision, hidden
	// until the active hole sizes the ring. SetGimmeRadiusFt positions + orients it.
	GimmeText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("GimmeText"));
	GimmeText->SetupAttachment(Root);
	GimmeText->SetMobility(EComponentMobility::Movable);
	GimmeText->SetText(FText::FromString(TEXT("GIMME")));
	GimmeText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	GimmeText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	GimmeText->SetWorldSize(22.f);
	GimmeText->SetTextRenderColor(FColor(250, 242, 102));   // warm yellow, matches the ring
	GimmeText->SetVisibility(false);

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
		CollarMesh->SetStaticMesh(PlaneMesh.Object);   // FlagMesh + GimmeRingMesh + HoleCupMesh are procedural, built later
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

	// Hole cup: built later by BuildHoleCup (terrain-draped procedural disc), so no static scale or
	// Z lift here -- the per-vertex traces place each vertex relative to the actor origin (= ground).

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
	// Hole cup: same masked disc, near-black, so the flag reads as standing in a hole. Stored as a
	// member so BuildHoleCup can re-apply it after each CreateMeshSection (GOL-211), like the gimme ring.
	HoleCupMID = MakeColorMID(GreenSrc, this, FLinearColor(0.02f, 0.02f, 0.02f));
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

	// Gimme ring material is the translucent M_GimmeRing, loaded + applied lazily in BuildGimmeRing
	// (so it picks up a freshly-authored asset at runtime without an editor restart).
}

void AGolfPinActor::BeginPlay()
{
	Super::BeginPlay();

	// GOL-211: re-trace the cup onto the terrain whenever the pin moves. Every placement path
	// (round SetActiveHole/SpawnAllHolePins, range ApplyPinDistance, putting PlacePuttOnGreen)
	// moves the actor, so this single hook covers them all -- no call-site changes needed.
	if (USceneComponent* Root = GetRootComponent())
	{
		Root->TransformUpdated.AddUObject(this, &AGolfPinActor::OnPinMoved);
	}
	BuildHoleCup();   // initial drape at the spawn location
}

void AGolfPinActor::OnPinMoved(USceneComponent* /*UpdatedComponent*/, EUpdateTransformFlags /*Flags*/, ETeleportType /*Teleport*/)
{
	BuildHoleCup();
}

void AGolfPinActor::BuildHoleCup()
{
	if (!HoleCupMesh) { return; }

	// A small filled disc (triangle fan) draped over the terrain: each rim vertex is traced down to
	// the landscape so the cup follows green undulation/slope instead of lying flat. Same per-vertex
	// trace idiom as BuildGimmeRing. (Deferred polish: a real recessed 3D cup + lip ring.)
	const double Radius = HoleCupDiameterUU * 0.5;
	constexpr int32 Segs = 24;

	UWorld* World = GetWorld();
	const FVector ActorLoc = GetActorLocation();
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GolfsimHoleCupTrace), /*bTraceComplex=*/true);
	TraceParams.AddIgnoredActor(this);

	auto LocalZAt = [&](double LocalX, double LocalY) -> double
	{
		if (!World) { return HoleCupLiftUU; }
		const double Wx = ActorLoc.X + LocalX, Wy = ActorLoc.Y + LocalY;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(Wx, Wy, ActorLoc.Z + 5000.0),
			FVector(Wx, Wy, ActorLoc.Z - 5000.0), ECC_WorldStatic, TraceParams))
		{
			return (Hit.ImpactPoint.Z - ActorLoc.Z) + HoleCupLiftUU;   // local Z (actor origin = ground)
		}
		return HoleCupLiftUU;   // flat fallback (off-landscape / not yet streamed -- a later move re-traces)
	};

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	const TArray<FLinearColor> NoColors;
	Verts.Reserve(Segs + 1);

	// Center vertex (index 0). UVs are all the disc center so the M_GolfGreen circular alpha mask
	// samples fully-opaque everywhere -- the geometry is already a circle, the mask must not clip it.
	Verts.Add(FVector(0.0, 0.0, LocalZAt(0.0, 0.0)));
	Normals.Add(FVector::UpVector);
	UVs.Add(FVector2D(0.5f, 0.5f));
	Tangents.Add(FProcMeshTangent(1, 0, 0));

	for (int32 s = 0; s < Segs; ++s)
	{
		const double A = (2.0 * PI) * ((double)s / Segs);
		const double X = FMath::Cos(A) * Radius, Y = FMath::Sin(A) * Radius;
		Verts.Add(FVector(X, Y, LocalZAt(X, Y)));
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(0.5f, 0.5f));
		Tangents.Add(FProcMeshTangent(1, 0, 0));
	}

	// Fan winding (rim_s, rim_s+1, center) matches BuildGimmeRing's up-facing front face.
	for (int32 s = 0; s < Segs; ++s)
	{
		Tris.Add(1 + s);
		Tris.Add(1 + ((s + 1) % Segs));
		Tris.Add(0);
	}

	HoleCupMesh->ClearAllMeshSections();
	HoleCupMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, NoColors, Tangents, /*bCreateCollision=*/false);
	if (HoleCupMID) { HoleCupMesh->SetMaterial(0, HoleCupMID); }
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
		if (GimmeText) { GimmeText->SetVisibility(false); }
		return;
	}
	BuildGimmeRing(RadiusFt * CmPerFt);   // outer radius in cm; also places the GIMME label
	GimmeRingMesh->SetVisibility(true);
}

void AGolfPinActor::SetGimmeApproachDir(const FVector& TeeToPinWorld)
{
	FVector D = TeeToPinWorld;
	D.Z = 0.0;
	GimmeApproachDirWorld = D.IsNearlyZero() ? FVector::ForwardVector : D.GetSafeNormal();
}

void AGolfPinActor::BuildGimmeRing(double OuterRadiusUU)
{
	if (!GimmeRingMesh) { return; }

	// Lazy-load the translucent halo material so a freshly-authored M_GimmeRing is picked up at
	// runtime (no editor restart). Missing on a fresh clone -> the ring draws with the engine default
	// until engine/scripts/build_gimme_ring_material.py has run.
	if (!GimmeRingMID)
	{
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GimmeRing.M_GimmeRing")))
		{
			GimmeRingMID = UMaterialInstanceDynamic::Create(Mat, this);
			if (GimmeRingMID)
			{
				GimmeRingMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.98f, 0.95f, 0.40f));
				GimmeRingMID->SetScalarParameterValue(TEXT("Opacity"), 0.38f);
			}
		}
	}

	// A thin DASHED annulus (BandUU wide) at OuterRadiusUU, draped over the terrain: each rim vertex is
	// traced down to the landscape so the band follows green undulation instead of floating/clipping flat.
	constexpr double BandUU = 22.0;   // ring thickness (cm)
	const double InnerRadius = FMath::Max(OuterRadiusUU - BandUU, 1.0);

	// Dashes: a few long arcs with gaps (target-reticle look). Fixed count regardless of gimme size.
	// Duty = fraction of each period that's solid (0.55 -> dash slightly longer than gap).
	constexpr int32 NumDashes = 8;
	constexpr double Duty = 0.55;
	constexpr int32 SubSegs = 4;   // arc segments per dash (keeps the longer arcs curved + terrain-followed)
	const double Period = (2.0 * PI) / NumDashes;
	const double DashArc = Period * Duty;

	UWorld* World = GetWorld();
	const FVector ActorLoc = GetActorLocation();
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GolfsimGimmeRingTrace), /*bTraceComplex=*/true);
	TraceParams.AddIgnoredActor(this);

	auto LocalZAt = [&](double LocalX, double LocalY) -> double
	{
		if (!World) { return GimmeRingLiftUU; }
		const double Wx = ActorLoc.X + LocalX, Wy = ActorLoc.Y + LocalY;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(Wx, Wy, ActorLoc.Z + 5000.0),
			FVector(Wx, Wy, ActorLoc.Z - 5000.0), ECC_WorldStatic, TraceParams))
		{
			return (Hit.ImpactPoint.Z - ActorLoc.Z) + GimmeRingLiftUU;   // local Z (actor origin = ground)
		}
		return GimmeRingLiftUU;   // flat fallback
	};

	// The flat GIMME label fills one gap on the near (tee) side of the ring. Phase the whole dash
	// pattern so dash 0 is centered exactly on the label angle; skipping it then leaves a gap
	// perfectly centered on the text (vs. skipping the "nearest" dash, which left it lopsided).
	const FVector Dir = GimmeApproachDirWorld;                  // tee -> pin (XY)
	const double GapAngle = FMath::Atan2(-Dir.Y, -Dir.X);      // near side = toward the tee
	const double Phase = GapAngle - DashArc * 0.5;

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	const TArray<FLinearColor> NoColors;
	Verts.Reserve(NumDashes * (SubSegs + 1) * 2);

	for (int32 d = 0; d < NumDashes; ++d)
	{
		if (d == 0) { continue; }   // leave the (phased) gap centered on the GIMME label
		const double Start = d * Period + Phase;
		const int32 Base = Verts.Num();
		for (int32 s = 0; s <= SubSegs; ++s)
		{
			const double A = Start + DashArc * ((double)s / SubSegs);
			const double Cos = FMath::Cos(A), Sin = FMath::Sin(A);
			const double IX = Cos * InnerRadius, IY = Sin * InnerRadius;
			const double OX = Cos * OuterRadiusUU, OY = Sin * OuterRadiusUU;
			Verts.Add(FVector(IX, IY, LocalZAt(IX, IY)));   // inner rim
			Verts.Add(FVector(OX, OY, LocalZAt(OX, OY)));   // outer rim
			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(0.f, (float)s / SubSegs));
			UVs.Add(FVector2D(1.f, (float)s / SubSegs));
			Tangents.Add(FProcMeshTangent(1, 0, 0));
			Tangents.Add(FProcMeshTangent(1, 0, 0));
		}
		for (int32 s = 0; s < SubSegs; ++s)
		{
			const int32 I0 = Base + s * 2, O0 = Base + s * 2 + 1, I1 = Base + (s + 1) * 2, O1 = Base + (s + 1) * 2 + 1;
			Tris.Add(I0); Tris.Add(O0); Tris.Add(I1);
			Tris.Add(O0); Tris.Add(O1); Tris.Add(I1);
		}
	}

	GimmeRingMesh->ClearAllMeshSections();
	GimmeRingMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, NoColors, Tangents, /*bCreateCollision=*/false);
	if (GimmeRingMID) { GimmeRingMesh->SetMaterial(0, GimmeRingMID); }

	// Lay the GIMME label flat in the skipped gap, on the near (tee) side, oriented to read upright as
	// the player approaches: face up (+Z), glyph-top toward the pin (+Dir). (If it reads mirrored or
	// upside-down on a hole, flip the glyph-up axis -- this is the finicky flat-text bit.)
	if (GimmeText)
	{
		const double MidRadius = (InnerRadius + OuterRadiusUU) * 0.5;
		const double Lx = -Dir.X * MidRadius, Ly = -Dir.Y * MidRadius;
		GimmeText->SetRelativeLocation(FVector(Lx, Ly, LocalZAt(Lx, Ly) + 1.0));
		GimmeText->SetRelativeRotation(
			FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector(Dir.X, Dir.Y, 0.0)).Rotator());
		GimmeText->SetWorldSize((float)FMath::Clamp(MidRadius * 0.20, 12.0, 36.0));   // scale to ring size
		GimmeText->SetVisibility(true);
	}
}

void AGolfPinActor::SetGreenSurfaceVisible(bool bVisible)
{
	// GOL-191: the synthetic green disc + fringe collar. Course pins hide these so the painted
	// splat_green landscape is the green; the hole cup, pole, flag and gimme ring stay.
	if (DiscMesh)   { DiscMesh->SetVisibility(bVisible); }
	if (CollarMesh) { CollarMesh->SetVisibility(bVisible); }
}
