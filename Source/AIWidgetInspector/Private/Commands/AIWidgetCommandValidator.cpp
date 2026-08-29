// AI Widget Inspector

#include "Commands/AIWidgetCommandValidator.h"

#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetInspectionResult.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FAIWidgetCommandValidator"

UWidget* FAIWidgetCommandValidator::ResolveTargetWidget(FName InTargetWidgetName, const FAIWidgetInspectionResult& InInspection)
{
	if (InTargetWidgetName.IsNone())
	{
		return nullptr;
	}

	// 선택된 Widget 자신이면 그대로 쓴다.
	if (UWidget* SelectedWidget = InInspection.SourceWidget.Get())
	{
		if (SelectedWidget->GetFName() == InTargetWidgetName)
		{
			return SelectedWidget;
		}
	}

	// 아니면 같은 UserWidget 안에서 찾는다.
	if (const UUserWidget* OwnerUserWidget = InInspection.OwnerUserWidget.Get())
	{
		if (UWidgetTree* WidgetTree = OwnerUserWidget->WidgetTree)
		{
			return WidgetTree->FindWidget(InTargetWidgetName);
		}
	}

	return nullptr;
}

FString FAIWidgetCommandValidator::DescribeCurrentValue(const UWidget* InWidget, EAIWidgetOperation InOperation)
{
	if (!InWidget)
	{
		return FString();
	}

	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:
	{
		const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
		return VisibilityEnum
			? VisibilityEnum->GetNameStringByValue(static_cast<int64>(InWidget->GetVisibility()))
			: FString();
	}

	case EAIWidgetOperation::SetEnabled:
		return InWidget->GetIsEnabled() ? TEXT("true") : TEXT("false");

	case EAIWidgetOperation::SetRenderOpacity:
		return FString::Printf(TEXT("%.2f"), InWidget->GetRenderOpacity());

	case EAIWidgetOperation::SetRenderTranslation:
	{
		const FVector2D Translation = InWidget->GetRenderTransform().Translation;
		return FString::Printf(TEXT("%.0f, %.0f"), Translation.X, Translation.Y);
	}

	case EAIWidgetOperation::SetColorAndOpacity:
	{
		FLinearColor CurrentColor;
		if (FAIWidgetCommand::GetColorAndOpacity(InWidget, CurrentColor))
		{
			return FAIWidgetCommand::ToHexColor(CurrentColor);
		}

		// 고정된 색이 없는 경우다. 빈 문자열을 두면 계획 줄이 "-> #FF0000"처럼 보여
		// 원래 색이 없었던 건지 못 읽은 건지 구분이 안 된다.
		return FString(TEXT("(no fixed colour)"));
	}

	case EAIWidgetOperation::SetText:
		if (const UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			return FString::Printf(TEXT("\"%s\""), *AsTextBlock->GetText().ToString());
		}
		return FString();

	default:
		return FString();
	}
}

FAIWidgetCommandValidation FAIWidgetCommandValidator::Validate(const FAIWidgetCommand& InCommand, const FAIWidgetInspectionResult& InInspection)
{
	FAIWidgetCommandValidation Result;

	// 1. 화이트리스트 밖의 Operation은 여기서 끝난다.
	if (!FAIWidgetCommand::IsRuntimeSupported(InCommand.Operation))
	{
		Result.Error = FText::Format(
			LOCTEXT("NotSupported", "Operation not allowed: {0}"),
			FText::FromString(FAIWidgetCommand::GetOperationName(InCommand.Operation)));
		return Result;
	}

	// 2. 이름이 실제 Widget을 가리켜야 한다.
	UWidget* TargetWidget = ResolveTargetWidget(InCommand.TargetWidgetName, InInspection);
	if (!TargetWidget)
	{
		Result.Error = FText::Format(
			LOCTEXT("TargetNotFound", "'{0}' was not found. It is neither the selected widget nor a sibling in the same UserWidget."),
			FText::FromName(InCommand.TargetWidgetName));
		return Result;
	}

	// 3. 그 Widget이 이 Operation을 받을 수 있어야 한다. (예: SetText는 TextBlock만)
	if (!FAIWidgetRuntimePreview::CanApply(TargetWidget, InCommand.Operation))
	{
		Result.Error = FText::Format(
			LOCTEXT("CannotApply", "{2} cannot be applied to {0} ({1})."),
			FText::FromString(TargetWidget->GetName()),
			FText::FromString(TargetWidget->GetClass()->GetName()),
			FText::FromString(FAIWidgetCommand::GetOperationName(InCommand.Operation)));
		return Result;
	}

	// 4. 값이 범위 안이어야 한다.
	if (InCommand.Operation == EAIWidgetOperation::SetColorAndOpacity)
	{
		const FLinearColor& Color = InCommand.ColorAndOpacity;
		if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A))
		{
			Result.Error = LOCTEXT("ColorNotFinite", "The colour value is not valid.");
			return Result;
		}
	}

	if (InCommand.Operation == EAIWidgetOperation::SetRenderOpacity)
	{
		if (!FMath::IsFinite(InCommand.RenderOpacity) || InCommand.RenderOpacity < 0.0f || InCommand.RenderOpacity > 1.0f)
		{
			Result.Error = FText::Format(
				LOCTEXT("OpacityOutOfRange", "RenderOpacity must be between 0 and 1. Got: {0}"),
				FText::AsNumber(InCommand.RenderOpacity));
			return Result;
		}
	}

	if (InCommand.Operation == EAIWidgetOperation::SetRenderTranslation)
	{
		if (!FMath::IsFinite(InCommand.RenderTranslation.X) || !FMath::IsFinite(InCommand.RenderTranslation.Y))
		{
			Result.Error = LOCTEXT("TranslationNotFinite", "RenderTranslation is not a valid pair of numbers.");
			return Result;
		}
	}

	Result.bIsValid = true;
	Result.TargetWidget = TargetWidget;

	const FString CurrentValue = DescribeCurrentValue(TargetWidget, InCommand.Operation);
	Result.PlanLine = FString::Printf(
		TEXT("%s   %s   %s -> %s"),
		*TargetWidget->GetName(),
		FAIWidgetCommand::GetOperationName(InCommand.Operation),
		*CurrentValue,
		*InCommand.DescribeValue());

	return Result;
}

#undef LOCTEXT_NAMESPACE
