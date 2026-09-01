// AI Widget Inspector

#include "UI/SAIWidgetTerminal.h"

#include "AI/AICliEnvironment.h"
#include "AI/AITerminalCommand.h"
#include "AI/AITerminalProvider.h"
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
	TerminalScrollBar = SNew(SScrollBar)
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
				.ColorAndOpacity(this, &SAIWidgetTerminal::GetStatusColor)
				.AutoWrapText(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RestartCli", "Restart CLI"))
				.ToolTipText(LOCTEXT("RestartCliTooltip", "Throw away this terminal and start a fresh CLI. Use it after the CLI exits, or to drop the conversation and begin a new one."))
				.OnClicked(this, &SAIWidgetTerminal::HandleRestartClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			// 이제 일은 대부분 여기서 벌어진다. 답과 승인 요청이 모두 이 안을 지나가므로
			// 다른 섹션만큼만 주면 계속 스크롤하며 읽게 된다.
			.HeightOverride(420.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(TerminalHost, SBox)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					TerminalScrollBar.ToSharedRef()
				]
			]
		]
	];

	BuildTerminal();

	// 패널을 여는 것만으로 CLI가 뜨게 한다. 물어보려고 버튼을 누른 뒤에야 뜨기 시작하면
	// 첫 질문마다 CLI 부팅을 기다리게 된다.
	StartCli();
}

