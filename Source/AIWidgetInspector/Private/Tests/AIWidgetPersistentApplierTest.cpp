// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Commands/AIWidgetPersistentApplier.h"

#include "Inspection/AIWidgetInspectionResult.h"

#include "BaseWidgetBlueprint.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_TESTS

namespace AIWidgetPersistentApplierTest
{
	static const TCHAR* SampleAssetPath = TEXT("/AIWidgetInspector/Samples/EUW_AIInspectorSample.EUW_AIInspectorSample");
	static const FName TargetWidgetName(TEXT("Btn_Upgrade"));
}

/**
 * 영구 변경이 지켜야 할 약속을 확인한다.
 *
 *  - 에셋 안의 원본 Widget이 실제로 바뀐다 (화면 인스턴스가 아니라)
 *  - 받을 수 없는 Operation은 거부된다
 *  - Ctrl+Z 한 번으로 이번 Apply 전체가 되돌아간다
 *
 * 마지막 항목이 핵심이다. Modify()를 빠뜨리면 값은 바뀌지만 Undo가 복원하지 못하는데,
 * 눈으로는 구분이 안 되고 나중에 사용자가 되돌리려 할 때야 드러난다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetPersistentApplierTest,
	"AIWidgetInspector.PersistentApplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetPersistentApplierTest::RunTest(const FString& Parameters)
{
	using namespace AIWidgetPersistentApplierTest;

	UBaseWidgetBlueprint* Blueprint = LoadObject<UBaseWidgetBlueprint>(nullptr, SampleAssetPath);
	if (!Blueprint)
	{
		AddError(FString::Printf(TEXT("샘플 에셋을 불러오지 못했습니다: %s"), SampleAssetPath));
		return false;
	}

	FAIWidgetInspectionResult Inspection;
	Inspection.bIsValid = true;
	Inspection.BlueprintAsset = Blueprint;

	TestTrue(TEXT("Widget Blueprint가 있으면 영구 변경이 가능해야 한다"), FAIWidgetPersistentApplier::CanApply(Inspection));

	UWidget* TemplateWidget = FAIWidgetPersistentApplier::ResolveTemplateWidget(TargetWidgetName, Inspection);
	if (!TemplateWidget)
	{
		AddError(TEXT("Widget Blueprint 안에서 Btn_Upgrade를 찾지 못했습니다."));
		return false;
	}

	const float OriginalOpacity = TemplateWidget->GetRenderOpacity();
	const ESlateVisibility OriginalVisibility = TemplateWidget->GetVisibility();

	// 없는 이름과 받을 수 없는 Operation을 섞어서 보낸다.
	TArray<FAIWidgetCommand> Commands;
	Commands.Add(FAIWidgetCommand::MakeSetRenderOpacity(TargetWidgetName, 0.25f));
	Commands.Add(FAIWidgetCommand::MakeSetVisibility(TargetWidgetName, ESlateVisibility::HitTestInvisible));
	Commands.Add(FAIWidgetCommand::MakeSetText(TargetWidgetName, FText::FromString(TEXT("버튼은 TextBlock이 아니다"))));
	Commands.Add(FAIWidgetCommand::MakeSetRenderOpacity(FName(TEXT("NoSuchWidget")), 0.5f));

	const FAIWidgetPersistentResult Result = FAIWidgetPersistentApplier::Apply(Commands, Inspection);

	TestEqual(TEXT("적용 2건"), Result.AppliedCount, 2);
	TestEqual(TEXT("거부 2건"), Result.FailedCount, 2);
	TestTrue(TEXT("적용됐으면 컴파일해야 한다"), Result.bCompiled);

	// --- 적용 결과가 Blueprint를 들고 나온다 ---
	//
	// 이게 없으면 저장 경로가 끊긴다. Apply는 Blueprint를 재컴파일하고, 그 컴파일이
	// 화면에 떠 있던 Widget 인스턴스를 파괴한다. 그 순간 선택이 죽어서, 선택을 거쳐
	// 에셋을 찾던 코드가 "이 Widget은 Blueprint에서 온 게 아니다"라고 답하게 된다.
	//
	// 실제로 그렇게 깨졌었다. 적용은 성공했는데 3초 뒤 저장이 대상을 못 찾았다.
	// 적용까지는 멀쩡히 되기 때문에 눈으로는 안 잡히고, 사용자가 저장을 누를 때가
	// 되어서야 엉뚱한 원인을 가리키는 오류가 나온다.
	TestTrue(TEXT("건드린 Blueprint를 돌려준다"), Result.Blueprint.IsValid());
	TestEqual(TEXT("돌려준 것이 그 Blueprint다"), Result.Blueprint.Get(), Blueprint);

	// 선택이 죽은 상황. 검사 결과가 비어 있으면 에셋을 못 찾는 게 맞다.
	const FAIWidgetInspectionResult DeadSelection;
	TestNull(
		TEXT("죽은 선택으로는 Blueprint를 못 찾는다"),
		FAIWidgetPersistentApplier::GetWidgetBlueprint(DeadSelection));
	TestFalse(
		TEXT("죽은 선택으로는 dirty도 못 본다"),
		FAIWidgetPersistentApplier::IsAssetDirty(DeadSelection));

	// 버그의 전부가 이 두 줄이 갈리는 데 있었다. 위는 없다고 하고 아래는 있다고 한다.
	// 저장은 아래를 봐야 한다.
	TestTrue(
		TEXT("Blueprint 손잡이로는 여전히 저장 대상이 보인다"),
		FAIWidgetPersistentApplier::IsAssetDirty(Result.Blueprint.Get()));
	TestTrue(TEXT("에셋이 dirty여야 한다"), FAIWidgetPersistentApplier::IsAssetDirty(Inspection));

	// 컴파일이 클래스를 다시 만들므로 템플릿을 다시 찾는다.
	TemplateWidget = FAIWidgetPersistentApplier::ResolveTemplateWidget(TargetWidgetName, Inspection);
	if (!TemplateWidget)
	{
		AddError(TEXT("컴파일 후 Btn_Upgrade를 다시 찾지 못했습니다."));
		return false;
	}

	TestEqual(TEXT("에셋의 RenderOpacity가 바뀌어야 한다"), TemplateWidget->GetRenderOpacity(), 0.25f);
	TestEqual(TEXT("에셋의 Visibility가 바뀌어야 한다"),
		static_cast<int32>(TemplateWidget->GetVisibility()),
		static_cast<int32>(ESlateVisibility::HitTestInvisible));

	// --- Undo 한 번으로 이번 Apply 전체가 되돌아가야 한다 ---
	if (GEditor)
	{
		GEditor->UndoTransaction();

		TemplateWidget = FAIWidgetPersistentApplier::ResolveTemplateWidget(TargetWidgetName, Inspection);
		if (TemplateWidget)
		{
			TestEqual(TEXT("Undo가 RenderOpacity를 복원해야 한다"), TemplateWidget->GetRenderOpacity(), OriginalOpacity);
			TestEqual(TEXT("Undo가 Visibility를 복원해야 한다"),
				static_cast<int32>(TemplateWidget->GetVisibility()),
				static_cast<int32>(OriginalVisibility));
		}
		else
		{
			AddError(TEXT("Undo 후 Btn_Upgrade를 찾지 못했습니다."));
		}
	}
	else
	{
		AddWarning(TEXT("GEditor가 없어 Undo는 확인하지 못했습니다."));
	}

	// 값은 되돌렸으니 테스트가 에셋을 저장 대상으로 남기지 않게 한다.
	if (UPackage* Package = Blueprint->GetPackage())
	{
		Package->SetDirtyFlag(false);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
