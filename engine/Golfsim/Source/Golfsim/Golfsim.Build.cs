// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Golfsim : ModuleRules
{
	public Golfsim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allow path-qualified includes from the module root (e.g. "Physics/BallFlightSolver.h").
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "ProceduralMeshComponent" });

		// WebSockets + Json: the OpenFlight launch-monitor driver (GOL-11) talks to OpenFlight's
		// WebSocket and parses its shot JSON. Both are engine runtime modules (no uproject plugin).
		// Sockets + Networking: the GSPro Open Connect server driver (GOL-178) runs a raw TCP listener
		// on 127.0.0.1:921 (no websockets -- sidesteps GOL-36) and parses its newline-delimited JSON.
		// ImageWrapper: runtime PNG decode for the course splatmaps (GOL-40, Physics/CourseSurface).
		// JsonUtilities: FJsonObjectConverter for the ShotHistory JSONL round-trip (GOL-65).
		PrivateDependencyModuleNames.AddRange(new string[] { "WebSockets", "Sockets", "Networking", "Json", "JsonUtilities", "RHI", "ImageWrapper" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
