// AI Widget Inspector

#include "UI/SAIWidgetTerminal.h"

#include "AI/AICliProvider.h"
#include "AIWidgetInspectorLog.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "STerminal.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAIWidgetTerminal"

namespace AIWidgetTerminalPrivate
{
	/** 프롬프트를 한 줄로 눕힌다. TUI는 줄바꿈을 전송으로 받아 첫 줄만 보내 버린다. */
	static FString FlattenToSingleLine(const FString& InText)
	{
		FString Flattened = InText;
		Flattened.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Flattened.ReplaceInline(TEXT("\r"), TEXT(" "));
		Flattened.ReplaceInline(TEXT("\n"), TEXT(" "));
		Flattened.ReplaceInline(TEXT("\t"), TEXT(" "));

		// 눕히고 나면 공백이 뭉치는데, 그대로 보내면 터미널 한 줄을 쓸데없이 잡아먹는다.
		while (Flattened.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
		{
		}

		return Flattened.TrimStartAndEnd();
	}
}

void SAIWidgetTerminal::Construct(const FArguments& InArgs)
{
	const TSharedRef<SScrollBar> ScrollBar = SNew(SScrollBar)
		.Thickness(8.0f);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SAIWidgetTerminal::GetStatusText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RestartCli", "Restart CLI"))
				.ToolTipText(LOCTEXT("RestartCliTooltip", "Start the CLI again in the same shell. Use this after it exits, or to drop a conversation and begin a new one."))
				.OnClicked(this, &SAIWidgetTerminal::HandleRestartClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(320.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(Terminal, STerminal)
					.ExternalScrollbar(ScrollBar)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ScrollBar
				]
			]
		]
	];

	// 패널을 여는 것만으로 CLI가 뜨게 한다. 물어보려고 버튼을 누른 뒤에야 뜨기 시작하면
	// 첫 질문마다 CLI 부팅을 기다리게 된다.
	StartCli();
}

bool SAIWidgetTerminal::IsSessionRunning() const
{
	return Terminal.IsValid() && Terminal->IsSessionRunning();
}

int32 SAIWidgetTerminal::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	bEverPainted = true;

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SAIWidgetTerminal::StartCli()
{
	if (bCliLaunched)
	{
		return;
	}

	EnsurePumpRunning();
}

void SAIWidgetTerminal::SendPrompt(const FString& InPrompt)
{
	const FString Flattened = AIWidgetTerminalPrivate::FlattenToSingleLine(InPrompt);
	if (Flattened.IsEmpty())
	{
		return;
	}

	// 아직 못 보낸 프롬프트가 있으면 덮어쓴다. 둘 다 보내면 CLI가 첫 답을 쓰는 중에
	// 두 번째가 끼어들어 두 대화가 섞인다.
	PendingPrompt = Flattened;
	EnsurePumpRunning();
}

void SAIWidgetTerminal::EnsurePumpRunning()
{
	if (PumpHandle.IsValid())
	{
		return;
	}

	ShellWaitStartTime = FSlateApplication::Get().GetCurrentTime();
	bShellTimedOut = false;

	PumpHandle = RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SAIWidgetTerminal::OnPump));
}

FString SAIWidgetTerminal::GetSessionIdFilePath()
{
	// Saved 아래에 둔다. 사용자마다 다른 값이고 저장소에 들어갈 것이 아니다.
	return FPaths::ProjectSavedDir() / TEXT("AIWidgetInspector") / TEXT("TerminalSession.txt");
}

FString SAIWidgetTerminal::LoadOrCreateSessionId(bool& bOutIsNew)
{
	const FString FilePath = GetSessionIdFilePath();

	FString StoredId;
	if (FFileHelper::LoadFileToString(StoredId, *FilePath))
	{
		StoredId.TrimStartAndEndInline();

		// 손으로 고쳤거나 반쯤 쓰이다 만 파일을 그대로 넘기면 CLI가 인자를 거부한다.
		FGuid ParsedGuid;
		if (FGuid::Parse(StoredId, ParsedGuid))
		{
			bOutIsNew = false;
			return StoredId;
		}

		UE_LOG(LogAIWidgetInspector, Warning,
			TEXT("The stored terminal session id is not a GUID. Starting a new conversation. (%s)"), *FilePath);
	}

	const FString NewId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	// 적어 두지 못하면 이어받을 방법이 없다. 그래도 이번 실행은 그대로 진행한다.
	if (!FFileHelper::SaveStringToFile(NewId, *FilePath))
	{
		UE_LOG(LogAIWidgetInspector, Warning,
			TEXT("Could not write the terminal session id, so this conversation will not survive a restart. (%s)"), *FilePath);
	}

	bOutIsNew = true;
	return NewId;
}

