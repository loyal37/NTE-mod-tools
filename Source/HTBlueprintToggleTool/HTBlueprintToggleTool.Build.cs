using UnrealBuildTool;

public class HTBlueprintToggleTool : ModuleRules
{
	public HTBlueprintToggleTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AssetDefinition",
				"AssetTools",
				"AssetRegistry",
				"AnimGraph",
				"BlueprintGraph",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"EditorFramework",
				"EditorStyle",
				"Engine",
				"GraphEditor",
				"ImageCore",
				"InputCore",
				"Json",
				"Kismet",
				"KismetCompiler",
				"MaterialEditor",
				"PropertyEditor",
				"Projects",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
