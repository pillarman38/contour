using UnrealBuildTool;

public class GolfSimUETarget : TargetRules
{
	public GolfSimUETarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("GolfSimUE");
	}
}
