// Copyright 2026 Junghyeon0710. All Rights Reserved.
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
		Operation->SetStringField(TEXT("description"), TEXT("Which property to change. Only the listed values run."));
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
			TEXT("Name of the widget to change. Must be one returned by list_widget_tree.")));

		// value는 Operation마다 타입이 다르다. JSON Schema로 그걸 표현하면 oneOf가 길어지는데,
		// 어차피 실제 검사는 ParseChange가 하므로 여기서는 설명으로 알려준다.
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("description"), TEXT(
			"Type is decided by the operation. "
			"SetVisibility: string, one of Visible / Collapsed / Hidden / HitTestInvisible / SelfHitTestInvisible. "
			"SetEnabled: boolean. "
			"SetText: string. "
			"SetRenderOpacity: number from 0 to 1. "
			"SetRenderTranslation: object with numeric x and y. "
			"SetColorAndOpacity: string \"#RRGGBB\" or \"#RRGGBBAA\", read as sRGB."));
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
		TEXT("Describes the widget currently selected in the editor: type, state, layout, owning Blueprint, and the C++ location that created it. Call this before deciding what to change."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolListWidgetTree,
		TEXT("Lists the name and type of every widget in the UserWidget that owns the selection. Only these names are accepted as target_widget."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolPreviewChange,
		TEXT("Applies a change to the live widget on screen. The asset file is untouched and revert_preview restores it. Show the result this way first, then write to the asset if the user is happy."),
		MakeChangeProperties(),
		{ TEXT("operation"), TEXT("target_widget"), TEXT("value") }));

	Tools.Add(MakeTool(
		ToolApplyToAsset,
		TEXT("Writes a change into the Widget Blueprint asset. Undoing it takes Ctrl+Z in the editor. Does not save, so the user still has to save for it to reach the file. Only call it when the user has clearly asked."),
		MakeChangeProperties(),
		{ TEXT("operation"), TEXT("target_widget"), TEXT("value") }));

	Tools.Add(MakeTool(
		ToolSaveAsset,
		TEXT("Saves the Widget Blueprint changed by apply_widget_change_to_asset to disk. After this it survives closing the editor. Only call it when the user has asked to save."),
		MakeShared<FJsonObject>(),
		{}));

	Tools.Add(MakeTool(
		ToolRevertPreview,
		TEXT("Restores every value changed by preview_widget_change to what it was."),
		MakeShared<FJsonObject>(),
		{}));

	return Tools;
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::Call(const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments)
{
	using namespace AIWidgetMcpToolsPrivate;

	// Widget을 만지는 일은 전부 게임 스레드에서만 해야 한다. HTTP 핸들러가 게임 스레드에서
	// 돌기 때문에 여기까지 그대로 오지만, 그 전제가 깨지면 조용히 망가지므로 확인해 둔다.
	if (!ensureMsgf(IsInGameThread(), TEXT("MCP tools must run on the game thread.")))
	{
		return FAIWidgetMcpToolResult::Error(TEXT("Cannot run a tool off the game thread."));
	}

	const FAIWidgetMcpToolResult Result = Dispatch(InToolName, InArguments);

	// 호출 사실만 남기면, 거부된 호출이 로그에서 성공한 것과 똑같아 보인다.
	// AI가 실패를 읽고 다시 시도하는 구조라 거부는 정상 동작인데, 그렇다고
	// 이유를 안 남기면 나중에 왜 두 번 불렸는지 아무도 설명할 수 없다.
	if (Result.bIsError)
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("MCP tool rejected: %s - %s"), *InToolName, *Result.Text);
	}
	else
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("MCP tool done: %s"), *InToolName);
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

	return FAIWidgetMcpToolResult::Error(FString::Printf(TEXT("Unknown tool: %s"), *InToolName));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::GetSelectedWidget() const
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("No widget is selected. Ask the user to turn on Inspect Mode in the editor and click a widget."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	if (!Inspection.bIsValid)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("The selected widget no longer exists. It was probably destroyed by a Blueprint recompile; ask the user to select it again."));
	}

	const FAIWidgetSourceInfo SourceInfo = FAIWidgetSourceResolver::Resolve(Inspection, Selection->GetSelectedWidget());
	const FString Context = FAIWidgetContextBuilder::BuildContext(*Selection, Inspection, SourceInfo);

	return FAIWidgetMcpToolResult::Ok(Context);
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::ListWidgetTree() const
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("No widget is selected."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	UUserWidget* OwnerUserWidget = Inspection.OwnerUserWidget.Get();
	if (!OwnerUserWidget)
	{
		return FAIWidgetMcpToolResult::Error(TEXT(
			"This widget does not belong to a UMG UserWidget. It is a C++ Slate widget, so there is no named widget tree to list."));
	}

	UWidgetTree* WidgetTree = OwnerUserWidget->WidgetTree;
	if (!WidgetTree)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("The UserWidget has no WidgetTree."));
	}

	const FName SelectedName = Inspection.WidgetName;

	TStringBuilder<2048> Builder;
	Builder.Appendf(TEXT("UserWidget: %s\n"), *OwnerUserWidget->GetName());
	Builder.Append(TEXT("\nname / class / current visibility\n"));

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
			Widget->GetFName() == SelectedName ? TEXT("   <-- selected") : TEXT(""));
	});

	if (Count == 0)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("The WidgetTree is empty."));
	}

	return FAIWidgetMcpToolResult::Ok(Builder.ToString());
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::ApplyChange(const TSharedPtr<FJsonObject>& InArguments, bool bInWriteToAsset)
{
	if (!InArguments.IsValid())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("No arguments were given."));
	}

	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("No widget is selected."));
	}

	const FAIWidgetInspectionResult Inspection = InspectSelection();
	if (!Inspection.bIsValid)
	{
		return FAIWidgetMcpToolResult::Error(TEXT("The selected widget no longer exists. It was probably destroyed by a Blueprint recompile; ask the user to select it again."));
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
				"This widget did not come from a Widget Blueprint, so there is no asset to write to. preview_widget_change still works."));
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
				TEXT("Could not write to the asset: %s"), *Result.Error.ToString()));
		}

		return FAIWidgetMcpToolResult::Ok(FString::Printf(
			TEXT("Applied to the asset: %s\n%s\n\nNot saved yet. The user has to save in the editor for it to reach the file, or press Ctrl+Z there to undo."),
			*Validation.PlanLine,
			Result.bCompiled ? TEXT("The Blueprint was recompiled.") : TEXT("")));
	}

	UWidget* TargetWidget = Validation.TargetWidget.Get();
	FText ApplyError;
	if (!RuntimePreview->Apply(TargetWidget, Command, ApplyError))
	{
		return FAIWidgetMcpToolResult::Error(ApplyError.ToString());
	}

	return FAIWidgetMcpToolResult::Ok(FString::Printf(
		TEXT("Applied as a preview: %s\n\nThe asset is unchanged. revert_preview restores it."),
		*Validation.PlanLine));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::SaveAsset()
{
	if (!HasSelection())
	{
		return FAIWidgetMcpToolResult::Error(TEXT("No widget is selected."));
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
			"Could not find a Widget Blueprint to save. Nothing has been applied to an asset this session, "
			"and the current selection did not come from one either."));
	}

	// 저장할 게 없는데 저장했다고 답하면, 미리보기만 해 놓고 끝난 걸 사용자가 눈치채지 못한다.
	if (!FAIWidgetPersistentApplier::IsAssetDirty(Blueprint))
	{
		return FAIWidgetMcpToolResult::Error(TEXT(
			"Nothing to save. A preview leaves the asset untouched, "
			"so call apply_widget_change_to_asset first."));
	}

	FText SaveError;
	if (!FAIWidgetPersistentApplier::SaveAsset(Blueprint, SaveError))
	{
		return FAIWidgetMcpToolResult::Error(FString::Printf(TEXT("Could not save: %s"), *SaveError.ToString()));
	}

	return FAIWidgetMcpToolResult::Ok(FString::Printf(
		TEXT("Saved %s. It now survives closing the editor."), *Blueprint->GetName()));
}

FAIWidgetMcpToolResult FAIWidgetMcpTools::RevertPreview()
{
	const int32 Count = RuntimePreview->Num();
	if (Count == 0)
	{
		return FAIWidgetMcpToolResult::Ok(TEXT("There are no previews to revert."));
	}

	RuntimePreview->RevertAll();

	return FAIWidgetMcpToolResult::Ok(FString::Printf(TEXT("Restored %d value(s) to what they were."), Count));
}

#undef LOCTEXT_NAMESPACE
