// AI Widget Inspector

#include "AI/AIWidgetContextBuilder.h"

#include "Inspection/AIWidgetInspectionResult.h"
#include "Inspection/AIWidgetSelection.h"
#include "Inspection/AIWidgetSourceResolver.h"
#include "WidgetPicking/AIWidgetPicker.h"

#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Widgets/SWidget.h"

namespace AIWidgetContextBuilderPrivate
{
	/** 값이 없으면 줄 자체를 빼서 AI가 빈 값을 사실로 오해하지 않게 한다. */
	static void AppendLineIfSet(TStringBuilder<4096>& InBuilder, const TCHAR* InLabel, const FString& InValue)
	{
		if (!InValue.IsEmpty())
		{
			InBuilder.Appendf(TEXT("%s: %s\n"), InLabel, *InValue);
		}
	}

	static void AppendLineIfSet(TStringBuilder<4096>& InBuilder, const TCHAR* InLabel, const FName& InValue)
	{
		if (!InValue.IsNone())
		{
			InBuilder.Appendf(TEXT("%s: %s\n"), InLabel, *InValue.ToString());
		}
	}

	/**
	 * 경로에서 어디부터 넣을지 정한다.
	 * SObjectWidget은 UserWidget 하나가 시작되는 지점이라, 거기부터가 사용자가 만든 UI다.
	 */
	static int32 FindContextStartIndex(const FAIWidgetSelection& InSelection)
	{
		const int32 SelectedIndex = InSelection.GetSelectedIndex();

		for (int32 Index = SelectedIndex; Index >= 0; --Index)
		{
			const TSharedPtr<SWidget> Widget = InSelection.GetWidgetAt(Index);
			if (Widget.IsValid() && Widget->GetTypeAsString() == TEXT("SObjectWidget"))
			{
				return Index;
			}
		}

		return FMath::Max(0, SelectedIndex - FAIWidgetContextBuilder::MaxAncestorsWithoutUserWidget);
	}
}

