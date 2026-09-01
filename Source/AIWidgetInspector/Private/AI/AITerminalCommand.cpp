// AI Widget Inspector

#include "AI/AITerminalCommand.h"

#include "AI/AITerminalProvider.h"

namespace AIWidgetInspector::TerminalCommand
{
	namespace Private
	{
		/** 에디터 MCP를 물리는 인자. 앞에 공백이 붙어 있어 그대로 이어 붙이면 된다. */
		static FString BuildMcpFlags(const FLaunch& InLaunch)
		{
			switch (InLaunch.Cli)
			{
			case EAITerminalCli::Claude:
				if (InLaunch.McpConfigPath.IsEmpty())
				{
					return FString();
				}

				// 설정을 파일로 넘긴다. JSON을 인자에 그대로 실으면 중첩 따옴표가 셸을
				// 지날 때마다 벗겨져 깨진다.
				return FString::Printf(TEXT(" --strict-mcp-config --mcp-config \"%s\""), *InLaunch.McpConfigPath);

			case EAITerminalCli::Codex:
				if (InLaunch.McpUrl.IsEmpty())
				{
					return FString();
				}

				// codex는 설정 파일을 통째로 받지 않고 ~/.codex/config.toml 의 항목을 -c로 덮는다.
				// 값에 따옴표를 두르지 않는다. TOML로 파싱되지 않으면 문자열 그대로 쓰는데 URL이
				// 그 경우라, 셸 안에서 따옴표를 겹치지 않아도 되는 이쪽이 안전하다.
				return FString::Printf(TEXT(" -c mcp_servers.unreal.url=%s"), *InLaunch.McpUrl);
			}

			return FString();
		}
	}

	FString BuildChangeDirectoryCommand(const FString& InDirectory, bool bInWindowsShell)
	{
		// cmd는 드라이브가 다르면 /d 없이 옮겨 가지 않는다. 엔진과 프로젝트가 다른 드라이브에
		// 있는 일이 흔하다.
		return bInWindowsShell
			? FString::Printf(TEXT("cd /d \"%s\""), *InDirectory)
			: FString::Printf(TEXT("cd \"%s\""), *InDirectory);
	}

	FString BuildLaunchCommand(const FLaunch& InLaunch)
	{
		const TCHAR* const Executable = FAITerminalProvider::GetExecutable(InLaunch.Cli);
		const FString McpFlags = Private::BuildMcpFlags(InLaunch);

		FString FreshCommand;
		FString ResumeCommand;
		switch (InLaunch.Cli)
		{
		case EAITerminalCli::Claude:
			FreshCommand = FString::Printf(TEXT("%s --session-id %s%s"), Executable, *InLaunch.SessionId, *McpFlags);
			ResumeCommand = FString::Printf(TEXT("%s --resume %s%s"), Executable, *InLaunch.SessionId, *McpFlags);
			break;

		case EAITerminalCli::Codex:
			FreshCommand = FString::Printf(TEXT("%s%s"), Executable, *McpFlags);
			ResumeCommand = FString::Printf(TEXT("%s resume --last%s"), Executable, *McpFlags);
			break;
		}

		FString CliCommand = FreshCommand;
		if (InLaunch.bResume)
		{
			// 기록이 있다고 이어받을 대화가 있는 것은 아니다. CLI는 오간 말이 있어야 대화를
			// 저장하므로, 패널만 열고 아무것도 묻지 않은 채 에디터를 닫으면 기록만 남는다.
			// 그때 이어받기는 실패하고 CLI가 그대로 끝난다. 실패하면 새로 시작하게 해서
			// 어긋난 기록이 스스로 풀리게 한다.
			CliCommand = FString::Printf(TEXT("%s || %s"), *ResumeCommand, *FreshCommand);
		}

		// CLI가 끝나면 셸도 함께 내린다.
		//
		// 셸만 남으면 그 다음에 보내는 프롬프트가 셸로 들어가고, 사용자가 쓴 문장이 그대로
		// 명령이 된다. 실제로 "Read ... for the Unreal widget"이 명령으로 실행됐다. 문장에
		// '>'나 '|'가 하나만 있어도 파일이 생기거나 엉뚱한 것이 실행된다.
		//
		// 화면을 읽을 수 없어서 CLI가 떠 있는지 알 방법이 IsSessionRunning뿐이다. 둘의
		// 수명을 묶어 두면 그 하나로 판단할 수 있다.
		//
		// 괄호로 묶는 이유는 || 가 exit 까지 삼키지 않게 하기 위해서다.
		return InLaunch.bWindowsShell
			? FString::Printf(TEXT("(%s) & exit"), *CliCommand)
			: FString::Printf(TEXT("{ %s; }; exit"), *CliCommand);
	}
}
