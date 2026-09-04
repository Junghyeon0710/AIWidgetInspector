// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "WidgetPicking/AIWidgetPicker.h"

#include "AIWidgetInspectorLog.h"
#include "WidgetPicking/AIWidgetPickerInputProcessor.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/StringBuilder.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

FAIWidgetPicker::~FAIWidgetPicker()
{
	// Shutdown()이 호출되지 않은 채 파괴되더라도 Slate에 dangling 등록이 남지 않게 한다.
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();
}

void FAIWidgetPicker::Initialize()
{
	// 현재는 별도 준비가 필요 없다. Inspect Mode 진입 시점에 Input Pre-Processor를 등록한다.
}

void FAIWidgetPicker::Shutdown()
{
	ExitInspectMode();

	InputProcessor.Reset();
	IgnoredWidgets.Reset();
	HoveredWidgetPath = FWeakWidgetPath();
	PickedWidgetPath = FWeakWidgetPath();
}

void FAIWidgetPicker::EnterInspectMode()
{
	if (bIsInspecting)
	{
		return;
	}

	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("Slate is not initialised, so Inspect Mode cannot start."));
		return;
	}

	if (!InputProcessor.IsValid())
	{
		InputProcessor = MakeShared<FAIWidgetPickerInputProcessor>(AsShared());
	}

	// 에디터/게임 쪽 처리기보다 먼저 입력을 보기 위해 PreEditor 버킷에 등록한다.
	if (!FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, EInputPreProcessorType::PreEditor))
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("Failed to register the input pre-processor."));
		return;
	}

	bIsInspecting = true;
	HoveredWidgetPath = FWeakWidgetPath();

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Inspect Mode on. Click a widget to select it; Esc or right-click cancels."));

	InspectModeChangedEvent.Broadcast(true);
}

void FAIWidgetPicker::ExitInspectMode()
{
	if (!bIsInspecting)
	{
		return;
	}

	bIsInspecting = false;
	HoveredWidgetPath = FWeakWidgetPath();

	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Inspect Mode off."));

	InspectModeChangedEvent.Broadcast(false);
}

void FAIWidgetPicker::ToggleInspectMode()
{
	if (bIsInspecting)
	{
		ExitInspectMode();
	}
	else
	{
		EnterInspectMode();
	}
}

FWidgetPath FAIWidgetPicker::ResolvePickedWidgetPath() const
{
	if (!PickedWidgetPath.IsValid())
	{
		return FWidgetPath();
	}

	// 경로 중간이 파괴되었으면 잘린 경로 대신 무효 경로를 받는 편이 오해가 없다.
	return PickedWidgetPath.ToWidgetPath(FWeakWidgetPath::EInterruptedPathHandling::ReturnInvalid);
}

void FAIWidgetPicker::AddIgnoredWidget(const TSharedRef<SWidget>& InWidget)
{
	// 죽은 항목은 이 참에 같이 정리한다.
	bool bAlreadyPresent = false;
	IgnoredWidgets.RemoveAll(
		[&InWidget, &bAlreadyPresent](const TWeakPtr<SWidget>& Entry)
		{
			const TSharedPtr<SWidget> Pinned = Entry.Pin();
			if (!Pinned.IsValid())
			{
				return true;
			}

			bAlreadyPresent |= (Pinned == InWidget);
			return false;
		});

	if (!bAlreadyPresent)
	{
		IgnoredWidgets.Emplace(InWidget);
	}
}

void FAIWidgetPicker::RemoveIgnoredWidget(const TSharedRef<SWidget>& InWidget)
{
	IgnoredWidgets.RemoveAll(
		[&InWidget](const TWeakPtr<SWidget>& Entry)
		{
			const TSharedPtr<SWidget> Pinned = Entry.Pin();
			return !Pinned.IsValid() || Pinned == InWidget;
		});
}

void FAIWidgetPicker::HandleCursorMoved(const FVector2f& InScreenSpacePosition)
{
	if (!bIsInspecting)
	{
		return;
	}

	const FWidgetPath NewPath = LocateWidgetPathAt(InScreenSpacePosition);

	// 가장 깊은 Widget이 그대로면 이벤트를 굳이 다시 쏘지 않는다.
	const TSharedPtr<SWidget> PreviousLeaf = HoveredWidgetPath.IsValid() ? HoveredWidgetPath.GetLastWidget().Pin() : nullptr;
	const TSharedPtr<SWidget> NewLeaf = NewPath.IsValid() ? NewPath.GetLastWidget() : TSharedPtr<SWidget>();

	if (PreviousLeaf == NewLeaf)
	{
		return;
	}

	HoveredWidgetPath = FWeakWidgetPath(NewPath);
	HoverChangedEvent.Broadcast(NewPath);
}

