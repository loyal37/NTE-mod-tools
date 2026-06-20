using UnrealBuildTool;

public class HTBlueprintToggleTool : ModuleRules
{
	public HTBlueprintToggleTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetTools",
				"AssetRegistry",
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Json",
				"Kismet",
				"KismetCompiler",
				"MaterialEditor",
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
