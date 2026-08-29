// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Inspection/AIWidgetInspectionResult.h"

class SWidget;

/**
 * 화면에서 잡은 SWidget을 원본 UMG/Blueprint까지 거슬러 올라가 정리한다.
 *
 * 추적 경로:
 *   SWidget
 *     -> FReflectionMetaData          (UMG가 SWidget을 만들 때 붙여둔 꼬리표)
 *     -> UWidget                      (MetaData::SourceObject)
 *     -> UUserWidget                  (UWidget의 Outer 체인. UWidget -> UWidgetTree -> UUserWidget)
 *     -> UWidgetBlueprintGeneratedClass
 *     -> UWidgetBlueprint             (UClass::ClassGeneratedBy)
 *
 * 중간에 끊기면 거기서 멈춘다. 없는 정보를 추측해서 채우지 않는다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetInspector
{
public:
	static FAIWidgetInspectionResult Inspect(const TSharedPtr<const SWidget>& InWidget);

	/** Blueprint가 아닌, 실제 C++로 작성된 첫 조상 클래스를 찾는다. */
	static UClass* FindNativeParentClass(UClass* InClass);
};
