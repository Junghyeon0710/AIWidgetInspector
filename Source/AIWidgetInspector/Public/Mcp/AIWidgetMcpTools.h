// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"

class FAIWidgetRuntimePreview;
class FAIWidgetSelection;
class FJsonObject;
struct FAIWidgetInspectionResult;

/** Tool 하나가 남긴 결과. 실패해도 예외 대신 이유가 담긴 텍스트를 돌려준다. */
struct FAIWidgetMcpToolResult
{
	FString Text;
	bool bIsError = false;

	static FAIWidgetMcpToolResult Ok(FString InText) { return FAIWidgetMcpToolResult{ MoveTemp(InText), false }; }
	static FAIWidgetMcpToolResult Error(FString InText) { return FAIWidgetMcpToolResult{ MoveTemp(InText), true }; }
};

/**
 * AI가 직접 부를 수 있는 동작들.
 *
 * 이 목록이 곧 화이트리스트다. AI는 이제 JSON을 돌려주는 대신 여기 있는 Tool을 부르지만,
 * 부를 수 있는 것은 여전히 여기 적힌 것뿐이다. 값은 기존 파서·검사기를 그대로 통과하므로
 * 검증 경로가 둘로 갈라지지 않는다.
 *
 * 에셋에 쓰는 것과 미리보기는 Tool 자체를 나눠 두었다. 인자 하나 차이로 파일이 바뀌는 것보다,
 * 이름이 다른 Tool을 부르게 하는 편이 AI에게도 로그를 읽는 사람에게도 분명하다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetMcpTools
{
public:
	FAIWidgetMcpTools(
		TSharedRef<FAIWidgetSelection> InSelection,
		TSharedRef<FAIWidgetRuntimePreview> InRuntimePreview);

	/** MCP `tools/list` 에 실을 정의들. */
	TArray<TSharedPtr<FJsonObject>> BuildToolDefinitions() const;

	/** 이름으로 Tool을 실행한다. 모르는 이름이면 오류 결과를 돌려준다. */
	FAIWidgetMcpToolResult Call(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments);

	/** 이 Tool들이 다룰 수 있는 상태인지. 선택이 없으면 아무것도 못 한다. */
	bool HasSelection() const;

private:
	/** 이름으로 갈라 준다. 결과 로깅을 한 곳에서 하려고 Call과 분리했다. */
	FAIWidgetMcpToolResult Dispatch(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments);

	FAIWidgetMcpToolResult GetSelectedWidget() const;
	FAIWidgetMcpToolResult ListWidgetTree() const;
	FAIWidgetMcpToolResult ApplyChange(const TSharedPtr<FJsonObject>& InArguments, bool bInWriteToAsset);
	FAIWidgetMcpToolResult RevertPreview();
	FAIWidgetMcpToolResult SaveAsset();

	/** 지금 선택을 다시 검사한다. Tool은 언제 불릴지 모르므로 그때그때 새로 본다. */
	FAIWidgetInspectionResult InspectSelection() const;

	TSharedRef<FAIWidgetSelection> Selection;
	TSharedRef<FAIWidgetRuntimePreview> RuntimePreview;
};
