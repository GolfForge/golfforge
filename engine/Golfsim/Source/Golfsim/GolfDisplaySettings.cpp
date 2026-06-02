#include "GolfDisplaySettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"   // GConfig + GGameUserSettingsIni for the pin-distance helpers
#include "HAL/PlatformProcess.h"   // FPlatformProcess::UserName for the player-name default (GOL-121)
#include "RHI.h"   // IsRHIDeviceNVIDIA() -- gate DLSS to NVIDIA GPUs

namespace GolfDisplay
{
	TOptional<FIntPoint> ParseResolution(const FString& In)
	{
		FString L, R;
		if (!In.Split(TEXT("x"), &L, &R) && !In.Split(TEXT("X"), &L, &R))
		{
			return TOptional<FIntPoint>();
		}
		const int32 W = FCString::Atoi(*L);
		const int32 H = FCString::Atoi(*R);
		if (W <= 0 || H <= 0)
		{
			return TOptional<FIntPoint>();
		}
		return FIntPoint(W, H);
	}

	int32 ClampQualityLevel(int32 Level) { return FMath::Clamp(Level, 0, 3); }
	int32 ClampWindowModeIndex(int32 Index) { return FMath::Clamp(Index, 0, 2); }
	int32 ClampUpscalerIndex(int32 Index) { return FMath::Clamp(Index, 0, 2); }

	// Vendor-plugin cvars, driven by name (not by linking the plugins) so this builds with or without
	// them installed; an absent cvar means that upscaler isn't available. Verified against the UE 5.7
	// NVIDIA DLSS + Intel XeSS plugins. FSR deferred (GOL-62): r.FidelityFX.FSR.Enabled +
	// r.FidelityFX.FSR.QualityMode, and its plugin defaults frame-gen on -- handle separately.
	static const TCHAR* DLSS_CVAR = TEXT("r.NGX.DLSS.Enable");        // DLSS Super Resolution (NGX)
	static const TCHAR* XESS_CVAR = TEXT("r.XeSS.Enabled");           // Intel XeSS-SR
	static const TCHAR* XESS_QUALITY_CVAR = TEXT("r.XeSS.Quality");   // 0=UltraPerf .. 6=AntiAliasing

	FString UpscalerName(int32 Index)
	{
		switch (ClampUpscalerIndex(Index))
		{
			case 1:  return TEXT("DLSS");
			case 2:  return TEXT("XeSS");
			default: return TEXT("TSR");   // built-in, always available
		}
	}

	TArray<int32> AvailableUpscalerIndices()
	{
		TArray<int32> Out;
		Out.Add(0);   // TSR is built into UE5 -- always present
		IConsoleManager& CM = IConsoleManager::Get();
		// DLSS: its cvar is registered even on non-RTX / AMD / Intel where DLSS can't run, so also
		// require an NVIDIA GPU -- don't offer a dead option.
		if (CM.FindConsoleVariable(DLSS_CVAR) && IsRHIDeviceNVIDIA()) { Out.Add(1); }
		if (CM.FindConsoleVariable(XESS_CVAR)) { Out.Add(2); }   // XeSS has a broad DP4a fallback
		return Out;
	}

	// Per-upscaler quality tiers (label + render-scale %), high -> low quality. Each upscaler shows the
	// names its users know; the % is the render scale we drive (DLSS reads it directly; XeSS maps it to
	// its own quality enum via XeSSQualityFromScreenPct; TSR upscales by it). TSR has no official tier
	// names, so it gets generic ones.
	struct FUpscaleTier { const TCHAR* Name; float Pct; };
	static const FUpscaleTier kTsrTiers[] = {
		{ TEXT("Native"), 100.f }, { TEXT("Quality"), 66.7f }, { TEXT("Balanced"), 58.f },
		{ TEXT("Performance"), 50.f }, { TEXT("Ultra Performance"), 33.3f } };
	static const FUpscaleTier kDlssTiers[] = {
		{ TEXT("DLAA (Native)"), 100.f }, { TEXT("Quality"), 66.7f }, { TEXT("Balanced"), 58.f },
		{ TEXT("Performance"), 50.f }, { TEXT("Ultra Performance"), 33.3f } };
	static const FUpscaleTier kXessTiers[] = {
		{ TEXT("Native AA"), 100.f }, { TEXT("Ultra Quality Plus"), 77.f }, { TEXT("Ultra Quality"), 66.7f },
		{ TEXT("Quality"), 58.8f }, { TEXT("Balanced"), 50.f }, { TEXT("Performance"), 43.f },
		{ TEXT("Ultra Performance"), 33.3f } };

