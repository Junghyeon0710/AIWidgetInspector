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
		LOCTEXT("DescriptionMissing", "{0}  —  '{1}' 을(를) PATH에서 찾지 못했습니다."),
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
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("MCP 설정 파일을 쓰지 못했습니다: %s"), *ConfigPath);
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
				LOCTEXT("MissingCliWithHint", "{0} 이(가) 설치돼 있지 않습니다.  터미널에서  {1}  을(를) 실행한 뒤 에디터를 다시 시작하세요."),
				FText::FromString(Config.Executable),
				FText::FromString(Config.InstallCommand));
		}

		return FText::Format(
			LOCTEXT("MissingCli", "{0} 을(를) PATH에서 찾지 못했습니다. 설치한 뒤 에디터를 다시 시작하세요."),
			FText::FromString(Config.Executable));
	}

	if (Config.bUseUnrealMcp && !IsEditorMcpRunning())
	{
		return LOCTEXT("McpOff",
			"에디터의 MCP 서버가 꺼져 있어 AI가 Widget을 직접 고칠 수 없습니다.  "
			"Edit > Project Settings > Plugins > Model Context Protocol 에서 Auto Start Server를 켜고 에디터를 다시 시작하세요.  "
			"그때까지는 'Claude Code' Provider로 바꾸면 응답 JSON을 받아 적용하는 방식으로 쓸 수 있습니다.");
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
		OutError = LOCTEXT("NoStdOutPipe", "출력 파이프를 만들지 못했습니다.");
		return false;
	}

	// stdin: 우리가 쓰고 자식이 읽는다. 쓰기 쪽이 우리 것이므로 bWritePipeLocal.
	void* StdInRead = nullptr;
	void* StdInWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(StdInRead, StdInWrite, /*bWritePipeLocal=*/true))
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
		OutError = LOCTEXT("NoStdInPipe", "입력 파이프를 만들지 못했습니다.");
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
			LOCTEXT("LaunchFailed", "'{0}' 을(를) 실행하지 못했습니다."),
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
			LOCTEXT("TimedOut", "{0}초 안에 응답이 없어 중단했습니다."),
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
			LOCTEXT("NotInstalled", "'{0}' 을(를) PATH에서 찾지 못했습니다. 설치하고 에디터를 다시 시작하세요."),
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
				"에디터의 MCP 서버가 꺼져 있습니다. Project Settings > Model Context Protocol 에서 Auto Start Server를 켜고 에디터를 다시 시작하세요.")));
			return;
		}

		const FString ConfigPath = WriteMcpConfigFile(Config.McpServerName);
		if (ConfigPath.IsEmpty())
		{
			InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(LOCTEXT(
				"McpConfigFailed", "MCP 설정 파일을 만들지 못했습니다.")));
			return;
		}

		ArgumentList.Add(TEXT("--strict-mcp-config"));
		ArgumentList.Add(TEXT("--mcp-config"));
		ArgumentList.Add(FString::Printf(TEXT("\"%s\""), *ConfigPath));

		// 미리 허용해 두지 않으면 -p 모드에서 승인을 물을 데가 없어 그냥 멈춘다.
		// 열어 주는 것은 에디터 MCP의 세 진입점뿐이고, 그 안에서 부를 수 있는 것은
		// 우리 툴세트가 내놓은 다섯 개다.
		ArgumentList.Add(TEXT("--allowedTools"));
		ArgumentList.Add(FString::Printf(
			TEXT("mcp__%s__list_toolsets,mcp__%s__describe_toolset,mcp__%s__call_tool"),
			*Config.McpServerName, *Config.McpServerName, *Config.McpServerName));
	}

	const FString Arguments = FString::Join(ArgumentList, TEXT(" "));
	const FString ProviderName = Config.DisplayName.ToString();

	UE_LOG(LogAIWidgetInspector, Log, TEXT("%s 실행: %s %s (프롬프트 %d자)"),
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
						UE_LOG(LogAIWidgetInspector, Warning, TEXT("%s 실행 실패: %s"), *ProviderName, *Error.ToString());
						InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(Error));
						return;
					}

					if (ReturnCode != 0)
					{
						UE_LOG(LogAIWidgetInspector, Warning, TEXT("%s 종료 코드 %d"), *ProviderName, ReturnCode);
						InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeFailure(FText::Format(
							LOCTEXT("NonZeroExit", "{0}이(가) 종료 코드 {1}로 끝났습니다.\n\n{2}"),
							FText::FromString(ProviderName),
							FText::AsNumber(ReturnCode),
							FText::FromString(StdOut))));
						return;
					}

					UE_LOG(LogAIWidgetInspector, Log, TEXT("%s 응답 %d자."), *ProviderName, StdOut.Len());
					InOnComplete.ExecuteIfBound(FAIWidgetResponse::MakeSuccess(FText::FromString(StdOut), StdOut));
				});
		});
}

#undef LOCTEXT_NAMESPACE
