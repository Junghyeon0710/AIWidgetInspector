// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "WidgetPicking/AIWidgetPickerInputProcessor.h"

#include "WidgetPicking/AIWidgetPicker.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

FAIWidgetPickerInputProcessor::FAIWidgetPickerInputProcessor(const TSharedRef<FAIWidgetPicker>& InPicker)
	: PickerWeak(InPicker)
{
}

TSharedPtr<FAIWidgetPicker> FAIWidgetPickerInputProcessor::GetPicker() const
{
	TSharedPtr<FAIWidgetPicker> Picker = PickerWeak.Pin();
	return (Picker.IsValid() && Picker->IsInspecting()) ? Picker : nullptr;
}

void FAIWidgetPickerInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// 커서가 멈춰 있어도 그 아래 UI는 바뀔 수 있으므로 MouseMove가 아니라 매 Tick 갱신한다.
	if (TSharedPtr<FAIWidgetPicker> Picker = GetPicker())
	{
		Picker->HandleCursorMoved(FVector2f(SlateApp.GetCursorPos()));
	}
}

bool FAIWidgetPickerInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (TSharedPtr<FAIWidgetPicker> Picker = GetPicker())
	{
		Picker->HandleCursorMoved(FVector2f(MouseEvent.GetScreenSpacePosition()));
	}

	// 이동 자체는 소비하지 않는다. 소비하면 커서 모양/툴팁 같은 것들이 죽는다.
	return false;
}

bool FAIWidgetPickerInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FAIWidgetPicker> Picker = GetPicker();
	if (!Picker.IsValid())
	{
		return false;
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Picker->HandlePickAt(FVector2f(MouseEvent.GetScreenSpacePosition()));
	}
	else
	{
		// 좌클릭 이외의 버튼은 Inspect Mode 취소로 처리한다.
		Picker->HandleCancel();
	}

	bConsumedButtonDown = true;
	return true;
}

bool FAIWidgetPickerInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (bConsumedButtonDown)
	{
		bConsumedButtonDown = false;
		return true;
	}

	return false;
}

bool FAIWidgetPickerInputProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return GetPicker().IsValid();
}

bool FAIWidgetPickerInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	TSharedPtr<FAIWidgetPicker> Picker = GetPicker();
	if (!Picker.IsValid())
	{
		return false;
	}

	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Picker->HandleCancel();
		return true;
	}

	return false;
}
