// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Inspection/AIWidgetSourceResolver.h"

#include "Inspection/AIWidgetInspectionResult.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_TESTS

/**
 * 소스 리졸버는 조용히 실패하기 쉬운 코드다.
 *
 * 경로를 못 옮기거나 파일을 못 읽어도 빈 값을 돌려줄 뿐이라 UI에서는 그냥
 * "C++ 정보 없음"처럼 보인다. AI에게 코드가 안 갔는데도 답이 오기 때문에
 * 눈으로는 알아채기 어렵다. 그래서 remap과 snippet을 따로 확인한다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetSourceResolverTest,
	"AIWidgetInspector.SourceResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetSourceResolverTest::RunTest(const FString& Parameters)
{
	// --- 이 컴퓨터에 있는 경로는 그대로 통과한다 ---
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AIWidgetInspector"));
		if (!TestTrue(TEXT("플러그인을 찾을 수 있어야 한다"), Plugin.IsValid()))
		{
			return false;
		}

		const FString OwnHeader = Plugin->GetBaseDir() / TEXT("Source/AIWidgetInspector/Public/Inspection/AIWidgetSourceResolver.h");
		TestTrue(TEXT("테스트 대상 헤더가 실제로 있어야 한다"), FPaths::FileExists(OwnHeader));

		FString Resolved;
		TestTrue(TEXT("로컬 경로는 그대로 리졸브된다"), FAIWidgetSourceResolver::ResolveLocalPath(OwnHeader, Resolved));
		TestTrue(TEXT("리졸브 결과가 실제 파일이다"), FPaths::FileExists(Resolved));
	}

	// --- Epic 빌드 머신 경로를 로컬 엔진으로 옮긴다 ---
	//
	// 엔진 Slate Widget을 선택했을 때 실제로 들어오는 형태다. 이 경로는 이 컴퓨터에
	// 존재하지 않으므로, 앞부분을 버리고 로컬 EngineDir에 붙일 수 있어야 한다.
	{
		const FString BuildMachinePath = TEXT("D:/build/++UE5/Sync/Engine/Source/Runtime/SlateCore/Public/Widgets/SWidget.h");
		const FString LocalEquivalent = FPaths::EngineDir() / TEXT("Source/Runtime/SlateCore/Public/Widgets/SWidget.h");

		FString Resolved;
		const bool bResolved = FAIWidgetSourceResolver::ResolveLocalPath(BuildMachinePath, Resolved);

		// 소스가 없는 설치본(Launcher 바이너리)에서는 remap 대상 자체가 없다.
		// 그럴 때 실패하는 건 정상이므로, 파일이 있을 때만 성공을 요구한다.
		if (FPaths::FileExists(LocalEquivalent))
		{
			TestTrue(TEXT("빌드 머신 경로가 로컬 엔진으로 옮겨진다"), bResolved);
			TestTrue(TEXT("옮긴 경로가 실제 파일이다"), FPaths::FileExists(Resolved));
		}
		else
		{
			AddInfo(TEXT("엔진 소스가 설치돼 있지 않아 remap 확인을 건너뛴다."));
			TestFalse(TEXT("없는 파일을 있다고 하지 않는다"), bResolved);
		}
	}

	// --- 존재하지 않는 경로는 없다고 답한다 ---
	{
		FString Resolved;
		TestFalse(TEXT("빈 경로는 실패한다"), FAIWidgetSourceResolver::ResolveLocalPath(FString(), Resolved));
		TestFalse(
			TEXT("아무데도 없는 경로는 실패한다"),
			FAIWidgetSourceResolver::ResolveLocalPath(TEXT("Q:/nope/NotAFile.cpp"), Resolved));
	}

	// --- 생성 위치 주변만 잘라 오고, 그 줄에 표시를 남긴다 ---
	//
	// 임시 파일을 직접 만들어 확인한다. 실제 소스 파일에 의존하면 줄 번호가
	// 코드 수정에 따라 바뀌어 테스트가 엉뚱하게 깨진다.
	{
		const FString TempFile = FPaths::ProjectIntermediateDir() / TEXT("AIWidgetInspectorSnippetTest.txt");

		TArray<FString> Lines;
		for (int32 LineNumber = 1; LineNumber <= 100; ++LineNumber)
		{
			Lines.Add(FString::Printf(TEXT("line %d"), LineNumber));
		}

		if (!TestTrue(TEXT("임시 파일을 쓸 수 있어야 한다"), FFileHelper::SaveStringArrayToFile(Lines, *TempFile)))
		{
			return false;
		}

		FAIWidgetInspectionResult Inspection;
		Inspection.bIsValid = true;
		// FName의 Number가 라인, PlainName이 파일이다. SNew이 남기는 형식과 같다.
		Inspection.SlateCreatedIn = FName(*TempFile, 50);

		const FAIWidgetSourceInfo Info = FAIWidgetSourceResolver::Resolve(Inspection, nullptr);

		TestTrue(TEXT("파일을 찾았다"), Info.HasResolvedFile());
		TestTrue(TEXT("스니펫을 읽었다"), Info.HasSnippet());
		TestEqual(TEXT("생성 위치 위로 10줄부터"), Info.SnippetStartLine, 40);
		TestEqual(TEXT("생성 위치 아래로 10줄까지"), Info.SnippetEndLine, 60);

		// AI가 어느 줄인지 세지 않아도 되도록 표시가 붙어야 한다.
		TestTrue(TEXT("생성된 줄에 표시가 있다"), Info.Snippet.Contains(TEXT(">   50 | line 50")));
		TestTrue(TEXT("주변 줄에는 표시가 없다"), Info.Snippet.Contains(TEXT("    49 | line 49")));

		// 범위 밖은 들어오지 않는다. 프로젝트 코드를 통째로 보내지 않겠다는 약속이다.
		TestFalse(TEXT("범위 위쪽은 빠진다"), Info.Snippet.Contains(TEXT("line 39")));
		TestFalse(TEXT("범위 아래쪽은 빠진다"), Info.Snippet.Contains(TEXT("line 61")));

		IFileManager::Get().Delete(*TempFile);
	}

	// --- 파일 끝 근처에서도 범위를 넘지 않는다 ---
	{
		const FString TempFile = FPaths::ProjectIntermediateDir() / TEXT("AIWidgetInspectorSnippetEdgeTest.txt");

		TArray<FString> Lines;
		for (int32 LineNumber = 1; LineNumber <= 5; ++LineNumber)
		{
			Lines.Add(FString::Printf(TEXT("line %d"), LineNumber));
		}

		if (TestTrue(TEXT("임시 파일을 쓸 수 있어야 한다"), FFileHelper::SaveStringArrayToFile(Lines, *TempFile)))
		{
			FAIWidgetInspectionResult Inspection;
			Inspection.bIsValid = true;
			Inspection.SlateCreatedIn = FName(*TempFile, 2);

			const FAIWidgetSourceInfo Info = FAIWidgetSourceResolver::Resolve(Inspection, nullptr);

			TestTrue(TEXT("짧은 파일도 읽는다"), Info.HasSnippet());
			TestEqual(TEXT("첫 줄 아래로 내려가지 않는다"), Info.SnippetStartLine, 1);
			TestEqual(TEXT("마지막 줄을 넘지 않는다"), Info.SnippetEndLine, 5);

			// 파일에 없는 줄을 요구하면 스니펫은 없어야 한다.
			Inspection.SlateCreatedIn = FName(*TempFile, 999);
			const FAIWidgetSourceInfo OutOfRange = FAIWidgetSourceResolver::Resolve(Inspection, nullptr);
			TestTrue(TEXT("파일은 찾는다"), OutOfRange.HasResolvedFile());
			TestFalse(TEXT("없는 줄이면 스니펫을 만들지 않는다"), OutOfRange.HasSnippet());

			IFileManager::Get().Delete(*TempFile);
		}
	}

	// --- 유효하지 않은 검사 결과는 아무것도 만들지 않는다 ---
	{
		FAIWidgetInspectionResult Invalid;
		const FAIWidgetSourceInfo Info = FAIWidgetSourceResolver::Resolve(Invalid, nullptr);

		TestFalse(TEXT("파일 없음"), Info.HasResolvedFile());
		TestFalse(TEXT("스니펫 없음"), Info.HasSnippet());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
