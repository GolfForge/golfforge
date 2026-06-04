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
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostInitHandle);
	}

private:
	FDelegateHandle PostInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE( FGolfsimModule, Golfsim, "Golfsim" );
