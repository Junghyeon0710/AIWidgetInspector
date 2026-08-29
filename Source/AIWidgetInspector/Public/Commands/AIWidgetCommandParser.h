// AI Widget Inspector

#pragma once

#include "Commands/AIWidgetCommand.h"
#include "CoreMinimal.h"
#include "Serialization/JsonTypes.h"

class FJsonObject;

/**
 * AI 응답에서 변경 명령을 뽑아낸다.
 *
 * AI가 보낸 문자열을 코드로 실행하지 않는다. 정해진 JSON만 읽고, 허용된 Operation으로만
 * 옮겨 담는다. 모르는 Operation, 모르는 필드, 타입이 안 맞는 값은 전부 거부한다.
 *
 * 응답에 설명이 섞여 있는 걸 전제한다. 코드 펜스를 걷어내고 첫 JSON 오브젝트 하나만 읽는다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetCommandParser
{
public:
	/**
	 * @param OutCommands  읽어낸 명령. 일부만 읽혔어도 읽힌 것은 담긴다.
	 * @param OutErrors    항목별 실패 이유.
	 * @return             명령을 하나라도 읽었으면 true.
	 */
	static bool Parse(const FString& InResponse, TArray<FAIWidgetCommand>& OutCommands, TArray<FText>& OutErrors);

	/** AI에게 어떤 형식으로 답하라고 알려줄 지시문. 변경 요청 프롬프트 끝에 붙는다. */
	static FString GetSchemaInstructions();

private:
	/** 설명이 섞인 응답에서 JSON 오브젝트 하나를 잘라낸다. 문자열 안의 중괄호는 세지 않는다. */
	static bool ExtractJsonObject(const FString& InResponse, FString& OutJson);

	static bool ParseOperation(const FString& InOperationName, EAIWidgetOperation& OutOperation);

	/** Operation이 요구하는 JSON 타입인지. FJsonValue의 관대한 변환을 우회해 엄격하게 본다. */
	static bool HasExpectedValueType(EAIWidgetOperation InOperation, EJson InValueType);
	static bool ParseChange(const TSharedPtr<FJsonObject>& InChangeObject, int32 InIndex, FAIWidgetCommand& OutCommand, FText& OutError);
};
