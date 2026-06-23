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
