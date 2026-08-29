// AI Widget Inspector

#include "Mcp/AIWidgetMcpTools.h"

#include "AIWidgetInspectorLog.h"
#include "AIWidgetInspectorModule.h"
#include "Commands/AIWidgetCommand.h"
#include "Commands/AIWidgetCommandParser.h"
#include "Commands/AIWidgetCommandValidator.h"
#include "Commands/AIWidgetPersistentApplier.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetInspectionResult.h"
#include "Inspection/AIWidgetInspector.h"
#include "Inspection/AIWidgetSelection.h"
#include "Inspection/AIWidgetSourceResolver.h"
#include "AI/AIWidgetContextBuilder.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "BaseWidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/StringBuilder.h"

#define LOCTEXT_NAMESPACE "FAIWidgetMcpTools"

namespace AIWidgetMcpToolsPrivate
{
	static const TCHAR* ToolGetSelectedWidget = TEXT("get_selected_widget");
	static const TCHAR* ToolListWidgetTree    = TEXT("list_widget_tree");
	static const TCHAR* ToolPreviewChange     = TEXT("preview_widget_change");
	static const TCHAR* ToolApplyToAsset      = TEXT("apply_widget_change_to_asset");
	static const TCHAR* ToolRevertPreview     = TEXT("revert_preview");
	static const TCHAR* ToolSaveAsset         = TEXT("save_widget_asset");

	/** 문자열 배열을 JSON 스키마의 enum 값으로. */
	static TArray<TSharedPtr<FJsonValue>> MakeStringEnum(std::initializer_list<const TCHAR*> InValues)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Value : InValues)
		{
			Values.Add(MakeShared<FJsonValueString>(Value));
		}
		return Values;
	}

	static TSharedPtr<FJsonObject> MakeProperty(const TCHAR* InType, const FString& InDescription)
	{
		TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), InType);
		Property->SetStringField(TEXT("description"), InDescription);
		return Property;
	}

	static TSharedPtr<FJsonObject> MakeTool(
		const TCHAR* InName,
		const FString& InDescription,
		const TSharedPtr<FJsonObject>& InProperties,
		const TArray<FString>& InRequired)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), InProperties.IsValid() ? InProperties : MakeShared<FJsonObject>());

		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const FString& Required : InRequired)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(Required));
		}
		Schema->SetArrayField(TEXT("required"), RequiredValues);

		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), InName);
		Tool->SetStringField(TEXT("description"), InDescription);
		Tool->SetObjectField(TEXT("inputSchema"), Schema);
		return Tool;
	}

	/** 변경 Tool 두 개가 같은 인자를 받는다. 스키마도 한 곳에서 만든다. */
	static TSharedPtr<FJsonObject> MakeChangeProperties()
	{
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
		Operation->SetStringField(TEXT("type"), TEXT("string"));
		Operation->SetStringField(TEXT("description"), TEXT("바꿀 속성. 여기 있는 것만 실행된다."));
		Operation->SetArrayField(TEXT("enum"), MakeStringEnum({
			TEXT("SetVisibility"),
			TEXT("SetEnabled"),
			TEXT("SetText"),
			TEXT("SetRenderOpacity"),
			TEXT("SetRenderTranslation"),
			TEXT("SetColorAndOpacity"),
		}));
		Properties->SetObjectField(TEXT("operation"), Operation);

		Properties->SetObjectField(TEXT("target_widget"), MakeProperty(
			TEXT("string"),
			TEXT("대상 Widget 이름. list_widget_tree 가 돌려준 이름 중 하나여야 한다.")));

		// value는 Operation마다 타입이 다르다. JSON Schema로 그걸 표현하면 oneOf가 길어지는데,
		// 어차피 실제 검사는 ParseChange가 하므로 여기서는 설명으로 알려준다.
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("description"), TEXT(
			"operation이 정하는 타입. "
			"SetVisibility=문자열(Visible/Collapsed/Hidden/HitTestInvisible/SelfHitTestInvisible), "
			"SetEnabled=불리언, "
			"SetText=문자열, "
			"SetRenderOpacity=0~1 숫자, "
			"SetRenderTranslation={\"x\":숫자,\"y\":숫자}, "
			"SetColorAndOpacity=\"#RRGGBB\" 또는 \"#RRGGBBAA\" 문자열(sRGB)"));
		Properties->SetObjectField(TEXT("value"), Value);

		return Properties;
	}
}