	static void GetTierTable(int32 UpscalerIndex, const FUpscaleTier*& OutTable, int32& OutCount)
	{
		switch (ClampUpscalerIndex(UpscalerIndex))
		{
			case 1: OutTable = kDlssTiers; OutCount = (int32)UE_ARRAY_COUNT(kDlssTiers); break;
			case 2: OutTable = kXessTiers; OutCount = (int32)UE_ARRAY_COUNT(kXessTiers); break;
			default: OutTable = kTsrTiers; OutCount = (int32)UE_ARRAY_COUNT(kTsrTiers); break;
		}
	}

	TArray<FString> UpscaleModeNames(int32 UpscalerIndex)
	{
		const FUpscaleTier* T; int32 N; GetTierTable(UpscalerIndex, T, N);
		TArray<FString> Out;
		Out.Reserve(N);
		for (int32 i = 0; i < N; ++i) { Out.Add(T[i].Name); }
		return Out;
	}

	float ScreenPctForMode(int32 UpscalerIndex, int32 ModeIndex)
	{
		const FUpscaleTier* T; int32 N; GetTierTable(UpscalerIndex, T, N);
		return T[FMath::Clamp(ModeIndex, 0, N - 1)].Pct;
	}

	int32 ModeForScreenPct(int32 UpscalerIndex, float Pct)
	{
		const FUpscaleTier* T; int32 N; GetTierTable(UpscalerIndex, T, N);
		int32 Best = 0;
		float BestDelta = TNumericLimits<float>::Max();
		for (int32 i = 0; i < N; ++i)
		{
			const float D = FMath::Abs(Pct - T[i].Pct);
			if (D < BestDelta) { BestDelta = D; Best = i; }
		}
		return Best;
	}

