#include "GolfEnvironment.h"

#include "EngineUtils.h"   // TActorIterator
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"   // declares AVolumetricCloud + the component
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/SceneComponent.h"

AGolfEnvironment::AGolfEnvironment()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));   // logic-only actor; a root keeps spawn quiet

	// Code-seeded presets (editable per-instance in Details if the actor is ever placed). Morning
	// reproduces the range's current look so PIE starts unchanged; the rest are dialed in PIE.
	// Built field-by-field (USTRUCTs aren't reliably aggregates, so no brace-init of the structs).
	auto MakeTime = [](const TCHAR* N, float Pitch, float Yaw, float K, float Sun, float Fog, float Sky)
	{
		FGolfTimePreset P;
		P.Name = N; P.SunPitch = Pitch; P.SunYaw = Yaw; P.TemperatureK = K;
		P.SunIntensity = Sun; P.FogDensity = Fog; P.SkyLightScale = Sky;
		return P;
	};
	auto MakeSky = [](const TCHAR* N, float Cover, float Dens, float SunScale, float FogAdd, float Sky)
	{
		FGolfSkyPreset P;
		P.Name = N; P.CloudCoverage = Cover; P.CloudDensity = Dens;
		P.SunIntensityScale = SunScale; P.FogDensityAdd = FogAdd; P.SkyLightScale = Sky;
		return P;
	};
	//                       Name          Pitch   Yaw    TempK   Sun   Fog     SkyScale
	TimePresets.Add(MakeTime(TEXT("Dawn"),    -4.f,  75.f, 3000.f, 3.0f, 0.040f, 0.7f));
	TimePresets.Add(MakeTime(TEXT("Morning"), -9.f,  35.f, 5000.f, 6.0f, 0.030f, 1.0f));
	TimePresets.Add(MakeTime(TEXT("Noon"),   -55.f,  10.f, 6500.f, 9.0f, 0.020f, 1.0f));
	TimePresets.Add(MakeTime(TEXT("Dusk"),    -3.f, -60.f, 2600.f, 3.0f, 0.045f, 0.7f));
	TimePresets.Add(MakeTime(TEXT("Night"),  -10.f, 200.f, 9000.f, 0.5f, 0.050f, 0.3f));
	// GOL-161 canonical "tournament Saturday afternoon": sun ~35deg above the horizon, warm-neutral
	// 5000K, full open-air brightness, thin fog. This is the course's placed-director default (set
	// CurrentTimeIdx=5 + bApplyPostProcess on the placed instance); also a usable range time-of-day.
	TimePresets.Add(MakeTime(TEXT("Afternoon"), -35.f, 50.f, 5000.f, 8.0f, 0.018f, 1.1f));
	//                      Name            Cover  Dens   SunScale FogAdd  SkyScale
	SkyPresets.Add(MakeSky(TEXT("Clear"),    0.05f, 0.3f, 1.00f, 0.000f, 1.00f));
	SkyPresets.Add(MakeSky(TEXT("Cloudy"),   0.45f, 0.5f, 0.85f, 0.005f, 1.05f));
	SkyPresets.Add(MakeSky(TEXT("Overcast"), 0.85f, 0.7f, 0.45f, 0.012f, 1.15f));

	// Course post-process look (only applied when bApplyPostProcess is on -- the placed course director).
	// Locked exposure (min==max) so the open-air scene doesn't pump; subtle bloom; gentle vignette; a cool
	// shadow / warm highlight grade. Range leaves bApplyPostProcess off so its authored look is untouched.
	PostProcess.Name = TEXT("CourseAfternoon");
	PostProcess.ExposureCompensation = 1.0f;    // AutoExposureBias EV (UE-neutral seed; tune up in PIE)
	PostProcess.AutoExposureMinBrightness = 1.0f;
	PostProcess.AutoExposureMaxBrightness = 1.0f;
	PostProcess.BloomIntensity = 0.5f;
	PostProcess.VignetteIntensity = 0.4f;
	PostProcess.ShadowTint = FLinearColor(0.95f, 0.98f, 1.05f, 1.f);
	PostProcess.HighlightTint = FLinearColor(1.05f, 1.0f, 0.95f, 1.f);
}

void AGolfEnvironment::BeginPlay()
{
	Super::BeginPlay();
	if (bApplyOnBeginPlay)
	{
		ApplyEnvironment();   // take control of the sky immediately (range defaults = Morning x Clear)
	}
}

void AGolfEnvironment::EnsureRefs()
{
	if (bRefsReady)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Exactly one of each ships in a UE5 Basic level (verified); take the first found.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLightActor = *It; break; }
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	for (TActorIterator<AVolumetricCloud> It(World); It; ++It) { Cloud = *It; break; }
	bRefsReady = true;
}