FString FAIWidgetContextBuilder::BuildContext(
	const FAIWidgetSelection& InSelection,
	const FAIWidgetInspectionResult& InInspection,
	const FAIWidgetSourceInfo& InSourceInfo)
{
	using namespace AIWidgetContextBuilderPrivate;

	if (!InSelection.IsValid() || !InInspection.bIsValid)
	{
		return FString();
	}

	TStringBuilder<4096> Builder;

	// --- 무엇이 선택됐는지 ---
	Builder.Append(TEXT("[Selected Widget]\n"));
	AppendLineIfSet(Builder, TEXT("Name"), InInspection.WidgetName);
	AppendLineIfSet(Builder, TEXT("Slate Type"), InInspection.SlateType);
	AppendLineIfSet(Builder, TEXT("UMG Type"), InInspection.GetUMGTypeName());
	AppendLineIfSet(Builder, TEXT("Owner"), InInspection.GetOwnerUserWidgetName());
	AppendLineIfSet(Builder, TEXT("Owner Class"), InInspection.GetOwnerClassName());
	AppendLineIfSet(Builder, TEXT("Native Parent"), InInspection.GetNativeParentClassName());

	if (InInspection.bMetaDataFromAncestor)
	{
		Builder.Append(TEXT("Note: this Slate widget carries no UMG metadata, so the fields below come from an ancestor and may not describe this widget exactly.\n"));
	}

	// --- 지금 상태 ---
	const TSharedPtr<SWidget> SelectedWidget = InSelection.GetSelectedWidget();
	if (SelectedWidget.IsValid())
	{
		Builder.Append(TEXT("\n[State]\n"));
		Builder.Appendf(TEXT("Visibility = %s\n"), *SelectedWidget->GetVisibility().ToString());
		Builder.Appendf(TEXT("Enabled = %s\n"), SelectedWidget->IsEnabled() ? TEXT("true") : TEXT("false"));

		const FVector2D WidgetDesiredSize = SelectedWidget->GetDesiredSize();
		Builder.Appendf(TEXT("DesiredSize = %.0f x %.0f\n"), WidgetDesiredSize.X, WidgetDesiredSize.Y);

		const FWidgetPath ResolvedPath = InSelection.ResolvePath();
		const int32 SelectedIndex = InSelection.GetSelectedIndex();
		if (ResolvedPath.IsValid() && SelectedIndex >= 0 && SelectedIndex < ResolvedPath.Widgets.Num())
		{
			const FGeometry& Geometry = ResolvedPath.Widgets[SelectedIndex].Geometry;
			const FVector2f AbsolutePosition = FVector2f(Geometry.GetAbsolutePosition());
			const FVector2f LocalSize = FVector2f(Geometry.GetLocalSize());

			Builder.Appendf(TEXT("Geometry = pos %.0f, %.0f  size %.0f x %.0f\n"),
				AbsolutePosition.X, AbsolutePosition.Y, LocalSize.X, LocalSize.Y);

			if (Geometry.HasRenderTransform())
			{
				Builder.Append(TEXT("RenderTransform = set\n"));
			}
		}

		AppendLineIfSet(Builder, TEXT("Slot Type"), InInspection.GetSlotTypeName());
		AppendLineIfSet(Builder, TEXT("UMG Parent"), InInspection.GetParentWidgetName());

		if (InInspection.ChildWidgetCount != INDEX_NONE)
		{
			Builder.Appendf(TEXT("Child Count = %d\n"), InInspection.ChildWidgetCount);
		}
	}

	// --- 어디에 박혀 있는지 ---
	const int32 StartIndex = FindContextStartIndex(InSelection);
	const int32 NumWidgets = InSelection.Num();
	const int32 SelectedIndex = InSelection.GetSelectedIndex();

	Builder.Append(TEXT("\n[Widget Path]\n"));
	if (StartIndex > 0)
	{
		Builder.Appendf(TEXT("... (%d level(s) above omitted)\n"), StartIndex);
	}

	for (int32 Index = StartIndex; Index < NumWidgets; ++Index)
	{
		const TSharedPtr<SWidget> Widget = InSelection.GetWidgetAt(Index);
		const FString Indent = FString::ChrN((Index - StartIndex) * 2, TEXT(' '));
		const FString Description = Widget.IsValid()
			? FAIWidgetPicker::DescribeWidget(Widget.ToSharedRef())
			: FString(TEXT("<destroyed>"));

		Builder.Appendf(TEXT("%s%s%s\n"),
			*Indent,
			*Description,
			Index == SelectedIndex ? TEXT("   <-- selected") : TEXT(""));
	}

	// --- 원본이 어디인지 ---
	const bool bHasBlueprint = !InInspection.SourceAssetPath.IsEmpty();
	const bool bHasSource = !InInspection.SlateCreatedIn.IsNone();
	if (bHasBlueprint || bHasSource)
	{
		Builder.Append(TEXT("\n[Source]\n"));
		AppendLineIfSet(Builder, TEXT("Blueprint"), InInspection.SourceAssetPath);

		if (bHasSource)
		{
			Builder.Appendf(TEXT("C++: %s:%d\n"),
				*FPaths::GetCleanFilename(InInspection.SlateCreatedIn.GetPlainNameString()),
				InInspection.SlateCreatedIn.GetNumber());
		}

		// 파일을 실제로 열 수 있을 때만 전체 경로를 준다. 없는 경로를 주면 AI가 그걸 읽으려 든다.
		AppendLineIfSet(Builder, TEXT("C++ Path"), InSourceInfo.ResolvedFile);
		AppendLineIfSet(Builder, TEXT("Native Header"), InSourceInfo.NativeHeaderPath);
		AppendLineIfSet(Builder, TEXT("Native Source"), InSourceInfo.NativeSourcePath);
	}

	// --- 그 코드가 어떻게 생겼는지 ---
	//
	// 프로젝트 코드를 통째로 보내지 않는다. Widget이 만들어진 줄 주변만, 그것도 21줄까지다.
	// "왜 클릭이 안 되는지" 같은 질문은 대개 SNew 근처의 Visibility나 OnClicked에서 답이 나온다.
	if (InSourceInfo.HasSnippet())
	{
		Builder.Appendf(TEXT("\n[Source Snippet] %s:%d-%d  ('>' marks the line this widget was created on)\n"),
			*FPaths::GetCleanFilename(InSourceInfo.ResolvedFile),
			InSourceInfo.SnippetStartLine,
			InSourceInfo.SnippetEndLine);
		Builder.Append(InSourceInfo.Snippet);
	}

	return Builder.ToString();
}
