using UnrealBuildTool;
using System.Collections.Generic;

public class ReflexionArenaTarget : TargetRules
{
	public ReflexionArenaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ReflexionArena");
	}
}
