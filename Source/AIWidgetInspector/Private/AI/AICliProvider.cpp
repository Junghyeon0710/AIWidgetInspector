// AI Widget Inspector

#include "AI/AICliProvider.h"

#include "AIWidgetInspectorLog.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ModelContextProtocolSettings.h"

#define LOCTEXT_NAMESPACE "FAICliProvider"

namespace AICliProviderPrivate
{
	/** 실행 파일 후보 확장자. npm으로 깔린 CLI는 Windows에서 .cmd 형태다. */
	static const TCHAR* ExecutableExtensions[] =
	{
#if PLATFORM_WINDOWS
		TEXT(".exe"), TEXT(".cmd"), TEXT(".bat"), TEXT(""),
#else
		TEXT(""),
#endif
	};

	/** CreateProcess는 .cmd / .bat을 직접 실행하지 못한다. cmd.exe를 거쳐야 한다. */
	static bool NeedsCommandShell(const FString& InPath)
	{
		return InPath.EndsWith(TEXT(".cmd"), ESearchCase::IgnoreCase)
			|| InPath.EndsWith(TEXT(".bat"), ESearchCase::IgnoreCase);
	}
}

FAICliProvider::FAICliProvider(FConfig InConfig)
	: Config(MoveTemp(InConfig))
{
}

FText FAICliProvider::GetDescription() const
{
	FString FoundPath;
	if (FindExecutable(Config.Executable, FoundPath))
	{
		return FText::Format(
			LOCTEXT("DescriptionFound", "{0}  ({1})"),
			Config.Description,
			FText::FromString(FoundPath));
	}

	return FText::Format(
		LOCTEXT("DescriptionMissing", "{0}  -  '{1}' was not found on PATH."),
		Config.Description,
		FText::FromString(Config.Executable));
}

bool FAICliProvider::FindExecutable(const FString& InExecutableName, FString& OutPath)
{
	using namespace AICliProviderPrivate;

	if (InExecutableName.IsEmpty())
	{
		return false;
	}

	const FString PathVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	if (PathVariable.IsEmpty())
	{
		return false;
	}

	TArray<FString> PathEntries;
#if PLATFORM_WINDOWS
	PathVariable.ParseIntoArray(PathEntries, TEXT(";"), true);
#else
	PathVariable.ParseIntoArray(PathEntries, TEXT(":"), true);
#endif

	for (const FString& PathEntry : PathEntries)
	{
		const FString TrimmedEntry = PathEntry.TrimStartAndEnd();
		if (TrimmedEntry.IsEmpty())
		{
			continue;
		}

		for (const TCHAR* Extension : ExecutableExtensions)
		{
			const FString Candidate = TrimmedEntry / (InExecutableName + Extension);
			if (FPaths::FileExists(Candidate))
			{
				OutPath = Candidate;
				FPaths::NormalizeFilename(OutPath);
				return true;
			}
		}
	}

	return false;
}

FString FAICliProvider::GetEditorMcpUrl()
{
	// 포트와 경로는 프로젝트별 설정이라 하드코딩하면 사용자가 바꿔 놓았을 때 조용히 어긋난다.
	return FString::Printf(TEXT("http://127.0.0.1:%u%s"),
		UE::ModelContextProtocol::GetServerPortNumber(),
		*UE::ModelContextProtocol::GetServerUrlPath());
}

bool FAICliProvider::IsEditorMcpRunning()
{
	return UE::ModelContextProtocol::ShouldAutoStartServer();
}

FString FAICliProvider::WriteMcpConfigFile(const FString& InServerName)
{
	const FString ConfigPath = FPaths::ProjectIntermediateDir() / TEXT("AIWidgetInspector") / TEXT("McpConfig.json");

	const FString Contents = FString::Printf(
		TEXT("{\"mcpServers\":{\"%s\":{\"type\":\"http\",\"url\":\"%s\"}}}"),
		*InServerName,
		*GetEditorMcpUrl());

	if (!FFileHelper::SaveStringToFile(Contents, *ConfigPath))
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("Could not write the MCP config file: %s"), *ConfigPath);
		return FString();
	}

	return ConfigPath;
}

bool FAICliProvider::IsAvailable() const
{
	FString FoundPath;
	if (!FindExecutable(Config.Executable, FoundPath))
	{
		return false;
	}

	// MCP 모드는 에디터 서버가 떠 있어야 성립한다. 실행 파일만 보고 쓸 수 있다고 하면
	// 사용자는 보낼 수 있는 줄 알고 눌렀다가 180초 타임아웃을 기다리게 된다.
	if (Config.bUseUnrealMcp && !IsEditorMcpRunning())
	{
		return false;
	}

	return true;
}

