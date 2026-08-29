// AI Widget Inspector

#pragma once

#include "Commands/AIWidgetCommand.h"
#include "CoreMinimal.h"

class UBaseWidgetBlueprint;
class UWidget;
struct FAIWidgetInspectionResult;

/** 영구 변경 한 번의 결과. */
struct FAIWidgetPersistentResult
{
	int32 AppliedCount = 0;
	int32 FailedCount = 0;

	/** Blueprint를 다시 컴파일했는지. 실제로 적용된 게 있을 때만 한다. */
	bool bCompiled = false;

	/** 마지막 실패 이유. 아무것도 적용하지 못했으면 여기에만 내용이 있다. */
	FText Error;

	/**
	 * 실제로 건드린 Blueprint.
	 *
	 * 이걸 돌려주는 이유는 Apply가 Blueprint를 재컴파일하면서 화면에 떠 있던 Widget
	 * 인스턴스를 파괴하기 때문이다. 그 순간 선택은 죽고, 선택을 거쳐 에셋을 다시 찾는
	 * 경로는 전부 끊긴다. 저장하려면 여기서 받아 둔 것을 써야 한다.
	 */
	TWeakObjectPtr<UBaseWidgetBlueprint> Blueprint;
};

/**
 * 변경을 Widget Blueprint 에셋에 쓴다.
 *
 * 런타임 미리보기와 결정적으로 다른 점은 대상이다. 미리보기는 화면에 떠 있는 인스턴스를 건드리지만
 * 여기서는 에셋 안의 원본 Widget(WidgetTree의 템플릿)을 고친다. 그래서 다음에 만들어지는 인스턴스와
 * 저장되는 파일에 남는다.
 *
 * 모든 변경은 FScopedTransaction 하나로 묶고 각 UObject에 Modify()를 먼저 부른다.
 * Ctrl+Z 한 번으로 전부 되돌아가야 하기 때문이다.
 *
 * 저장은 하지 않는다. 적용은 에셋을 dirty로 남기고, 저장은 사용자가 따로 눌러야 한다.
 * 자동 저장하면 Ctrl+Z로 되돌린 뒤에도 파일에는 바뀐 내용이 남아 두 상태가 어긋난다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetPersistentApplier
{
public:
	/** 선택된 Widget이 속한 Widget Blueprint. C++ Slate Widget이면 nullptr. */
	static UBaseWidgetBlueprint* GetWidgetBlueprint(const FAIWidgetInspectionResult& InInspection);

	static bool CanApply(const FAIWidgetInspectionResult& InInspection);

	/**
	 * 에셋 안의 원본 Widget을 이름으로 찾는다.
	 *
	 * 화면에 떠 있는 인스턴스가 아니다. 인스턴스를 고쳐도 에셋에는 남지 않는다.
	 */
	static UWidget* ResolveTemplateWidget(FName InTargetWidgetName, const FAIWidgetInspectionResult& InInspection);

	static FAIWidgetPersistentResult Apply(const TArray<FAIWidgetCommand>& InCommands, const FAIWidgetInspectionResult& InInspection);

	static bool IsAssetDirty(const FAIWidgetInspectionResult& InInspection);
	static bool SaveAsset(const FAIWidgetInspectionResult& InInspection, FText& OutError);

	//~ Blueprint를 직접 아는 경우. 선택이 죽은 뒤에도 저장할 수 있다.
	static bool IsAssetDirty(const UBaseWidgetBlueprint* InBlueprint);
	static bool SaveAsset(UBaseWidgetBlueprint* InBlueprint, FText& OutError);
};
