// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"
#include "AI/AIWidgetProvider.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScrollBar;
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
 * 다만 CLI가 끝나면 셸도 같이 내린다. 셸만 남으면 그 다음에 보내는 프롬프트가 셸로 들어가
 * 사용자가 쓴 문장이 명령으로 실행되기 때문이다. 화면으로는 CLI가 떠 있는지 알 수 없어서,
 * 세션이 살아 있는지로만 판단할 수 있게 둘의 수명을 묶었다.
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
	 * 어떤 CLI를 띄울지 정한다. 바뀌면 지금 것을 끝내고 새로 띄운다.
	 *
	 * 목록에서 고른 것과 실제로 도는 것이 다르면 사용자가 무엇과 이야기하고 있는지 알 수 없다.
	 */
	void SetCli(EAITerminalCli InCli);

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
	 * 터미널 위젯을 새로 만들어 끼운다.
	 *
	 * STerminal은 세션이 죽어도 되살리지 않는다. 초기화를 한 번만 하기 때문이다.
	 * 그래서 다시 띄우려면 위젯째 갈아 끼우는 수밖에 없다.
	 */
	void BuildTerminal();

	/** 이 패널의 대화 id를 적어 두는 곳. 에디터를 다시 켜도 남아 있어야 한다. */
	FString GetSessionIdFilePath() const;

	/**
	 * 이 패널이 쓰는 대화 id를 읽어 온다. 없으면 새로 만들어 적는다.
	 *
	 * id를 우리가 정해서 넘기는 이유는, --continue가 "그 폴더의 가장 최근 대화"를
	 * 집어오기 때문이다. 같은 프로젝트에서 CLI를 따로 띄워 두면 그쪽 대화를 가져와
	 * 엉뚱한 맥락에 이어 붙는다.
	 *
	 * 파일이 있다는 것 자체가 그 id로 이미 시작했다는 뜻이므로, CLI가 대화를 어디에
	 * 어떤 이름으로 쌓아 두는지는 알 필요가 없다.
	 *
	 * @param bOutIsNew 이번에 새로 만들었으면 true. 이어받을 것이 없다는 뜻이다.
	 */
	FString LoadOrCreateSessionId(bool& bOutIsNew) const;

	FReply HandleRestartClicked();

	/**
	 * 상태줄에 쓸 문장과 색.
	 *
	 * 둘을 따로 만들면 분기가 갈라져서, 빨간 글씨에 잘 되고 있다는 문장이 뜨는 날이 온다.
	 * 한 번에 정한다.
	 */
	struct FStatus
	{
		FText Text;
		FSlateColor Color = FSlateColor::UseSubduedForeground();
	};
	FStatus GetStatus() const;

	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	/** CLI가 PATH에 있는지. 매 프레임 PATH를 뒤지지 않도록 기억해 둔다. */
	bool IsCliOnPath() const;

	/** 지금 띄우는 CLI. 패널이 목록에서 고른 것을 넘겨 준다. */
	EAITerminalCli Cli = EAITerminalCli::Claude;

	TSharedPtr<STerminal> Terminal;

	/** 터미널이 들어가는 자리. 다시 띄울 때 이 안의 내용만 갈아 끼운다. */
	TSharedPtr<SBox> TerminalHost;

	/** 터미널과 함께 쓰는 스크롤바. 위젯을 새로 만들어도 이건 그대로 쓴다. */
	TSharedPtr<SScrollBar> TerminalScrollBar;

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

	/** 띄운 CLI가 이미 나갔는지. 셸과 수명을 묶어 두어 세션이 사라지면 이걸로 본다. */
	bool bCliExited = false;

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

	/** 이 패널의 대화 id. 처음 실행할 때 정해져서 에디터를 다시 켜도 유지된다. */
	FString SessionId;

	/**
	 * CLI를 PATH에서 찾았는지. 아직 찾아보지 않았으면 비어 있다.
	 *
	 * 상태줄은 매 프레임 그려진다. 그때마다 PATH를 훑으면 파일 시스템을 헛되이 두드린다.
	 * 목록을 만든 뒤에 설치하는 일이 있으므로, 터미널을 새로 만들 때 다시 찾는다.
	 */
	mutable TOptional<bool> bCliOnPath;

	/**
	 * 띄운 CLI가 화면 그리기를 한 번이라도 멈췄는지.
	 *
	 * 실행 명령을 넣었다고 CLI가 뜬 것은 아니다. 그 사이에 "돌고 있다"고 말하면, 아직
	 * 아무것도 받을 수 없는 터미널을 두고 준비됐다고 알리는 셈이다.
	 */
	mutable bool bCliSettled = false;

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
