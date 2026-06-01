// Practice-range target pin: a flat green disc with a flagpole + cloth flag standing on top.
// Pure C++, code-only meshes (engine BasicShapes). No collision -- the ground trace that places
// the pin must not intersect the disc itself. AGolfRangeHUD find-or-spawns one and moves it
// when the player changes the pin distance; see GOL-29.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GolfPinActor.generated.h"

class UStaticMeshComponent;
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

	// Resize the green (m); flagpole + flag are unaffected.
	void SetGreenDiameterMeters(double DiameterM);

private:
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> DiscMesh;
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> PoleMesh;
	UPROPERTY(VisibleAnywhere, Category = "Golfsim") TObjectPtr<UStaticMeshComponent> FlagMesh;
};