void FAIWidgetPicker::HandlePickAt(const FVector2f& InScreenSpacePosition)
{
	if (!bIsInspecting)
	{
		return;
	}

	const FWidgetPath NewPath = LocateWidgetPathAt(InScreenSpacePosition);
	if (!NewPath.IsValid())
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("No widget was hit under the cursor."));
		return;
	}

	if (IsIgnoredPath(NewPath))
	{
		// Inspector 자신을 클릭한 경우. 선택을 바꾸지 않고 Inspect Mode만 빠져나간다.
		ExitInspectMode();
		return;
	}

	PickedWidgetPath = FWeakWidgetPath(NewPath);

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Widget selected:\n%s"), *DescribeWidgetPath(NewPath));

	// 선택이 끝나면 에디터 입력을 곧바로 돌려준다.
	ExitInspectMode();

	WidgetPickedEvent.Broadcast(NewPath);
}

void FAIWidgetPicker::HandleCancel()
{
	ExitInspectMode();
}

FWidgetPath FAIWidgetPicker::LocateWidgetPathAt(const FVector2f& InScreenSpacePosition) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return FWidgetPath();
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();

	// Widget Reflector의 HitTesting 모드와 동일한 경로. HitTest Grid를 쓰기 때문에
	// "실제로 클릭을 받을 수 있는" Widget들만 경로에 들어온다. 클릭이 안 되는 원인을 찾는 데 이게 중요하다.
	return SlateApp.LocateWindowUnderMouse(
		InScreenSpacePosition,
		SlateApp.GetInteractiveTopLevelWindows(),
		/*bIgnoreEnabledStatus=*/false,
		INDEX_NONE);
}

bool FAIWidgetPicker::IsIgnoredPath(const FWidgetPath& InWidgetPath) const
{
	for (const TWeakPtr<SWidget>& IgnoredWeak : IgnoredWidgets)
	{
		const TSharedPtr<SWidget> Ignored = IgnoredWeak.Pin();
		if (Ignored.IsValid() && InWidgetPath.ContainsWidget(Ignored.Get()))
		{
			return true;
		}
	}

	return false;
}

FName FAIWidgetPicker::GetWidgetName(const TSharedRef<SWidget>& InWidget)
{
	// UMG가 만든 Slate Widget에는 FReflectionMetaData가 붙어 있다. 이름을 여기서 얻는다.
	if (const TSharedPtr<FReflectionMetaData> MetaData = InWidget->GetMetaData<FReflectionMetaData>())
	{
		return MetaData->Name;
	}

	return NAME_None;
}

FString FAIWidgetPicker::DescribeWidget(const TSharedRef<SWidget>& InWidget)
{
	FString Result = InWidget->GetTypeAsString();

	const FName Name = GetWidgetName(InWidget);
	if (!Name.IsNone())
	{
		Result += FString::Printf(TEXT(" (%s)"), *Name.ToString());
	}

	return Result;
}

FString FAIWidgetPicker::DescribeWidgetPath(const FWidgetPath& InWidgetPath)
{
	if (!InWidgetPath.IsValid())
	{
		return TEXT("<invalid widget path>");
	}

	TStringBuilder<2048> Builder;

	for (int32 Index = 0; Index < InWidgetPath.Widgets.Num(); ++Index)
	{
		const FArrangedWidget& ArrangedWidget = InWidgetPath.Widgets[Index];
		const TSharedRef<SWidget>& Widget = ArrangedWidget.Widget;

		const FVector2f AbsolutePosition = FVector2f(ArrangedWidget.Geometry.GetAbsolutePosition());
		const FVector2f LocalSize = FVector2f(ArrangedWidget.Geometry.GetLocalSize());

		const FString Indent = FString::ChrN(Index * 2, TEXT(' '));

		Builder.Appendf(TEXT("%s%s"), *Indent, *DescribeWidget(Widget));
		Builder.Appendf(TEXT("  [pos %.0f,%.0f  size %.0f x %.0f  visibility %s  enabled %s]"),
			AbsolutePosition.X,
			AbsolutePosition.Y,
			LocalSize.X,
			LocalSize.Y,
			*Widget->GetVisibility().ToString(),
			Widget->IsEnabled() ? TEXT("true") : TEXT("false"));

		// SNew/SAssignNew가 기록해 둔 생성 위치. C++ Slate로 만들어진 Widget이면 파일/라인이 잡힌다.
		const FName CreatedInLocation = Widget->GetCreatedInLocation();
		if (!CreatedInLocation.IsNone())
		{
			Builder.Appendf(TEXT("  {%s:%d}"), *CreatedInLocation.GetPlainNameString(), CreatedInLocation.GetNumber());
		}

		Builder.Append(TEXT("\n"));
	}

	return Builder.ToString();
}
