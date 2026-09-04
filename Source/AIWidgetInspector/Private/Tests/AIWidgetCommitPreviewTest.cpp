// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Commands/AIWidgetCommand.h"
#include "Commands/AIWidgetPersistentApplier.h"
#include "Commands/AIWidgetRuntimePreview.h"

#include "Inspection/AIWidgetInspectionResult.h"

#include "BaseWidgetBlueprint.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_TESTS

namespace AIWidgetCommitPreviewTest
{
	static const TCHAR* SampleAssetPath = TEXT("/AIWidgetInspector/Samples/EUW_AIInspectorSample.EUW_AIInspectorSample");
	static const FName TargetWidgetName(TEXT("Btn_Upgrade"));
}

/**
 * Save to Asset 버튼이 밟는 경로.
 *
 * 버튼 자체는 눌러 볼 수 없지만, 그 안에서 갈리는 두 가지는 여기서 확인할 수 있다.
 * 둘 다 틀려도 컴파일되고, 화면에서는 한동안 멀쩡해 보인다.
 *
 *  1. 저장할 값은 미리보기 항목이 아니라 Widget에서 읽어야 한다. 항목은 되돌리려고
 *     '처음 값'을 들고 있어서, 그걸 저장하면 사용자가 방금 본 색이 아니라 그 전 색이
 *     에셋에 들어간다. 조용히 틀린 값이 저장되는 셈이다.
 *
 *  2. 저장한 뒤에는 미리보기를 '되돌리지 말고' 잊어야 한다. 되돌리면 방금 저장한 값이
 *     화면에서 사라져 에셋과 어긋나고, 어느 쪽이 맞는지 알 방법이 없다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIWidgetCommitPreviewTest,
	"AIWidgetInspector.CommitPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAIWidgetCommitPreviewTest::RunTest(const FString& Parameters)
{
	using namespace AIWidgetCommitPreviewTest;

	UBaseWidgetBlueprint* Blueprint = LoadObject<UBaseWidgetBlueprint>(nullptr, SampleAssetPath);
	if (!Blueprint)
	{
		AddError(FString::Printf(TEXT("샘플 에셋을 불러오지 못했습니다: %s"), SampleAssetPath));
		return false;
	}

	FAIWidgetInspectionResult Inspection;
	Inspection.bIsValid = true;
	Inspection.BlueprintAsset = Blueprint;

	UWidget* Widget = FAIWidgetPersistentApplier::ResolveTemplateWidget(TargetWidgetName, Inspection);
	if (!Widget)
	{
		AddError(TEXT("Btn_Upgrade를 찾지 못했습니다."));
		return false;
	}

	const float OriginalOpacity = Widget->GetRenderOpacity();
	const float PreviewedOpacity = OriginalOpacity > 0.5f ? 0.25f : 0.75f;

	// --- 저장할 값은 지금 값이지 처음 값이 아니다 ---
	{
		FAIWidgetRuntimePreview Preview;

		FText Error;
		TestTrue(
			TEXT("미리보기가 적용된다"),
			Preview.Apply(Widget, FAIWidgetCommand::MakeSetRenderOpacity(TargetWidgetName, PreviewedOpacity), Error));

		TestEqual(TEXT("Widget에 지금 값이 들어가 있다"), Widget->GetRenderOpacity(), PreviewedOpacity);
		TestEqual(TEXT("미리보기 1건"), Preview.Num(), 1);

		// 항목이 들고 있는 건 처음 값이다. 저장 대상이 아니다.
		if (Preview.GetEntries().Num() == 1)
		{
			TestEqual(
				TEXT("항목은 처음 값을 들고 있다"),
				Preview.GetEntries()[0].OriginalRenderOpacity,
				OriginalOpacity);
		}

		// 버튼이 하는 일. Widget에서 읽어야 사용자가 본 값이 나온다.
		FAIWidgetCommand Captured;
		TestTrue(
			TEXT("지금 값을 명령으로 만든다"),
			FAIWidgetCommand::CaptureFrom(Widget, EAIWidgetOperation::SetRenderOpacity, TargetWidgetName, Captured));

		TestEqual(TEXT("잡아낸 값이 지금 값이다"), Captured.RenderOpacity, PreviewedOpacity);
		TestNotEqual(TEXT("처음 값을 잡아오면 안 된다"), Captured.RenderOpacity, OriginalOpacity);
		TestEqual(
			TEXT("대상 이름이 실린다"),
			Captured.TargetWidgetName,
			TargetWidgetName);

		// --- 저장 뒤에는 되돌리지 말고 잊는다 ---
		Preview.ForgetAll();

		TestEqual(TEXT("목록이 비었다"), Preview.Num(), 0);
		TestEqual(
			TEXT("Widget 값은 그대로 남아 있다"),
			Widget->GetRenderOpacity(),
			PreviewedOpacity);
	}

	// --- RevertAll은 반대로 동작해야 한다 ---
	//
	// 둘이 같아져 버리면 저장 후 Revert가 방금 저장한 값을 날린다.
	{
		FAIWidgetRuntimePreview Preview;

		FText Error;
		Preview.Apply(Widget, FAIWidgetCommand::MakeSetRenderOpacity(TargetWidgetName, OriginalOpacity), Error);
		Preview.RevertAll();

		TestEqual(TEXT("Revert는 처음 값으로 되돌린다"), Widget->GetRenderOpacity(), PreviewedOpacity);
		TestEqual(TEXT("Revert 후 목록도 비었다"), Preview.Num(), 0);
	}

	// 위 블록에서 Widget은 PreviewedOpacity로 남아 있다. 원래대로 돌려놓는다.
	Widget->SetRenderOpacity(OriginalOpacity);

	// --- 잡아낸 명령이 그대로 에셋에 들어간다 ---
	{
		FAIWidgetRuntimePreview Preview;

		FText Error;
		Preview.Apply(Widget, FAIWidgetCommand::MakeSetRenderOpacity(TargetWidgetName, PreviewedOpacity), Error);

		TArray<FAIWidgetCommand> CommandsToApply;
		for (const FAIWidgetPreviewEntry& Entry : Preview.GetEntries())
		{
			FAIWidgetCommand Command;
			if (FAIWidgetCommand::CaptureFrom(Entry.Widget.Get(), Entry.Operation, Entry.WidgetName, Command))
			{
				CommandsToApply.Add(MoveTemp(Command));
			}
		}

		TestEqual(TEXT("명령 1건이 만들어진다"), CommandsToApply.Num(), 1);

		const FAIWidgetPersistentResult Result = FAIWidgetPersistentApplier::Apply(CommandsToApply, Inspection);

		TestEqual(TEXT("에셋에 1건 적용"), Result.AppliedCount, 1);
		TestTrue(TEXT("저장할 Blueprint를 돌려준다"), Result.Blueprint.IsValid());
		TestTrue(
			TEXT("저장 대상이 dirty하다"),
			FAIWidgetPersistentApplier::IsAssetDirty(Result.Blueprint.Get()));

		Preview.ForgetAll();

		// 여기서 실제로 저장하지는 않는다. 테스트가 리포지토리의 샘플 에셋을 고쳐 놓으면
		// 다음 실행의 출발점이 달라진다. 저장 직전까지가 확인 범위다.
		if (GEditor)
		{
			GEditor->UndoTransaction();
		}
	}

	Widget->SetRenderOpacity(OriginalOpacity);

	if (UPackage* Package = Blueprint->GetPackage())
	{
		Package->SetDirtyFlag(false);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
