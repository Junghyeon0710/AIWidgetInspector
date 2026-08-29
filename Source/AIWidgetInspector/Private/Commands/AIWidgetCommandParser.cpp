// AI Widget Inspector

#include "Commands/AIWidgetCommandParser.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FAIWidgetCommandParser"

namespace AIWidgetCommandParserPrivate
{
	static const TCHAR* ChangesFieldName = TEXT("changes");
	static const TCHAR* OperationFieldName = TEXT("operation");
	static const TCHAR* TargetWidgetFieldName = TEXT("target_widget");
	static const TCHAR* ValueFieldName = TEXT("value");
}

FString FAIWidgetCommandParser::GetSchemaInstructions()
{
	return TEXT(
		"[Response Format]\n"
		"Answer with the JSON below. Prose outside the JSON is fine, but there must be exactly one JSON block.\n"
		"\n"
		"{\n"
		"  \"changes\": [\n"
		"    { \"operation\": \"SetVisibility\",        \"target_widget\": \"Btn_Upgrade\", \"value\": \"Collapsed\" },\n"
		"    { \"operation\": \"SetEnabled\",           \"target_widget\": \"Btn_Upgrade\", \"value\": true },\n"
		"    { \"operation\": \"SetText\",              \"target_widget\": \"Txt_Label\",   \"value\": \"Upgrade\" },\n"
		"    { \"operation\": \"SetRenderOpacity\",     \"target_widget\": \"Btn_Upgrade\", \"value\": 0.5 },\n"
		"    { \"operation\": \"SetRenderTranslation\", \"target_widget\": \"Btn_Upgrade\", \"value\": { \"x\": 30, \"y\": 0 } },\n"
		"    { \"operation\": \"SetColorAndOpacity\",   \"target_widget\": \"Txt_Label\",   \"value\": \"#4FC3F7\" }\n"
		"  ]\n"
		"}\n"
		"\n"
		"Rules:\n"
		"- Only the six operations above are allowed. Anything else is not executed.\n"
		"- target_widget must be one of the UMG names shown in the Widget Path above.\n"
		"- The operation decides the type of value.\n"
		"  Visibility: one of Visible / Collapsed / Hidden / HitTestInvisible / SelfHitTestInvisible, as a string.\n"
		"  Enabled: boolean. Text: string. RenderOpacity: number 0 to 1. RenderTranslation: object with numeric x and y.\n"
		"  ColorAndOpacity: \"#RRGGBB\" or \"#RRGGBBAA\", read as sRGB.\n"
		"  ColorAndOpacity applies to TextBlock, Image, Button and UserWidget only. Other types are rejected.\n"
		"- Resizing or repositioning through a Slot is not supported yet. For those, return an empty changes list and explain why.\n");
}


bool FAIWidgetCommandParser::ExtractJsonObject(const FString& InResponse, FString& OutJson)
{
	int32 StartIndex = INDEX_NONE;
	if (!InResponse.FindChar(TEXT('{'), StartIndex))
	{
		return false;
	}

	// 중괄호 깊이를 세면서 짝을 찾는다. 문자열 리터럴 안의 중괄호와 이스케이프는 건너뛴다.
	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;

	for (int32 Index = StartIndex; Index < InResponse.Len(); ++Index)
	{
		const TCHAR Character = InResponse[Index];

		if (bInString)
		{
			if (bEscaped)
			{
				bEscaped = false;
			}
			else if (Character == TEXT('\\'))
			{
				bEscaped = true;
			}
			else if (Character == TEXT('"'))
			{
				bInString = false;
			}

			continue;
		}

		if (Character == TEXT('"'))
		{
			bInString = true;
		}
		else if (Character == TEXT('{'))
		{
			++Depth;
		}
		else if (Character == TEXT('}'))
		{
			--Depth;
			if (Depth == 0)
			{
				OutJson = InResponse.Mid(StartIndex, Index - StartIndex + 1);
				return true;
			}
		}
	}

	return false;
}

bool FAIWidgetCommandParser::ParseOperation(const FString& InOperationName, EAIWidgetOperation& OutOperation)
{
	// 화이트리스트를 이름으로 되짚는다. 여기 없는 이름은 전부 거부된다.
	static const EAIWidgetOperation AllowedOperations[] =
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
		if (InOperationName.Equals(FAIWidgetCommand::GetOperationName(Operation), ESearchCase::IgnoreCase))
		{
			OutOperation = Operation;
			return true;
		}
	}

	return false;
}

