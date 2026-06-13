// In-world green-reading break grid (GOL-203): a terrain-draped procedural mesh over the
// green's slope-grid footprint, rendered with the M_GreenFlow flow-map material -- animated
// dots drift downhill along the local break, speed/heat scaled by slope (PGA-2K style).
//
// Geometry reuses the corner heights the HUD already traced for the GolfMap::FGreenSlopeGrid
// (no re-tracing); the flow field rides in a small runtime texture (UI/GreenFlowTexture). The
// mesh is a rectangle over the grid -- the material's alpha (flow-texture A channel, bilinear)
// feathers it to the green polygon, and quads with no on-green cell in reach are skipped on the
// CPU to kill margin overdraw. Vertex colors carry (dir, slope, mask) too, so a texture-free
// fallback material can drive the same mesh. AGolfRangeHUD find-or-spawns one per green
// (same ownership idiom as AGolfPinActor).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/HoleMapProjection.h"
#include "GreenBreakGridActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

UCLASS()
class GOLFSIM_API AGreenBreakGridActor : public AActor
{
	GENERATED_BODY()

public:
	AGreenBreakGridActor();

	// (Re)build the draped mesh + flow texture from a slope grid and the corner heights that
	// produced it ((NX+1)*(NY+1), row-major, GolfMap::InvalidHeightCm for trace misses).
	void BuildFromGrid(const GolfMap::FGreenSlopeGrid& Grid, const TArray<double>& CornerHeightsCm);

	void SetGridVisible(bool bVisible);
	bool IsGridVisible() const;

	// Hover height above the traced turf (cm): above the GOL-191 green lift (2), below the
	// gimme ring (4), so the overlay never z-fights either.
	static constexpr float GridLiftUU = 3.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UProceduralMeshComponent> GridMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GridMID;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> FlowTexture;   // keeps the transient texture alive
};
