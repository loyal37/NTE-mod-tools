using UnrealBuildTool;

public class HTBlueprintToggleTool : ModuleRules
{
	public HTBlueprintToggleTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Json",
				"Kismet",
				"KismetCompiler",
				"PropertyEditor",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
