// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Inspection/AIWidgetInspector.h"

#include "Blueprint/UserWidget.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Types/ReflectionMetadata.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

namespace AIWidgetInspectorPrivate
{
	static FString GetClassDisplayName(const TWeakObjectPtr<UClass>& InClass)
	{
		const UClass* ResolvedClass = InClass.Get();
		return ResolvedClass ? ResolvedClass->GetName() : FString();
	}
}

FString FAIWidgetInspectionResult::GetUMGTypeName() const
{
	return AIWidgetInspectorPrivate::GetClassDisplayName(UMGClass);
}

FString FAIWidgetInspectionResult::GetOwnerUserWidgetName() const
{
	const UUserWidget* Owner = OwnerUserWidget.Get();
	return Owner ? Owner->GetName() : FString();
}

FString FAIWidgetInspectionResult::GetOwnerClassName() const
{
	return AIWidgetInspectorPrivate::GetClassDisplayName(OwnerUserWidgetClass);
}

FString FAIWidgetInspectionResult::GetNativeParentClassName() const
{
	return AIWidgetInspectorPrivate::GetClassDisplayName(NativeParentClass);
}

FString FAIWidgetInspectionResult::GetSlotTypeName() const
{
	const UPanelSlot* ResolvedSlot = Slot.Get();
	return ResolvedSlot ? ResolvedSlot->GetClass()->GetName() : FString();
}

FString FAIWidgetInspectionResult::GetParentWidgetName() const
{
	const UWidget* Parent = ParentWidget.Get();
	return Parent ? Parent->GetName() : FString();
}

UClass* FAIWidgetInspector::FindNativeParentClass(UClass* InClass)
{
	UClass* Current = InClass;
	while (Current && Current->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		Current = Current->GetSuperClass();
	}

	return Current;
}

FAIWidgetInspectionResult FAIWidgetInspector::Inspect(const TSharedPtr<const SWidget>& InWidget)
{
	FAIWidgetInspectionResult Result;

	if (!InWidget.IsValid())
	{
		return Result;
	}

	Result.bIsValid = true;
	Result.SlateType = InWidget->GetTypeAsString();
	Result.SlateCreatedIn = InWidget->GetCreatedInLocation();

	// UMG가 붙여둔 꼬리표. 이게 없으면 순수 C++ Slate Widget이다.
	TSharedPtr<FReflectionMetaData> MetaData = InWidget->GetMetaData<FReflectionMetaData>();
	if (!MetaData.IsValid())
	{
		// UMG Widget 하나가 Slate Widget을 여러 겹 만드는 경우가 많아서
		// 가장 깊은 Slate Widget에는 꼬리표가 없을 수 있다. 조상까지 찾아본다.
		MetaData = FReflectionMetaData::GetWidgetOrParentMetaData(InWidget.Get());
		Result.bMetaDataFromAncestor = MetaData.IsValid();
	}

	if (!MetaData.IsValid())
	{
		return Result;
	}

	Result.WidgetName = MetaData->Name;
	Result.UMGClass = MetaData->Class;

	// 꼬리표에 담긴 Asset은 const라서 여는 용도로는 쓰지 않는다. 경로 표시에만 쓴다.
	if (const UObject* MetaDataAsset = MetaData->Asset.Get())
	{
		const UPackage* MetaDataPackage = MetaDataAsset->GetPackage();
		Result.SourceAssetPath = MetaDataPackage ? MetaDataPackage->GetName() : MetaDataAsset->GetPathName();
	}

	UWidget* SourceWidget = Cast<UWidget>(MetaData->SourceObject.Get());
	if (!SourceWidget)
	{
		return Result;
	}

	Result.SourceWidget = SourceWidget;
	Result.Slot = SourceWidget->Slot;
	Result.ParentWidget = SourceWidget->GetParent();

	if (const UPanelWidget* AsPanel = Cast<UPanelWidget>(SourceWidget))
	{
		Result.ChildWidgetCount = AsPanel->GetChildrenCount();
	}

	// UWidget -> UWidgetTree -> UUserWidget
	UUserWidget* OwnerUserWidget = SourceWidget->GetTypedOuter<UUserWidget>();
	if (!OwnerUserWidget)
	{
		return Result;
	}

	Result.OwnerUserWidget = OwnerUserWidget;

	UClass* OwnerClass = OwnerUserWidget->GetClass();
	Result.OwnerUserWidgetClass = OwnerClass;
	Result.NativeParentClass = FindNativeParentClass(OwnerClass);

	// Blueprint에서 생성된 클래스면 ClassGeneratedBy가 원본 Widget Blueprint 에셋이다.
	if (OwnerClass)
	{
		if (UObject* GeneratedBy = OwnerClass->ClassGeneratedBy)
		{
			Result.BlueprintAsset = GeneratedBy;

			// GetPathName()은 "/Game/UI/WBP_Foo.WBP_Foo"까지 나온다. 표시에는 패키지 경로가 읽기 좋다.
			const UPackage* AssetPackage = GeneratedBy->GetPackage();
			Result.SourceAssetPath = AssetPackage ? AssetPackage->GetName() : GeneratedBy->GetPathName();
		}
	}

	return Result;
}
