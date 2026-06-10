// Practice-range target pin: a flat green disc with a flagpole + cloth flag standing on top.
// Pure C++, code-only meshes (engine BasicShapes). No collision -- the ground trace that places
// the pin must not intersect the disc itself. AGolfRangeHUD find-or-spawns one and moves it
// when the player changes the pin distance; see GOL-29.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GolfPinActor.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class GOLFSIM_API AGolfPinActor : public AActor
{
	GENERATED_BODY()

public:
	AGolfPinActor();

	// Default 15-yard (~13.7 m) green diameter. Real practice-target greens read at this scale from
	// 200+ yd; the 6-yd v1 was too small. Future ticket: proper green material/asset + UI to size.
	static constexpr double DefaultDiameterMeters = 15.0 * 0.9144;

	// Resize the green (m); the collar ring tracks it, flagpole + flag + hole cup are unaffected.
	void SetGreenDiameterMeters(double DiameterM);

	// GOL-123: scale the gimme-ring overlay to a radius in FEET (matches FSwingDifficultyProfile).
	// Pass 0 (or negative) to hide the ring (e.g. range pin -- no gimme to visualize).
	void SetGimmeRadiusFt(double RadiusFt);

	// GOL-191: hide/show the synthetic green surface (the flat disc + fringe collar). Course pins call
	// this false so the painted splat_green landscape reads as the green; the range keeps it (the disc
	// is the range's target green). Pole/flag/hole-cup/gimme-ring are unaffected.
	void SetGreenSurfaceVisible(bool bVisible);

private:
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> DiscMesh;
	// Collar/fringe: a darker, duller textured disc UNDER the green, slightly larger, so a ring of it
	// shows around the green's edge (reads as the fringe cut). Tracks the green size.
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> CollarMesh;
	// Hole cup: a small dark disc at the pole base so the flag sits in a hole, not on flat turf.
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> HoleCupMesh;
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> PoleMesh;
	// GOL-165: a subdivided procedural grid (not the 2-tri engine Plane) so the M_FlagWind material's
	// World Position Offset can ripple it like cloth.
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UProceduralMeshComponent> FlagMesh;
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> GimmeRingMesh;   // GOL-123
};
