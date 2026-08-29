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
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::I));

	// Ctrl+S는 에디터 전역에서 이미 의미가 있다. 이 명령은 패널의 CommandList에만
	// 묶여 있어서 패널에 포커스가 있을 때만 듣는다. 전역 저장을 가로채지 않는다.
	UI_COMMAND(
		SaveWidgetAsset,
		"Save Widget Asset",
		"선택된 Widget의 Blueprint를 저장한다. 저장 전까지는 Ctrl+Z로 되돌릴 수 있다.",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control, EKeys::S));
}

#undef LOCTEXT_NAMESPACE
