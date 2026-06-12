#include "GolfDisplaySettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"   // GConfig + GGameUserSettingsIni for the pin-distance helpers
#include "HAL/PlatformProcess.h"   // FPlatformProcess::UserName for the player-name default (GOL-121)
#include "RHI.h"   // IsRHIDeviceNVIDIA() -- gate DLSS to NVIDIA GPUs

#if GOLF_WITH_DLSSG
#include "StreamlineLibraryDLSSG.h"   // UStreamlineLibraryDLSSG + EStreamlineDLSSGMode (Win64 + plugin only)
#endif

namespace GolfDisplay
{
	// EStreamlineDLSSGMode values, mirrored as plain ints so the header/POD stays engine-enum-free and the
	// non-Win64 / no-plugin build (GOLF_WITH_DLSSG=0) still has the names + clamp. Matches the plugin enum.
	namespace FG
	{
		static constexpr int32 Off = 0, Auto = 251, Dynamic = 241,
			On2X = 17, On3X = 23, On4X = 31, On5X = 37, On6X = 41;
	}
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

	// Grass Detail (GOL-162): 0=Off 1=Low 2=High, driving the LandscapeGrass density + cull cvars. Off=0
	// density leaves the painted KBG-green Fairway texture as the standalone fallback.
	int32 ClampGrassDetail(int32 Level) { return FMath::Clamp(Level, 0, 2); }
	TArray<FString> GrassDetailNames() { return { TEXT("Off"), TEXT("Low"), TEXT("High") }; }
	float GrassDensityScaleFor(int32 Level)
	{
		switch (ClampGrassDetail(Level)) { case 0: return 0.f; case 1: return 0.5f; default: return 1.f; }
	}
	float GrassCullScaleFor(int32 Level)
	{
		switch (ClampGrassDetail(Level)) { case 0: return 0.f; case 1: return 0.7f; default: return 1.f; }
	}
	// Default conservative on weak hardware (Mac M4) without GOL-102's full per-platform INI. Single
	// source of truth for the per-platform default; ReadCurrent + the boot apply both use it.
	static int32 PlatformDefaultGrassDetail()
	{
#if PLATFORM_MAC
		return 1;   // Low
#else
		return 2;   // High
#endif
	}

	// --- DLSS Frame Generation (GOL-189) --------------------------------------------------------------
	int32 ClampFrameGenMode(int32 M)
	{
		switch (M)
		{
			case FG::On2X: case FG::On3X: case FG::On4X: case FG::On5X: case FG::On6X:
			case FG::Auto: case FG::Dynamic: case FG::Off:
				return M;
			default:
				return FG::Off;
		}
	}

	FString FrameGenModeName(int32 M)
	{
		switch (M)
		{
			case FG::On2X:    return TEXT("2X");
			case FG::On3X:    return TEXT("3X");
			case FG::On4X:    return TEXT("4X");
			case FG::On5X:    return TEXT("5X");
			case FG::On6X:    return TEXT("6X");
			case FG::Auto:    return TEXT("Auto");
			case FG::Dynamic: return TEXT("Dynamic");
			default:          return TEXT("Off");
		}
	}

	bool IsFrameGenAvailable()
	{
#if GOLF_WITH_DLSSG
		// The DLSS-FG cvars are registered even on non-RTX GPUs, so also require an NVIDIA device + the
		// plugin's own support verdict (driver/GPU-family aware). IsDLSSGSupported() returns false when
		// WITH_STREAMLINE is off, so this is safe.
		return IsRHIDeviceNVIDIA() && UStreamlineLibraryDLSSG::IsDLSSGSupported();
#else
		return false;
#endif
	}

	TArray<int32> SupportedFrameGenModes()
	{
		TArray<int32> Out;
#if GOLF_WITH_DLSSG
		if (IsFrameGenAvailable())
		{
			// Authoritative per-GPU list ("Can be used to populate UI"): 40-series -> Off/2X(+Auto),
			// 50-series -> Off/2X/3X/4X. Drop Dynamic to keep the selector to Off / NX / Auto.
			for (EStreamlineDLSSGMode Mode : UStreamlineLibraryDLSSG::GetSupportedDLSSGModes())
			{
				const int32 V = (int32)Mode;
				if (V != FG::Dynamic) { Out.AddUnique(V); }
			}
		}
#endif
		if (!Out.Contains(FG::Off)) { Out.Insert(FG::Off, 0); }   // Off is always offerable + first
		return Out;
	}