void AGolfEnvironment::EnsureCloudMID()
{
	// Wrap the cloud material in a dynamic instance so Cloud_GlobalCoverage / Cloud_GlobalDensity are
	// settable per preset. Base = the cloud's current material (the engine m_SimpleVolumetricCloud_Inst),
	// falling back to loading it by path if unresolved. Runtime only -- the MID is transient, so we never
	// assign it to a PLACED cloud at edit time (that would leave an unsaveable material on the umap).
	if (CloudMID || !Cloud)
	{
		return;
	}
	if (UVolumetricCloudComponent* CC = Cloud->FindComponentByClass<UVolumetricCloudComponent>())
	{
		UMaterialInterface* Base = CC->GetMaterial();
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst"));
		}
		if (Base)
		{
			CloudMID = UMaterialInstanceDynamic::Create(Base, this);
			CC->SetMaterial(CloudMID);
		}
	}
}

void AGolfEnvironment::SetTime(int32 Index)
{
	if (TimePresets.Num() == 0)
	{
		return;
	}
	CurrentTimeIdx = FMath::Clamp(Index, 0, TimePresets.Num() - 1);
	ApplyEnvironment();
}

void AGolfEnvironment::SetSky(int32 Index)
{
	if (SkyPresets.Num() == 0)
	{
		return;
	}
	CurrentSkyIdx = FMath::Clamp(Index, 0, SkyPresets.Num() - 1);
	ApplyEnvironment();
}

void AGolfEnvironment::ApplyEnvironment()
{
	ApplyEnvironmentInternal(/*bIncludeClouds=*/true);
}

void AGolfEnvironment::ApplyEnvironmentInternal(bool bIncludeClouds)
{
	EnsureRefs();
	if (TimePresets.Num() == 0 || SkyPresets.Num() == 0)
	{
		return;
	}
	const FGolfTimePreset& T = TimePresets[FMath::Clamp(CurrentTimeIdx, 0, TimePresets.Num() - 1)];
	const FGolfSkyPreset&  S = SkyPresets[FMath::Clamp(CurrentSkyIdx, 0, SkyPresets.Num() - 1)];

	// Sun: orientation + warmth + brightness. Sky scales the brightness so weather dims the sun.
	if (Sun)
	{
		Sun->SetActorRotation(FRotator(T.SunPitch, T.SunYaw, 0.f));   // (Pitch, Yaw, Roll)
		if (ULightComponent* LC = Sun->GetLightComponent())
		{
			LC->SetUseTemperature(true);
			LC->SetTemperature(T.TemperatureK);
			LC->SetIntensity(T.SunIntensity * S.SunIntensityScale);
		}
	}

	// Fog: time haze plus a weather bump.
	if (Fog)
	{
		if (UExponentialHeightFogComponent* FC = Fog->FindComponentByClass<UExponentialHeightFogComponent>())
		{
			FC->SetFogDensity(T.FogDensity + S.FogDensityAdd);
		}
	}

	// Ambient: the SkyLight is real-time-capture, so it tracks the new sun on its own; we only
	// scale its intensity (night darkens, overcast flattens brighter).
	if (SkyLightActor)
	{
		if (USkyLightComponent* SC = SkyLightActor->GetLightComponent())
		{
			SC->SetIntensity(T.SkyLightScale * S.SkyLightScale);
		}
	}

	// Clouds: coverage + density via the dynamic cloud material. Runtime only (see EnsureCloudMID).
	if (bIncludeClouds)
	{
		EnsureCloudMID();
		if (CloudMID)
		{
			CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), S.CloudCoverage);
			CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), S.CloudDensity);
		}
	}

	// Post-process: course look only. Off on the range so its authored grade is untouched.
	if (bApplyPostProcess)
	{
		ApplyPostProcessVolume();
	}

	UE_LOG(LogTemp, Display,
		TEXT("golfsim env: Time=%s Sky=%s sun=%.1f temp=%.0fK fog=%.3f cover=%.2f post=%d"),
		*T.Name, *S.Name, T.SunIntensity * S.SunIntensityScale, T.TemperatureK,
		T.FogDensity + S.FogDensityAdd, S.CloudCoverage, bApplyPostProcess ? 1 : 0);
}

