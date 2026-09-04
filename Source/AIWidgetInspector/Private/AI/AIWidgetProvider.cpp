// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "AI/AIWidgetProvider.h"

#include "Commands/AIWidgetCommandParser.h"

FString FAIWidgetRequest::BuildPrompt() const
{
	const TCHAR* SectionHeader = TEXT("[User Question]");
	if (Kind == EAIWidgetRequestKind::ChangeRequest || Kind == EAIWidgetRequestKind::ToolChangeRequest)
	{
		SectionHeader = TEXT("[Requested Change]");
	}

	FString Prompt;
	Prompt.Reserve(Context.Len() + UserMessage.Len() + 64);

	if (!Context.IsEmpty())
	{
		Prompt += Context;
		Prompt += TEXT("\n");
	}

	Prompt += SectionHeader;
	Prompt += TEXT("\n");
	Prompt += UserMessage;
	Prompt += TEXT("\n");


	// 변경 요청이면 응답 형식을 알려 줘야 한다. 이게 빠지면 모델은 자기가 무엇을
	// 돌려주면 실제로 적용되는지 알 길이 없어서, 사람에게 하듯 산문으로 답한다.
	// 그 답은 파서를 통과하지 못하므로 아무 일도 일어나지 않는다.
	if (Kind == EAIWidgetRequestKind::ChangeRequest)
	{
		Prompt += TEXT("\n");
		Prompt += FAIWidgetCommandParser::GetSchemaInstructions();
	}

	// Tool 모드에서는 형식 안내를 붙이지 않는다. 부를 수 있는 함수와 인자 형식은
	// MCP 쪽 스키마가 이미 들고 있고, 여기서 또 적으면 둘이 어긋날 때 모델이 헷갈린다.
	// 대신 어디에 붙어 있고 무엇부터 해야 하는지를 알려 준다.
	if (Kind == EAIWidgetRequestKind::ToolChangeRequest)
	{
		Prompt += TEXT("\n[How to apply]\n");
		Prompt += TEXT("You are connected to the Unreal Editor over MCP and can change the widget above directly.\n");
		Prompt += TEXT("- Use the AIWidgetInspector.AIWidgetInspectorToolset toolset. Call describe_toolset for argument formats.\n");
		Prompt += TEXT("- Make changes with PreviewWidgetChange. It leaves the asset alone and is easy to undo.\n");
		Prompt += TEXT("- Do not write to the asset and do not save. A preview is enough.\n");
		Prompt += TEXT("- Close by telling the user the Save to Asset button in the panel will save it. Do not ask them to tell you to save.\n");
		Prompt += TEXT("- To change a different widget, call ListWidgetTree first for its name. Never invent a name.\n");
		Prompt += TEXT("- Actually call the tools. Do not describe what you would do.\n");
		Prompt += TEXT("- Finish with a sentence or two on what you changed and why.\n");
	}

	return Prompt;
}

FAIWidgetResponse FAIWidgetResponse::MakeSuccess(const FText& InMessage, const FString& InRawResponse)
{
	FAIWidgetResponse Response;
	Response.bSuccess = true;
	Response.Message = InMessage;
	Response.RawResponse = InRawResponse;
	return Response;
}

FAIWidgetResponse FAIWidgetResponse::MakeFailure(const FText& InMessage)
{
	FAIWidgetResponse Response;
	Response.bSuccess = false;
	Response.Message = InMessage;
	return Response;
}
