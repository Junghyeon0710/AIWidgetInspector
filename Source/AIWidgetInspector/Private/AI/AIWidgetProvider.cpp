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
		Prompt += TEXT("너는 Unreal 에디터에 MCP로 연결돼 있다. 위 Widget을 직접 고칠 수 있다.\n");
		Prompt += TEXT("- AIWidgetInspector.AIWidgetInspectorToolset 툴세트를 쓴다. describe_toolset으로 인자 형식을 확인해라.\n");
		Prompt += TEXT("- 눈으로 봐야 하는 변경은 PreviewWidgetChange로 한다. 에셋은 그대로 두고 되돌리기 쉽다.\n");
		Prompt += TEXT("- 에셋에 쓰거나 저장하지 마라. 미리보기까지만 하면 된다.\n");
		Prompt += TEXT("- 마무리 말에 패널의 Save to Asset 버튼을 누르면 저장된다고 알려 줘라. 저장해 달라고 말해 달라고 요구하지 마라.\n");
		Prompt += TEXT("- 다른 Widget을 바꿔야 하면 ListWidgetTree로 이름을 먼저 확인해라. 이름을 지어내지 마라.\n");
		Prompt += TEXT("- 실제로 툴을 호출해라. 무엇을 하겠다는 설명만 남기지 마라.\n");
		Prompt += TEXT("- 다 끝나면 무엇을 왜 그렇게 바꿨는지 한두 문장으로 알려 줘라.\n");
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
