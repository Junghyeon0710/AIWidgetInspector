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
	};

	explicit FAICliProvider(FConfig InConfig);

	//~ IAIWidgetProvider
	virtual FName GetProviderName() const override { return Config.Name; }
	virtual FText GetDisplayName() const override { return Config.DisplayName; }
	virtual FText GetDescription() const override;
	virtual bool IsAvailable() const override;
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) override;
	//~ End IAIWidgetProvider

	/** PATH를 뒤져 실행 파일을 찾는다. Windows에서는 .exe / .cmd / .bat도 본다. */
	static bool FindExecutable(const FString& InExecutableName, FString& OutPath);

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
