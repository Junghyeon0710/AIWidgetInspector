using UnrealBuildTool;

public class AIWidgetInspector : ModuleRules
{
	public AIWidgetInspector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"InputCore",
				"Json",
				"Projects",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"EditorFramework",
				"EditorWidgets",
				"UnrealEd",
				"UMG",
			}
		);
	}
}
