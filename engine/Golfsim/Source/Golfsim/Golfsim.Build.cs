// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Golfsim : ModuleRules
{
	public Golfsim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allow path-qualified includes from the module root (e.g. "Physics/BallFlightSolver.h").
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore" });

		// WebSockets + Json: the OpenFlight launch-monitor driver (GOL-11) talks to OpenFlight's
		// WebSocket and parses its shot JSON. Both are engine runtime modules (no uproject plugin).
		PrivateDependencyModuleNames.AddRange(new string[] { "WebSockets", "Json" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
