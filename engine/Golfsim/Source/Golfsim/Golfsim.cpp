// Copyright Epic Games, Inc. All Rights Reserved.

#include "Golfsim.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "GolfDisplaySettings.h"

// Primary game module. Beyond the default impl it applies the saved/per-platform Grass Detail (GOL-162)
// to the LandscapeGrass cvars once the engine + renderer cvars are initialized, so the setting and the
// Mac-conservative default take effect on a cold launch before any settings menu is opened.
class FGolfsimModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		PostInitHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&GolfDisplay::ApplyGrassDetailFromSaved);
		// Restore the saved DLSS Frame Generation mode on a cold launch (GOL-189), same as grass. DLSS-FG
		// is inert in the editor; in a standalone/cooked build this re-applies the persisted choice.
		FrameGenInitHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&GolfDisplay::ApplyFrameGenFromSaved);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostInitHandle);
		FCoreDelegates::OnPostEngineInit.Remove(FrameGenInitHandle);
	}

private:
	FDelegateHandle PostInitHandle;
	FDelegateHandle FrameGenInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE( FGolfsimModule, Golfsim, "Golfsim" );