void SAIWidgetTerminal::BuildTerminal()
{
	TerminalHost->SetContent(
		SAssignNew(Terminal, STerminal)
		.ExternalScrollbar(TerminalScrollBar));

	// 새로 만든 위젯은 아직 그려진 적이 없다. PTY는 첫 페인트에서 생기므로 여기서 다시 센다.
	// 없어서 못 띄웠던 CLI를 설치하고 Restart를 누르는 흐름이 있다. 그때 다시 찾는다.
	bCliOnPath.Reset();
	bCliSettled = false;

	bEverPainted = false;
	bCliLaunched = false;
	bCliExited = false;
	bAwaitingSubmit = false;
	bShellTimedOut = false;
	ShellWaitStartTime = FSlateApplication::Get().GetCurrentTime();
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

void SAIWidgetTerminal::SetCli(EAITerminalCli InCli)
{
	if (Cli == InCli)
	{
		return;
	}

	// 다른 CLI로 바꾸라는 것은 지금 떠 있는 것을 끝내라는 뜻이다. 죽은 세션은 되살릴 수
	// 없고 살아 있는 것도 안에서 무엇을 하는지 알 수 없으니, 위젯째 갈아 끼운다.
	Cli = InCli;
	PendingPrompt.Reset();

	BuildTerminal();
	StartCli();
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

FString SAIWidgetTerminal::GetSessionIdFilePath() const
{
	// CLI마다 따로 둔다. 대화가 저장되는 곳이 다르니 한 파일에 섞으면 서로의 id를 물려받는다.
	// Saved 아래에 두는 것은 사용자마다 다른 값이고 저장소에 들어갈 것이 아니기 때문이다.
	return FPaths::ProjectSavedDir() / TEXT("AIWidgetInspector")
		/ FString::Printf(TEXT("TerminalSession-%s.txt"), FAITerminalProvider::GetExecutable(Cli));
}

FString SAIWidgetTerminal::LoadOrCreateSessionId(bool& bOutIsNew) const
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
	using namespace AIWidgetInspector;

	const bool bWindowsShell = PLATFORM_WINDOWS != 0;

	// STerminal은 엔진 루트에서 셸을 연다. CLI가 프로젝트를 작업 디렉터리로 보게 옮긴다.
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Terminal->ExecuteCommand(TerminalCommand::BuildChangeDirectoryCommand(ProjectDir, bWindowsShell));

	TerminalCommand::FLaunch Launch;
	Launch.Cli = Cli;
	Launch.bWindowsShell = bWindowsShell;

	// 에디터 MCP가 떠 있으면 물려 준다. 그래야 CLI가 답만 하지 않고 위젯을 직접 고칠 수 있다.
	// 무엇을 허락할지는 미리 좁히지 않는다. 대화형이라 승인을 물을 자리가 있고, 그건
	// 사용자가 그 자리에서 정하면 된다.
	if (CliEnvironment::IsEditorMcpRunning())
	{
		if (Cli == EAITerminalCli::Codex)
		{
			Launch.McpUrl = CliEnvironment::GetEditorMcpUrl();
		}
		else
		{
			// 절대 경로로 펴서 넘긴다. WriteMcpConfigFile이 돌려주는 것은 엔진 실행 파일
			// 기준의 상대 경로인데, 우리는 셸을 프로젝트로 옮겨 놓고 CLI를 띄운다.
			const FString McpConfigPath = CliEnvironment::WriteMcpConfigFile(TEXT("unreal"));
			if (!McpConfigPath.IsEmpty())
			{
				Launch.McpConfigPath = FPaths::ConvertRelativePathToFull(McpConfigPath);
			}
		}
	}

	// 지난 대화를 이어받는다. 이 터미널은 에디터 안에 살기 때문에, C++을 고쳐 에디터를
	// 다시 켜면 세션이 함께 사라진다. 그런데 UE에서 C++을 고치면 재시작은 늘 있는 일이라,
	// 이어받지 않으면 패널에서 시작한 일을 패널에서 끝낼 수가 없다.
	if (bStartFresh)
	{
		// 새로 시작하라는 뜻이므로 지난 기록은 버린다.
		IFileManager::Get().Delete(*GetSessionIdFilePath(), /*RequireExists=*/false);
		bStartFresh = false;
	}

	// codex는 id를 쓰지 않지만 파일은 똑같이 남긴다. 여기서 필요한 것은 "전에 띄운 적이
	// 있는가"이고, 그건 파일이 있느냐로 알 수 있다.
	bool bIsNewSession = false;
	SessionId = LoadOrCreateSessionId(bIsNewSession);
	bResumedConversation = !bIsNewSession;

	Launch.SessionId = SessionId;
	Launch.bResume = bResumedConversation;

	const FString Command = TerminalCommand::BuildLaunchCommand(Launch);

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Terminal launching the CLI: %s"), *Command);

	Terminal->ExecuteCommand(Command);

	bCliLaunched = true;
	bCliSettled = false;
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

	// CLI와 셸의 수명을 묶어 두었으므로, 한 번 띄운 뒤에 세션이 없다는 것은 CLI가 나갔다는
	// 뜻이다. 아래의 "셸이 아직 안 떴다"와는 다른 상황이라 먼저 갈라 놓는다.
	//
	// 남은 프롬프트는 버린다. 보낼 곳이 없는데 들고 있으면, 다음에 무엇이 열리든 그리로
	// 들어간다.
	if (bCliLaunched && !Terminal->IsSessionRunning())
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("The CLI exited, so the terminal session ended."));

		bCliExited = true;
		PendingPrompt.Reset();
		bAwaitingSubmit = false;
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
	if (!TerminalHost.IsValid())
	{
		return FReply::Handled();
	}

	// 죽은 세션은 되살아나지 않고, 살아 있어도 안에서 CLI가 무엇을 하고 있는지 알 수 없다.
	// 어느 쪽이든 새 위젯으로 갈아 끼우는 편이 확실하다. 옛 위젯이 사라지면서 그 세션도
	// 함께 정리된다.
	bStartFresh = true;
	PendingPrompt.Reset();

	BuildTerminal();
	EnsurePumpRunning();

	return FReply::Handled();
}

bool SAIWidgetTerminal::IsCliOnPath() const
{
	if (!bCliOnPath.IsSet())
	{
		FString ExecutablePath;
		bCliOnPath = AIWidgetInspector::CliEnvironment::FindExecutable(FAITerminalProvider::GetExecutable(Cli), ExecutablePath);
	}

	return bCliOnPath.GetValue();
}

