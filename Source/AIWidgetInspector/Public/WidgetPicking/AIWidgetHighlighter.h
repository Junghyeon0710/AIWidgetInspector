// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IWidgetReflector.h"
#include "Layout/ArrangedWidget.h"

class FAIWidgetPicker;
class FAIWidgetSelection;
class FSlateWindowElementList;
class FWidgetPath;
class SWindow;

/**
 * 화면 위에 Widget 경계를 그리는 오버레이.
 *
 * FSlateApplication은 IWidgetReflector 구현체 하나를 등록받아 매 프레임 창을 그릴 때
 * Visualize()를 불러준다(SlateApplication.cpp의 DrawWindowAndChildren). 이 훅이 Widget Reflector의
 * 하이라이트가 그려지는 바로 그 경로라서, 별도 오버레이 창을 띄우는 것보다 정확하고 싸다.
 *
 * 주의: FSlateApplication의 리플렉터 슬롯은 하나뿐이다(TWeakPtr 단일 멤버).
 * 엔진 Widget Reflector 탭을 열면 그쪽이 슬롯을 가져가고 우리 하이라이트가 멈춘다.
 * 그래서 Inspect Mode에 들어갈 때마다 Register()를 다시 호출해 슬롯을 되찾는다.
 *
 * 부수 효과로 얻는 것: 엔진이 등록 시점에 Source/Asset 접근 델리게이트를 넘겨준다.
 * [Open Blueprint] / [Open Source]를 직접 구현하지 않고 이걸 쓰면 된다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetHighlighter
	: public IWidgetReflector
	, public TSharedFromThis<FAIWidgetHighlighter>
{
public:
	FAIWidgetHighlighter(const TSharedRef<FAIWidgetPicker>& InPicker, const TSharedRef<FAIWidgetSelection>& InSelection);
	virtual ~FAIWidgetHighlighter() = default;

	/** FSlateApplication의 리플렉터 슬롯을 차지한다. 이미 우리가 갖고 있어도 호출해도 무해하다. */
	void Register();

	void SetHighlightSelected(bool bInHighlightSelected);
	bool IsHighlightingSelected() const { return bHighlightSelected; }

	/** 엔진이 넘겨준 델리게이트. 바인딩되어 있지 않으면 false를 반환한다. */
	bool OpenSourceLocation(const FString& InFileName, int32 InLineNumber, int32 InColumnNumber = 0) const;
	bool OpenAsset(UObject* InAsset) const;
	bool OpenDebugObject(UObject* InObject) const;

	bool CanOpenSourceLocation() const { return SourceAccessDelegate.IsBound(); }
	bool CanOpenAsset() const { return AssetAccessDelegate.IsBound(); }

	//~ IWidgetReflector
	virtual void OnWidgetPicked() override;
	virtual bool IsShowingFocus() const override { return false; }
	virtual bool IsInPickingMode() const override;
	virtual bool IsVisualizingLayoutUnderCursor() const override;
	virtual int32 Visualize(const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId) override;
	virtual void SetWidgetsToVisualize(const FWidgetPath& InWidgetsToVisualize) override;
	virtual void SetSourceAccessDelegate(FAccessSourceCode InDelegate) override { SourceAccessDelegate = InDelegate; }
	virtual void SetAssetAccessDelegate(FAccessAsset InDelegate) override { AssetAccessDelegate = InDelegate; }
	virtual void SetDebugObjectAccessDelegate(FAccessDebugObject InDelegate) override { DebugObjectAccessDelegate = InDelegate; }
	virtual bool ReflectorNeedsToDrawIn(TSharedRef<SWindow> ThisWindow) const override;
	//~ End IWidgetReflector

private:
	/** Hover 중인 전체 경로. 조상은 흐리게, 가장 깊은 Widget은 진하게. */
	int32 DrawHoveredPath(const FWidgetPath& InWidgetPath, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** 선택된 Widget 하나만. */
	int32 DrawSelectedWidget(FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/**
	 * 경로 기준 Geometry는 desktop space에 뿌리를 두고 있어서 그대로 그리면 창 위치만큼 어긋난다.
	 * 창 위치의 역변환을 뒤에 붙여 window space로 되돌린다. (SWidgetReflector와 동일한 처리)
	 */
	static void DrawWidgetOutline(
		const FArrangedWidget& InArrangedWidget,
		const TSharedRef<SWindow>& InTopLevelWindow,
		const FLinearColor& InColor,
		FSlateWindowElementList& OutDrawElements,
		int32& InOutLayerId);

	TSharedRef<FAIWidgetPicker> Picker;
	TSharedRef<FAIWidgetSelection> Selection;

	bool bHighlightSelected = true;

	FAccessSourceCode SourceAccessDelegate;
	FAccessAsset AssetAccessDelegate;
	FAccessDebugObject DebugObjectAccessDelegate;
};
