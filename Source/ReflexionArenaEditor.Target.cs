using UnrealBuildTool;
using System.Collections.Generic;

public class ReflexionArenaEditorTarget : TargetRules
{
	public ReflexionArenaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ReflexionArena");
	}
}
