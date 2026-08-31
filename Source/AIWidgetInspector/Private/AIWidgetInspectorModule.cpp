// AI Widget Inspector

#include "AIWidgetInspectorModule.h"

#include "AI/AICliProvider.h"
#include "BaseWidgetBlueprint.h"
#include "Mcp/AIWidgetInspectorToolset.h"

#include "Misc/CoreDelegates.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "AI/AIClipboardProvider.h"
#include "AIWidgetInspectorCommands.h"
#include "AIWidgetInspectorLog.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetSelection.h"
#include "UI/SAIWidgetInspectorPanel.h"
#include "WidgetPicking/AIWidgetHighlighter.h"
#include "WidgetPicking/AIWidgetPicker.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

DEFINE_LOG_CATEGORY(LogAIWidgetInspector);

#define LOCTEXT_NAMESPACE "FAIWidgetInspectorModule"

namespace AIWidgetInspector
{
	static const FName ModuleName(TEXT("AIWidgetInspector"));
	static const FName InspectorTabName(TEXT("AIWidgetInspector"));

	/** 플러그인 버튼이 붙는 레벨 에디터 우측 툴바. */
	static const FName LevelEditorUserToolBarName(TEXT("LevelEditor.LevelEditorToolBar.User"));
}

FAIWidgetInspectorModule& FAIWidgetInspectorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAIWidgetInspectorModule>(AIWidgetInspector::ModuleName);
}

bool FAIWidgetInspectorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(AIWidgetInspector::ModuleName);
}

void FAIWidgetInspectorModule::SetLastAppliedBlueprint(UBaseWidgetBlueprint* InBlueprint)
{
	LastAppliedBlueprint = InBlueprint;
}

UBaseWidgetBlueprint* FAIWidgetInspectorModule::GetLastAppliedBlueprint() const
{
	return LastAppliedBlueprint.Get();
}

