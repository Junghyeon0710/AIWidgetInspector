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
 */
class FAITerminalProvider : public IAIWidgetProvider
{
public:
	//~ IAIWidgetProvider
	virtual FName GetProviderName() const override { return TEXT("ClaudeTerminal"); }
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual bool IsAvailable() const override;
	virtual FText GetUnavailableReason() const override;
	virtual bool UsesEditorTools() const override { return true; }
	virtual bool IsInteractive() const override { return true; }
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) override;
	//~ End IAIWidgetProvider

private:
	/** PATH에서 claude를 찾는다. 목록을 만든 뒤에 설치하는 일이 있어 매번 다시 본다. */
	static bool FindExecutablePath(FString& OutPath);
};
