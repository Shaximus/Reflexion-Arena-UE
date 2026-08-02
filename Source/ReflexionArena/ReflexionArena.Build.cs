using UnrealBuildTool;

public class ReflexionArena : ModuleRules
{
	public ReflexionArena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
