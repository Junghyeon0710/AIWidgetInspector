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
				"ModelContextProtocolEngine",
				"ToolsetRegistry",
				"Projects",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"EditorFramework",
				"EditorWidgets",
				"UnrealEd",
				"UMG",
				// 패널 안에서 CLI를 대화형으로 돌리는 터미널 위젯(STerminal)이 여기 있다.
				"Terminal",
			}
		);
	}
}
