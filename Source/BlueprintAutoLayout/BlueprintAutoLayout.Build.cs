using UnrealBuildTool;

public class BlueprintAutoLayout : ModuleRules
{
	public BlueprintAutoLayout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// These modules are shipped together by GeneralBlueprint. Keeping
			// them as module dependencies preserves independent feature modules
			// while enabling the integrated import lifecycle hooks.
			"BlueprintLisp",
			"AnimBP2FP",
			"MatBP2FP",
		});

		PublicDefinitions.AddRange(new string[]
		{
			"WITH_BLUEPRINTLISP=1",
			"WITH_ANIMBP2FP=1",
			"WITH_MATBP2FP=1",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorSubsystem",
			"BlueprintGraph",
			"GraphEditor",
			"Kismet",
			"KismetCompiler",
			"MaterialEditor",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"ToolMenus",
			"InputCore",
			"DeveloperSettings",
			"DrivenHighlight",
			"DrivenHighlightEditor",
		});
	}
}