bool FAIWidgetCommandParser::HasExpectedValueType(EAIWidgetOperation InOperation, EJson InValueType)
{
	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:
	case EAIWidgetOperation::SetText:
	case EAIWidgetOperation::SetColorAndOpacity:
		return InValueType == EJson::String;

	case EAIWidgetOperation::SetEnabled:
		return InValueType == EJson::Boolean;

	case EAIWidgetOperation::SetRenderOpacity:
		return InValueType == EJson::Number;

	case EAIWidgetOperation::SetRenderTranslation:
		return InValueType == EJson::Object;

	default:
		return false;
	}
}

bool FAIWidgetCommandParser::ParseChange(const TSharedPtr<FJsonObject>& InChangeObject, int32 InIndex, FAIWidgetCommand& OutCommand, FText& OutError)
{
	using namespace AIWidgetCommandParserPrivate;

	if (!InChangeObject.IsValid())
	{
		OutError = FText::Format(LOCTEXT("NotAnObject", "changes[{0}] is not an object."), FText::AsNumber(InIndex));
		return false;
	}

	FString OperationName;
	if (!InChangeObject->TryGetStringField(OperationFieldName, OperationName))
	{
		OutError = FText::Format(LOCTEXT("NoOperation", "changes[{0}] has no operation."), FText::AsNumber(InIndex));
		return false;
	}

	if (!ParseOperation(OperationName, OutCommand.Operation))
	{
		OutError = FText::Format(
			LOCTEXT("UnknownOperation", "changes[{0}]: operation '{1}' is not allowed."),
			FText::AsNumber(InIndex),
			FText::FromString(OperationName));
		return false;
	}

	FString TargetWidgetName;
	if (!InChangeObject->TryGetStringField(TargetWidgetFieldName, TargetWidgetName) || TargetWidgetName.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("NoTarget", "changes[{0}] has no target_widget."), FText::AsNumber(InIndex));
		return false;
	}

	OutCommand.TargetWidgetName = FName(*TargetWidgetName);

	const TSharedPtr<FJsonValue> ValueField = InChangeObject->TryGetField(ValueFieldName);
	if (!ValueField.IsValid())
	{
		OutError = FText::Format(LOCTEXT("NoValue", "changes[{0}] has no value."), FText::AsNumber(InIndex));
		return false;
	}

	// value의 타입은 operation이 정한다.
	//
	// FJsonValue는 타입 변환에 관대해서 문자열 "true"도 TryGetBool을 통과한다.
	// 그대로 두면 AI가 "yes"를 보냈을 때 ToBool()이 false가 되어 요청과 반대로 적용된다.
	// 그래서 값을 읽기 전에 JSON 타입 자체를 먼저 확인한다.
	if (!HasExpectedValueType(OutCommand.Operation, ValueField->Type))
	{
		OutError = FText::Format(
			LOCTEXT("WrongValueType", "changes[{0}]: wrong value type for {1}."),
			FText::AsNumber(InIndex),
			FText::FromString(FAIWidgetCommand::GetOperationName(OutCommand.Operation)));
		return false;
	}

	switch (OutCommand.Operation)
	{
	case EAIWidgetOperation::SetVisibility:
	{
		FString VisibilityName;
		if (!ValueField->TryGetString(VisibilityName))
		{
			OutError = FText::Format(LOCTEXT("VisibilityNotString", "changes[{0}]: Visibility value must be a string."), FText::AsNumber(InIndex));
			return false;
		}

		const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
		const int64 EnumValue = VisibilityEnum ? VisibilityEnum->GetValueByNameString(VisibilityName) : INDEX_NONE;
		if (EnumValue == INDEX_NONE)
		{
			OutError = FText::Format(
				LOCTEXT("UnknownVisibility", "changes[{0}]: unknown Visibility '{1}'."),
				FText::AsNumber(InIndex),
				FText::FromString(VisibilityName));
			return false;
		}

		OutCommand.Visibility = static_cast<ESlateVisibility>(EnumValue);
		return true;
	}

	case EAIWidgetOperation::SetEnabled:
	{
		bool bValue = false;
		if (!ValueField->TryGetBool(bValue))
		{
			OutError = FText::Format(LOCTEXT("EnabledNotBool", "changes[{0}]: Enabled value must be true or false."), FText::AsNumber(InIndex));
			return false;
		}

		OutCommand.bEnabled = bValue;
		return true;
	}

	case EAIWidgetOperation::SetText:
	{
		FString TextValue;
		if (!ValueField->TryGetString(TextValue))
		{
			OutError = FText::Format(LOCTEXT("TextNotString", "changes[{0}]: Text value must be a string."), FText::AsNumber(InIndex));
			return false;
		}

		OutCommand.Text = FText::FromString(TextValue);
		return true;
	}

	case EAIWidgetOperation::SetRenderOpacity:
	{
		double OpacityValue = 0.0;
		if (!ValueField->TryGetNumber(OpacityValue))
		{
			OutError = FText::Format(LOCTEXT("OpacityNotNumber", "changes[{0}]: RenderOpacity value must be a number."), FText::AsNumber(InIndex));
			return false;
		}

		OutCommand.RenderOpacity = static_cast<float>(OpacityValue);
		return true;
	}

	case EAIWidgetOperation::SetRenderTranslation:
	{
		const TSharedPtr<FJsonObject>* TranslationObject = nullptr;
		if (!ValueField->TryGetObject(TranslationObject) || !TranslationObject || !TranslationObject->IsValid())
		{
			OutError = FText::Format(LOCTEXT("TranslationNotObject", "changes[{0}]: RenderTranslation value must be an object with x and y."), FText::AsNumber(InIndex));
			return false;
		}

		double TranslationX = 0.0;
		double TranslationY = 0.0;
		if (!(*TranslationObject)->TryGetNumberField(TEXT("x"), TranslationX)
			|| !(*TranslationObject)->TryGetNumberField(TEXT("y"), TranslationY))
		{
			OutError = FText::Format(LOCTEXT("TranslationMissingAxis", "changes[{0}]: RenderTranslation is missing x or y."), FText::AsNumber(InIndex));
			return false;
		}

		OutCommand.RenderTranslation = FVector2D(TranslationX, TranslationY);
		return true;
	}

	case EAIWidgetOperation::SetColorAndOpacity:
	{
		FString HexValue;
		if (!ValueField->TryGetString(HexValue))
		{
			OutError = FText::Format(LOCTEXT("ColorNotString", "changes[{0}]: ColorAndOpacity value must be a string."), FText::AsNumber(InIndex));
			return false;
		}

		if (!FAIWidgetCommand::ParseHexColor(HexValue, OutCommand.ColorAndOpacity))
		{
			OutError = FText::Format(
				LOCTEXT("ColorNotHex", "changes[{0}]: '{1}' is not a colour. Use #RRGGBB or #RRGGBBAA."),
				FText::AsNumber(InIndex),
				FText::FromString(HexValue));
			return false;
		}

		return true;
	}

	default:
		OutError = FText::Format(LOCTEXT("UnhandledOperation", "changes[{0}]: operation cannot be handled."), FText::AsNumber(InIndex));
		return false;
	}
}