	static void SetCVarIfPresent(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value);
		}
	}

	// XeSS uses its own quality enum (0=UltraPerf 3x .. 6=AntiAliasing 1x). It ignores r.ScreenPercentage
	// since UE5.1, so map the render scale chosen by the Upscale Mode preset to the nearest XeSS tier.
	static int32 XeSSQualityFromScreenPct(float Pct)
	{
		if (Pct >= 88.f) { return 6; }   // Anti-Aliasing (1.0x)
		if (Pct >= 72.f) { return 5; }   // Ultra Quality Plus (1.3x)
		if (Pct >= 63.f) { return 4; }   // Ultra Quality (1.5x)
		if (Pct >= 54.f) { return 3; }   // Quality (1.7x)
		if (Pct >= 46.f) { return 2; }   // Balanced (2.0x)
		if (Pct >= 38.f) { return 1; }   // Performance (2.3x)
		return 0;                        // Ultra Performance (3.0x)
	}

	FString CreditsText()
	{
		return FString::Join(TArray<FString>{
			TEXT("GolfForge"),
			TEXT(""),
			TEXT("Course data: © OpenStreetMap contributors (ODbL)."),
			TEXT("Elevation: USGS 3DEP / SRTM (public domain), via OpenTopography."),
			TEXT("Unreal® Engine, Copyright 1998 - 2026, Epic Games, Inc. All rights reserved."),
			TEXT(""),
			TEXT("GolfForge is licensed under AGPL-3.0, plus a commercial option."),
			TEXT("See LICENSE, COMMERCIAL.md, and ATTRIBUTION.md."),
		}, TEXT("\n"));
	}

	FGolfDisplaySettings ReadCurrent()
	{
		FGolfDisplaySettings S;
		if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			S.Resolution = GUS->GetScreenResolution();
			switch (GUS->GetFullscreenMode())
			{
				case EWindowMode::WindowedFullscreen: S.WindowModeIndex = 1; break;
				case EWindowMode::Fullscreen:         S.WindowModeIndex = 2; break;
				default:                              S.WindowModeIndex = 0; break;
			}
			const int32 Q = GUS->GetOverallScalabilityLevel();
			S.QualityLevel = (Q < 0) ? 3 : ClampQualityLevel(Q);
		}
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			const float V = CVar->GetFloat();
			S.ScreenPercentage = (V > 0.f) ? V : 100.f;
		}
		auto CVarOn = [](const TCHAR* N){ IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(N); return C && C->GetInt() != 0; };
		if      (CVarOn(DLSS_CVAR)) { S.UpscalerIndex = 1; }
		else if (CVarOn(XESS_CVAR)) { S.UpscalerIndex = 2; }
		else                        { S.UpscalerIndex = 0; }
		return S;
	}

	void Apply(const FGolfDisplaySettings& S)
	{
		UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!GUS)
		{
			return;
		}
		GUS->SetScreenResolution(S.Resolution);
		EWindowMode::Type Mode = EWindowMode::Windowed;
		switch (ClampWindowModeIndex(S.WindowModeIndex))
		{
			case 1: Mode = EWindowMode::WindowedFullscreen; break;
			case 2: Mode = EWindowMode::Fullscreen; break;
			default: Mode = EWindowMode::Windowed; break;
		}
		GUS->SetFullscreenMode(Mode);
		GUS->SetOverallScalabilityLevel(ClampQualityLevel(S.QualityLevel));
		GUS->ApplyResolutionSettings(false);
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			CVar->Set(FMath::Clamp(S.ScreenPercentage, 50.f, 100.f));
		}
		// DLSS (like FSR/XeSS) is a UE5 temporal upscaler that takes over the TSR slot, so keep AA method
		// = TSR (4) + temporal upsampling on, and toggle DLSS's enable cvar. Only one temporal upscaler
		// runs at a time (DLSS on = DLSS; off = built-in TSR). Quality follows the screen-% slider for both.
		SetCVarIfPresent(TEXT("r.TemporalAA.Upsampling"), 1);
		SetCVarIfPresent(TEXT("r.AntiAliasingMethod"), 4);
		SetCVarIfPresent(DLSS_CVAR, 0);
		SetCVarIfPresent(XESS_CVAR, 0);
		switch (ClampUpscalerIndex(S.UpscalerIndex))
		{
			case 1: SetCVarIfPresent(DLSS_CVAR, 1); break;   // DLSS reads the render scale from screen %
			case 2:
				SetCVarIfPresent(XESS_CVAR, 1);
				SetCVarIfPresent(XESS_QUALITY_CVAR, XeSSQualityFromScreenPct(S.ScreenPercentage));
				break;
			default: break;   // 0 = TSR
		}
		GUS->ApplySettings(false);
		GUS->SaveSettings();
	}

	TArray<FIntPoint> SupportedResolutions()
	{
		TArray<FIntPoint> Res;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Res);
		if (Res.Num() == 0)
		{
			Res = { FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) };
		}
		return Res;
	}

	// GOL-29 pin-distance round-trip. Same .ini as UGameUserSettings, separate section so it doesn't
	// collide with engine-owned keys. SetDouble + Flush so the value survives a crash before the next
	// UGameUserSettings::SaveSettings call.
	static const TCHAR* PinSection = TEXT("GolfForge.Range");
	static const TCHAR* PinKey = TEXT("PinDistanceYd");
	static constexpr double PinDefaultYd = 150.0;
	static constexpr double PinMaxYd = 400.0;   // matches LANE_LEN_YD in build_range_splatmap.py / RangeSurface

	double ReadPinDistanceYd()
	{
		if (!GConfig) { return PinDefaultYd; }
		double V = PinDefaultYd;
		const bool bFound = GConfig->GetDouble(PinSection, PinKey, V, GGameUserSettingsIni);
		if (!bFound) { return PinDefaultYd; }
		return FMath::Clamp(V, 0.0, PinMaxYd);
	}

	void WritePinDistanceYd(double Yards)
	{
		if (!GConfig) { return; }
		const double Clamped = FMath::Clamp(Yards, 0.0, PinMaxYd);
		GConfig->SetDouble(PinSection, PinKey, Clamped, GGameUserSettingsIni);
		GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
	}

	// GOL-121 pre-round picker player name. Default = system user, persisted across launches.
	static const TCHAR* PlayerNameSection = TEXT("GolfForge.Round");
	static const TCHAR* PlayerNameKey = TEXT("PlayerName");

	FString ReadPlayerName()
	{
		FString V;
		if (GConfig && GConfig->GetString(PlayerNameSection, PlayerNameKey, V, GGameUserSettingsIni)
			&& !V.IsEmpty())
		{
			return V;
		}
		return FString(FPlatformProcess::UserName());
	}

	void WritePlayerName(const FString& Name)
	{
		if (!GConfig) { return; }
		GConfig->SetString(PlayerNameSection, PlayerNameKey, *Name, GGameUserSettingsIni);
		GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
	}
}
