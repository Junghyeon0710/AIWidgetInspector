// Copyright 2026 Junghyeon0710. All Rights Reserved.
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

	/** 선택된 Widget의 Blueprint를 저장한다. 패널 안에서만 듣는다. */
	TSharedPtr<FUICommandInfo> SaveWidgetAsset;
};
