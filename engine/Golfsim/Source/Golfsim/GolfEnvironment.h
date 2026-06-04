// Environment director. One actor that owns a level's time-of-day + weather (+ post-process) state
// and applies presets at runtime by nudging the level's EXISTING sky kit -- the canonical UE pattern
// (a single director holding references to the sun / sky light / fog / cloud, like BP_Sky_Sphere or
// Ultra Dynamic Sky). Shared by both maps (renamed from AGolfRangeEnvironment in GOL-161 -- atmosphere
// is a dynamic, course-agnostic domain that GOL-154 live weather also rides, not a per-map one-off):
//
//   - Range: AGolfRangeHUD find-or-spawns one in PIE; the panel's Time/Sky dropdowns and the
//     golfsim.SetTime / golfsim.SetSky console commands drive it across the Time x Sky matrix. PIE-only,
//     so the editor viewport keeps its authored look and nothing is written into the range umap.
//   - Course: one is PLACED in GolfForgeDemoBlack.umap, configured to the canonical "Afternoon" preset
//     with bApplyPostProcess on. It applies at BeginPlay (alpha-3, Lumen on); the editor "Apply To
//     Level" button stamps the preset into the placed sun/fog/skylight actors so GOL-101's Lightmass
//     bake captures the same look later with no rework.
//
// The level's UE5 Basic-level sky already ships a DirectionalLight, a Movable + RealTimeCapture
// SkyLight, ExponentialHeightFog, SkyAtmosphere and a VolumetricCloud, so this SPAWNS only the
// PostProcessVolume (course look) and finds the rest by class. Real-time capture means ambient tracks
// the sun on its own, so there's no manual SkyLight recapture.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GolfEnvironment.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class AVolumetricCloud;
class APostProcessVolume;
class UMaterialInstanceDynamic;

// Time-of-day preset. SunPitch < 0 = above the horizon (matches build_range_lighting.py's SUN_PITCH
// convention). SunIntensity is in the directional light's intensity units; the Morning defaults
// reproduce the range's current golden-hour look (pitch -9, 5000 K, 6, fog 0.03).
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

// Post-process look (GOL-161). Applied to an unbound PostProcessVolume only when bApplyPostProcess is
// on (course); the range leaves it off so its authored look is untouched. AutoExposure min == max locks
// exposure to a fixed brightness band (outdoor scenes read best without auto-exposure pumping). The two
// tints map to the color-grade gain wheels (cool shadows / warm highlights = the "golf afternoon" grade).
USTRUCT()
struct FGolfPostProcessPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "PostProcess") FString Name;
	UPROPERTY(EditAnywhere, Category = "PostProcess") float ExposureCompensation = 0.f;
	UPROPERTY(EditAnywhere, Category = "PostProcess") float AutoExposureMinBrightness = 1.f;
	UPROPERTY(EditAnywhere, Category = "PostProcess") float AutoExposureMaxBrightness = 1.f;
	UPROPERTY(EditAnywhere, Category = "PostProcess") float BloomIntensity = 0.6f;
	UPROPERTY(EditAnywhere, Category = "PostProcess") float VignetteIntensity = 0.4f;
	UPROPERTY(EditAnywhere, Category = "PostProcess") FLinearColor ShadowTint = FLinearColor(0.95f, 0.98f, 1.05f, 1.f);
	UPROPERTY(EditAnywhere, Category = "PostProcess") FLinearColor HighlightTint = FLinearColor(1.05f, 1.0f, 0.95f, 1.f);
};

UCLASS()
class GOLFSIM_API AGolfEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AGolfEnvironment();

	virtual void BeginPlay() override;

	// Pick a Time / Sky preset by index and re-apply. Clamped; out-of-range is ignored safely.
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Environment") void SetTime(int32 Index);
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Environment") void SetSky(int32 Index);

	// Recompute final sun / fog / ambient / cloud (+ post-process if enabled) from the active Time x Sky
	// pair and push to the level.
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Environment") void ApplyEnvironment();

	// Live editor/PIE tuning (GOL-161): drive the sun angle/warmth/brightness directly from a 0-24 hour,
	// bypassing the discrete presets. Backs the golfsim.SetTimeOfDay console command.
	UFUNCTION(BlueprintCallable, Category = "Golfsim|Environment") void ApplyTimeOfDayHour(float Hour);

	// Dropdown sources (the HUD reads these to populate the combo boxes -- single source of truth).
	TArray<FString> GetTimePresetNames() const;
	TArray<FString> GetSkyPresetNames() const;
	int32 GetTimeIndex() const { return CurrentTimeIdx; }
	int32 GetSkyIndex() const { return CurrentSkyIdx; }

#if WITH_EDITOR
	// Stamp the active preset into the placed sky actors from the editor (no PIE), so saving the umap
	// persists the canonical look and GOL-101's Lightmass bake captures it. Shows as a button in Details.
	UFUNCTION(CallInEditor, Category = "Golfsim|Environment") void ApplyToLevel();
#endif

protected:
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") TArray<FGolfTimePreset> TimePresets;
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") TArray<FGolfSkyPreset> SkyPresets;
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") int32 CurrentTimeIdx = 1;   // Morning
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") int32 CurrentSkyIdx = 0;    // Clear

	// Course look. Off by default so the range is unaffected; the placed course director turns it on.
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") bool bApplyPostProcess = false;
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") FGolfPostProcessPreset PostProcess;

	// Re-apply the active preset at BeginPlay (true for both maps; an escape hatch for a placed course
	// director that wants its editor-stamped values to be authoritative without a runtime re-apply).
	UPROPERTY(EditAnywhere, Category = "Golfsim|Environment") bool bApplyOnBeginPlay = true;

private:
	void EnsureRefs();          // find the 1-of-each sky actors, once
	void EnsureCloudMID();      // wrap the cloud material in a dynamic instance (runtime only)
	void ApplyEnvironmentInternal(bool bIncludeClouds);   // bIncludeClouds=false for the editor stamp
	void ApplyPostProcessVolume();   // find-or-spawn the unbound PostProcessVolume and push PostProcess

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLightActor;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog;
	UPROPERTY(Transient) TObjectPtr<AVolumetricCloud> Cloud;
	UPROPERTY(Transient) TObjectPtr<APostProcessVolume> PostVolume;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	bool bRefsReady = false;
};
