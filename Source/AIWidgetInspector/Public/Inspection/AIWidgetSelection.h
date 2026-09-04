// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Layout/WidgetPath.h"

class SWidget;

/**
 * 현재 선택된 Widget과 그 경로.
 *
 * Picker가 찾아준 FWidgetPath 중 "사용자가 실제로 관심 있는" 항목이 무엇인지는 따로 관리해야 한다.
 * 클릭은 항상 가장 깊은 Widget(예: STextBlock)을 잡지만, 정작 고치고 싶은 건 부모 SButton인 경우가 많다.
 * 그래서 경로 전체를 보관하고 그 안에서 인덱스로 선택을 옮긴다.
 *
 * 경로는 FWeakWidgetPath로 들고 있다가 필요할 때만 되살린다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetSelection
{
public:
	/** 경로를 통째로 받고 가장 깊은 Widget을 선택한다. */
	void SetFromPath(const FWidgetPath& InWidgetPath);

	void Clear();

	/** 경로가 있고 선택 인덱스가 살아있는지. Widget이 파괴되었는지까지는 보지 않는다. */
	bool IsValid() const { return WeakPath.IsValid() && SelectedIndex != INDEX_NONE; }

	const FWeakWidgetPath& GetWeakPath() const { return WeakPath; }

	/** 살아있는 경로로 되살린다. 중간이 끊겼으면 무효 경로. */
	FWidgetPath ResolvePath() const;

	int32 Num() const { return WeakPath.Widgets.Num(); }
	int32 GetSelectedIndex() const { return SelectedIndex; }

	TSharedPtr<SWidget> GetWidgetAt(int32 InIndex) const;
	TSharedPtr<SWidget> GetSelectedWidget() const { return GetWidgetAt(SelectedIndex); }

	void SelectIndex(int32 InIndex);

	bool CanSelectParent() const { return IsValid() && SelectedIndex > 0; }
	bool CanSelectChild() const { return IsValid() && SelectedIndex + 1 < Num(); }
	void SelectParent();
	void SelectChild();

	/** 선택 자체가 바뀌었거나 경로가 통째로 교체되었을 때. */
	FSimpleMulticastDelegate& OnChanged() { return ChangedEvent; }

private:
	FWeakWidgetPath WeakPath;
	int32 SelectedIndex = INDEX_NONE;

	FSimpleMulticastDelegate ChangedEvent;
};
