// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "Commands/AIWidgetCommand.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

// Include보다 뒤에 둔다. UserWidget.h 같은 엔진 헤더가 자기 LOCTEXT_NAMESPACE를
// 정의했다가 #undef 하기 때문에, 앞에 두면 우리 것이 지워진다.
#define LOCTEXT_NAMESPACE "FAIWidgetCommand"

const TCHAR* FAIWidgetCommand::GetOperationName(EAIWidgetOperation InOperation)
{
	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:        return TEXT("SetVisibility");
	case EAIWidgetOperation::SetEnabled:           return TEXT("SetEnabled");
	case EAIWidgetOperation::SetText:              return TEXT("SetText");
	case EAIWidgetOperation::SetRenderOpacity:     return TEXT("SetRenderOpacity");
	case EAIWidgetOperation::SetRenderTranslation: return TEXT("SetRenderTranslation");
	case EAIWidgetOperation::SetColorAndOpacity:   return TEXT("SetColorAndOpacity");
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
	case EAIWidgetOperation::SetColorAndOpacity:
		return true;
	default:
		return false;
	}
}

bool FAIWidgetCommand::GetColorAndOpacity(const UWidget* InWidget, FLinearColor& OutColor)
{
	if (const UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
	{
		const FSlateColor SlateColor = AsTextBlock->GetColorAndOpacity();

		// Foreground나 스타일을 따라가도록 돼 있으면 고정된 색이 없다. 그때의
		// SpecifiedColor는 실제로 화면에 나오는 색이 아니므로 없는 것으로 취급한다.
		if (!SlateColor.IsColorSpecified())
		{
			return false;
		}

		OutColor = SlateColor.GetSpecifiedColor();
		return true;
	}

	if (const UImage* AsImage = Cast<UImage>(InWidget))
	{
		OutColor = AsImage->GetColorAndOpacity();
		return true;
	}

	if (const UButton* AsButton = Cast<UButton>(InWidget))
	{
		OutColor = AsButton->GetColorAndOpacity();
		return true;
	}

	if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(InWidget))
	{
		OutColor = AsUserWidget->GetColorAndOpacity();
		return true;
	}

	return false;
}

bool FAIWidgetCommand::ParseHexColor(const FString& InHex, FLinearColor& OutColor)
{
	FString Digits = InHex.TrimStartAndEnd();
	if (Digits.StartsWith(TEXT("#")))
	{
		Digits.RightChopInline(1);
	}

	if (Digits.Len() != 3 && Digits.Len() != 6 && Digits.Len() != 8)
	{
		return false;
	}

	for (const TCHAR Digit : Digits)
	{
		if (!FChar::IsHexDigit(Digit))
		{
			return false;
		}
	}

	// 여기까지 통과했으면 FromHex가 형식 때문에 실패할 일은 없다.
	OutColor = FLinearColor::FromSRGBColor(FColor::FromHex(Digits));
	return true;
}

bool FAIWidgetCommand::CaptureFrom(
	const UWidget* InWidget,
	EAIWidgetOperation InOperation,
	FName InTargetWidgetName,
	FAIWidgetCommand& OutCommand)
{
	if (!InWidget)
	{
		return false;
	}

	OutCommand = FAIWidgetCommand();
	OutCommand.Operation = InOperation;
	OutCommand.TargetWidgetName = InTargetWidgetName;

	switch (InOperation)
	{
	case EAIWidgetOperation::SetVisibility:
		OutCommand.Visibility = InWidget->GetVisibility();
		return true;

	case EAIWidgetOperation::SetEnabled:
		OutCommand.bEnabled = InWidget->GetIsEnabled();
		return true;

	case EAIWidgetOperation::SetRenderOpacity:
		OutCommand.RenderOpacity = InWidget->GetRenderOpacity();
		return true;

	case EAIWidgetOperation::SetRenderTranslation:
		OutCommand.RenderTranslation = InWidget->GetRenderTransform().Translation;
		return true;

	case EAIWidgetOperation::SetText:
		if (const UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			OutCommand.Text = AsTextBlock->GetText();
			return true;
		}
		return false;

	case EAIWidgetOperation::SetColorAndOpacity:
		return GetColorAndOpacity(InWidget, OutCommand.ColorAndOpacity);

	default:
		return false;
	}
}

FString FAIWidgetCommand::ToHexColor(const FLinearColor& InColor)
{
	return FString::Printf(TEXT("#%s"), *InColor.ToFColor(/*bSRGB=*/true).ToHex());
}

bool FAIWidgetCommand::ApplyTo(UWidget* InWidget, FText& OutError) const
{
	if (!InWidget)
	{
		OutError = LOCTEXT("NoWidget", "There is no widget to apply to.");
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

	case EAIWidgetOperation::SetColorAndOpacity:
		// 타입마다 받는 게 다르다. TextBlock만 FSlateColor고 나머지는 FLinearColor다.
		if (UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			AsTextBlock->SetColorAndOpacity(FSlateColor(ColorAndOpacity));
			return true;
		}
		if (UImage* AsImage = Cast<UImage>(InWidget))
		{
			AsImage->SetColorAndOpacity(ColorAndOpacity);
			return true;
		}
		if (UButton* AsButton = Cast<UButton>(InWidget))
		{
			AsButton->SetColorAndOpacity(ColorAndOpacity);
			return true;
		}
		if (UUserWidget* AsUserWidget = Cast<UUserWidget>(InWidget))
		{
			AsUserWidget->SetColorAndOpacity(ColorAndOpacity);
			return true;
		}

		OutError = FText::Format(
			LOCTEXT("NoColorProperty", "{0} ({1}) has no ColorAndOpacity."),
			FText::FromString(InWidget->GetName()),
			FText::FromString(InWidget->GetClass()->GetName()));
		return false;

	case EAIWidgetOperation::SetText:
		if (UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			AsTextBlock->SetText(Text);
			return true;
		}

		OutError = FText::Format(
			LOCTEXT("NotATextBlock", "{0} is not a TextBlock, so SetText does not apply."),
			FText::FromString(InWidget->GetName()));
		return false;

	default:
		OutError = FText::Format(
			LOCTEXT("UnsupportedOperation", "Unsupported operation: {0}"),
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

	case EAIWidgetOperation::SetColorAndOpacity:
		return ToHexColor(ColorAndOpacity);

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

FAIWidgetCommand FAIWidgetCommand::MakeSetColorAndOpacity(FName InTargetWidgetName, const FLinearColor& InColor)
{
	FAIWidgetCommand Command;
	Command.Operation = EAIWidgetOperation::SetColorAndOpacity;
	Command.TargetWidgetName = InTargetWidgetName;
	Command.ColorAndOpacity = InColor;
	return Command;
}

#undef LOCTEXT_NAMESPACE
