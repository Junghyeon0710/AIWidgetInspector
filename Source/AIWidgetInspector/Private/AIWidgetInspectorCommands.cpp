// Copyright 2026 Junghyeon0710. All Rights Reserved.
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
		"Turn on Inspect Mode. Move the mouse over the UI to track the widget under the cursor, click to select it, Esc to cancel.",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::I));

	// Ctrl+S는 에디터 전역에서 이미 의미가 있다. 이 명령은 패널의 CommandList에만
	// 묶여 있어서 패널에 포커스가 있을 때만 듣는다. 전역 저장을 가로채지 않는다.
	UI_COMMAND(
		SaveWidgetAsset,
		"Save Widget Asset",
		"Save the selected widget's Blueprint. Until you save, Ctrl+Z still undoes the change.",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control, EKeys::S));
}

#undef LOCTEXT_NAMESPACE