void FAIWidgetInspectorModule::StartupModule()
{
	WidgetPicker = MakeShared<FAIWidgetPicker>();
	WidgetPicker->Initialize();
	WidgetPicker->OnWidgetPicked().AddRaw(this, &FAIWidgetInspectorModule::HandleWidgetPicked);
	WidgetPicker->OnInspectModeChanged().AddRaw(this, &FAIWidgetInspectorModule::HandleInspectModeChanged);

	WidgetSelection = MakeShared<FAIWidgetSelection>();

	WidgetHighlighter = MakeShared<FAIWidgetHighlighter>(WidgetPicker.ToSharedRef(), WidgetSelection.ToSharedRef());
	WidgetHighlighter->Register();

	RuntimePreview = MakeShared<FAIWidgetRuntimePreview>();


	// Clipboard가 기본값이다. 아무것도 설치돼 있지 않아도 동작하기 때문이다.
	Providers.Add(MakeShared<FAIClipboardProvider>());

	// CLI Provider는 설치돼 있지 않으면 목록에는 남지만 전송 버튼이 꺼진다.
	// 없는 걸 감추는 것보다, 왜 못 쓰는지 툴팁으로 보여주는 편이 낫다.
	{
		FAICliProvider::FConfig ClaudeConfig;
		ClaudeConfig.Name = TEXT("ClaudeCli");
		ClaudeConfig.DisplayName = LOCTEXT("ClaudeCli", "Claude Code");
		ClaudeConfig.Description = LOCTEXT("ClaudeCliDesc", "Pipes the prompt to the claude CLI and shows the reply.");
		ClaudeConfig.Executable = TEXT("claude");
		ClaudeConfig.InstallCommand = TEXT("npm install -g @anthropic-ai/claude-code");
		// -p 는 대화형 세션 대신 답만 찍고 끝내라는 뜻이다.
		//
		// 나머지 둘은 코딩 에이전트가 아니라 응답기로 쓰기 위한 것이다. 그냥 부르면
		// 프로젝트 디렉터리에서 파일을 뒤지며 스스로 고치려 들고, 정작 우리가 기다리는
		// JSON 대신 "직접 이렇게 하세요" 같은 답을 돌려준다. 프롬프트에 필요한 정보는
		// 이미 다 들어 있으므로 도구는 필요 없다.
		//
		// --bare 는 쓰지 않는다. OAuth와 키체인을 읽지 않고 ANTHROPIC_API_KEY만 보기
		// 때문에, CLI로 로그인해 둔 사용자의 인증이 오히려 깨진다.
		ClaudeConfig.Arguments =
		{
			TEXT("-p"),
			TEXT("--restricted"),
			TEXT("--disallowedTools"),
			TEXT("Read,Edit,Write,Glob,Grep,Task,WebSearch,WebFetch,NotebookEdit"),
		};
		Providers.Add(MakeShared<FAICliProvider>(MoveTemp(ClaudeConfig)));
	}

	// 같은 CLI를 두 번 등록한다. 하나는 답을 받아 우리가 적용하는 방식,
	// 하나는 AI가 에디터 Tool을 직접 부르는 방식이다. 어느 쪽인지 목록에서 보이는 편이
	// 인자 하나로 몰래 갈리는 것보다 낫다.
	{
		FAICliProvider::FConfig ClaudeMcpConfig;
		ClaudeMcpConfig.Name = TEXT("ClaudeCliMcp");
		ClaudeMcpConfig.DisplayName = LOCTEXT("ClaudeCliMcp", "Claude Code (Unreal MCP)");
		ClaudeMcpConfig.Description = LOCTEXT("ClaudeCliMcpDesc", "The claude CLI connects to the editor over MCP and changes widgets itself.");
		ClaudeMcpConfig.Executable = TEXT("claude");
		ClaudeMcpConfig.InstallCommand = TEXT("npm install -g @anthropic-ai/claude-code");
		ClaudeMcpConfig.Arguments = { TEXT("-p") };
		ClaudeMcpConfig.bUseUnrealMcp = true;
		Providers.Add(MakeShared<FAICliProvider>(MoveTemp(ClaudeMcpConfig)));
	}

	{
		FAICliProvider::FConfig CodexConfig;
		CodexConfig.Name = TEXT("CodexCli");
		CodexConfig.DisplayName = LOCTEXT("CodexCli", "Codex");
		CodexConfig.Description = LOCTEXT("CodexCliDesc", "Pipes the prompt to the codex CLI and shows the reply.");
		CodexConfig.Executable = TEXT("codex");
		CodexConfig.InstallCommand = TEXT("npm install -g @openai/codex");
		// "-" 는 프롬프트를 stdin에서 읽으라는 뜻이다.
		CodexConfig.Arguments = { TEXT("exec"), TEXT("-") };
		Providers.Add(MakeShared<FAICliProvider>(MoveTemp(CodexConfig)));
	}

	// 어떤 Provider가 왜 못 쓰이는지는 로그에 남겨 둔다.
	// 회색으로 비활성화된 버튼만 보고 원인을 짚기는 어렵기 때문이다.
	for (const TSharedPtr<IAIWidgetProvider>& Provider : Providers)
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("Provider '%s': %s"),
			*Provider->GetDisplayName().ToString(),
			*Provider->GetDescription().ToString());
	}

	// 엔진 MCP 서버에 Widget Tool을 실어 보낸다. AI CLI는 그 서버에 붙어 여기 함수를 부른다.
	FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FAIWidgetInspectorModule::HandleAllModuleLoadingPhasesComplete);
	FCoreDelegates::OnPreExit.AddRaw(this, &FAIWidgetInspectorModule::HandlePreExit);

	FAIWidgetInspectorCommands::Register();

	PluginCommands = MakeShared<FUICommandList>();
	PluginCommands->MapAction(
		FAIWidgetInspectorCommands::Get().ToggleInspectMode,
		FExecuteAction::CreateRaw(this, &FAIWidgetInspectorModule::ToggleInspectMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateRaw(this, &FAIWidgetInspectorModule::IsInspectModeActive));

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(
			AIWidgetInspector::InspectorTabName,
			FOnSpawnTab::CreateRaw(this, &FAIWidgetInspectorModule::HandleSpawnInspectorTab))
		.SetDisplayName(LOCTEXT("InspectorTabTitle", "AI Widget Inspector"))
		.SetTooltipText(LOCTEXT("InspectorTabTooltip", "Inspect the selected Slate or UMG widget and where it came from."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visibility")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAIWidgetInspectorModule::RegisterMenus));
}

void FAIWidgetInspectorModule::HandleAllModuleLoadingPhasesComplete()
{
	UToolsetRegistry::RegisterToolsetClass(UAIWidgetInspectorToolset::StaticClass());
	UE_LOG(LogAIWidgetInspector, Log, TEXT("Registered the widget toolset with MCP."));
}

void FAIWidgetInspectorModule::HandlePreExit()
{
	UToolsetRegistry::UnregisterToolsetClass(UAIWidgetInspectorToolset::StaticClass());
}

void FAIWidgetInspectorModule::ShutdownModule()
{
	FCoreDelegates::OnAllModuleLoadingPhasesComplete.RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIWidgetInspector::InspectorTabName);
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FAIWidgetInspectorCommands::Unregister();
	PluginCommands.Reset();
	Providers.Reset();


	// 임시 변경은 에셋에 남지 않으므로, 모듈이 내려갈 때 원래 값으로 되돌려 준다.
	if (RuntimePreview.IsValid())
	{
		RuntimePreview->RevertAll();
		RuntimePreview.Reset();
	}

	// Highlighter는 FSlateApplication이 TWeakPtr로만 잡고 있으므로 여기서 놓으면 등록이 자연히 풀린다.
	WidgetHighlighter.Reset();
	WidgetSelection.Reset();

	if (WidgetPicker.IsValid())
	{
		WidgetPicker->OnWidgetPicked().RemoveAll(this);
		WidgetPicker->OnInspectModeChanged().RemoveAll(this);
		WidgetPicker->Shutdown();
		WidgetPicker.Reset();
	}
}

void FAIWidgetInspectorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(AIWidgetInspector::LevelEditorUserToolBarName))
	{
		FToolMenuSection& Section = ToolBar->FindOrAddSection(TEXT("AIWidgetInspector"));

		FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FAIWidgetInspectorCommands::Get().ToggleInspectMode,
			LOCTEXT("ToolbarLabel", "AI Widget"),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visibility"))));

		Entry.SetCommandList(PluginCommands);
	}
}

