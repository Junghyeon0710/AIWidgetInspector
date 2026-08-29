// AI Widget Inspector

#include "AI/AIWidgetProvider.h"

#include "Commands/AIWidgetCommandParser.h"

FString FAIWidgetRequest::BuildPrompt() const
{
	const TCHAR* SectionHeader = (Kind == EAIWidgetRequestKind::ChangeRequest)
		? TEXT("[Requested Change]")
		: TEXT("[User Question]");

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
