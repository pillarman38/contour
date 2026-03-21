using UnrealBuildTool;

public class GolfSimUE : ModuleRules
{
	public GolfSimUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Sockets",
			"Networking",
			"Json",
			"JsonUtilities"
		});
	}
}