SAIWidgetTerminal::FStatus SAIWidgetTerminal::GetStatus() const
{
	const FText CliName = FText::FromString(FAITerminalProvider::GetExecutable(Cli));

	// 색은 세 가지만 쓴다. 손을 써야 하는 것, 기다리면 되는 것, 잘 돌고 있는 것.
	const FSlateColor Problem(FLinearColor(1.0f, 0.45f, 0.35f));
	const FSlateColor Waiting = FSlateColor::UseSubduedForeground();
	const FSlateColor Running(FLinearColor(0.45f, 0.85f, 0.5f));
	const FSlateColor Caution(FLinearColor(1.0f, 0.78f, 0.35f));

	// 설치돼 있지 않다는 것부터 말한다. 이건 기다려도 해결되지 않고, 무엇을 해야 하는지도
	// 분명하다. 아래의 "시작하는 중"이 먼저 뜨면 영영 안 뜨는 것처럼 보인다.
	if (!IsCliOnPath())
	{
		return { FText::Format(
			LOCTEXT("CliMissing", "{0} was not found on PATH.  Install it, then press Restart CLI."),
			CliName), Problem };
	}

	if (bCliExited)
	{
		return { FText::Format(
			LOCTEXT("CliExited", "{0} exited, so this terminal is finished.  Press Restart CLI to start another one."),
			CliName), Problem };
	}

	if (bShellTimedOut)
	{
		return { LOCTEXT("ShellFailed", "The terminal did not start.  Check the output log."), Problem };
	}

	if (!IsSessionRunning())
	{
		return { LOCTEXT("StartingShell", "Starting the terminal..."), Waiting };
	}

	if (!bCliLaunched)
	{
		return { FText::Format(LOCTEXT("StartingCli", "Starting {0}..."), CliName), Waiting };
	}

	if (!PendingPrompt.IsEmpty() || bAwaitingSubmit)
	{
		return { FText::Format(
			LOCTEXT("WaitingForCli", "Waiting for {0} to be ready, then sending your question..."),
			CliName), Waiting };
	}

	// 명령을 넣은 뒤 화면이 한 번 멎어야 뜬 것으로 본다. 뜨는 동안에는 계속 다시 그린다.
	if (!bCliSettled)
	{
		if (!IsTerminalQuiet(FSlateApplication::Get().GetCurrentTime()))
		{
			return { FText::Format(LOCTEXT("StartingCli", "Starting {0}..."), CliName), Waiting };
		}

		bCliSettled = true;
	}

	// 여기까지 왔으면 돌고 있다. 남은 것은 무엇을 할 수 있는 상태인지다.
	//
	// 에디터 MCP가 붙었는지 여기서 말해 준다. 이게 없으면 CLI는 파일만 고칠 수 있고
	// 살아 있는 에디터의 위젯은 건드리지 못하는데, 화면만 봐서는 둘을 구분할 수 없다.
	// MCP가 없어도 CLI는 돈다. 다만 이 패널을 쓰는 이유가 대개 "고른 위젯을 고쳐 줘"인데,
	// 그건 못 하는 상태다. 초록으로 두면 다 잘 되고 있다고 읽힌다.
	const bool bMcpAttached = AIWidgetInspector::CliEnvironment::IsEditorMcpRunning();
	const FText McpNote = bMcpAttached
		? LOCTEXT("McpOn", "Unreal MCP is attached, so it can change the widget in the running editor.")
		: LOCTEXT("McpOff", "Unreal MCP is off, so it can only read and write files.  Turn on Auto Start Server under Project Settings > Plugins > Model Context Protocol, then restart the editor.");

	const FSlateColor ReadyColor = bMcpAttached ? Running : Caution;

	if (bResumedConversation)
	{
		return { FText::Format(
			LOCTEXT("CliReadyResumed", "{0} is running and picked up the previous conversation.  {1}"),
			CliName, McpNote), ReadyColor };
	}

	return { FText::Format(LOCTEXT("CliReady", "{0} is running.  {1}"), CliName, McpNote), ReadyColor };
}

FText SAIWidgetTerminal::GetStatusText() const
{
	return GetStatus().Text;
}

FSlateColor SAIWidgetTerminal::GetStatusColor() const
{
	return GetStatus().Color;
}

#undef LOCTEXT_NAMESPACE
