// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"

/**
 * 요청 종류.
 *
 * 스펙에서는 SendQuestion() / RequestChange() 두 함수로 나눴지만, 두 경우가 주고받는 것이
 * Context + 문장으로 똑같아서 한 진입점에 종류만 붙였다. Provider 구현체가 분기를 두 번
 * 적을 필요가 없고, 나중에 종류가 늘어도 인터페이스가 안 바뀐다.
 */
enum class EAIWidgetRequestKind : uint8
{
	/** 선택된 Widget에 대해 물어본다. 답은 자연어. */
	Question,

	/** 선택된 Widget을 바꿔달라고 요청한다. 답은 구조화된 변경 명령. (Phase 6) */
	ChangeRequest,

	/**
	 * 바꿔달라고 요청하되, AI가 에디터 Tool을 직접 불러 처리한다. (Phase 9)
	 *
	 * ChangeRequest와 달리 응답 JSON을 파싱하지 않는다. 답이 돌아왔을 때는 변경이
	 * 이미 끝나 있고, 본문은 무엇을 왜 했는지에 대한 설명이다.
	 */
	ToolChangeRequest,
};

/** Provider에게 넘기는 한 건의 요청. */
struct FAIWidgetRequest
{
	EAIWidgetRequestKind Kind = EAIWidgetRequestKind::Question;

	/** FAIWidgetContextBuilder가 만든 Widget 정보 블록. */
	FString Context;

	/** 사용자가 입력한 문장. */
	FString UserMessage;

	/** Context와 사용자 문장을 합친 최종 프롬프트. */
	AIWIDGETINSPECTOR_API FString BuildPrompt() const;
};

/** Provider가 돌려주는 결과. */
struct FAIWidgetResponse
{
	bool bSuccess = false;

	/** 패널에 보여줄 내용. 실패면 실패 이유. */
	FText Message;

	/** Provider가 돌려준 원문. Phase 6에서 변경 명령 파싱에 쓴다. */
	FString RawResponse;

	static AIWIDGETINSPECTOR_API FAIWidgetResponse MakeSuccess(const FText& InMessage, const FString& InRawResponse = FString());
	static AIWIDGETINSPECTOR_API FAIWidgetResponse MakeFailure(const FText& InMessage);
};

DECLARE_DELEGATE_OneParam(FOnAIWidgetResponse, const FAIWidgetResponse& /*Response*/);

/**
 * AI 연동 지점.
 *
 * 이 인터페이스 뒤로 Clipboard / Claude / Codex 구현이 들어간다.
 * 구현체는 API Key를 소스에 넣지 않고, 에디터를 멈추지 않도록 오래 걸리는 작업은 비동기로 하고,
 * 완료 콜백은 반드시 게임 스레드에서 호출해야 한다.
 */
class AIWIDGETINSPECTOR_API IAIWidgetProvider
{
public:
	virtual ~IAIWidgetProvider() = default;

	/** 설정 저장 등에 쓰는 고정 식별자. */
	virtual FName GetProviderName() const = 0;

	/** 콤보박스에 표시할 이름. */
	virtual FText GetDisplayName() const = 0;

	/** 툴팁에 표시할 한 줄 설명. */
	virtual FText GetDescription() const = 0;

	/** 지금 쓸 수 있는 상태인지. (CLI 미설치, 인증 안 됨 등) */
	virtual bool IsAvailable() const = 0;

	/**
	 * 못 쓰는 이유. 쓸 수 있으면 빈 FText.
	 *
	 * 패널에 그대로 나가므로 무엇이 없는지와 어떻게 채우는지가 같이 들어 있어야 한다.
	 * 툴팁에만 두면 회색 버튼을 보고 이유를 찾으려 마우스를 올려 볼 사람이 드물다.
	 */
	virtual FText GetUnavailableReason() const { return FText::GetEmpty(); }

	/**
	 * 이 Provider가 에디터 Tool을 직접 부를 수 있는지.
	 *
	 * true면 변경 요청이 ToolChangeRequest로 나가고, 패널은 응답에서 JSON을 찾지 않는다.
	 * 답이 왔을 때 이미 적용돼 있기 때문이다.
	 */
	virtual bool UsesEditorTools() const { return false; }

	/** 요청을 보낸다. 완료되면 InOnComplete를 게임 스레드에서 호출한다. */
	virtual void SendRequest(const FAIWidgetRequest& InRequest, FOnAIWidgetResponse InOnComplete) = 0;
};
