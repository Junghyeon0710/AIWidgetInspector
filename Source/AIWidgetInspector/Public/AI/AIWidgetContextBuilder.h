// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"

class FAIWidgetSelection;
struct FAIWidgetInspectionResult;
struct FAIWidgetSourceInfo;

/**
 * 선택된 Widget 하나를 AI에게 넘길 텍스트로 정리한다.
 *
 * 프로젝트 코드를 통째로 보내지 않는다. 선택된 Widget과 그 주변만 담는다.
 * Widget 경로도 전부 넣지 않는다. 에디터 창부터 세면 30단계가 넘는데 앞쪽은 전부
 * 에디터 chrome이라 답변에 도움이 안 된다. UserWidget 경계(SObjectWidget)가 있으면
 * 거기서부터, 없으면 선택 지점 위 몇 단계까지만 넣는다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetContextBuilder
{
public:
	static FString BuildContext(
		const FAIWidgetSelection& InSelection,
		const FAIWidgetInspectionResult& InInspection,
		const FAIWidgetSourceInfo& InSourceInfo);

	/** 경로에서 선택 지점 위로 최대 몇 단계까지 넣을지. UserWidget 경계를 찾으면 이 값은 무시된다. */
	static constexpr int32 MaxAncestorsWithoutUserWidget = 8;
};