FText FAICliProvider::GetUnavailableReason() const
{
	FString FoundPath;
	if (!FindExecutable(Config.Executable, FoundPath))
	{
		if (!Config.InstallCommand.IsEmpty())
		{
			return FText::Format(
				LOCTEXT("MissingCliWithHint", "{0} is not installed.  Run  {1}  in a terminal, then restart the editor."),
				FText::FromString(Config.Executable),
				FText::FromString(Config.InstallCommand));
		}

		return FText::Format(
			LOCTEXT("MissingCli", "{0} was not found on PATH. Install it, then restart the editor."),
			FText::FromString(Config.Executable));
	}

	if (Config.bUseUnrealMcp && !IsEditorMcpRunning())
	{
		return LOCTEXT("McpOff",
			"The editor's MCP server is off, so the assistant cannot change widgets directly.  "
			"Turn on Auto Start Server under Edit > Project Settings > Plugins > Model Context Protocol, then restart the editor.  "
			"Until then, switch to the 'Claude Code' provider to keep working by applying the assistant's JSON reply.");
	}

	return FText::GetEmpty();
}

bool FAICliProvider::RunProcess(
	const FString& InExecutablePath,
	const FString& InArguments,
	const FString& InStdIn,
	double InTimeoutSeconds,
	FString& OutStdOut,
	int32& OutReturnCode,
	FText& OutError)
{
	using namespace AICliProviderPrivate;

	// stdout: 자식이 쓰고 우리가 읽는다.
	void* StdOutRead = nullptr;
	void* StdOutWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(StdOutRead, StdOutWrite))
	{
		OutError = LOCTEXT("NoStdOutPipe", "Could not create the output pipe.");
		return false;
	}

	// stdin: 우리가 쓰고 자식이 읽는다. 쓰기 쪽이 우리 것이므로 bWritePipeLocal.
	void* StdInRead = nullptr;
	void* StdInWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(StdInRead, StdInWrite, /*bWritePipeLocal=*/true))
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
		OutError = LOCTEXT("NoStdInPipe", "Could not create the input pipe.");
		return false;
	}

	FString Url = InExecutablePath;
	FString Params = InArguments;
	if (NeedsCommandShell(InExecutablePath))
	{
		Params = FString::Printf(TEXT("/c \"\"%s\" %s\""), *InExecutablePath, *InArguments);
		Url = TEXT("cmd.exe");
	}

	uint32 ProcessId = 0;
	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(
		*Url,
		*Params,
		/*bLaunchDetached=*/false,
		/*bLaunchHidden=*/true,
		/*bLaunchReallyHidden=*/true,
		&ProcessId,
		/*PriorityModifier=*/0,
		/*OptionalWorkingDirectory=*/nullptr,
		StdOutWrite,
		StdInRead);

	if (!ProcessHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
		FPlatformProcess::ClosePipe(StdInRead, StdInWrite);
		OutError = FText::Format(
			LOCTEXT("LaunchFailed", "Could not launch '{0}'."),
			FText::FromString(InExecutablePath));
		return false;
	}

	// 프롬프트를 넘긴 뒤 쓰기 쪽을 닫는다. 닫지 않으면 CLI가 입력이 더 올 줄 알고 기다린다.
	FPlatformProcess::WritePipe(StdInWrite, InStdIn);
	FPlatformProcess::ClosePipe(nullptr, StdInWrite);
	StdInWrite = nullptr;

	const double StartTime = FPlatformTime::Seconds();
	bool bTimedOut = false;

	while (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		OutStdOut += FPlatformProcess::ReadPipe(StdOutRead);

		if (FPlatformTime::Seconds() - StartTime > InTimeoutSeconds)
		{
			bTimedOut = true;
			FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree=*/true);
			break;
		}

		FPlatformProcess::Sleep(0.05f);
	}

	// 종료 직전에 쏟아진 출력이 남아 있을 수 있다.
	OutStdOut += FPlatformProcess::ReadPipe(StdOutRead);

	FPlatformProcess::GetProcReturnCode(ProcessHandle, &OutReturnCode);
	FPlatformProcess::CloseProc(ProcessHandle);

	FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
	FPlatformProcess::ClosePipe(StdInRead, nullptr);

	if (bTimedOut)
	{
		OutError = FText::Format(
			LOCTEXT("TimedOut", "Gave up after {0} seconds with no reply."),
			FText::AsNumber(static_cast<int32>(InTimeoutSeconds)));
		return false;
	}

	return true;
}

