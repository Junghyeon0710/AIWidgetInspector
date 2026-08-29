// AI Widget Inspector

#pragma once

#include "AI/AIWidgetProvider.h"

/**
 * 프롬프트를 클립보드에 올려놓는 Provider.
 *
 * AI를 직접 호출하지 않는다. 사용자가 쓰는 Claude / Codex 창에 그대로 붙여넣으면 된다.
 * 네트워크도, API Key도, 인증도 필요 없어서 어떤 환경에서든 동작한다는 게 장점이다.
 * Phase 8에서 CLI 연동이 붙어도 이건 폴백으로 남는다.
 */
class FAIClipboardProvider : public IAIWidgetProvider
{
public:
	static const FName ProviderName;

	//~ IAIWidgetProvider
	virtual FName GetProviderName() const override { return ProviderName; }
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual bool IsAvailable() const override { return true; }
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) override;
	//~ End IAIWidgetProvider
};
