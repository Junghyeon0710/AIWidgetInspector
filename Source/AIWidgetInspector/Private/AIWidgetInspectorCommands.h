// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FAIWidgetInspectorCommands : public TCommands<FAIWidgetInspectorCommands>
{
public:
	FAIWidgetInspectorCommands();

	virtual void RegisterCommands() override;

	/** Inspect Mode 토글 (Toolbar / Window 메뉴). */
	TSharedPtr<FUICommandInfo> ToggleInspectMode;
};
