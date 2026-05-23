// Practice-range environment director. One actor that owns the range's time-of-day + weather
// state and applies presets at runtime by nudging the level's EXISTING sky kit -- the canonical
// UE pattern (a single director actor holding references to the sun / sky light / fog / cloud,
// like BP_Sky_Sphere or Ultra Dynamic Sky). AGolfRangeHUD's panel dropdowns and the
// golfsim.SetTime / golfsim.SetSky console commands drive it.
//
// The range's UE5 Basic-level sky already ships a DirectionalLight (atmosphere sun), a Movable +
// RealTimeCapture SkyLight, ExponentialHeightFog, SkyAtmosphere and a VolumetricCloud, so this
// actor SPAWNS NOTHING -- it finds those by class and mutates them (mirrors build_range_lighting.py,
// but at runtime across several presets). Real-time capture means ambient tracks the sun on its
// own, so there's no manual SkyLight recapture. Range-only: the HUD find-or-spawns it in PIE, so
// the editor viewport keeps its authored look and BethPage is untouched.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GolfRangeEnvironment.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class AVolumetricCloud;
class UMaterialInstanceDynamic;

// Time-of-day preset. SunPitch < 0 = above the horizon (matches build_range_lighting.py's
// SUN_PITCH convention). SunIntensity is in the directional light's lux units; the Morning
// defaults reproduce the range's current golden-hour look (pitch -9, 5000 K, 6 lux, fog 0.03).
USTRUCT()
struct FGolfTimePreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Time") FString Name;
	UPROPERTY(EditAnywhere, Category = "Time") float SunPitch = -9.f;
	UPROPERTY(EditAnywhere, Category = "Time") float SunYaw = 35.f;
	UPROPERTY(EditAnywhere, Category = "Time") float TemperatureK = 5000.f;
	UPROPERTY(EditAnywhere, Category = "Time") float SunIntensity = 6.f;
	UPROPERTY(EditAnywhere, Category = "Time") float FogDensity = 0.03f;
	UPROPERTY(EditAnywhere, Category = "Time") float SkyLightScale = 1.f;
};

// Sky/weather preset. Coverage/density drive the cloud material's Cloud_GlobalCoverage /
// Cloud_GlobalDensity scalars; the scales/adds layer on top of the active Time preset so the two
// dropdowns compose (e.g. Overcast dims whatever sun the time set rather than resetting it).
USTRUCT()
struct FGolfSkyPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Sky") FString Name;
	UPROPERTY(EditAnywhere, Category = "Sky") float CloudCoverage = 0.1f;
	UPROPERTY(EditAnywhere, Category = "Sky") float CloudDensity = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Sky") float SunIntensityScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Sky") float FogDensityAdd = 0.f;
	UPROPERTY(EditAnywhere, Category = "Sky") float SkyLightScale = 1.f;
};

UCLASS()
class GOLFSIM_API AGolfRangeEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AGolfRangeEnvironment();

	virtual void BeginPlay() override;

	// Pick a Time / Sky preset by index and re-apply. Clamped; out-of-range is ignored safely.
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Range") void SetTime(int32 Index);
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Range") void SetSky(int32 Index);

	// Recompute final sun / fog / ambient / cloud from the active Time x Sky pair and push to the level.
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Range") void ApplyEnvironment();

	// Dropdown sources (the HUD reads these to populate the combo boxes -- single source of truth).
	TArray<FString> GetTimePresetNames() const;
	TArray<FString> GetSkyPresetNames() const;
	int32 GetTimeIndex() const { return CurrentTimeIdx; }
	int32 GetSkyIndex() const { return CurrentSkyIdx; }

protected:
	UPROPERTY(EditAnywhere, Category = "Golfsim|Range") TArray<FGolfTimePreset> TimePresets;
	UPROPERTY(EditAnywhere, Category = "Golfsim|Range") TArray<FGolfSkyPreset> SkyPresets;
	UPROPERTY(EditAnywhere, Category = "Golfsim|Range") int32 CurrentTimeIdx = 1;   // Morning
	UPROPERTY(EditAnywhere, Category = "Golfsim|Range") int32 CurrentSkyIdx = 0;    // Clear

private:
	void EnsureRefs();   // find the 1-of-each sky actors + build the cloud MID, once

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLightActor;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog;
	UPROPERTY(Transient) TObjectPtr<AVolumetricCloud> Cloud;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	bool bRefsReady = false;
};