	// Vendor-plugin cvars, driven by name (not by linking the plugins) so this builds with or without
	// them installed; an absent cvar means that upscaler isn't available. Verified against the UE 5.7
	// NVIDIA DLSS + Intel XeSS plugins. FSR deferred (GOL-62): r.FidelityFX.FSR.Enabled +
	// r.FidelityFX.FSR.QualityMode, and its plugin defaults frame-gen on -- handle separately.
	static const TCHAR* DLSS_CVAR = TEXT("r.NGX.DLSS.Enable");        // DLSS Super Resolution (NGX)
	static const TCHAR* XESS_CVAR = TEXT("r.XeSS.Enabled");           // Intel XeSS-SR
	static const TCHAR* XESS_QUALITY_CVAR = TEXT("r.XeSS.Quality");   // 0=UltraPerf .. 6=AntiAliasing

	// LandscapeGrass system cvars (engine-registered floats, default 1.0). Driven by the Grass Detail
	// setting (GOL-162). Grass Detail persists in a custom ini section (it isn't a scalability level).
	static const TCHAR* GRASS_DENSITY_CVAR = TEXT("grass.densityScale");
	static const TCHAR* GRASS_CULL_CVAR    = TEXT("grass.CullDistanceScale");
	static const TCHAR* GraphicsSection = TEXT("GolfForge.Graphics");
	static const TCHAR* GrassDetailKey  = TEXT("GrassDetail");
	static const TCHAR* FrameGenKey     = TEXT("FrameGen");   // DLSS-FG mode value (GOL-189)

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

