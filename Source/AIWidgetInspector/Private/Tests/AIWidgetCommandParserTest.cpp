// AI Widget Inspector

#include "Commands/AIWidgetCommandParser.h"

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

/**
 * 파서는 AI가 보낸 문자열을 그대로 믿지 않는 첫 관문이다.
 * 여기서 막지 못하면 뒤의 검사기와 실행기가 잘못된 명령을 받게 되므로,
 * 정상 입력만이 아니라 거부해야 할 입력들을 같이 확인한다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetCommandParserTest,
	"AIWidgetInspector.CommandParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetCommandParserTest::RunTest(const FString& Parameters)
{
	// --- 설명과 코드 펜스에 둘러싸인 JSON을 찾아낸다 ---
	{
		const FString Response = TEXT(
			"버튼이 안 눌리는 건 RenderOpacity 때문입니다.\n"
			"```json\n"
			"{ \"changes\": [ { \"operation\": \"SetRenderOpacity\", \"target_widget\": \"Btn_Upgrade\", \"value\": 0.5 } ] }\n"
			"```\n"
			"적용해 보세요. { 이건 JSON이 아닙니다 }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestTrue(TEXT("설명에 둘러싸인 JSON을 읽어야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("명령 1건"), Commands.Num(), 1);
		TestEqual(TEXT("오류 없음"), Errors.Num(), 0);

		if (Commands.Num() == 1)
		{
			TestEqual(TEXT("Operation"), static_cast<int32>(Commands[0].Operation), static_cast<int32>(EAIWidgetOperation::SetRenderOpacity));
			TestEqual(TEXT("대상 이름"), Commands[0].TargetWidgetName, FName(TEXT("Btn_Upgrade")));
			TestEqual(TEXT("값"), Commands[0].RenderOpacity, 0.5f);
		}
	}

	// --- 다섯 가지 Operation을 모두 읽는다 ---
	{
		const FString Response = TEXT(
			"{ \"changes\": ["
			"  { \"operation\": \"SetVisibility\",        \"target_widget\": \"A\", \"value\": \"Collapsed\" },"
			"  { \"operation\": \"SetEnabled\",           \"target_widget\": \"B\", \"value\": false },"
			"  { \"operation\": \"SetText\",              \"target_widget\": \"C\", \"value\": \"Ultra\" },"
			"  { \"operation\": \"SetRenderOpacity\",     \"target_widget\": \"D\", \"value\": 0.25 },"
			"  { \"operation\": \"SetRenderTranslation\", \"target_widget\": \"E\", \"value\": { \"x\": 30, \"y\": -10 } }"
			"] }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestTrue(TEXT("다섯 건 모두 읽어야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("명령 5건"), Commands.Num(), 5);
		TestEqual(TEXT("오류 없음"), Errors.Num(), 0);

		if (Commands.Num() == 5)
		{
			TestEqual(TEXT("Visibility"), static_cast<int32>(Commands[0].Visibility), static_cast<int32>(ESlateVisibility::Collapsed));
			TestFalse(TEXT("Enabled"), Commands[1].bEnabled);
			TestEqual(TEXT("Text"), Commands[2].Text.ToString(), FString(TEXT("Ultra")));
			TestEqual(TEXT("Opacity"), Commands[3].RenderOpacity, 0.25f);
			TestEqual(TEXT("Translation X"), Commands[4].RenderTranslation.X, 30.0);
			TestEqual(TEXT("Translation Y"), Commands[4].RenderTranslation.Y, -10.0);
		}
	}

	// --- 화이트리스트 밖의 Operation은 거부한다 ---
	{
		const FString Response = TEXT(
			"{ \"changes\": ["
			"  { \"operation\": \"DeleteEverything\", \"target_widget\": \"A\", \"value\": 1 },"
			"  { \"operation\": \"SetProperty\",      \"target_widget\": \"A\", \"value\": 1 },"
			"  { \"operation\": \"SetEnabled\",       \"target_widget\": \"A\", \"value\": true }"
			"] }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		FAIWidgetCommandParser::Parse(Response, Commands, Errors);

		TestEqual(TEXT("허용된 1건만 통과"), Commands.Num(), 1);
		TestEqual(TEXT("거부 2건"), Errors.Num(), 2);

		if (Commands.Num() == 1)
		{
			TestEqual(TEXT("통과한 것은 SetEnabled"), static_cast<int32>(Commands[0].Operation), static_cast<int32>(EAIWidgetOperation::SetEnabled));
		}
	}

	// --- value 타입이 Operation과 안 맞으면 거부한다 ---
	{
		const FString Response = TEXT(
			"{ \"changes\": ["
			"  { \"operation\": \"SetRenderOpacity\",     \"target_widget\": \"A\", \"value\": \"반투명하게\" },"
			"  { \"operation\": \"SetEnabled\",           \"target_widget\": \"A\", \"value\": \"true\" },"
			"  { \"operation\": \"SetVisibility\",        \"target_widget\": \"A\", \"value\": \"Invisible\" },"
			"  { \"operation\": \"SetRenderTranslation\", \"target_widget\": \"A\", \"value\": 30 },"
			"  { \"operation\": \"SetRenderTranslation\", \"target_widget\": \"A\", \"value\": { \"x\": 30 } }"
			"] }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestFalse(TEXT("전부 거부되어야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("통과 0건"), Commands.Num(), 0);
		TestEqual(TEXT("거부 5건"), Errors.Num(), 5);
	}

	// --- 필수 필드가 빠지면 거부한다 ---
	{
		const FString Response = TEXT(
			"{ \"changes\": ["
			"  { \"target_widget\": \"A\", \"value\": true },"
			"  { \"operation\": \"SetEnabled\", \"value\": true },"
			"  { \"operation\": \"SetEnabled\", \"target_widget\": \"A\" }"
			"] }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestFalse(TEXT("전부 거부되어야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("거부 3건"), Errors.Num(), 3);
	}

	// --- JSON이 아예 없거나 깨졌으면 이유를 남긴다 ---
	{
		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;

		TestFalse(TEXT("JSON 없음"), FAIWidgetCommandParser::Parse(TEXT("버튼을 좀 더 크게 만들면 좋겠습니다."), Commands, Errors));
		TestEqual(TEXT("이유 1건"), Errors.Num(), 1);

		Errors.Reset();
		TestFalse(TEXT("깨진 JSON"), FAIWidgetCommandParser::Parse(TEXT("{ \"changes\": [ { \"operation\": } ] }"), Commands, Errors));
		TestEqual(TEXT("이유 1건"), Errors.Num(), 1);

		Errors.Reset();
		TestFalse(TEXT("빈 changes"), FAIWidgetCommandParser::Parse(TEXT("{ \"changes\": [] }"), Commands, Errors));
		TestEqual(TEXT("이유 1건"), Errors.Num(), 1);
	}

	// --- changes 배열 없이 변경 하나만 보내는 응답도 받아준다 ---
	{
		const FString Response = TEXT("{ \"operation\": \"SetEnabled\", \"target_widget\": \"Btn\", \"value\": false }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestTrue(TEXT("단일 오브젝트도 읽어야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("명령 1건"), Commands.Num(), 1);
	}

	// --- 문자열 안의 중괄호에 속지 않는다 ---
	{
		const FString Response = TEXT(
			"{ \"changes\": [ { \"operation\": \"SetText\", \"target_widget\": \"A\", \"value\": \"} 닫는 괄호 { 포함\" } ] }"
			" 뒤에 붙은 설명 }");

		TArray<FAIWidgetCommand> Commands;
		TArray<FText> Errors;
		TestTrue(TEXT("문자열 안 중괄호를 무시해야 한다"), FAIWidgetCommandParser::Parse(Response, Commands, Errors));
		TestEqual(TEXT("명령 1건"), Commands.Num(), 1);

		if (Commands.Num() == 1)
		{
			TestEqual(TEXT("값이 온전해야 한다"), Commands[0].Text.ToString(), FString(TEXT("} 닫는 괄호 { 포함")));
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
