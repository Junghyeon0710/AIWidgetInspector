// AI Widget Inspector

#include "AI/AITerminalProvider.h"

#include "AI/AICliProvider.h"

#define LOCTEXT_NAMESPACE "FAITerminalProvider"

FText FAITerminalProvider::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Claude Code (Terminal)");
}

FText FAITerminalProvider::GetDescription() const
{
	FString ExecutablePath;
	if (FindExecutablePath(ExecutablePath))
	{
		return FText::Format(
			LOCTEXT("DescriptionFound", "Runs claude in the CLI Session below, so you can answer its questions.  ({0})"),
			FText::FromString(ExecutablePath));
	}

	return LOCTEXT("DescriptionMissing", "Runs claude in the CLI Session below.  'claude' was not found on PATH.");
}

bool FAITerminalProvider::IsAvailable() const
{
	FString ExecutablePath;
	return FindExecutablePath(ExecutablePath);
}

FText FAITerminalProvider::GetUnavailableReason() const
{
	if (IsAvailable())
	{
		return FText::GetEmpty();
	}

	return LOCTEXT("MissingCli",
		"claude is not installed.  Run  npm install -g @anthropic-ai/claude-code  in a terminal, then press Restart CLI below.");
}

void FAITerminalProvider::SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete)
{
	// 패널은 IsInteractive()를 보고 터미널로 직접 보내므로 여기까지 오지 않는다.
	// 그래도 조용히 아무 일도 일어나지 않는 것보다는 이유가 보이는 편이 낫다.
	InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(LOCTEXT(
		"NotRouted",
		"This provider talks to the CLI Session in the panel. Open the CLI Session section and try again.")));
}

bool FAITerminalProvider::FindExecutablePath(FString& OutPath)
{
	return FAICliProvider::FindExecutable(TEXT("claude"), OutPath);
}

#undef LOCTEXT_NAMESPACE
