// AI Widget Inspector

#include "Commands/AIWidgetPersistentApplier.h"

#include "AIWidgetInspectorLog.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetInspectionResult.h"

#include "BaseWidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FAIWidgetPersistentApplier"

UBaseWidgetBlueprint* FAIWidgetPersistentApplier::GetWidgetBlueprint(const FAIWidgetInspectionResult& InInspection)
{
	return Cast<UBaseWidgetBlueprint>(InInspection.BlueprintAsset.Get());
}

bool FAIWidgetPersistentApplier::CanApply(const FAIWidgetInspectionResult& InInspection)
{
	const UBaseWidgetBlueprint* Blueprint = GetWidgetBlueprint(InInspection);
	return Blueprint != nullptr && Blueprint->WidgetTree != nullptr;
}

UWidget* FAIWidgetPersistentApplier::ResolveTemplateWidget(FName InTargetWidgetName, const FAIWidgetInspectionResult& InInspection)
{
	if (InTargetWidgetName.IsNone())
	{
		return nullptr;
	}

	const UBaseWidgetBlueprint* Blueprint = GetWidgetBlueprint(InInspection);
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		return nullptr;
	}

	return Blueprint->WidgetTree->FindWidget(InTargetWidgetName);
}

FAIWidgetPersistentResult FAIWidgetPersistentApplier::Apply(const TArray<FAIWidgetCommand>& InCommands, const FAIWidgetInspectionResult& InInspection)
{
	FAIWidgetPersistentResult Result;

	UBaseWidgetBlueprint* Blueprint = GetWidgetBlueprint(InInspection);
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		Result.Error = LOCTEXT("NoBlueprint", "This widget has no source Widget Blueprint. C++ Slate widgets cannot be changed as assets.");
		return Result;
	}

	Result.Blueprint = Blueprint;

	{
		// 전부 한 트랜잭션에 넣는다. Ctrl+Z 한 번에 이번 Apply 전체가 되돌아가야 한다.
		FScopedTransaction Transaction(LOCTEXT("ApplyToAssetTransaction", "AI Widget Inspector: apply change to asset"));

		for (const FAIWidgetCommand& Command : InCommands)
		{
			UWidget* TemplateWidget = ResolveTemplateWidget(Command.TargetWidgetName, InInspection);
			if (!TemplateWidget)
			{
				++Result.FailedCount;
				Result.Error = FText::Format(
					LOCTEXT("TemplateNotFound", "'{0}' was not found inside the Widget Blueprint."),
					FText::FromName(Command.TargetWidgetName));
				continue;
			}

			if (!FAIWidgetRuntimePreview::CanApply(TemplateWidget, Command.Operation))
			{
				++Result.FailedCount;
				Result.Error = FText::Format(
					LOCTEXT("CannotApplyToTemplate", "{1} cannot be applied to {0}."),
					FText::FromString(TemplateWidget->GetName()),
					FText::FromString(FAIWidgetCommand::GetOperationName(Command.Operation)));
				continue;
			}

			// 값을 쓰기 전에 Modify(). 이게 빠지면 Undo가 이전 값을 복원하지 못한다.
			TemplateWidget->Modify();

			FText ApplyError;
			if (!Command.ApplyTo(TemplateWidget, ApplyError))
			{
				++Result.FailedCount;
				Result.Error = ApplyError;
				continue;
			}

			// 아키타입 변경을 에디터에 알려 이미 만들어진 인스턴스에도 전파되게 한다.
			TemplateWidget->PostEditChange();

			++Result.AppliedCount;

			UE_LOG(LogAIWidgetInspector, Log, TEXT("Applied to asset: %s.%s (%s)"),
				*TemplateWidget->GetName(), *Command.Describe(), *Blueprint->GetName());
		}

		if (Result.AppliedCount == 0)
		{
			// 아무것도 못 바꿨으면 Undo 목록에 빈 항목을 남기지 않는다.
			Transaction.Cancel();
			return Result;
		}

		Blueprint->Modify();
	}

	// 컴파일은 트랜잭션 밖에서 한다. 클래스를 다시 만드는 작업이라 되돌리기 대상이 아니다.
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Result.bCompiled = true;

	UE_LOG(LogAIWidgetInspector, Log, TEXT("%s compiled. %d applied, %d rejected. Not saved yet."),
		*Blueprint->GetName(), Result.AppliedCount, Result.FailedCount);

	return Result;
}

bool FAIWidgetPersistentApplier::IsAssetDirty(const FAIWidgetInspectionResult& InInspection)
{
	return IsAssetDirty(GetWidgetBlueprint(InInspection));
}

bool FAIWidgetPersistentApplier::IsAssetDirty(const UBaseWidgetBlueprint* InBlueprint)
{
	const UPackage* Package = InBlueprint ? InBlueprint->GetPackage() : nullptr;
	return Package != nullptr && Package->IsDirty();
}

bool FAIWidgetPersistentApplier::SaveAsset(const FAIWidgetInspectionResult& InInspection, FText& OutError)
{
	return SaveAsset(GetWidgetBlueprint(InInspection), OutError);
}

bool FAIWidgetPersistentApplier::SaveAsset(UBaseWidgetBlueprint* InBlueprint, FText& OutError)
{
	UBaseWidgetBlueprint* Blueprint = InBlueprint;
	UPackage* Package = Blueprint ? Blueprint->GetPackage() : nullptr;
	if (!Package)
	{
		OutError = LOCTEXT("NoPackage", "There is no asset to save.");
		return false;
	}

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Package);

	// bPromptToSave = false: 사용자가 이미 저장을 눌렀으므로 목록을 또 묻지 않는다.
	// 리비전 컨트롤 체크아웃이 필요하면 그 대화상자는 뜬다.
	const FEditorFileUtils::EPromptReturnCode ReturnCode = FEditorFileUtils::PromptForCheckoutAndSave(
		PackagesToSave,
		/*bCheckDirty=*/false,
		/*bPromptToSave=*/false);

	if (ReturnCode != FEditorFileUtils::EPromptReturnCode::PR_Success)
	{
		OutError = LOCTEXT("SaveFailed", "Could not save the asset. Check the output log.");
		return false;
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Saved %s."), *Blueprint->GetName());
	return true;
}

#undef LOCTEXT_NAMESPACE
