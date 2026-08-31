// AI Widget Inspector

#include "AIWidgetInspectorModule.h"

#include "AI/AITerminalProvider.h"
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

	// CLI는 패널 안의 터미널에서만 돈다.
	//
	// 예전에는 프롬프트를 stdin으로 밀어 넣고 답만 받아 오는 원샷 Provider도 함께 올렸다.
	// 그쪽은 승인을 물을 자리가 없어 도구를 미리 좁혀야 했고, 그래서 코드를 읽지도 쓰지도
	// 못했다. 위젯을 고쳐 달라고 하면 무엇을 못 하는지 설명하는 답이 돌아왔다.
	// 같은 이름이 목록에 여러 개 있는데 할 수 있는 일이 서로 다르면, 고르기 전에는
	// 무엇이 다른지 알 수가 없다.
	//
	// CLI마다 하나씩 올린다. 설정 어딘가에 숨겨 두면 지금 무엇과 이야기하고 있는지 알 수 없다.
	Providers.Add(MakeShared<FAITerminalProvider>(EAITerminalCli::Claude));
	Providers.Add(MakeShared<FAITerminalProvider>(EAITerminalCli::Codex));

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
