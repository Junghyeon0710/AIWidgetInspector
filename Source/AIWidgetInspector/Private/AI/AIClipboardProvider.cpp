// AI Widget Inspector

#include "AI/AIClipboardProvider.h"

#include "AIWidgetInspectorLog.h"

#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "FAIClipboardProvider"

const FName FAIClipboardProvider::ProviderName(TEXT("Clipboard"));

FText FAIClipboardProvider::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Clipboard");
}

FText FAIClipboardProvider::GetDescription() const
{
	return LOCTEXT("Description", "프롬프트를 클립보드에 복사한다. Claude나 Codex 창에 그대로 붙여넣으면 된다.");
}

void FAIClipboardProvider::SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete)
{
	const FString Prompt = InRequest.BuildPrompt();

	if (Prompt.IsEmpty())
	{
		InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(
			LOCTEXT("EmptyPrompt", "보낼 내용이 없습니다.")));
		return;
	}

	FPlatformApplicationMisc::ClipboardCopy(*Prompt);

	UE_LOG(LogAIWidgetInspector, Log, TEXT("프롬프트 %d자를 클립보드에 복사했습니다."), Prompt.Len());

	InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeSuccess(
		FText::Format(
			LOCTEXT("Copied", "프롬프트 {0}자를 클립보드에 복사했습니다. Claude 또는 Codex에 붙여넣으세요.\n\n----\n{1}"),
			FText::AsNumber(Prompt.Len()),
			FText::FromString(Prompt)),
		Prompt));
}

#undef LOCTEXT_NAMESPACE
