// AI Widget Inspector

#include "Commands/AIWidgetCommand.h"

#define LOCTEXT_NAMESPACE "FAIWidgetCommand"

#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

const TCHAR* FAIWidgetCommand::GetOperationName(EAIWidgetOperation InOperation)
{
	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:        return TEXT("SetVisibility");
	case EAIWidgetOperation::SetEnabled:           return TEXT("SetEnabled");
	case EAIWidgetOperation::SetText:              return TEXT("SetText");
	case EAIWidgetOperation::SetRenderOpacity:     return TEXT("SetRenderOpacity");
	case EAIWidgetOperation::SetRenderTranslation: return TEXT("SetRenderTranslation");
	default:                                       return TEXT("None");
	}
}

bool FAIWidgetCommand::IsRuntimeSupported(EAIWidgetOperation InOperation)
{
	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:
	case EAIWidgetOperation::SetEnabled:
	case EAIWidgetOperation::SetText:
	case EAIWidgetOperation::SetRenderOpacity:
	case EAIWidgetOperation::SetRenderTranslation:
		return true;
	default:
		return false;
	}
}

bool FAIWidgetCommand::ApplyTo(UWidget* InWidget, FText& OutError) const
{
	if (!InWidget)
	{
		OutError = LOCTEXT("NoWidget", "적용할 Widget이 없습니다.");
		return false;
	}

	switch (Operation)
	{
	case EAIWidgetOperation::SetVisibility:
		InWidget->SetVisibility(Visibility);
		return true;

	case EAIWidgetOperation::SetEnabled:
		InWidget->SetIsEnabled(bEnabled);
		return true;

	case EAIWidgetOperation::SetRenderOpacity:
		InWidget->SetRenderOpacity(RenderOpacity);
		return true;

	case EAIWidgetOperation::SetRenderTranslation:
		InWidget->SetRenderTranslation(RenderTranslation);
		return true;

	case EAIWidgetOperation::SetText:
		if (UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			AsTextBlock->SetText(Text);
			return true;
		}

		OutError = FText::Format(
			LOCTEXT("NotATextBlock", "{0}은(는) TextBlock이 아니라 SetText를 적용할 수 없습니다."),
			FText::FromString(InWidget->GetName()));
		return false;

	default:
		OutError = FText::Format(
			LOCTEXT("UnsupportedOperation", "지원하지 않는 Operation입니다: {0}"),
			FText::FromString(GetOperationName(Operation)));
		return false;
	}
}

FString FAIWidgetCommand::DescribeValue() const
{
	switch (Operation)
	{
	case EAIWidgetOperation::SetVisibility:
	{
		const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
		return VisibilityEnum
			? VisibilityEnum->GetNameStringByValue(static_cast<int64>(Visibility))
			: FString::FromInt(static_cast<int32>(Visibility));
	}

	case EAIWidgetOperation::SetEnabled:
		return bEnabled ? TEXT("true") : TEXT("false");

	case EAIWidgetOperation::SetText:
		return FString::Printf(TEXT("\"%s\""), *Text.ToString());

	case EAIWidgetOperation::SetRenderOpacity:
		return FString::Printf(TEXT("%.2f"), RenderOpacity);

	case EAIWidgetOperation::SetRenderTranslation:
		return FString::Printf(TEXT("%.0f, %.0f"), RenderTranslation.X, RenderTranslation.Y);

	default:
		return FString();
	}
}

FString FAIWidgetCommand::Describe() const
{
	if (Operation == EAIWidgetOperation::None)
	{
		return FString(GetOperationName(Operation));
	}

	return FString::Printf(TEXT("%s(%s)"), GetOperationName(Operation), *DescribeValue());
}

FAIWidgetCommand FAIWidgetCommand::MakeSetVisibility(FName InTargetWidgetName, ESlateVisibility InVisibility)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetVisibility;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.Visibility = InVisibility;
	return Command;
}

FAIWidgetCommand FAIWidgetCommand::MakeSetEnabled(FName InTargetWidgetName, bool bInEnabled)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetEnabled;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.bEnabled = bInEnabled;
	return Command;
}

FAIWidgetCommand FAIWidgetCommand::MakeSetText(FName InTargetWidgetName, const FText& InText)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetText;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.Text = InText;
	return Command;
}

FAIWidgetCommand FAIWidgetCommand::MakeSetRenderOpacity(FName InTargetWidgetName, float InOpacity)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetRenderOpacity;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.RenderOpacity = InOpacity;
	return Command;
}

FAIWidgetCommand FAIWidgetCommand::MakeSetRenderTranslation(FName InTargetWidgetName, const FVector2D& InTranslation)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetRenderTranslation;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.RenderTranslation = InTranslation;
	return Command;
}

#undef LOCTEXT_NAMESPACE
