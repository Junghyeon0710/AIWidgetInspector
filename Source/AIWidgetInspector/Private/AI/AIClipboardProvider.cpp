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
	return LOCTEXT("Description", "Copies the prompt to the clipboard to paste into any assistant.");
}

void FAIClipboardProvider::SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete)
{
	const FString Prompt = InRequest.BuildPrompt();

	if (Prompt.IsEmpty())
	{
		InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(
			LOCTEXT("EmptyPrompt", "There is nothing to send.")));
		return;
	}

	FPlatformApplicationMisc::ClipboardCopy(*Prompt);

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Copied %d chars to the clipboard."), Prompt.Len());

	InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeSuccess(
		FText::Format(
			LOCTEXT("Copied", "Copied {0} chars to the clipboard. Paste it into your assistant.\n\n----\n{1}"),
			FText::AsNumber(Prompt.Len()),
			FText::FromString(Prompt)),
		Prompt));
}

#undef LOCTEXT_NAMESPACE
