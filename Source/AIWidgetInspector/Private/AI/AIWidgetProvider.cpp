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
