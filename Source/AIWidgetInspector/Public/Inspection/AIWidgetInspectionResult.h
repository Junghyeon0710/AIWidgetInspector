// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UClass;
class UPanelSlot;
class UUserWidget;
class UWidget;

/**
 * 선택된 Slate Widget 하나에서 긁어모은 정보.
 *
 * UMG 쪽 필드는 전부 optional이다. 엔진 툴바처럼 순수 C++ Slate로 만들어진 Widget에는
 * FReflectionMetaData 자체가 없으므로 비어 있는 게 정상이고, 이때 추측으로 채우지 않는다.
 */
struct FAIWidgetInspectionResult
{
	/** 최소한 Slate 정보는 채워졌는지. */
	bool bIsValid = false;

	//~ Slate
	FString SlateType;

	/** SNew/SAssignNew가 기록한 생성 위치. PlainName = 파일, Number = 라인. */
	FName SlateCreatedIn;

	//~ UMG. FReflectionMetaData가 있을 때만 채워진다.

	/** UMG에서의 Widget 이름 (Btn_Upgrade). */
	FName WidgetName;

	/** 이 Slate Widget을 만든 UWidget. */
	TWeakObjectPtr<UWidget> SourceWidget;

	/** UMG 타입 (UButton, UTextBlock ...). */
	TWeakObjectPtr<UClass> UMGClass;

	/** SourceWidget을 소유한 UserWidget 인스턴스. */
	TWeakObjectPtr<UUserWidget> OwnerUserWidget;

	/** OwnerUserWidget의 클래스. Blueprint면 UWBP_Xxx_C. */
	TWeakObjectPtr<UClass> OwnerUserWidgetClass;

	/** OwnerUserWidgetClass 위로 올라가면서 만난 첫 네이티브 클래스. */
	TWeakObjectPtr<UClass> NativeParentClass;

	/** Widget Blueprint 에셋. [Open Blueprint]가 여는 대상. */
	TWeakObjectPtr<UObject> BlueprintAsset;

	/** 표시용 에셋 경로. BlueprintAsset을 못 얻어도 MetaData의 Asset으로 채워질 수 있다. */
	FString SourceAssetPath;

	/** 위치/크기를 실제로 들고 있는 Slot. CanvasPanelSlot / VerticalBoxSlot 등. */
	TWeakObjectPtr<UPanelSlot> Slot;

	/** UMG 계층에서의 부모. */
	TWeakObjectPtr<UWidget> ParentWidget;

	/** Panel Widget이면 자식 수, 아니면 INDEX_NONE. */
	int32 ChildWidgetCount = INDEX_NONE;

	/**
	 * MetaData를 선택한 Widget 자신이 아니라 조상에서 가져왔는지.
	 *
	 * UMG의 UTextBlock 하나가 내부적으로 STextBlock 여러 겹을 만들 수 있어서,
	 * 가장 깊은 Slate Widget에는 MetaData가 없는 경우가 흔하다. 이 경우 조상 것을 쓰되
	 * "정확히 이 Slate Widget의 정보는 아니다"라는 걸 UI에서 알려줘야 한다.
	 */
	bool bMetaDataFromAncestor = false;

	/** UMG까지 연결이 됐는지. */
	bool HasUMGLink() const { return SourceWidget.IsValid() || UMGClass.IsValid(); }

	//~ 표시용. 얻지 못한 값은 빈 문자열을 반환한다.
	FString GetUMGTypeName() const;
	FString GetOwnerUserWidgetName() const;
	FString GetOwnerClassName() const;
	FString GetNativeParentClassName() const;
	FString GetSlotTypeName() const;
	FString GetParentWidgetName() const;
};
