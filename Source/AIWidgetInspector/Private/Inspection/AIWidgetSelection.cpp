// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Inspection/AIWidgetSelection.h"

#include "Widgets/SWidget.h"

void FAIWidgetSelection::SetFromPath(const FWidgetPath& InWidgetPath)
{
	if (!InWidgetPath.IsValid())
	{
		Clear();
		return;
	}

	WeakPath = FWeakWidgetPath(InWidgetPath);
	SelectedIndex = WeakPath.Widgets.Num() - 1;

	ChangedEvent.Broadcast();
}

void FAIWidgetSelection::Clear()
{
	if (!WeakPath.IsValid() && SelectedIndex == INDEX_NONE)
	{
		return;
	}

	WeakPath = FWeakWidgetPath();
	SelectedIndex = INDEX_NONE;

	ChangedEvent.Broadcast();
}

FWidgetPath FAIWidgetSelection::ResolvePath() const
{
	if (!WeakPath.IsValid())
	{
		return FWidgetPath();
	}

	// 중간이 끊긴 경로를 잘라서 쓰면 선택 인덱스가 엉뚱한 Widget을 가리키게 된다.
	return WeakPath.ToWidgetPath(FWeakWidgetPath::EInterruptedPathHandling::ReturnInvalid);
}

TSharedPtr<SWidget> FAIWidgetSelection::GetWidgetAt(int32 InIndex) const
{
	if (!WeakPath.Widgets.IsValidIndex(InIndex))
	{
		return nullptr;
	}

	return WeakPath.Widgets[InIndex].Pin();
}

void FAIWidgetSelection::SelectIndex(int32 InIndex)
{
	if (!WeakPath.Widgets.IsValidIndex(InIndex) || InIndex == SelectedIndex)
	{
		return;
	}

	SelectedIndex = InIndex;
	ChangedEvent.Broadcast();
}

void FAIWidgetSelection::SelectParent()
{
	if (CanSelectParent())
	{
		SelectIndex(SelectedIndex - 1);
	}
}

void FAIWidgetSelection::SelectChild()
{
	if (CanSelectChild())
	{
		SelectIndex(SelectedIndex + 1);
	}
}
