// AI Widget Inspector

#pragma once

#include "AI/AIWidgetProvider.h"

/**
 * 로컬에 설치된 AI CLI를 돌려서 답을 받는 Provider.
 *
 * API Key를 다루지 않는다. 인증은 이미 `claude` / `codex` CLI가 각자 갖고 있고,
 * 우리는 그 실행 파일을 찾아 프롬프트를 stdin으로 넘기고 stdout을 읽을 뿐이다.
 * 그래서 플러그인이 자격 증명을 저장하거나 네트워크를 직접 열 일이 없다.
 *
 * 프로세스는 백그라운드 스레드에서 돌린다. CLI 응답은 수십 초가 걸릴 수 있어
 * 게임 스레드에서 기다리면 에디터가 그동안 멈춘다. 완료 콜백만 게임 스레드로 되돌린다.
 */
class FAICliProvider : public IAIWidgetProvider
{
public:
	struct FConfig
	{
		FName Name;
		FText DisplayName;
		FText Description;

		/** PATH에서 찾을 실행 파일 이름. 확장자는 붙이지 않는다. */
		FString Executable;

		/** 프롬프트 앞에 붙는 인자. 프롬프트 자체는 stdin으로 넘어간다. */
		TArray<FString> Arguments;

		/**
		 * 에디터의 MCP 서버에 붙여서 Tool을 직접 부르게 할지.
		 *
		 * 켜면 실행 직전에 MCP 설정 파일을 만들고 --mcp-config로 넘긴다. 포트는
		 * 엔진 설정에서 읽으므로 사용자가 바꿔 놓았어도 따라간다.
		 */
		bool bUseUnrealMcp = false;

		/** MCP 설정에 적을 서버 이름. Tool 이름이 mcp__<이름>__* 형태가 된다. */
		FString McpServerName = TEXT("unreal");

		/** 없을 때 안내할 설치 명령. 패널에 그대로 보여 준다. */
		FString InstallCommand;
	};

	explicit FAICliProvider(FConfig InConfig);

	//~ IAIWidgetProvider
	virtual FName GetProviderName() const override { return Config.Name; }
	virtual bool UsesEditorTools() const override { return Config.bUseUnrealMcp; }
	virtual FText GetDisplayName() const override { return Config.DisplayName; }
	virtual FText GetDescription() const override;
	virtual bool IsAvailable() const override;
	virtual FText GetUnavailableReason() const override;
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) override;
	//~ End IAIWidgetProvider

	/** PATH를 뒤져 실행 파일을 찾는다. Windows에서는 .exe / .cmd / .bat도 본다. */
	static bool FindExecutable(const FString& InExecutableName, FString& OutPath);

	/**
	 * 에디터 MCP 서버를 가리키는 설정 파일을 쓰고 그 경로를 돌려준다. 실패하면 빈 문자열.
	 *
	 * 인자로 JSON을 그대로 넘기지 않는 이유는 Windows 명령줄 따옴표 때문이다.
	 * 특히 .cmd를 cmd.exe로 감싸는 경로에서 중첩 따옴표가 한 번 더 벗겨져 깨진다.
	 * 파일 경로 하나만 넘기면 그런 일이 없다.
	 */
	static FString WriteMcpConfigFile(const FString& InServerName);

	/** 엔진 ModelContextProtocol 설정에서 읽은 http://127.0.0.1:<port><path> 주소. */
	static FString GetEditorMcpUrl();

	/** MCP 서버가 실제로 떠 있는지. 꺼져 있으면 Tool을 부를 수 없다. */
	static bool IsEditorMcpRunning();

	/**
	 * 프로세스를 돌리고 끝날 때까지 기다린다. 반드시 백그라운드 스레드에서 부른다.
	 *
	 * @param InStdIn  프롬프트. 다 쓰고 나면 쓰기 쪽을 닫아 CLI에게 입력 끝을 알린다.
	 */
	static bool RunProcess(
		const FString& InExecutablePath,
		const FString& InArguments,
		const FString& InStdIn,
		double InTimeoutSeconds,
		FString& OutStdOut,
		int32& OutReturnCode,
		FText& OutError);

	/** CLI가 이 시간 안에 답하지 않으면 끊는다. */
	static constexpr double DefaultTimeoutSeconds = 180.0;

private:
	FConfig Config;
};
