// AI Widget Inspector

#include "AI/AIWidgetProvider.h"

#include "Commands/AIWidgetCommandParser.h"

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * 프롬프트에 응답 형식이 들어가는지 확인한다.
 *
 * 이게 빠져도 아무것도 깨지지 않는다. 컴파일되고, 요청이 나가고, 답도 돌아온다.
 * 다만 그 답이 산문이라 파서를 통과하지 못하고, 화면에서는 "AI가 변경을 거부했다"처럼
 * 보인다. 실제로는 모델이 JSON으로 답해야 한다는 사실 자체를 못 들은 것이다.
 * 조용히 사라지는 연결이라 테스트로 붙들어 둔다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetRequestTest,
	"AIWidgetInspector.Request",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetRequestTest::RunTest(const FString& Parameters)
{
	const FString Context = TEXT("[Selected Widget]\nName: Txt_UpgradeLabel\n");

	// --- 변경 요청에는 응답 형식이 붙는다 ---
	{
		FAIWidgetRequest Request;
		Request.Kind = EAIWidgetRequestKind::ChangeRequest;
		Request.Context = Context;
		Request.UserMessage = TEXT("텍스트 색깔 이쁘게 바꿔줘");

		const FString Prompt = Request.BuildPrompt();

		TestTrue(TEXT("Context가 들어간다"), Prompt.Contains(TEXT("Txt_UpgradeLabel")));
		TestTrue(TEXT("사용자 요청이 들어간다"), Prompt.Contains(TEXT("텍스트 색깔 이쁘게 바꿔줘")));
		TestTrue(TEXT("변경 요청임을 알린다"), Prompt.Contains(TEXT("[Requested Change]")));

		// 형식 안내가 통째로 들어가야 한다. 한 줄만 우연히 겹치는 걸로는 부족하다.
		TestTrue(TEXT("응답 형식이 붙는다"), Prompt.Contains(TEXT("[Response Format]")));
		TestTrue(TEXT("changes 키를 알려준다"), Prompt.Contains(TEXT("\"changes\"")));
		TestTrue(
			TEXT("안내 전문이 그대로 들어간다"),
			Prompt.Contains(FAIWidgetCommandParser::GetSchemaInstructions()));
	}

	// --- 허용된 Operation이 빠짐없이 안내된다 ---
	//
	// 화이트리스트에 넣고 안내에 적지 않으면, 모델은 그 기능이 없는 줄 알고
	// "지원하지 않습니다"라고 답한다. 두 목록이 갈라지지 않게 여기서 묶어 둔다.
	{
		FAIWidgetRequest Request;
		Request.Kind = EAIWidgetRequestKind::ChangeRequest;

		const FString Prompt = Request.BuildPrompt();

		const EAIWidgetOperation AllowedOperations[] =
		{
			EAIWidgetOperation::SetVisibility,
			EAIWidgetOperation::SetEnabled,
			EAIWidgetOperation::SetText,
			EAIWidgetOperation::SetRenderOpacity,
			EAIWidgetOperation::SetRenderTranslation,
			EAIWidgetOperation::SetColorAndOpacity,
		};

		for (EAIWidgetOperation Operation : AllowedOperations)
		{
			const FString OperationName = FAIWidgetCommand::GetOperationName(Operation);

			TestTrue(
				*FString::Printf(TEXT("%s가 안내에 있다"), *OperationName),
				Prompt.Contains(OperationName));

			// 안내에 적힌 이름을 파서가 되읽을 수 있어야 한다. 오타가 나면
			// 모델은 시키는 대로 썼는데 거부당한다.
			EAIWidgetOperation RoundTripped = EAIWidgetOperation::None;
			TestTrue(
				*FString::Printf(TEXT("%s를 파서가 되읽는다"), *OperationName),
				FAIWidgetCommandParser::ParseOperation(OperationName, RoundTripped));
			TestEqual(
				*FString::Printf(TEXT("%s가 같은 Operation으로 돌아온다"), *OperationName),
				static_cast<int32>(RoundTripped),
				static_cast<int32>(Operation));
		}
	}

	// --- 단순 질문에는 형식 안내를 붙이지 않는다 ---
	//
	// 물어보기만 했는데 JSON을 요구하면, 설명을 기대한 자리에 기계용 응답이 온다.
	{
		FAIWidgetRequest Request;
		Request.Kind = EAIWidgetRequestKind::Question;
		Request.Context = Context;
		Request.UserMessage = TEXT("이 버튼이 왜 안 눌리지?");

		const FString Prompt = Request.BuildPrompt();

		TestTrue(TEXT("질문임을 알린다"), Prompt.Contains(TEXT("[User Question]")));
		TestFalse(TEXT("응답 형식은 붙지 않는다"), Prompt.Contains(TEXT("[Response Format]")));
	}

	// --- Context가 없어도 형식 안내는 살아있다 ---
	{
		FAIWidgetRequest Request;
		Request.Kind = EAIWidgetRequestKind::ChangeRequest;
		Request.UserMessage = TEXT("숨겨줘");

		const FString Prompt = Request.BuildPrompt();

		TestTrue(TEXT("응답 형식이 붙는다"), Prompt.Contains(TEXT("[Response Format]")));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
