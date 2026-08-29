// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FAIWidgetHighlighter;
class FAIWidgetPicker;
class FAIWidgetRuntimePreview;
class FAIWidgetSelection;
class FSpawnTabArgs;
class FUICommandList;
class FWidgetPath;
class IAIWidgetProvider;
class SDockTab;

class FAIWidgetInspectorModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Toolset은 모든 모듈이 올라온 뒤에 등록한다.
	 *
	 * StartupModule 시점에는 ToolsetRegistry가 아직 준비되지 않았을 수 있다.
	 * 엔진의 UMGToolSet도 같은 델리게이트를 쓴다.
	 */
	void HandleAllModuleLoadingPhasesComplete();
	void HandlePreExit();
	//~ End IModuleInterface

	static FAIWidgetInspectorModule& Get();
	static bool IsAvailable();

	/** Inspect Mode 상태 기계. 모듈이 살아있는 동안 유효하다. */
	TSharedPtr<FAIWidgetPicker> GetWidgetPicker() const { return WidgetPicker; }

	/** 현재 선택된 Widget과 그 경로. */
	TSharedPtr<FAIWidgetSelection> GetWidgetSelection() const { return WidgetSelection; }

	/** 화면 위 테두리 오버레이. Source/Asset 열기 델리게이트도 여기에 있다. */
	TSharedPtr<FAIWidgetHighlighter> GetWidgetHighlighter() const { return WidgetHighlighter; }

	/** 등록된 AI Provider. 첫 항목이 기본값이다. */
	const TArray<TSharedPtr<IAIWidgetProvider>>& GetProviders() const { return Providers; }

	/** 살아있는 인스턴스에만 적용되는 임시 변경. 에셋은 건드리지 않는다. */
	TSharedPtr<FAIWidgetRuntimePreview> GetRuntimePreview() const { return RuntimePreview; }

private:
	void RegisterMenus();

	TSharedRef<SDockTab> HandleSpawnInspectorTab(const FSpawnTabArgs& InSpawnTabArgs);

	void ToggleInspectMode();
	bool IsInspectModeActive() const;

	void HandleInspectModeChanged(bool bInIsInspecting);
	void HandleWidgetPicked(const FWidgetPath& InPickedPath);

	TSharedPtr<FAIWidgetPicker> WidgetPicker;
	TSharedPtr<FAIWidgetSelection> WidgetSelection;
	TSharedPtr<FAIWidgetHighlighter> WidgetHighlighter;
	TSharedPtr<FAIWidgetRuntimePreview> RuntimePreview;

	TArray<TSharedPtr<IAIWidgetProvider>> Providers;
	TSharedPtr<FUICommandList> PluginCommands;
};
