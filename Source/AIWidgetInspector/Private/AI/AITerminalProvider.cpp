// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "AI/AITerminalProvider.h"

#include "AI/AICliEnvironment.h"

#define LOCTEXT_NAMESPACE "FAITerminalProvider"

const TCHAR* FAITerminalProvider::GetExecutable(EAITerminalCli InCli)
{
	return (InCli == EAITerminalCli::Codex) ? TEXT("codex") : TEXT("claude");
}

const TCHAR* FAITerminalProvider::GetInstallCommand(EAITerminalCli InCli)
{
	return (InCli == EAITerminalCli::Codex)
		? TEXT("npm install -g @openai/codex")
		: TEXT("npm install -g @anthropic-ai/claude-code");
}

FName FAITerminalProvider::GetProviderName() const
{
	return (Cli == EAITerminalCli::Codex) ? TEXT("CodexTerminal") : TEXT("ClaudeTerminal");
}

FText FAITerminalProvider::GetDisplayName() const
{
	return (Cli == EAITerminalCli::Codex)
		? LOCTEXT("DisplayNameCodex", "Codex (Terminal)")
		: LOCTEXT("DisplayNameClaude", "Claude Code (Terminal)");
}

FText FAITerminalProvider::GetDescription() const
{
	const FText ExecutableName = FText::FromString(GetExecutable(Cli));

	FString ExecutablePath;
	if (FindExecutablePath(ExecutablePath))
	{
		return FText::Format(
			LOCTEXT("DescriptionFound", "Runs {0} in the CLI Session below, so you can answer its questions.  ({1})"),
			ExecutableName,
			FText::FromString(ExecutablePath));
	}

	return FText::Format(
		LOCTEXT("DescriptionMissing", "Runs {0} in the CLI Session below.  '{0}' was not found on PATH."),
		ExecutableName);
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

	return FText::Format(
		LOCTEXT("MissingCli", "{0} is not installed.  Run  {1}  in a terminal, then press Restart CLI below."),
		FText::FromString(GetExecutable(Cli)),
		FText::FromString(GetInstallCommand(Cli)));
}

void FAITerminalProvider::SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete)
{
	// 패널은 IsInteractive()를 보고 터미널로 직접 보내므로 여기까지 오지 않는다.
	// 그래도 조용히 아무 일도 일어나지 않는 것보다는 이유가 보이는 편이 낫다.
	InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(LOCTEXT(
		"NotRouted",
		"This provider talks to the CLI Session in the panel. Open the CLI Session section and try again.")));
}

bool FAITerminalProvider::FindExecutablePath(FString& OutPath) const
{
	return AIWidgetInspector::CliEnvironment::FindExecutable(GetExecutable(Cli), OutPath);
}

#undef LOCTEXT_NAMESPACE