bool FAIWidgetCommandParser::Parse(const FString& InResponse, TArray<FAIWidgetCommand>& OutCommands, TArray<FText>& OutErrors)
{
	using namespace AIWidgetCommandParserPrivate;

	OutCommands.Reset();
	OutErrors.Reset();

	FString JsonText;
	if (!ExtractJsonObject(InResponse, JsonText))
	{
		OutErrors.Add(LOCTEXT("NoJson", "No JSON object was found in the reply."));
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
	{
		OutErrors.Add(LOCTEXT("BadJson", "The JSON could not be parsed."));
		return false;
	}

	// "changes" 배열이 정식이지만, 변경 하나만 오브젝트로 보내는 응답도 받아준다.
	TArray<TSharedPtr<FJsonValue>> ChangeValues;
	const TArray<TSharedPtr<FJsonValue>>* ChangesArray = nullptr;
	if (RootObject->TryGetArrayField(ChangesFieldName, ChangesArray) && ChangesArray)
	{
		ChangeValues = *ChangesArray;
	}
	else if (RootObject->HasField(OperationFieldName))
	{
		ChangeValues.Add(MakeShared<FJsonValueObject>(RootObject));
	}
	else
	{
		OutErrors.Add(LOCTEXT("NoChanges", "The JSON has no changes array."));
		return false;
	}

	if (ChangeValues.IsEmpty())
	{
		OutErrors.Add(LOCTEXT("EmptyChanges", "changes is empty. The assistant may have decided there was nothing to change."));
		return false;
	}

	for (int32 Index = 0; Index < ChangeValues.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* ChangeObject = nullptr;
		if (!ChangeValues[Index].IsValid() || !ChangeValues[Index]->TryGetObject(ChangeObject) || !ChangeObject)
		{
			OutErrors.Add(FText::Format(LOCTEXT("ChangeNotObject", "changes[{0}] is not an object."), FText::AsNumber(Index)));
			continue;
		}

		FAIWidgetCommand Command;
		FText Error;
		if (ParseChange(*ChangeObject, Index, Command, Error))
		{
			OutCommands.Add(MoveTemp(Command));
		}
		else
		{
			OutErrors.Add(Error);
		}
	}

	return !OutCommands.IsEmpty();
}

#undef LOCTEXT_NAMESPACE
