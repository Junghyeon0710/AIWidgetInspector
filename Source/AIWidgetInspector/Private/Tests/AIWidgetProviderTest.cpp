// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "AI/AICliEnvironment.h"
#include "AI/AITerminalProvider.h"

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * 못 쓰는 Provider가 이유를 말하는지.
 *
 * 이건 설치가 멀쩡한 컴퓨터에서는 눈으로 확인할 수 없다. CLI가 다 깔려 있으면 경고가
 * 아예 안 뜨기 때문이다. 그래서 설치 여부에 따라 갈리는 두 갈래를 다 확인한다.
 *
 * 조용히 틀리는 쪽은 안내가 비는 경우다. 버튼은 회색이고 이유는 어디에도 없어서,
 * 사용자는 플러그인이 고장 났다고 생각하지 설치가 필요하다고 생각하지 않는다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetProviderTest,
	"AIWidgetInspector.Provider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetProviderTest::RunTest(const FString& Parameters)
{
	using namespace AIWidgetInspector;

	const EAITerminalCli Clis[] = { EAITerminalCli::Claude, EAITerminalCli::Codex };

	for (const EAITerminalCli Cli : Clis)
	{
		const FString Executable = FAITerminalProvider::GetExecutable(Cli);
		const FString InstallCommand = FAITerminalProvider::GetInstallCommand(Cli);

		const FAITerminalProvider Provider(Cli);

		FString FoundPath;
		const bool bInstalled = CliEnvironment::FindExecutable(Executable, FoundPath);

		// 쓸 수 있다는 답과 PATH에서 찾았다는 사실이 어긋나면, 사용자는 눌리는 버튼을
		// 보고 눌렀다가 아무 일도 일어나지 않는 것을 겪는다.
		TestEqual(
			*FString::Printf(TEXT("%s: PATH에서 찾은 것과 쓸 수 있다는 답이 같다"), *Executable),
			Provider.IsAvailable(), bInstalled);

		const FText Reason = Provider.GetUnavailableReason();

		if (bInstalled)
		{
			// 멀쩡한데도 경고가 뜨면 사용자는 곧 경고를 무시하기 시작한다.
			TestTrue(
				*FString::Printf(TEXT("%s: 쓸 수 있으면 이유는 비어 있다"), *Executable),
				Reason.IsEmpty());
		}
		else
		{
			const FString ReasonText = Reason.ToString();

			TestFalse(
				*FString::Printf(TEXT("%s: 이유가 비어 있으면 안 된다"), *Executable),
				Reason.IsEmpty());
			TestTrue(
				*FString::Printf(TEXT("%s: 무엇이 없는지 말한다"), *Executable),
				ReasonText.Contains(Executable));
			TestTrue(
				*FString::Printf(TEXT("%s: 어떻게 설치하는지 말한다"), *Executable),
				ReasonText.Contains(InstallCommand));
		}
	}

	// --- 없는 실행 파일을 있다고 하면 안 된다 ---
	//
	// FindExecutable이 무엇이든 참을 돌려주면 위의 확인이 통째로 무의미해진다.
	{
		FString FoundPath;
		TestFalse(
			TEXT("없는 실행 파일은 못 찾는다"),
			CliEnvironment::FindExecutable(TEXT("definitely-not-installed-aiwi-probe"), FoundPath));
	}

	// --- 있는 것은 찾고, 돌려준 경로가 실제로 있어야 한다 ---
	{
		FString FoundPath;
#if PLATFORM_WINDOWS
		const TCHAR* KnownExecutable = TEXT("cmd");
#else
		const TCHAR* KnownExecutable = TEXT("sh");
#endif
		if (CliEnvironment::FindExecutable(KnownExecutable, FoundPath))
		{
			TestTrue(TEXT("돌려준 경로가 실제로 있다"), FPaths::FileExists(FoundPath));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("PATH에서 %s를 찾지 못해 '찾음' 쪽은 건너뛴다."), KnownExecutable));
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
