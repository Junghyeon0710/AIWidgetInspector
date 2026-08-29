// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Layout/WidgetPath.h"

class FAIWidgetPickerInputProcessor;
class SWidget;

/** Inspect Mode 중 커서 아래 Widget이 바뀔 때마다 브로드캐스트된다. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAIInspectorHoverChanged, const FWidgetPath& /*HoveredPath*/);

/** 사용자가 클릭으로 Widget을 확정 선택했을 때 브로드캐스트된다. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAIInspectorWidgetPicked, const FWidgetPath& /*PickedPath*/);

/** Inspect Mode 진입/종료. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAIInspectModeChanged, bool /*bIsInspecting*/);

/**
 * Inspect Mode의 상태 기계.
 *
 * Widget Reflector(EWidgetPickingMode::HitTesting)와 같은 방식으로 FSlateApplication의 HitTest Grid를 통해
 * 커서 아래 FWidgetPath를 찾는다. 다만 Widget Reflector가 "hover -> ESC로 확정"인 것과 달리
 * 여기서는 IInputProcessor로 마우스 클릭을 가로채서 "hover -> click으로 확정"으로 동작한다.
 *
 * Widget에 대한 SharedRef를 계속 들고 있으면 원래 수명보다 오래 살아남을 수 있으므로,
 * 유지되는 상태(Hovered/Picked)는 항상 FWeakWidgetPath로 보관하고 필요할 때 FWidgetPath로 되살린다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetPicker : public TSharedFromThis<FAIWidgetPicker>
{
public:
	FAIWidgetPicker() = default;
	~FAIWidgetPicker();

	void Initialize();
	void Shutdown();

	void EnterInspectMode();
	void ExitInspectMode();
	void ToggleInspectMode();
	bool IsInspecting() const { return bIsInspecting; }

	/** 현재 커서 아래 Widget 경로(Inspect Mode 중에만 유효). */
	const FWeakWidgetPath& GetHoveredWidgetPath() const { return HoveredWidgetPath; }

	/** 마지막으로 선택된 Widget 경로. Inspect Mode를 나가도 유지된다. */
	const FWeakWidgetPath& GetPickedWidgetPath() const { return PickedWidgetPath; }

	/** 살아있는 경로로 되살린다. 대상이 이미 파괴되었으면 무효 경로를 반환한다. */
	FWidgetPath ResolvePickedWidgetPath() const;

	FOnAIInspectorHoverChanged& OnHoverChanged() { return HoverChangedEvent; }
	FOnAIInspectorWidgetPicked& OnWidgetPicked() { return WidgetPickedEvent; }
	FOnAIInspectModeChanged& OnInspectModeChanged() { return InspectModeChangedEvent; }

	/**
	 * Picking 대상에서 제외할 Widget을 등록한다.
	 * Inspector 패널 자신을 클릭했을 때 자기 자신을 선택하지 않도록 하기 위한 것.
	 */
	void AddIgnoredWidget(const TSharedRef<SWidget>& InWidget);
	void RemoveIgnoredWidget(const TSharedRef<SWidget>& InWidget);

	//~ IInputProcessor에서 호출된다.
	void HandleCursorMoved(const FVector2f& InScreenSpacePosition);
	void HandlePickAt(const FVector2f& InScreenSpacePosition);
	void HandleCancel();
	//~ End

	/** 로그/Context용 다중 라인 경로 덤프. */
	static FString DescribeWidgetPath(const FWidgetPath& InWidgetPath);

	/** "SButton (Btn_Upgrade)" 형태의 한 줄 요약. */
	static FString DescribeWidget(const TSharedRef<SWidget>& InWidget);

	/** UMG가 붙여준 이름(FReflectionMetaData::Name). C++ Slate Widget이면 NAME_None. */
	static FName GetWidgetName(const TSharedRef<SWidget>& InWidget);

private:
	/** HitTest Grid 기준으로 해당 스크린 좌표 아래의 경로를 찾는다. */
	FWidgetPath LocateWidgetPathAt(const FVector2f& InScreenSpacePosition) const;

	/** 무시 목록에 등록된 Widget이 경로에 포함되어 있는지. */
	bool IsIgnoredPath(const FWidgetPath& InWidgetPath) const;

	bool bIsInspecting = false;

	FWeakWidgetPath HoveredWidgetPath;
	FWeakWidgetPath PickedWidgetPath;

	TSharedPtr<FAIWidgetPickerInputProcessor> InputProcessor;

	TArray<TWeakPtr<SWidget>> IgnoredWidgets;

	FOnAIInspectorHoverChanged HoverChangedEvent;
	FOnAIInspectorWidgetPicked WidgetPickedEvent;
	FOnAIInspectModeChanged InspectModeChangedEvent;
};
