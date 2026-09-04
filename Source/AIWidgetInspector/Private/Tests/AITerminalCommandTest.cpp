// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "AI/AITerminalCommand.h"

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * 터미널에 넣는 명령이 어긋나지 않는지.
 *
 * 이 자리는 조용히 깨진다. 따옴표 하나, || 의 우선순위, 끝의 exit 중 하나만 어긋나도
 * 컴파일은 되고, 에디터를 켜서 CLI가 안 뜨는 것을 볼 때까지 알 수 없다. 그래서 눈으로
 * 확인하는 대신 여기서 문자열을 확인한다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAITerminalCommandTest,
	"AIWidgetInspector.TerminalCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAITerminalCommandTest::RunTest(const FString& Parameters)
{
	using namespace AIWidgetInspector::TerminalCommand;

	const FString SessionId = TEXT("11111111-2222-3333-4444-555555555555");

	// 에디터를 Claude Code 안에서 띄우면 그쪽 환경 변수가 여기까지 내려온다. 그대로 두면
	// 우리가 띄운 CLI가 자기를 남의 자식으로 보고 대화 저장을 꺼서, 다음에 이어받을 것이
	// 없어진다.
	const TCHAR* const WindowsPrefix = TEXT("set \"CLAUDE_CODE_CHILD_SESSION=\" & set \"CLAUDE_CODE_SESSION_ID=\" & ");

	// --- 처음 띄울 때는 이어받기를 시도하지 않는다 ---
	//
	// 없는 대화를 이어받으려 하면 CLI가 오류를 찍는다. 처음 켠 사람이 그 줄부터 보게 된다.
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Claude;
		Launch.SessionId = SessionId;
		Launch.bResume = false;
		Launch.bWindowsShell = true;

		TestEqual(TEXT("claude 첫 실행"),
			BuildLaunchCommand(Launch),
			FString::Printf(TEXT("%s(claude --session-id %s) & exit"), WindowsPrefix, *SessionId));
	}

	// --- 이어받기가 실패하면 같은 id로 새로 시작한다 ---
	//
	// 기록이 있다고 대화가 있는 것은 아니다. || 가 빠지면 그때 CLI가 그대로 끝나 버린다.
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Claude;
		Launch.SessionId = SessionId;
		Launch.bResume = true;
		Launch.bWindowsShell = true;

		TestEqual(TEXT("claude 이어받기와 폴백"),
			BuildLaunchCommand(Launch),
			FString::Printf(
				TEXT("%s(claude --resume %s || claude --session-id %s) & exit"),
				WindowsPrefix, *SessionId, *SessionId));
	}

	// --- MCP 인자는 양쪽 모두에 붙어야 한다 ---
	//
	// 폴백에만 빠뜨리면, 이어받기가 실패한 날에만 위젯을 못 고치게 된다. 재현이 어려운
	// 종류의 고장이다.
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Claude;
		Launch.SessionId = SessionId;
		Launch.bResume = true;
		Launch.bWindowsShell = true;
		Launch.McpConfigPath = TEXT("C:/Example/Intermediate/McpConfig.json");

		const FString Command = BuildLaunchCommand(Launch);
		const FString Flags = TEXT("--strict-mcp-config --mcp-config \"C:/Example/Intermediate/McpConfig.json\"");

		int32 First = INDEX_NONE;
		const bool bFound = Command.FindChar(TEXT('-'), First);
		TestTrue(TEXT("인자가 들어 있다"), bFound && Command.Contains(Flags));

		int32 Count = 0;
		int32 SearchFrom = 0;
		while (true)
		{
			const int32 At = Command.Find(Flags, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (At == INDEX_NONE)
			{
				break;
			}
			++Count;
			SearchFrom = At + Flags.Len();
		}

		TestEqual(TEXT("이어받기와 폴백 양쪽에 붙는다"), Count, 2);
	}

	// --- codex는 세션 id를 받지 못한다 ---
	//
	// claude 쪽 인자를 그대로 흘려 보내면 codex가 알 수 없는 인자라며 즉시 끝난다.
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Codex;
		Launch.SessionId = SessionId;
		Launch.bResume = false;
		Launch.bWindowsShell = true;

		const FString Command = BuildLaunchCommand(Launch);

		TestEqual(TEXT("codex 첫 실행"), Command, FString::Printf(TEXT("%s(codex) & exit"), WindowsPrefix));
		TestFalse(TEXT("codex 명령에 세션 id가 실리지 않는다"), Command.Contains(SessionId));
		TestFalse(TEXT("claude 전용 인자가 새어 들어가지 않는다"), Command.Contains(TEXT("--session-id")));
	}

	// --- codex는 가장 최근 대화만 이어받을 수 있다 ---
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Codex;
		Launch.SessionId = SessionId;
		Launch.bResume = true;
		Launch.bWindowsShell = true;
		Launch.McpUrl = TEXT("http://127.0.0.1:8000/mcp");

		TestEqual(TEXT("codex 이어받기와 폴백"),
			BuildLaunchCommand(Launch),
			FString::Printf(
				TEXT("%s(codex resume --last -c mcp_servers.unreal.url=http://127.0.0.1:8000/mcp")
				TEXT(" || codex -c mcp_servers.unreal.url=http://127.0.0.1:8000/mcp) & exit"),
				WindowsPrefix));
	}

	// --- 반대쪽 MCP 인자를 잘못 집지 않는다 ---
	//
	// claude에 URL만 있고 codex에 파일 경로만 있는 경우, 붙일 것이 없으면 아무것도 붙이지
	// 않아야 한다. 서로의 인자를 집으면 CLI가 곧바로 끝난다.
	{
		FLaunch ClaudeWithUrl;
		ClaudeWithUrl.Cli = EAITerminalCli::Claude;
		ClaudeWithUrl.SessionId = SessionId;
		ClaudeWithUrl.bWindowsShell = true;
		ClaudeWithUrl.McpUrl = TEXT("http://127.0.0.1:8000/mcp");

		TestFalse(TEXT("claude가 codex용 URL을 집지 않는다"),
			BuildLaunchCommand(ClaudeWithUrl).Contains(TEXT("mcp_servers")));

		FLaunch CodexWithPath;
		CodexWithPath.Cli = EAITerminalCli::Codex;
		CodexWithPath.bWindowsShell = true;
		CodexWithPath.McpConfigPath = TEXT("C:/Example/Intermediate/McpConfig.json");

		TestFalse(TEXT("codex가 claude용 설정 파일을 집지 않는다"),
			BuildLaunchCommand(CodexWithPath).Contains(TEXT("--mcp-config")));
	}

	// --- POSIX 셸에서는 괄호 대신 중괄호를 쓴다 ---
	//
	// sh 에서 ( ) 는 서브셸이라 exit 가 그 안에서 끝나 버리고 셸이 남는다. 셸이 남으면
	// 그 다음에 보내는 프롬프트가 명령으로 실행된다.
	{
		FLaunch Launch;
		Launch.Cli = EAITerminalCli::Claude;
		Launch.SessionId = SessionId;
		Launch.bResume = false;
		Launch.bWindowsShell = false;

		TestEqual(TEXT("POSIX 셸 문법"),
			BuildLaunchCommand(Launch),
			FString::Printf(
				TEXT("unset CLAUDE_CODE_CHILD_SESSION CLAUDE_CODE_SESSION_ID; { claude --session-id %s; }; exit"),
				*SessionId));
	}

	// --- cd 는 드라이브를 넘어가야 한다 ---
	//
	// 엔진과 프로젝트가 다른 드라이브에 있는 일이 흔하다. /d 가 없으면 셸은 조용히
	// 원래 드라이브에 남고, CLI가 엉뚱한 폴더를 프로젝트로 본다.
	{
		TestEqual(TEXT("cmd 는 /d 가 필요하다"),
			BuildChangeDirectoryCommand(TEXT("C:/Example/Project/"), true),
			TEXT("cd /d \"C:/Example/Project/\""));

		TestEqual(TEXT("POSIX 는 /d 를 모른다"),
			BuildChangeDirectoryCommand(TEXT("/home/example/Project/"), false),
			TEXT("cd \"/home/example/Project/\""));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