void SAIWidgetTerminal::LaunchCli(double InCurrentTime)
{
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	// STerminal은 엔진 루트에서 셸을 연다. CLI가 프로젝트를 작업 디렉터리로 보게 옮긴다.
#if PLATFORM_WINDOWS
	Terminal->ExecuteCommand(FString::Printf(TEXT("cd /d \"%s\""), *ProjectDir));
#else
	Terminal->ExecuteCommand(FString::Printf(TEXT("cd \"%s\""), *ProjectDir));
#endif

	FString Command = TEXT("claude");

	// 지난 대화를 이어받는다. 이 터미널은 에디터 안에 살기 때문에, C++을 고쳐 에디터를
	// 다시 켜면 세션이 함께 사라진다. 그런데 UE에서 C++을 고치면 재시작은 늘 있는 일이라,
	// 이어받지 않으면 패널에서 시작한 일을 패널에서 끝낼 수가 없다.
	//
	// 이어받을 대화를 id로 못 박는다. --continue는 "그 폴더의 가장 최근 대화"를 집어오는데,
	// 같은 프로젝트에서 CLI를 따로 띄워 두면 그쪽 대화를 가져와 엉뚱한 맥락에 이어 붙는다.
	if (bStartFresh)
	{
		// 새로 시작하라는 뜻이므로 지난 id는 버린다. 같은 id로 다시 열면 CLI가 거부한다.
		IFileManager::Get().Delete(*GetSessionIdFilePath(), /*RequireExists=*/false);
		bStartFresh = false;
	}

	bool bIsNewSession = false;
	SessionId = LoadOrCreateSessionId(bIsNewSession);
	bResumedConversation = !bIsNewSession;

	// Printf의 포맷은 컴파일 타임에 검사되므로 리터럴이어야 한다. 삼항으로 고를 수 없다.
	if (bResumedConversation)
	{
		Command += FString::Printf(TEXT(" --resume %s"), *SessionId);
	}
	else
	{
		Command += FString::Printf(TEXT(" --session-id %s"), *SessionId);
	}

	// 에디터 MCP가 떠 있으면 물려 준다. 그래야 CLI가 답만 하지 않고 위젯을 직접 고칠 수 있다.
	// --allowedTools로 미리 열어 주지 않는 것이 원샷 Provider와 다른 점이다. 대화형이라
	// 승인을 물을 데가 있고, 무엇을 허락할지는 사용자가 그 자리에서 정하면 된다.
	if (FAICliProvider::IsEditorMcpRunning())
	{
		// 절대 경로로 펴서 넘긴다. WriteMcpConfigFile이 돌려주는 것은 엔진 실행 파일 기준의
		// 상대 경로라, 에디터가 직접 띄우는 원샷 Provider에서는 맞지만 여기서는 아니다.
		// 우리는 셸을 프로젝트로 옮겨 놓고 CLI를 띄우므로 기준점이 다르다.
		const FString McpConfigPath = FAICliProvider::WriteMcpConfigFile(TEXT("unreal"));
		if (!McpConfigPath.IsEmpty())
		{
			Command += FString::Printf(TEXT(" --strict-mcp-config --mcp-config \"%s\""),
				*FPaths::ConvertRelativePathToFull(McpConfigPath));
		}
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Terminal launching the CLI: %s"), *Command);

	Terminal->ExecuteCommand(Command);

	bCliLaunched = true;
	CliLaunchTime = InCurrentTime;
}

bool SAIWidgetTerminal::IsTerminalQuiet(double InCurrentTime) const
{
	// 터미널이 그리기를 멈췄다는 것은 셸이든 CLI든 입력을 기다린다는 뜻이다. 그리는 중에
	// 밀어 넣으면 프롬프트가 뜨기 전이라 흘려버리거나, TUI가 다시 그리면서 지워 버린다.
	const double LastOutputTime = Terminal->GetLastOutputTime();

	return LastOutputTime > 0.0 && (InCurrentTime - LastOutputTime) >= QuietSecondsBeforeSend;
}

EActiveTimerReturnType SAIWidgetTerminal::OnPump(double InCurrentTime, float InDeltaTime)
{
	if (!Terminal.IsValid())
	{
		PumpHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}

	// STerminal은 첫 페인트에서 지오메트리를 보고 PTY를 만든다. 그래서 Construct 직후에는
	// 아직 셸이 없다.
	if (!Terminal->IsSessionRunning())
	{
		// 한 번도 그려지지 않았으면 셸이 없는 게 정상이다. 섹션이 접혀 있거나 스크롤 밖에
		// 있는 것뿐이니, 기다린 시간으로 세지 않고 기준점을 미룬다.
		if (!bEverPainted)
		{
			ShellWaitStartTime = InCurrentTime;
			return EActiveTimerReturnType::Continue;
		}

		if (InCurrentTime - ShellWaitStartTime >= MaxWaitForShellSeconds)
		{
			UE_LOG(LogAIWidgetInspector, Warning,
				TEXT("The terminal shell did not start within %.0f seconds."), MaxWaitForShellSeconds);

			bShellTimedOut = true;
			PendingPrompt.Reset();
			PumpHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		return EActiveTimerReturnType::Continue;
	}

	if (!bCliLaunched)
	{
		// 셸 프롬프트가 다 나오기를 기다린다. 재시작이면 앞선 CLI가 물러나기를 기다리는
		// 것이기도 하다.
		if (!IsTerminalQuiet(InCurrentTime) && (InCurrentTime - ShellWaitStartTime) < MaxWaitForShellSeconds)
		{
			return EActiveTimerReturnType::Continue;
		}

		LaunchCli(InCurrentTime);
		return EActiveTimerReturnType::Continue;
	}

	// 본문은 이미 들어갔고 Enter만 남았다. 빈 명령을 보내면 CR 하나만 나간다.
	if (bAwaitingSubmit)
	{
		if (InCurrentTime - PromptTypedTime < SubmitDelaySeconds)
		{
			return EActiveTimerReturnType::Continue;
		}

		Terminal->ExecuteCommand(FString());
		bAwaitingSubmit = false;

		PumpHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}

	if (PendingPrompt.IsEmpty())
	{
		PumpHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}

	// CLI가 뜨는 동안에는 화면이 계속 갱신된다. 멎으면 입력을 기다리는 상태로 본다.
	const bool bWaitedTooLong = (InCurrentTime - CliLaunchTime) >= MaxWaitForCliSeconds;
	if (!IsTerminalQuiet(InCurrentTime) && !bWaitedTooLong)
	{
		return EActiveTimerReturnType::Continue;
	}

	if (bWaitedTooLong && !IsTerminalQuiet(InCurrentTime))
	{
		// 보내기는 하되, 답이 이상하면 왜 그런지 짚을 수 있게 남긴다.
		UE_LOG(LogAIWidgetInspector, Warning,
			TEXT("The CLI kept drawing for %.0f seconds. Sending the prompt anyway."), MaxWaitForCliSeconds);
	}

	Terminal->ExecuteCommand(PendingPrompt);
	PendingPrompt.Reset();

	bAwaitingSubmit = true;
	PromptTypedTime = InCurrentTime;

	return EActiveTimerReturnType::Continue;
}

FReply SAIWidgetTerminal::HandleRestartClicked()
{
	if (!Terminal.IsValid())
	{
		return FReply::Handled();
	}

	// CLI가 돌고 있으면 물러나게 한다. /exit는 claude가 정리하고 나가는 길이라, 셸이
	// 그대로 남아 다음 실행을 받는다. 이미 셸에 있었다면 알 수 없는 명령이라는 한 줄이
	// 찍힐 뿐이고, 어느 쪽이든 그 다음은 조용해진 뒤에 다시 띄운다.
	Terminal->ExecuteCommand(TEXT("/exit"));

	bCliLaunched = false;
	bStartFresh = true;
	bAwaitingSubmit = false;
	PendingPrompt.Reset();
	ShellWaitStartTime = FSlateApplication::Get().GetCurrentTime();
	bShellTimedOut = false;
	EnsurePumpRunning();

	return FReply::Handled();
}

FText SAIWidgetTerminal::GetStatusText() const
{
	if (bShellTimedOut)
	{
		return LOCTEXT("ShellFailed", "The terminal shell did not start. Check the output log.");
	}

	if (!IsSessionRunning())
	{
		return LOCTEXT("StartingShell", "Starting the terminal...");
	}

	if (!bCliLaunched)
	{
		return LOCTEXT("StartingCli", "Starting the CLI...");
	}

	if (!PendingPrompt.IsEmpty() || bAwaitingSubmit)
	{
		return LOCTEXT("WaitingForCli", "Waiting for the CLI to be ready, then sending your question...");
	}

	FString ExecutablePath;
	if (!FAICliProvider::FindExecutable(TEXT("claude"), ExecutablePath))
	{
		return LOCTEXT("CliMissing", "'claude' was not found on PATH. Install it, then press Restart CLI.");
	}

	if (bResumedConversation)
	{
		return LOCTEXT("CliReadyResumed",
			"Picked up the previous conversation. Type here as in any terminal, and answer permission prompts with Enter.");
	}

	return LOCTEXT("CliReady", "Type here as you would in any terminal. Answer permission prompts with Enter.");
}

#undef LOCTEXT_NAMESPACE
