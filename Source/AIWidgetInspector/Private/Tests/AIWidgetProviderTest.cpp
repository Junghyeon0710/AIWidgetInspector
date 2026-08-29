// AI Widget Inspector

#include "AI/AICliProvider.h"

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * 못 쓰는 Provider가 이유를 말하는지.
 *
 * 이건 설치가 멀쩡한 컴퓨터에서는 눈으로 확인할 수 없다. CLI가 다 깔려 있으면 경고가
 * 아예 안 뜨기 때문이다. 그래서 없는 실행 파일을 가리키는 Provider를 만들어 확인한다.
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
	// --- 없는 CLI는 없다고 말하고, 설치 방법까지 알려 준다 ---
	{
		FAICliProvider::FConfig Config;
		Config.Name = TEXT("TestMissing");
		Config.DisplayName = FText::FromString(TEXT("없는 도구"));
		Config.Executable = TEXT("definitely-not-installed-aiwi-probe");
		Config.InstallCommand = TEXT("npm install -g example-package");

		const FAICliProvider Provider(Config);

		TestFalse(TEXT("쓸 수 없다고 답한다"), Provider.IsAvailable());

		const FText Reason = Provider.GetUnavailableReason();
		TestFalse(TEXT("이유가 비어 있으면 안 된다"), Reason.IsEmpty());

		const FString ReasonText = Reason.ToString();
		TestTrue(
			TEXT("무엇이 없는지 말한다"),
			ReasonText.Contains(TEXT("definitely-not-installed-aiwi-probe")));
		TestTrue(
			TEXT("어떻게 설치하는지 말한다"),
			ReasonText.Contains(TEXT("npm install -g example-package")));
	}

	// --- 설치 명령을 안 적어 뒀어도 이유는 나온다 ---
	//
	// 안내가 없다고 침묵하면 가장 중요한 사실(그 도구가 없다는 것)까지 사라진다.
	{
		FAICliProvider::FConfig Config;
		Config.Name = TEXT("TestMissingNoHint");
		Config.DisplayName = FText::FromString(TEXT("힌트 없는 도구"));
		Config.Executable = TEXT("definitely-not-installed-aiwi-probe");

		const FAICliProvider Provider(Config);

		TestFalse(TEXT("쓸 수 없다"), Provider.IsAvailable());
		TestFalse(TEXT("그래도 이유는 있다"), Provider.GetUnavailableReason().IsEmpty());
	}

	// --- 설치돼 있으면 조용하다 ---
	//
	// 멀쩡한데도 경고가 뜨면 사용자는 곧 경고를 무시하기 시작한다.
	{
		FString FoundPath;
		if (FAICliProvider::FindExecutable(TEXT("cmd"), FoundPath))
		{
			FAICliProvider::FConfig Config;
			Config.Name = TEXT("TestPresent");
			Config.DisplayName = FText::FromString(TEXT("있는 도구"));
			Config.Executable = TEXT("cmd");

			const FAICliProvider Provider(Config);

			TestTrue(TEXT("쓸 수 있다"), Provider.IsAvailable());
			TestTrue(TEXT("이유 줄이 비어 있다"), Provider.GetUnavailableReason().IsEmpty());
		}
		else
		{
			AddInfo(TEXT("PATH에서 cmd를 찾지 못해 '설치됨' 쪽은 건너뛴다."));
		}
	}

	// --- MCP 모드는 서버가 꺼져 있으면 못 쓴다고 말한다 ---
	//
	// 실행 파일만 보고 쓸 수 있다고 하면, 보낼 수 있는 줄 알고 눌렀다가
	// 타임아웃까지 기다리게 된다. 실패가 3분 뒤에 오는 것이 최악이다.
	{
		FString FoundPath;
		if (FAICliProvider::FindExecutable(TEXT("cmd"), FoundPath))
		{
			FAICliProvider::FConfig Config;
			Config.Name = TEXT("TestMcp");
			Config.DisplayName = FText::FromString(TEXT("MCP 도구"));
			Config.Executable = TEXT("cmd");
			Config.bUseUnrealMcp = true;

			const FAICliProvider Provider(Config);

			// 이 프로젝트에서 서버가 켜져 있는지에 따라 답이 갈린다. 둘 중 어느
			// 쪽이든, 쓸 수 없다고 답했으면 반드시 이유가 붙어 있어야 한다.
			if (!Provider.IsAvailable())
			{
				const FText Reason = Provider.GetUnavailableReason();
				TestFalse(TEXT("MCP가 꺼져 있으면 이유를 말한다"), Reason.IsEmpty());
				TestTrue(
					TEXT("어디서 켜는지 알려 준다"),
					Reason.ToString().Contains(TEXT("Model Context Protocol")));
			}
			else
			{
				AddInfo(TEXT("이 프로젝트는 MCP 서버가 켜져 있어 꺼진 경우는 확인하지 않는다."));
				TestTrue(TEXT("쓸 수 있으면 이유는 비어 있다"), Provider.GetUnavailableReason().IsEmpty());
			}
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
