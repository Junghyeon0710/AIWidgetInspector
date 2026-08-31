// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STerminal;

/**
 * 패널 안에서 AI CLI를 실제로 돌리는 터미널.
 *
 * 엔진의 STerminal은 PTY 위에 올라간 진짜 터미널 에뮬레이터다. 여기에 claude를 띄우면
 * 대화형 TUI가 그대로 돌아가므로, 권한을 물어보면 그 자리에서 엔터를 치면 된다.
 * 프롬프트를 stdin으로 밀어 넣고 종료를 기다리는 원샷 방식으로는 물어볼 데가 없어
 * --allowedTools로 미리 열어 두는 수밖에 없었다.
 *
 * 셸을 한 겹 거쳐서 CLI를 띄우는 이유는 STerminal이 셸 경로를 위젯별로 받지 않기 때문이다.
 * 대신 셸이 남아 있어서 CLI를 끝내도 터미널이 죽지 않고, 사용자가 직접 명령을 칠 수도 있다.
 */
class SAIWidgetTerminal : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAIWidgetTerminal) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** CLI를 띄운다. 셸이 아직 준비되지 않았으면 준비된 뒤에 띄운다. */
	void StartCli();

	/**
	 * 한 줄짜리 프롬프트를 CLI에 보낸다. 줄바꿈은 공백으로 눕힌다.
	 *
	 * CLI가 아직 뜨는 중이면 큐에 넣고 출력이 잠잠해지기를 기다렸다가 보낸다. TUI가 뜨기
	 * 전에 밀어 넣으면 아래 셸이 그걸 명령으로 받아 엉뚱한 걸 실행한다. 대기 중에 새
	 * 프롬프트가 오면 마지막 것만 남는다.
	 */
	void SendPrompt(const FString& InPrompt);

	/** 셸 세션이 살아 있는지. CLI 자체가 떠 있는지까지는 알 수 없다. */
	bool IsSessionRunning() const;

	//~ Begin SWidget
	/** 그려진 적이 있는지 표시만 한다. STerminal이 첫 페인트에서 PTY를 만들기 때문이다. */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	//~ End SWidget

private:
	/** 셸 준비 -> CLI 실행 -> 프롬프트 전송을 한 타이머에서 차례로 처리한다. */
	EActiveTimerReturnType OnPump(double InCurrentTime, float InDeltaTime);

	/** OnPump가 돌고 있지 않으면 건다. 중복 등록을 막는다. */
	void EnsurePumpRunning();

	/** 터미널이 그리기를 멈췄는지. 멈췄으면 입력을 기다리는 상태로 본다. */
	bool IsTerminalQuiet(double InCurrentTime) const;

	/** cd 후 claude를 실행한다. 셸이 준비된 뒤에만 부른다. */
	void LaunchCli(double InCurrentTime);

	/**
	 * 이 프로젝트로 나눈 대화가 남아 있는지.
	 *
	 * CLI는 대화를 작업 디렉터리별로 홈 아래에 쌓아 둔다. 폴더 이름은 그 경로에서
	 * 영숫자가 아닌 글자를 전부 '-'로 바꾼 것이다. 내부 규칙이라 언제든 바뀔 수 있으므로,
	 * 틀리면 이어받지 못할 뿐 실행 자체는 되도록 실패를 조용히 넘긴다.
	 */
	static bool HasPriorConversation(const FString& InWorkingDirectory);

	FReply HandleRestartClicked();
	FText GetStatusText() const;

	TSharedPtr<STerminal> Terminal;

	/** 보내려고 대기 중인 프롬프트. 비어 있으면 대기 중인 것이 없다. */
	FString PendingPrompt;

	/** OnPump 활성 타이머 핸들. 유효하면 이미 돌고 있다. */
	TWeakPtr<FActiveTimerHandle> PumpHandle;

	/** 셸을 기다리기 시작한 시점. 포기할 기준이 된다. 아직 안 그려졌으면 계속 미뤄진다. */
	double ShellWaitStartTime = 0.0;

	/**
	 * 한 번이라도 화면에 그려졌는지.
	 *
	 * STerminal은 첫 페인트에서 지오메트리를 보고 PTY를 만든다. 그래서 섹션이 접혀 있거나
	 * 스크롤 밖에 있으면 셸이 아예 생기지 않는다. 이걸 기다림의 실패로 세면, 패널을
	 * 열어 두기만 해도 "셸이 시작되지 않았다"가 뜬다.
	 */
	mutable bool bEverPainted = false;

	/** claude를 실행한 시점. 조용해지기를 기다리다 포기할 기준이 된다. */
	double CliLaunchTime = 0.0;

	/**
	 * 프롬프트는 넣었고 이제 Enter만 남았는지.
	 *
	 * 본문과 Enter를 한 번에 쓰면 CLI가 그 덩어리를 붙여넣기로 보고 줄바꿈을 글자로 넣는다.
	 * 그래서 입력창에 문장만 남고 전송이 되지 않는다. 조금 띄웠다가 Enter만 따로 보낸다.
	 */
	bool bAwaitingSubmit = false;

	/** 프롬프트를 넣은 시점. 여기서 SubmitDelaySeconds가 지나면 Enter를 보낸다. */
	double PromptTypedTime = 0.0;

	bool bCliLaunched = false;

	/**
	 * 다음 실행에서 지난 대화를 버릴지.
	 *
	 * 기본은 이어받기다. 이 터미널은 에디터 안에 살아서, C++을 고쳐 에디터를 다시 켜면
	 * 세션이 통째로 사라진다. 그때마다 처음부터 설명하게 두면 패널에서 일을 이어갈 수 없다.
	 * Restart CLI를 누른 경우에만 새로 시작한다. 그건 대화를 끊겠다는 뜻이기 때문이다.
	 */
	bool bStartFresh = false;

	/** 마지막 실행이 지난 대화를 이어받았는지. 상태줄에 알려 준다. */
	bool bResumedConversation = false;

	/** 셸이 끝내 뜨지 않았다. 상태줄에 이유를 남긴다. */
	bool bShellTimedOut = false;

	/** 본문을 넣고 이만큼 띄운 뒤에 Enter를 보낸다. 붙여넣기로 뭉뚱그려지지 않을 만큼이다. */
	static constexpr double SubmitDelaySeconds = 0.35;

	/** 출력이 이만큼 멈추면 TUI가 입력을 받을 준비가 됐다고 본다. */
	static constexpr double QuietSecondsBeforeSend = 0.75;

	/** 그래도 안 조용해지면 그냥 보낸다. 영원히 기다리는 것보다 낫다. */
	static constexpr double MaxWaitForCliSeconds = 20.0;

	/** 셸이 이 시간 안에 뜨지 않으면 포기한다. */
	static constexpr double MaxWaitForShellSeconds = 10.0;
};