FAIWidgetMcpTools::FAIWidgetMcpTools(
	TSharedRef<FAIWidgetSelection> InSelection,
	TSharedRef<FAIWidgetRuntimePreview> InRuntimePreview)
	: Selection(MoveTemp(InSelection))
	, RuntimePreview(MoveTemp(InRuntimePreview))
{
}

bool FAIWidgetMcpTools::HasSelection() const
{
	return Selection->IsValid();
}

FAIWidgetInspectionResult FAIWidgetMcpTools::InspectSelection() const
{
	return FAIWidgetInspector::Inspect(Selection->GetSelectedWidget());
}

TArray<TSharedPtr<FJsonObject>> FAIWidgetMcpTools::BuildToolDefinitions() const
{
	using namespace AIWidgetMcpToolsPrivate;

	TArray<TSharedPtr<FJsonObject>> Tools;

	Tools.Add(MakeTool(
		ToolGetSelectedWidget,
		TEXT("에디터에서 지금 선택된 Widget의 정보를 돌려준다. 타입, 상태, 배치, 소속 Blueprint, 만들어진 C++ 위치까지 포함한다. 무엇을 바꿀지 정하기 전에 먼저 부른다."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolListWidgetTree,
		TEXT("선택된 Widget이 속한 UserWidget 안의 Widget 이름과 타입을 모두 돌려준다. target_widget에 넣을 수 있는 이름은 이 목록에 있는 것뿐이다."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolPreviewChange,
		TEXT("화면에 떠 있는 Widget 인스턴스에 변경을 적용한다. 에셋 파일은 바뀌지 않고 revert_preview로 되돌릴 수 있다. 먼저 이걸로 보여준 뒤 사용자가 만족하면 에셋에 쓴다."),
		MakeChangeProperties(),
		{ TEXT("operation"), TEXT("target_widget"), TEXT("value") }));

	Tools.Add(MakeTool(
		ToolApplyToAsset,
		TEXT("Widget Blueprint 에셋 원본에 변경을 쓴다. 되돌리려면 에디터에서 Ctrl+Z를 눌러야 한다. 저장은 하지 않으므로 사용자가 직접 저장해야 최종 반영된다. 사용자가 분명히 요청했을 때만 부른다."),
		MakeChangeProperties(),
		{ TEXT("operation"), TEXT("target_widget"), TEXT("value") }));

	Tools.Add(MakeTool(
		ToolSaveAsset,
		TEXT("apply_widget_change_to_asset로 바꾼 Widget Blueprint를 디스크에 저장한다. 저장하고 나면 에디터를 닫아도 남는다. 사용자가 저장해 달라고 말했을 때만 부른다."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolRevertPreview,
		TEXT("preview_widget_change로 적용한 모든 변경을 처음 값으로 되돌린다."),
		MakeShared<FJsonObject>(),
		{}));

	return Tools;
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::Call(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments)
{
	using namespace AIWidgetMcpToolsPrivate;

	// Widget을 만지는 일은 전부 게임 스레드에서만 해야 한다. HTTP 핸들러가 게임 스레드에서
	// 돌기 때문에 여기까지 그대로 오지만, 그 전제가 깨지면 조용히 망가지므로 확인해 둔다.
	if (!ensureMsgf(IsInGameThread(), TEXT("MCP Tool은 게임 스레드에서만 실행되어야 한다.")))
	{
		return FAIWidgetMcpToolResult::Error(TEXT("Tool을 게임 스레드 밖에서 실행할 수 없습니다."));
	}

	const FAIWidgetMcpToolResult Result = Dispatch(InToolName, InArguments);

	// 호출 사실만 남기면, 거부된 호출이 로그에서 성공한 것과 똑같아 보인다.
	// AI가 실패를 읽고 다시 시도하는 구조라 거부는 정상 동작인데, 그렇다고
	// 이유를 안 남기면 나중에 왜 두 번 불렸는지 아무도 설명할 수 없다.
	if (Result.bIsError)
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("MCP Tool 거부: %s — %s"), *InToolName, *Result.Text);
	}
	else
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("MCP Tool 완료: %s"), *InToolName);
	}

	return Result;
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::Dispatch(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments)
{
	using namespace AIWidgetMcpToolsPrivate;

	if (InToolName == ToolGetSelectedWidget)
	{
		return GetSelectedWidget();
	}

	if (InToolName == ToolListWidgetTree)
	{
		return ListWidgetTree();
	}

	if (InToolName == ToolPreviewChange)
	{
		return ApplyChange(InArguments, /*bInWriteToAsset=*/false);
	}

	if (InToolName == ToolApplyToAsset)
	{
		return ApplyChange(InArguments, /*bInWriteToAsset=*/true);
	}

	if (InToolName == ToolRevertPreview)
	{
		return RevertPreview();
	}

	if (InToolName == ToolSaveAsset)
	{
		return SaveAsset();
	}

	return FAIWidgetMcpToolResult::Error(FString::Printf(TEXT("알 수 없는 Tool입니다: %s"), *InToolName));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::GetSelectedWidget() const
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 없습니다. 에디터에서 Inspect Mode로 Widget을 하나 클릭해야 합니다."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	if (!Inspection.bIsValid)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 이미 파괴되었습니다."));
	}

	const FAIWidgetSourceInfo SourceInfo = FAIWidgetSourceResolver::Resolve(Inspection, Selection->GetSelectedWidget());
	const FString Context = FAIWidgetContextBuilder::BuildContext(*Selection, Inspection, SourceInfo);

	return FAIWidgetMcpToolResult::Ok(Context);
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::ListWidgetTree() const
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 없습니다."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	UUserWidget* OwnerUserWidget = Inspection.OwnerUserWidget.Get();
	if (!OwnerUserWidget)
	{
		return FAIWidgetMcpToolResult::Error(TEXT(
			"이 Widget은 UMG UserWidget에 속해 있지 않습니다. C++ Slate Widget이라 이름으로 다룰 수 있는 트리가 없습니다."));
	}

	UWidgetTree* WidgetTree = OwnerUserWidget->WidgetTree;
	if (!WidgetTree)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("UserWidget에 WidgetTree가 없습니다."));
	}

	const FName SelectedName = Inspection.WidgetName;

	TStringBuilder<2048> Builder;
	Builder.Appendf(TEXT("UserWidget: %s\n"), *OwnerUserWidget->GetName());
	Builder.Append(TEXT("\n이름 / 타입 / 지금 Visibility\n"));

	int32 Count = 0;
	WidgetTree->ForEachWidget([&Builder, &Count, SelectedName](UWidget* Widget)
	{
		if (!Widget)
		{
			return;
		}

		++Count;
		Builder.Appendf(TEXT("%s / %s / %s%s\n"),
			*Widget->GetName(),
			*Widget->GetClass()->GetName(),
			*UEnum::GetValueAsString(Widget->GetVisibility()),
			Widget->GetFName() == SelectedName ? TEXT("   <-- 선택됨") : TEXT(""));
	});

	if (Count == 0)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("WidgetTree가 비어 있습니다."));
	}

	return FAIWidgetMcpToolResult::Ok(Builder.ToString());
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::ApplyChange(const TSharedPtr<FJsonObject>& InArguments, bool bInWriteToAsset)
{
	if (!InArguments.IsValid())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("인자가 없습니다."));
	}

	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 없습니다."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	if (!Inspection.bIsValid)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 이미 파괴되었습니다."));
	}

	// 응답 본문으로 오는 JSON과 똑같은 검사를 거친다. 경로가 둘이어도 관문은 하나다.
	FAIWidgetCommand Command;
	FText ParseError;
	if (!FAIWidgetCommandParser::ParseChange(InArguments, /*InIndex=*/0, Command, ParseError))
	{
		return FAIWidgetMcpToolResult::Error(ParseError.ToString());
	}

	const FAIWidgetCommandValidation Validation = FAIWidgetCommandValidator::Validate(Command, Inspection);
	if (!Validation.bIsValid)
	{
		return FAIWidgetMcpToolResult::Error(Validation.Error.ToString());
	}

	if (bInWriteToAsset)
	{
		if (!FAIWidgetPersistentApplier::CanApply(Inspection))
		{
			return FAIWidgetMcpToolResult::Error(TEXT(
				"이 Widget은 Widget Blueprint에서 온 것이 아니라 에셋에 쓸 수 없습니다. preview_widget_change는 쓸 수 있습니다."));
		}

		const FAIWidgetPersistentResult Result = FAIWidgetPersistentApplier::Apply({ Command }, Inspection);

		// 컴파일이 곧 선택을 죽인다. 저장할 때 쓸 손잡이를 지금 남겨 둔다.
		if (Result.Blueprint.IsValid())
		{
			FAIWidgetInspectorModule::Get().SetLastAppliedBlueprint(Result.Blueprint.Get());
		}

		if (Result.AppliedCount == 0)
		{
			return FAIWidgetMcpToolResult::Error(FString::Printf(
				TEXT("에셋에 쓰지 못했습니다: %s"), *Result.Error.ToString()));
		}

		return FAIWidgetMcpToolResult::Ok(FString::Printf(
			TEXT("에셋에 적용했습니다: %s\n%s\n\n아직 저장하지 않았습니다. 사용자가 에디터에서 저장해야 파일에 남습니다. 되돌리려면 에디터에서 Ctrl+Z."),
			*Validation.PlanLine,
			Result.bCompiled ? TEXT("Blueprint를 다시 컴파일했습니다.") : TEXT("")));
	}

	UWidget* TargetWidget = Validation.TargetWidget.Get();
	FText ApplyError;
	if (!RuntimePreview->Apply(TargetWidget, Command, ApplyError))
	{
		return FAIWidgetMcpToolResult::Error(ApplyError.ToString());
	}

	return FAIWidgetMcpToolResult::Ok(FString::Printf(
		TEXT("미리보기로 적용했습니다: %s\n\n에셋은 바뀌지 않았습니다. revert_preview로 되돌릴 수 있습니다."),
		*Validation.PlanLine));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::SaveAsset()
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("선택된 Widget이 없습니다."));
	}

	// 선택을 먼저 본다. 하지만 방금 에셋에 적용했다면 그 컴파일이 선택을 죽였을 수 있으므로,
	// 죽었으면 마지막으로 건드린 Blueprint로 넘어간다. 여기서 포기하면 "적용은 됐는데
	// 저장은 안 되는" 상태가 되어, 사용자가 보기엔 도구가 자기가 한 일을 잃어버린 것처럼 보인다.
	const FAIWidgetInspectionResult Inspection = InspectSelection();
	UBaseWidgetBlueprint* Blueprint = FAIWidgetPersistentApplier::GetWidgetBlueprint(Inspection);
	if (!Blueprint)
	{
		Blueprint = FAIWidgetInspectorModule::Get().GetLastAppliedBlueprint();
	}

	if (!Blueprint)
	{
		return FAIWidgetMcpToolResult::Error(TEXT(
			"저장할 Widget Blueprint를 찾지 못했습니다. 이 세션에서 에셋에 적용한 적이 없고, "
			"지금 선택된 것도 Widget Blueprint에서 온 Widget이 아닙니다."));
	}

	// 저장할 게 없는데 저장했다고 답하면, 미리보기만 해 놓고 끝난 걸 사용자가 눈치채지 못한다.
	if (!FAIWidgetPersistentApplier::IsAssetDirty(Blueprint))
	{
		return FAIWidgetMcpToolResult::Error(TEXT(
			"바뀐 내용이 없어 저장하지 않았습니다. 미리보기만 했다면 에셋에는 아직 아무것도 쓰이지 않은 상태입니다. "
			"apply_widget_change_to_asset을 먼저 불러야 합니다."));
	}

	FText SaveError;
	if (!FAIWidgetPersistentApplier::SaveAsset(Blueprint, SaveError))
	{
		return FAIWidgetMcpToolResult::Error(FString::Printf(TEXT("저장하지 못했습니다: %s"), *SaveError.ToString()));
	}

	return FAIWidgetMcpToolResult::Ok(FString::Printf(
		TEXT("%s 를 저장했습니다. 이제 에디터를 닫아도 남습니다."), *Blueprint->GetName()));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::RevertPreview()
{
	const int32 Count = RuntimePreview->Num();
	if (Count == 0)
	{
		return FAIWidgetMcpToolResult::Ok(TEXT("되돌릴 미리보기가 없습니다."));
	}

	RuntimePreview->RevertAll();

	return FAIWidgetMcpToolResult::Ok(FString::Printf(TEXT("%d건을 처음 값으로 되돌렸습니다."), Count));
}

#undef LOCTEXT_NAMESPACE
