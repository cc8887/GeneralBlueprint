using UnrealBuildTool;

public class DrivenHighlightMCP : ModuleRules
{
    public DrivenHighlightMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 8))
            throw new BuildException("DrivenHighlightMCP requires Unreal Engine 5.8 or newer.");
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "DrivenHighlightEditor", "Slate", "SlateCore", "Json" });
    }
}
