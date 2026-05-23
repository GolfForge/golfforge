#include "GolfRangeEnvironment.h"

#include "EngineUtils.h"   // TActorIterator
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"   // declares AVolumetricCloud + the component
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/SceneComponent.h"

AGolfRangeEnvironment::AGolfRangeEnvironment()
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
	//                      Name            Cover  Dens   SunScale FogAdd  SkyScale
	SkyPresets.Add(MakeSky(TEXT("Clear"),    0.05f, 0.3f, 1.00f, 0.000f, 1.00f));
	SkyPresets.Add(MakeSky(TEXT("Cloudy"),   0.45f, 0.5f, 0.85f, 0.005f, 1.05f));
	SkyPresets.Add(MakeSky(TEXT("Overcast"), 0.85f, 0.7f, 0.45f, 0.012f, 1.15f));
}

void AGolfRangeEnvironment::BeginPlay()
{
	Super::BeginPlay();
	EnsureRefs();
	ApplyEnvironment();   // take control of the sky immediately (defaults = Morning x Clear)
}

void AGolfRangeEnvironment::EnsureRefs()
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
	// Exactly one of each ships in the range's Basic level (verified); take the first found.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLightActor = *It; break; }
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }
	for (TActorIterator<AVolumetricCloud> It(World); It; ++It) { Cloud = *It; break; }

	// Wrap the cloud material in a dynamic instance so Cloud_GlobalCoverage / Cloud_GlobalDensity
	// are settable per preset. Base = the cloud's current material (the engine
	// m_SimpleVolumetricCloud_Inst), falling back to loading it by path if unresolved.
	if (Cloud)
	{
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
	bRefsReady = true;
}

void AGolfRangeEnvironment::SetTime(int32 Index)
{
	if (TimePresets.Num() == 0)
	{
		return;
	}
	CurrentTimeIdx = FMath::Clamp(Index, 0, TimePresets.Num() - 1);
	ApplyEnvironment();
}

void AGolfRangeEnvironment::SetSky(int32 Index)
{
	if (SkyPresets.Num() == 0)
	{
		return;
	}
	CurrentSkyIdx = FMath::Clamp(Index, 0, SkyPresets.Num() - 1);
	ApplyEnvironment();
}

void AGolfRangeEnvironment::ApplyEnvironment()
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

	// Clouds: coverage + density via the dynamic cloud material.
	if (CloudMID)
	{
		CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), S.CloudCoverage);
		CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), S.CloudDensity);
	}

	UE_LOG(LogTemp, Display,
		TEXT("golfsim range env: Time=%s Sky=%s sun=%.1flux temp=%.0fK fog=%.3f cover=%.2f"),
		*T.Name, *S.Name, T.SunIntensity * S.SunIntensityScale, T.TemperatureK,
		T.FogDensity + S.FogDensityAdd, S.CloudCoverage);
}

TArray<FString> AGolfRangeEnvironment::GetTimePresetNames() const
{
	TArray<FString> Names;
	Names.Reserve(TimePresets.Num());
	for (const FGolfTimePreset& P : TimePresets) { Names.Add(P.Name); }
	return Names;
}

TArray<FString> AGolfRangeEnvironment::GetSkyPresetNames() const
{
	TArray<FString> Names;
	Names.Reserve(SkyPresets.Num());
	for (const FGolfSkyPreset& P : SkyPresets) { Names.Add(P.Name); }
	return Names;
}
