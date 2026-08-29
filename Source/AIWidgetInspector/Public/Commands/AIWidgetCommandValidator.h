// AI Widget Inspector

#pragma once

#include "Commands/AIWidgetCommand.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWidget;
struct FAIWidgetInspectionResult;

/** 명령 하나를 검사한 결과. */
struct FAIWidgetCommandValidation
{
	bool bIsValid = false;

	/** 이름으로 찾아낸 실제 대상. 검사를 통과했을 때만 유효하다. */
	TWeakObjectPtr<UWidget> TargetWidget;

	/** 실패 이유. */
	FText Error;

	/** 미리보기 목록에 보여줄 한 줄. "Btn_Upgrade   SetRenderOpacity   1.00 -> 0.50" */
	FString PlanLine;
};

/**
 * 실행 전에 명령을 검사한다.
 *
 * 파서가 형식을 봤다면 여기서는 대상을 본다. 이름이 실제 Widget을 가리키는지,
 * 그 Widget이 그 Operation을 받을 수 있는지, 값이 범위 안인지.
 * 통과하지 못한 명령은 Apply 목록에 올라가지 않는다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetCommandValidator
{
public:
	static FAIWidgetCommandValidation Validate(const FAIWidgetCommand& InCommand, const FAIWidgetInspectionResult& InInspection);

	/**
	 * 이름으로 대상 UWidget을 찾는다.
	 *
	 * 선택된 Widget 자신이거나, 같은 UserWidget 안의 다른 Widget이어야 한다.
	 * 사용자가 STextBlock을 찍고 "이 버튼 숨겨줘"라고 했을 때 AI가 부모 Button 이름을
	 * 지목하는 게 정상이므로, 형제 탐색까지는 허용한다. 그 바깥은 찾지 않는다.
	 */
	static UWidget* ResolveTargetWidget(FName InTargetWidgetName, const FAIWidgetInspectionResult& InInspection);

	/** 이 Operation이 보는 속성의 현재 값. 미리보기의 "이전 값" 쪽에 쓴다. */
	static FString DescribeCurrentValue(const UWidget* InWidget, EAIWidgetOperation InOperation);
};