TSharedRef<SDockTab> FAIWidgetInspectorModule::HandleSpawnInspectorTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAIWidgetInspectorPanel, WidgetPicker.ToSharedRef(), WidgetSelection.ToSharedRef(), WidgetHighlighter.ToSharedRef())
		];
}

void FAIWidgetInspectorModule::ToggleInspectMode()
{
	if (WidgetPicker.IsValid())
	{
		WidgetPicker->ToggleInspectMode();
	}
}

bool FAIWidgetInspectorModule::IsInspectModeActive() const
{
	return WidgetPicker.IsValid() && WidgetPicker->IsInspecting();
}

void FAIWidgetInspectorModule::HandleInspectModeChanged(bool bInIsInspecting)
{
	if (!bInIsInspecting || !WidgetHighlighter.IsValid())
	{
		return;
	}

	// 엔진 Widget Reflector를 열었다 닫으면 리플렉터 슬롯을 그쪽이 가져간 상태다.
	// 슬롯은 하나뿐이라 Inspect Mode에 들어갈 때마다 되찾아 온다.
	WidgetHighlighter->Register();
}

void FAIWidgetInspectorModule::HandleWidgetPicked(const FWidgetPath& InPickedPath)
{
	if (!InPickedPath.IsValid() || !WidgetSelection.IsValid())
	{
		return;
	}

	WidgetSelection->SetFromPath(InPickedPath);

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Selected leaf widget: %s"),
		*FAIWidgetPicker::DescribeWidget(InPickedPath.GetLastWidget()));

	FGlobalTabmanager::Get()->TryInvokeTab(AIWidgetInspector::InspectorTabName);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAIWidgetInspectorModule, AIWidgetInspector)