void AGolfEnvironment::ApplyPostProcessVolume()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Find an existing unbound PostProcessVolume (one canonical course volume); spawn one if absent.
	if (!PostVolume)
	{
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->bUnbound)
			{
				PostVolume = *It;
				break;
			}
		}
	}
	if (!PostVolume)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PostVolume = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (PostVolume)
		{
			PostVolume->bUnbound = true;
			PostVolume->BlendWeight = 1.f;
		}
	}
	if (!PostVolume)
	{
		return;
	}

	FPostProcessSettings& PP = PostVolume->Settings;

	// Lock exposure to a fixed band (min==max) so the open-air scene reads consistently; bias = EV target.
	PP.bOverride_AutoExposureMinBrightness = true;
	PP.AutoExposureMinBrightness = PostProcess.AutoExposureMinBrightness;
	PP.bOverride_AutoExposureMaxBrightness = true;
	PP.AutoExposureMaxBrightness = PostProcess.AutoExposureMaxBrightness;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = PostProcess.ExposureCompensation;

	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = PostProcess.BloomIntensity;

	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = PostProcess.VignetteIntensity;

	// Color grade: cool shadows / warm highlights via the gain wheels (W = overall, keep at 1).
	PP.bOverride_ColorGainShadows = true;
	PP.ColorGainShadows = FVector4(PostProcess.ShadowTint.R, PostProcess.ShadowTint.G, PostProcess.ShadowTint.B, 1.f);
	PP.bOverride_ColorGainHighlights = true;
	PP.ColorGainHighlights = FVector4(PostProcess.HighlightTint.R, PostProcess.HighlightTint.G, PostProcess.HighlightTint.B, 1.f);
}

void AGolfEnvironment::ApplyTimeOfDayHour(float Hour)
{
	EnsureRefs();
	if (!Sun)
	{
		return;
	}
	// Simple day arc: sun rides from the horizon at 6h up to its zenith at 12h and back down by 18h;
	// before 6 / after 18 it sits just below the horizon (night). Elevation 0..1 drives pitch, warmth
	// and brightness; yaw sweeps east->west so the shadows track across the scene during live tuning.
	const float HourClamped = FMath::Clamp(Hour, 0.f, 24.f);
	const float DayFrac = FMath::Clamp((HourClamped - 6.f) / 12.f, 0.f, 1.f);   // 0 at 6h/18h, 1 at noon
	const float Elevation = FMath::Sin(PI * DayFrac);                            // 0..1..0 across the day
	const bool bDaylight = HourClamped > 6.f && HourClamped < 18.f;

	const float Pitch = bDaylight ? -(2.f + Elevation * 58.f) : 8.f;   // above horizon by day, below by night
	const float Yaw = FMath::Lerp(80.f, -80.f, FMath::Clamp(HourClamped / 24.f, 0.f, 1.f));
	const float TempK = FMath::Lerp(2800.f, 6500.f, Elevation);        // warm at the edges, neutral at noon
	const float Intensity = bDaylight ? FMath::Lerp(1.5f, 9.f, Elevation) : 0.3f;

	Sun->SetActorRotation(FRotator(Pitch, Yaw, 0.f));
	if (ULightComponent* LC = Sun->GetLightComponent())
	{
		LC->SetUseTemperature(true);
		LC->SetTemperature(TempK);
		LC->SetIntensity(Intensity);
	}
	UE_LOG(LogTemp, Display,
		TEXT("golfsim env: TimeOfDay=%.1fh pitch=%.1f yaw=%.1f temp=%.0fK intensity=%.1f"),
		HourClamped, Pitch, Yaw, TempK, Intensity);
}

#if WITH_EDITOR
void AGolfEnvironment::ApplyToLevel()
{
	// Editor stamp (no PIE): write the active preset into the placed sky actors so saving the umap
	// persists the canonical look (and GOL-101's Lightmass bake captures it). Skip the cloud MID -- it's
	// transient and runtime-only; clouds aren't baked. Mark the touched actors dirty so Save offers them.
	ApplyEnvironmentInternal(/*bIncludeClouds=*/false);
	if (Sun) { Sun->Modify(); }
	if (Fog) { Fog->Modify(); }
	if (SkyLightActor) { SkyLightActor->Modify(); }
	if (PostVolume) { PostVolume->Modify(); }
	UE_LOG(LogTemp, Display, TEXT("golfsim env: ApplyToLevel stamped preset into placed actors (save the umap to persist)"));
}
#endif

TArray<FString> AGolfEnvironment::GetTimePresetNames() const
{
	TArray<FString> Names;
	Names.Reserve(TimePresets.Num());
	for (const FGolfTimePreset& P : TimePresets) { Names.Add(P.Name); }
	return Names;
}

TArray<FString> AGolfEnvironment::GetSkyPresetNames() const
{
	TArray<FString> Names;
	Names.Reserve(SkyPresets.Num());
	for (const FGolfSkyPreset& P : SkyPresets) { Names.Add(P.Name); }
	return Names;
}
