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
				// 설정만 읽는 Engine 쪽과 달리, 이쪽에는 서버 인스턴스가 있다. 자동 시작이
				// 켜져 있는지가 아니라 지금 떠 있는지를 물어보려면 필요하다.
				"ModelContextProtocol",
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
