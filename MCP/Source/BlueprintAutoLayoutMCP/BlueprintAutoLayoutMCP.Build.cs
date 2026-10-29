using UnrealBuildTool;

public class BlueprintAutoLayoutMCP : ModuleRules
{
	public BlueprintAutoLayoutMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 8))
		{
			throw new BuildException("BlueprintAutoLayoutMCP requires Unreal Engine 5.8 or newer.");
		}
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new [] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
		PrivateDependencyModuleNames.AddRange(new [] { "BlueprintAutoLayout", "UnrealEd", "BlueprintGraph", "AnimGraph", "MaterialEditor" });
	}
}
