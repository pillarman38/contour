using UnrealBuildTool;

public class GolfSimUEEditorTarget : TargetRules
{
	public GolfSimUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("GolfSimUE");
	}
}
