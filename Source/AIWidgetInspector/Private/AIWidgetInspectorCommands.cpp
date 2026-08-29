// AI Widget Inspector

#include "AIWidgetInspectorCommands.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FAIWidgetInspectorModule"

FAIWidgetInspectorCommands::FAIWidgetInspectorCommands()
	: TCommands<FAIWidgetInspectorCommands>(
		TEXT("AIWidgetInspector"),
		NSLOCTEXT("Contexts", "AIWidgetInspector", "AI Widget Inspector"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FAIWidgetInspectorCommands::RegisterCommands()
{
	UI_COMMAND(
		ToggleInspectMode,
		"AI Widget Inspector",
		"Inspect Mode를 켠다. 마우스를 UI 위로 옮기면 대상 Widget을 추적하고, 클릭하면 해당 Widget을 선택한다. ESC로 취소.",
		EUserInterfaceActionType::ToggleButton,
		FInputChord());
}

#undef LOCTEXT_NAMESPACE
