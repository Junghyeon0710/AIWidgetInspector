// AI Widget Inspector

#include "Mcp/AIWidgetInspectorToolset.h"

#include "AIWidgetInspectorLog.h"
#include "AIWidgetInspectorModule.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetSelection.h"
#include "Mcp/AIWidgetMcpTools.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AIWidgetInspectorToolsetPrivate
{
	/**
	 * 모듈이 들고 있는 선택/미리보기 위에 Tool 묶음을 만든다.
	 *
	 * 매번 새로 만드는 게 낭비처럼 보이지만 안에 든 것은 공유 포인터 둘뿐이고,
	 * 상태를 들고 있지 않아야 "지금 선택된 것"이 항상 최신으로 보인다.
	 */
	static TSharedPtr<FAIWidgetMcpTools> MakeTools()
	{
		FAIWidgetInspectorModule& Module = FAIWidgetInspectorModule::Get();

		const TSharedPtr<FAIWidgetSelection> Selection = Module.GetWidgetSelection();
		const TSharedPtr<FAIWidgetRuntimePreview> RuntimePreview = Module.GetRuntimePreview();

		if (!Selection.IsValid() || !RuntimePreview.IsValid())
		{
			return nullptr;
		}

		return MakeShared<FAIWidgetMcpTools>(Selection.ToSharedRef(), RuntimePreview.ToSharedRef());
	}

	static FString CallTool(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments)
	{
		const TSharedPtr<FAIWidgetMcpTools> Tools = MakeTools();
		if (!Tools.IsValid())
		{
			return TEXT("AI Widget Inspector 플러그인이 아직 준비되지 않았습니다.");
		}

		const FAIWidgetMcpToolResult Result = Tools->Call(InToolName, InArguments);

		// 실패도 문자열로 돌려준다. 예외로 끊으면 모델은 이유를 못 보고 같은 실수를 반복한다.
		return Result.bIsError
			? FString::Printf(TEXT("실패: %s"), *Result.Text)
			: Result.Text;
	}

	/**
	 * 값 하나짜리 JSON 조각을 읽는다.
	 *
	 * "0.5", "\"#FF0000\"", "true", "{\"x\":30,\"y\":0}" 이 전부 올 수 있는데
	 * FJsonSerializer는 최상위에 오브젝트나 배열을 기대한다. 그래서 한 번 감싸서 읽고
	 * 도로 꺼낸다. 이렇게 하면 타입별로 분기할 필요가 없다.
	 */
	static TSharedPtr<FJsonValue> ParseLooseJsonValue(const FString& InValueJson)
	{
		const FString Wrapped = FString::Printf(TEXT("{\"value\": %s}"), *InValueJson.TrimStartAndEnd());

		TSharedPtr<FJsonObject> WrapperObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Wrapped);
		if (!FJsonSerializer::Deserialize(Reader, WrapperObject) || !WrapperObject.IsValid())
		{
			return nullptr;
		}

		return WrapperObject->TryGetField(TEXT("value"));
	}

	static FString ApplyChange(
		const FString& InOperation,
		const FString& InTargetWidget,
		const FString& InValueJson,
		bool bInWriteToAsset)
	{
		const TSharedPtr<FJsonValue> Value = ParseLooseJsonValue(InValueJson);
		if (!Value.IsValid())
		{
			return FString::Printf(
				TEXT("실패: ValueJson을 읽지 못했습니다. JSON 값이어야 합니다. 문자열이면 따옴표를 포함해야 합니다. 받은 값: %s"),
				*InValueJson);
		}

		// 응답 JSON 경로가 쓰는 것과 같은 모양으로 만든다. 그래야 같은 파서를 지난다.
		TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("operation"), InOperation);
		Arguments->SetStringField(TEXT("target_widget"), InTargetWidget);
		Arguments->SetField(TEXT("value"), Value);

		return CallTool(
			bInWriteToAsset ? TEXT("apply_widget_change_to_asset") : TEXT("preview_widget_change"),
			Arguments);
	}
}

FString UAIWidgetInspectorToolset::GetSelectedWidget()
{
	return AIWidgetInspectorToolsetPrivate::CallTool(TEXT("get_selected_widget"), MakeShared<FJsonObject>());
}

FString UAIWidgetInspectorToolset::ListWidgetTree()
{
	return AIWidgetInspectorToolsetPrivate::CallTool(TEXT("list_widget_tree"), MakeShared<FJsonObject>());
}

FString UAIWidgetInspectorToolset::PreviewWidgetChange(
	const FString& Operation,
	const FString& TargetWidget,
	const FString& ValueJson)
{
	return AIWidgetInspectorToolsetPrivate::ApplyChange(Operation, TargetWidget, ValueJson, /*bInWriteToAsset=*/false);
}

FString UAIWidgetInspectorToolset::ApplyWidgetChangeToAsset(
	const FString& Operation,
	const FString& TargetWidget,
	const FString& ValueJson)
{
	return AIWidgetInspectorToolsetPrivate::ApplyChange(Operation, TargetWidget, ValueJson, /*bInWriteToAsset=*/true);
}

FString UAIWidgetInspectorToolset::SaveWidgetAsset()
{
	return AIWidgetInspectorToolsetPrivate::CallTool(TEXT("save_widget_asset"), MakeShared<FJsonObject>());
}

FString UAIWidgetInspectorToolset::RevertPreview()
{
	return AIWidgetInspectorToolsetPrivate::CallTool(TEXT("revert_preview"), MakeShared<FJsonObject>());
}
