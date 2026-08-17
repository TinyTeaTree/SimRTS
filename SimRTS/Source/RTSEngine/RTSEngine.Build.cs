using UnrealBuildTool;

public class RTSEngine : ModuleRules
{
	public RTSEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		// Keep sim .cpp files free of forced Unreal PCH includes.
		PCHUsage = PCHUsageMode.NoPCHs;

		// Module glue needs Unreal Core; sim sources themselves stay STL-only.
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
