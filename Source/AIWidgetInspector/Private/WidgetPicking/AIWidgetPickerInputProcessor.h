// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class FAIWidgetPicker;

/**
 * Inspect Mode 동안에만 등록되는 Input Pre-Processor.
 *
 * Widget Reflector는 마우스를 가로채지 않고 hover만 추적한 뒤 ESC로 확정하지만,
 * 이 플러그인은 "클릭으로 선택"이 필요하므로 Slate가 이벤트를 Widget에 라우팅하기 전에
 * 마우스 입력을 소비한다. 그래서 Inspect Mode 중에는 에디터 UI가 클릭에 반응하지 않는다.
 */
class FAIWidgetPickerInputProcessor : public IInputProcessor
{
public:
	explicit FAIWidgetPickerInputProcessor(const TSharedRef<FAIWidgetPicker>& InPicker);

	//~ IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("AIWidgetPicker"); }
	//~ End IInputProcessor

private:
	TSharedPtr<FAIWidgetPicker> GetPicker() const;

	TWeakPtr<FAIWidgetPicker> PickerWeak;

	/** Down을 소비했으면 짝이 되는 Up/DoubleClick도 소비해서 반쪽짜리 클릭이 UI에 흘러가지 않게 한다. */
	bool bConsumedButtonDown = false;
};
