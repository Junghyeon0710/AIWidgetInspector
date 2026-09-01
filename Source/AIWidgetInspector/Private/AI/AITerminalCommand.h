// AI Widget Inspector

#pragma once

#include "AI/AIWidgetProvider.h"
#include "CoreMinimal.h"

/**
 * 터미널에 그대로 넣을 명령 한 줄을 만든다.
 *
 * 위젯에서 떼어 낸 이유는 여기가 조용히 깨지는 자리이기 때문이다. 따옴표 하나, || 의
 * 우선순위, 끝의 exit — 어느 하나가 어긋나도 컴파일은 되고, 런타임에 CLI가 뜨지 않을
 * 뿐이다. 에디터를 켜서 눈으로 보기 전에는 알 수 없다.
 *
 * 이 함수들은 에디터도 Slate도 파일 시스템도 건드리지 않는다. 넣은 것과 나온 문자열만
 * 있으므로 테스트가 CLI 종류 × 이어받기 × MCP × 셸 문법을 전부 확인할 수 있다.
 */
namespace AIWidgetInspector::TerminalCommand
{
	/** CLI를 한 번 띄우는 데 필요한 것 전부. */
	struct FLaunch
	{
		EAITerminalCli Cli = EAITerminalCli::Claude;

		/**
		 * claude가 대화를 구분하는 id.
		 *
		 * codex는 시작할 때 id를 받지 못하므로 쓰이지 않는다. 그쪽은 "이 폴더의 가장 최근
		 * 것"만 이어받을 수 있다.
		 */
		FString SessionId;

		/** 지난 대화를 이어받을지. */
		bool bResume = false;

		/** claude에 넘길 MCP 설정 파일의 절대 경로. 비어 있으면 MCP를 붙이지 않는다. */
		FString McpConfigPath;

		/** codex에 넘길 MCP 서버 주소. 비어 있으면 MCP를 붙이지 않는다. */
		FString McpUrl;

		/** cmd.exe 문법으로 만들지, POSIX 셸 문법으로 만들지. */
		bool bWindowsShell = PLATFORM_WINDOWS != 0;
	};

	/** 셸을 그 디렉터리로 옮기는 한 줄. */
	FString BuildChangeDirectoryCommand(const FString& InDirectory, bool bInWindowsShell);

	/** CLI를 띄우는 한 줄. 끝나면 셸도 함께 내려간다. */
	FString BuildLaunchCommand(const FLaunch& InLaunch);
}
