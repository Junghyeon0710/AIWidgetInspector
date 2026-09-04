// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "AI/AIWidgetProvider.h"

/**
 * 패널 안에 떠 있는 터미널 세션으로 프롬프트를 흘려보내는 Provider.
 *
 * 다른 Provider와 달리 여기서는 프로세스를 돌리지 않는다. CLI는 이미 SAIWidgetTerminal이
 * 띄워 놓았고, 이 클래스가 하는 일은 목록에 이름을 올리고 쓸 수 있는 상태인지 알려주는 것뿐이다.
 * 실제 전송은 패널이 IsInteractive()를 보고 터미널로 직접 넘긴다.
 *
 * 요청 하나에 응답 하나가 돌아오는 SendRequest 모양에 대화형 세션을 억지로 끼워 맞추지
 * 않았다. 답이 언제 끝나는지 알 수 없고, 중간에 사용자가 승인을 하거나 되물을 수도 있다.
 *
 * CLI 종류만 다른 같은 Provider를 여러 개 등록한다. 어느 CLI와 이야기하고 있는지는
 * 목록에서 골라야 하는 것이지, 설정 어딘가에 숨어 있을 일이 아니다.
 */
class FAITerminalProvider : public IAIWidgetProvider
{
public:
	explicit FAITerminalProvider(EAITerminalCli InCli)
		: Cli(InCli)
	{
	}

	/** PATH에서 찾을 실행 파일 이름. 터미널도 같은 이름으로 띄운다. */
	static const TCHAR* GetExecutable(EAITerminalCli InCli);

	/** 없을 때 안내할 설치 명령. */
	static const TCHAR* GetInstallCommand(EAITerminalCli InCli);

	//~ IAIWidgetProvider
	virtual FName GetProviderName() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual bool IsAvailable() const override;
	virtual FText GetUnavailableReason() const override;
	virtual bool UsesEditorTools() const override { return true; }
	virtual bool IsInteractive() const override { return true; }
	virtual EAITerminalCli GetTerminalCli() const override { return Cli; }
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) override;
	//~ End IAIWidgetProvider

private:
	/** PATH에서 실행 파일을 찾는다. 목록을 만든 뒤에 설치하는 일이 있어 매번 다시 본다. */
	bool FindExecutablePath(FString& OutPath) const;

	EAITerminalCli Cli;
};
