// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"

/**
 * CLI를 띄우기 전에 알아야 하는 것들.
 *
 * 실행 파일이 어디 있는지, 에디터 MCP 서버가 떠 있는지, 붙으려면 어떤 설정을 넘겨야
 * 하는지. Provider가 아니라 환경에 대한 질문이라 어느 Provider에도 매여 있지 않다.
 *
 * 예전에는 이 함수들이 FAICliProvider의 static이었다. 그 Provider는 프롬프트를 stdin으로
 * 넘기고 답만 받아 오는 원샷이었는데, 승인을 물을 자리가 없어 도구를 미리 좁혀야 했고
 * 그래서 코드를 읽지도 쓰지도 못했다. Provider는 걷어냈지만 이 질문들은 그대로 남는다.
 */
namespace AIWidgetInspector::CliEnvironment
{
	/** PATH를 뒤져 실행 파일을 찾는다. Windows에서는 .exe / .cmd / .bat도 본다. */
	bool FindExecutable(const FString& InExecutableName, FString& OutPath);

	/**
	 * 에디터 MCP 서버가 지금 떠 있는지.
	 *
	 * 자동 시작 설정을 읽지 않는다. 그건 "켜도록 해 뒀는가"일 뿐이라, 포트가 이미 잡혀
	 * 서버가 뜨지 못했어도 참이 된다.
	 */
	bool IsEditorMcpRunning();

	/** 에디터 MCP 서버를 가리키는 http://127.0.0.1:<port><path> 주소. */
	FString GetEditorMcpUrl();

	/**
	 * 그 주소를 담은 설정 파일을 쓰고 경로를 돌려준다. 실패하면 빈 문자열.
	 *
	 * 인자로 JSON을 그대로 넘기지 않는 이유는 Windows 명령줄 따옴표 때문이다. 중첩
	 * 따옴표가 셸을 한 번 지날 때마다 벗겨져 깨진다. 파일 경로 하나면 그런 일이 없다.
	 */
	FString WriteMcpConfigFile(const FString& InServerName);
}