void FAICliProvider::SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete)
{
	FString ExecutablePath;
	if (!FindExecutable(Config.Executable, ExecutablePath))
	{
		InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(FText::Format(
			LOCTEXT("NotInstalled", "'{0}' was not found on PATH. Install it and restart the editor."),
			FText::FromString(Config.Executable))));
		return;
	}

	const FString Prompt = InRequest.BuildPrompt();

	TArray<FString> ArgumentList = Config.Arguments;

	// Tool 모드면 에디터 MCP를 물려 준다. --strict-mcp-config를 함께 주는 이유는
	// 사용자의 다른 MCP 서버가 딸려 들어오면 이 요청과 상관없는 도구가 붙기 때문이다.
	if (Config.bUseUnrealMcp)
	{
		if (!IsEditorMcpRunning())
		{
			InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(LOCTEXT(
				"McpNotRunning",
				"The editor's MCP server is off. Turn on Auto Start Server under Project Settings > Model Context Protocol and restart the editor.")));
			return;
		}

		const FString ConfigPath = WriteMcpConfigFile(Config.McpServerName);
		if (ConfigPath.IsEmpty())
		{
			InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(LOCTEXT(
				"McpConfigFailed", "Could not create the MCP config file.")));
			return;
		}

		ArgumentList.Add(TEXT("--strict-mcp-config"));
		ArgumentList.Add(TEXT("--mcp-config"));
		ArgumentList.Add(FString::Printf(TEXT("\"%s\""), *ConfigPath));

		// 미리 허용해 두지 않으면 -p 모드에서 승인을 물을 데가 없어 그냥 멈춘다.
		// 열어 주는 것은 에디터 MCP의 세 진입점뿐이고, 그 안에서 부를 수 있는 것은
		// 우리 툴세트가 내놓은 다섯 개다.
		//
		// 그래서 이 Provider는 코드를 읽지도 쓰지도 못한다. C++ 클래스를 만들어 달라거나
		// 버그를 고쳐 달라고 하면 무엇을 못 하는지 설명하는 답이 돌아온다. 게다가 원샷이라
		// 에디터 프로세스의 작업 디렉터리를 물려받아, 파일을 열 수 있었더라도 프로젝트가
		// 아니라 엔진 Binaries 폴더에서 찾는다.
		//
		// 그런 일은 Terminal Provider가 맡는다. 대화형이라 승인을 물을 자리가 있어서
		// 도구를 좁힐 이유가 없고, 셸을 프로젝트로 옮겨 놓고 CLI를 띄운다.
		ArgumentList.Add(TEXT("--allowedTools"));
		ArgumentList.Add(FString::Printf(
			TEXT("mcp__%s__list_toolsets,mcp__%s__describe_toolset,mcp__%s__call_tool"),
			*Config.McpServerName, *Config.McpServerName, *Config.McpServerName));
	}

	const FString Arguments = FString::Join(ArgumentList, TEXT(" "));
	const FString ProviderName = Config.DisplayName.ToString();

	UE_LOG(LogAIWidgetInspector, Log, TEXT("%s launching: %s %s (prompt %d chars)"),
		*ProviderName, *ExecutablePath, *Arguments, Prompt.Len());

	// 여기서 기다리면 에디터가 멈춘다. 프로세스는 백그라운드에서 돌리고 결과만 게임 스레드로 되돌린다.
	Async(EAsyncExecution::ThreadPool,
		[ExecutablePath, Arguments, Prompt, ProviderName, InOnComplete]()
		{
			FString StdOut;
			int32 ReturnCode = -1;
			FText Error;
			const bool bRan = RunProcess(ExecutablePath, Arguments, Prompt, DefaultTimeoutSeconds, StdOut, ReturnCode, Error);

			AsyncTask(ENamedThreads::GameThread,
				[bRan, StdOut, ReturnCode, Error, ProviderName, InOnComplete]()
				{
					if (!bRan)
					{
						UE_LOG(LogAIWidgetInspector, Warning, TEXT("%s failed to launch: %s"), *ProviderName, *Error.ToString());
						InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(Error));
						return;
					}

					if (ReturnCode != 0)
					{
						UE_LOG(LogAIWidgetInspector, Warning, TEXT("%s exited with code %d"), *ProviderName, ReturnCode);
						InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(FText::Format(
							LOCTEXT("NonZeroExit", "{0} exited with code {1}.\n\n{2}"),
							FText::FromString(ProviderName),
							FText::AsNumber(ReturnCode),
							FText::FromString(StdOut))));
						return;
					}

					UE_LOG(LogAIWidgetInspector, Log, TEXT("%s replied with %d chars."), *ProviderName, StdOut.Len());
					InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeSuccess(FText::FromString(StdOut), StdOut));
				});
		});
}

#undef LOCTEXT_NAMESPACE