	static void SetCVarFloatIfPresent(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value);
		}
	}

	// Push a Grass Detail level to the LandscapeGrass density + cull cvars (no-op if the engine cvars
	// aren't registered yet). Shared by Apply() and the boot-time ApplyGrassDetailFromSaved().
	static void SetGrassCVars(int32 Level)
	{
		SetCVarFloatIfPresent(GRASS_DENSITY_CVAR, GrassDensityScaleFor(Level));
		SetCVarFloatIfPresent(GRASS_CULL_CVAR, GrassCullScaleFor(Level));
	}

	// Apply a DLSS-FG mode via the Streamline library (it sets r.Streamline.DLSSG.Enable +
	// FramesToGenerate internally). Falls back to Off if the GPU/driver doesn't support the requested
	// mode. No-op without the plugin. Shared by Apply() and the boot-time ApplyFrameGenFromSaved().
	static void SetFrameGenMode(int32 ModeValue)
	{
#if GOLF_WITH_DLSSG
		EStreamlineDLSSGMode Mode = (EStreamlineDLSSGMode)ClampFrameGenMode(ModeValue);
		if (!IsFrameGenAvailable() || !UStreamlineLibraryDLSSG::IsDLSSGModeSupported(Mode))
		{
			Mode = EStreamlineDLSSGMode::Off;
		}
		// DLSS-FG REQUIRES NVIDIA Reflex active at runtime, or it fails with
		// DLSSGStatus::eFailReflexNotDetectedAtRuntime and silently generates nothing. Reflex isn't on by
		// default, so tie it to FG: t.Streamline.Reflex.Mode 1 (low-latency) when FG is on, 0 when off.
		// Soft cvar set -> no-op if the Reflex plugin isn't present. (We expose no separate Reflex control.)
		SetCVarIfPresent(TEXT("t.Streamline.Reflex.Mode"), (Mode != EStreamlineDLSSGMode::Off) ? 1 : 0);
		UStreamlineLibraryDLSSG::SetDLSSGMode(Mode);
#endif
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
			TEXT("GolfForge is free and open source under the GNU AGPL-3.0."),
			TEXT("See LICENSE and ATTRIBUTION.md."),
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
		// Grass Detail rides a custom ini section (not a scalability level), defaulting per-platform.
		int32 Grass = PlatformDefaultGrassDetail();
		if (GConfig) { GConfig->GetInt(GraphicsSection, GrassDetailKey, Grass, GGameUserSettingsIni); }
		S.GrassDetailLevel = ClampGrassDetail(Grass);
		// DLSS Frame Generation rides the same custom ini section; default Off.
		int32 FrameGen = FG::Off;
		if (GConfig) { GConfig->GetInt(GraphicsSection, FrameGenKey, FrameGen, GGameUserSettingsIni); }
		S.FrameGenMode = ClampFrameGenMode(FrameGen);
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
		// Grass Detail: drive the LandscapeGrass cvars + persist to the custom ini section (Off=texture
		// only). Not a UGameUserSettings field, so it round-trips through GConfig like the pin distance.
		const int32 Grass = ClampGrassDetail(S.GrassDetailLevel);
		SetGrassCVars(Grass);
		if (GConfig)
		{
			GConfig->SetInt(GraphicsSection, GrassDetailKey, Grass, GGameUserSettingsIni);
			GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
		}
		// DLSS Frame Generation: drive it through the Streamline library + persist the requested mode
		// (clamped to a known value; applied mode falls back to Off if the GPU can't do it). DLSS-FG is
		// inert in editor PIE -- it only kicks in in a real/standalone/cooked swapchain.
		const int32 FrameGen = ClampFrameGenMode(S.FrameGenMode);
		SetFrameGenMode(FrameGen);
		if (GConfig)
		{
			GConfig->SetInt(GraphicsSection, FrameGenKey, FrameGen, GGameUserSettingsIni);
			GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
		}
		GUS->ApplySettings(false);
		GUS->SaveSettings();
	}

	void ApplyGrassDetailFromSaved()
	{
		int32 Grass = PlatformDefaultGrassDetail();
		if (GConfig) { GConfig->GetInt(GraphicsSection, GrassDetailKey, Grass, GGameUserSettingsIni); }
		SetGrassCVars(ClampGrassDetail(Grass));
	}

	void ApplyFrameGenFromSaved()
	{
		int32 FrameGen = FG::Off;
		if (GConfig) { GConfig->GetInt(GraphicsSection, FrameGenKey, FrameGen, GGameUserSettingsIni); }
		SetFrameGenMode(ClampFrameGenMode(FrameGen));
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

	// GOL-143 player handicap. Same [GolfForge.Round] section as the name.
	static const TCHAR* HandicapKey = TEXT("Handicap");

	int32 ReadHandicap()
	{
		int32 V = 0;
		if (GConfig) { GConfig->GetInt(PlayerNameSection, HandicapKey, V, GGameUserSettingsIni); }
		return FMath::Clamp(V, 0, 54);
	}

	void WriteHandicap(int32 Handicap)
	{
		if (!GConfig) { return; }
		GConfig->SetInt(PlayerNameSection, HandicapKey, FMath::Clamp(Handicap, 0, 54), GGameUserSettingsIni);
		GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
	}

	// GOL-209 hole-map card size. Same [GolfForge.Round] section. (The HOLE/GREEN tab is not
	// persisted -- it follows play: HOLE on hole start, GREEN when the ball reaches the green.)
	static const TCHAR* HoleMapSizeKey = TEXT("HoleMapSize");

	int32 ReadHoleMapSize()
	{
		int32 V = 0;   // default collapsed chip: always-open costs too much screen (the GOL-209 brief)
		if (GConfig) { GConfig->GetInt(PlayerNameSection, HoleMapSizeKey, V, GGameUserSettingsIni); }
		return FMath::Clamp(V, 0, 2);
	}

	void WriteHoleMapSize(int32 Size)
	{
		if (!GConfig) { return; }
		GConfig->SetInt(PlayerNameSection, HoleMapSizeKey, FMath::Clamp(Size, 0, 2), GGameUserSettingsIni);
		GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
	}
}
