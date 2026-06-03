// Display-settings data + logic, split from the UI widget so the parse/clamp helpers are unit-testable
// (no UObject, no UMG, no world). Apply/ReadCurrent touch UGameUserSettings (PIE-verified). GOL-52.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

// WindowModeIndex: 0 = Windowed, 1 = Borderless (WindowedFullscreen), 2 = Fullscreen. Kept as an int
// index (not EWindowMode) so this header stays engine-enum-free and POD; mapped in Apply/ReadCurrent.
struct FGolfDisplaySettings
{
	FIntPoint Resolution = FIntPoint(1920, 1080);
	int32 WindowModeIndex = 0;
	int32 QualityLevel = 3;       // 0=Low 1=Medium 2=High 3=Epic
	float ScreenPercentage = 100.f;
	int32 UpscalerIndex = 0;      // 0=TSR (built-in) 1=DLSS 2=XeSS  (FSR deferred)
};

namespace GolfDisplay
{
	// Pure (unit-tested):
	TOptional<FIntPoint> ParseResolution(const FString& In);   // "1920x1080" -> (1920,1080); else unset
	int32 ClampQualityLevel(int32 Level);                      // -> [0,3]
	int32 ClampWindowModeIndex(int32 Index);                   // -> [0,2]
	int32 ClampUpscalerIndex(int32 Index);                     // -> [0,2]
	FString UpscalerName(int32 Index);                         // 0=TSR 1=DLSS 2=XeSS
	TArray<int32> AvailableUpscalerIndices();                  // TSR always; DLSS if NVIDIA + plugin; XeSS if plugin
	TArray<FString> UpscaleModeNames(int32 UpscalerIndex);          // per-upscaler tier names, high->low quality
	float ScreenPctForMode(int32 UpscalerIndex, int32 ModeIndex);   // tier -> render-scale %
	int32 ModeForScreenPct(int32 UpscalerIndex, float Pct);         // render-scale % -> nearest tier
	FString CreditsText();                                     // attribution block (sync w/ ATTRIBUTION.md)

	// Engine-touching (PIE-verified):
	FGolfDisplaySettings ReadCurrent();                        // from UGameUserSettings + r.ScreenPercentage
	void Apply(const FGolfDisplaySettings& S);                 // write + ApplyResolutionSettings + SaveSettings
	TArray<FIntPoint> SupportedResolutions();                  // monitor modes, or a sane fallback list

	// Range pin distance (GOL-29). Stored in the same GameUserSettings.ini that Apply() writes, under
	// section [GolfForge.Range], key PinDistanceYd. Default 150 yd if unset; clamped to [0, 400] (the
	// range corridor's LANE_LEN_YD). These are split out from FGolfDisplaySettings because they're a
	// range-specific gameplay setting, not part of the display-settings round-trip.
	double ReadPinDistanceYd();
	void WritePinDistanceYd(double Yards);

	// Pre-round picker player name (GOL-121). Section [GolfForge.Round], key PlayerName. Default
	// falls back to FPlatformProcess::UserName() when the ini entry is absent. Same .ini as the
	// pin-distance helper; the section is split so round-specific keys don't crowd range-specific.
	FString ReadPlayerName();
	void WritePlayerName(const FString& Name);

	// Player handicap (GOL-143). Same [GolfForge.Round] section, key Handicap. Default 0; clamped to
	// [0, 54]. Stored for the round-setup wizard's player row; net scoring that consumes it is GOL-69.
	int32 ReadHandicap();
	void WriteHandicap(int32 Handicap);
}
