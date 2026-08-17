// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class SimRTS : ModuleRules
{
	public SimRTS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Keep flat #includes working after feature subfolders.
		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "Bridge"),
			Path.Combine(ModuleDirectory, "Framework"),
			Path.Combine(ModuleDirectory, "Level"),
			Path.Combine(ModuleDirectory, "Obstruction"),
			Path.Combine(ModuleDirectory, "Selection"),
			Path.Combine(ModuleDirectory, "Spawn"),
			Path.Combine(ModuleDirectory, "Units"),
			Path.Combine(ModuleDirectory, "Debug")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Json",
			"RTSEngine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
