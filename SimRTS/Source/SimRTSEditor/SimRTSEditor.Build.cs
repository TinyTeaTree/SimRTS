using System.IO;
using UnrealBuildTool;

public class SimRTSEditor : ModuleRules
{
	public SimRTSEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "Obstruction"),
			Path.Combine(ModuleDirectory, "Spawn"),
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"EditorFramework",
			"WorkspaceMenuStructure",
			"DesktopPlatform",
			"Json",
			"SimRTS",
		});
	}
}
