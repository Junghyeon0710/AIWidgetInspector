// AI Widget Inspector

#include "Commands/AIWidgetRuntimePreview.h"

#include "AIWidgetInspectorLog.h"

#include "Components/TextBlock.h"
#include "Components/Widget.h"

#define LOCTEXT_NAMESPACE "FAIWidgetRuntimePreview"

bool FAIWidgetRuntimePreview::CanApply(const UWidget* InWidget, EAIWidgetOperation InOperation)
{
	if (!InWidget || !FAIWidgetCommand::IsRuntimeSupported(InOperation))
	{
		return false;
	}

	// Text는 아무 Widget에나 있는 게 아니다. MVP에서는 UTextBlock만 받는다.
	if (InOperation == EAIWidgetOperation::SetText)
	{
		return InWidget->IsA<UTextBlock>();
	}

	// 색도 마찬가지로 일부 타입에만 있다. 읽을 수 있어야 되돌릴 수도 있다.
	if (InOperation == EAIWidgetOperation::SetColorAndOpacity)
	{
		FLinearColor Unused;
		return FAIWidgetCommand::GetColorAndOpacity(InWidget, Unused);
	}

	return true;
}

bool FAIWidgetRuntimePreview::IsNoOp(const UWidget* InWidget, const FAIWidgetCommand& InCommand)
{
	if (!InWidget)
	{
		return false;
	}

	switch (InCommand.Operation)
	{
	case EAIWidgetOperation::SetVisibility:
		return InWidget->GetVisibility() == InCommand.Visibility;

	case EAIWidgetOperation::SetEnabled:
		return InWidget->GetIsEnabled() == InCommand.bEnabled;

	case EAIWidgetOperation::SetRenderOpacity:
		return FMath::IsNearlyEqual(InWidget->GetRenderOpacity(), InCommand.RenderOpacity);

	case EAIWidgetOperation::SetRenderTranslation:
		return InWidget->GetRenderTransform().Translation.Equals(InCommand.RenderTranslation);

	case EAIWidgetOperation::SetText:
		if (const UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			return AsTextBlock->GetText().EqualTo(InCommand.Text);
		}
		return false;

	case EAIWidgetOperation::SetColorAndOpacity:
	{
		FLinearColor CurrentColor;
		if (!FAIWidgetCommand::GetColorAndOpacity(InWidget, CurrentColor))
		{
			return false;
		}

		// 8비트로 떨어지는 값이라 정확히 같은 색을 다시 넣는 경우가 흔하다.
		return CurrentColor.Equals(InCommand.ColorAndOpacity, UE_KINDA_SMALL_NUMBER);
	}

	default:
		return false;
	}
}

void FAIWidgetRuntimePreview::CaptureOriginal(const UWidget* InWidget, FAIWidgetPreviewEntry& OutEntry)
{
	OutEntry.OriginalVisibility = InWidget->GetVisibility();
	OutEntry.bOriginalEnabled = InWidget->GetIsEnabled();
	OutEntry.OriginalRenderOpacity = InWidget->GetRenderOpacity();
	OutEntry.OriginalRenderTranslation = InWidget->GetRenderTransform().Translation;

	if (const UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
	{
		OutEntry.OriginalText = AsTextBlock->GetText();
	}

	OutEntry.bHadColorAndOpacity = FAIWidgetCommand::GetColorAndOpacity(InWidget, OutEntry.OriginalColorAndOpacity);
}

void FAIWidgetRuntimePreview::RestoreOriginal(UWidget* InWidget, const FAIWidgetPreviewEntry& InEntry)
{
	switch (InEntry.Operation)
	{
	case EAIWidgetOperation::SetVisibility:
		InWidget->SetVisibility(InEntry.OriginalVisibility);
		break;

	case EAIWidgetOperation::SetEnabled:
		InWidget->SetIsEnabled(InEntry.bOriginalEnabled);
		break;

	case EAIWidgetOperation::SetRenderOpacity:
		InWidget->SetRenderOpacity(InEntry.OriginalRenderOpacity);
		break;

	case EAIWidgetOperation::SetRenderTranslation:
		InWidget->SetRenderTranslation(InEntry.OriginalRenderTranslation);
		break;

	case EAIWidgetOperation::SetText:
		if (UTextBlock* AsTextBlock = Cast<UTextBlock>(InWidget))
		{
			AsTextBlock->SetText(InEntry.OriginalText);
		}
		break;

	case EAIWidgetOperation::SetColorAndOpacity:
		// 원래 고정 색이 없었다면(Foreground 상속 등) 아무 색이나 써 넣는 것이
		// 되돌리기가 아니다. 그 경우는 건드리지 않고 둔다.
		if (InEntry.bHadColorAndOpacity)
		{
			FText RestoreError;
			FAIWidgetCommand::MakeSetColorAndOpacity(InEntry.WidgetName, InEntry.OriginalColorAndOpacity)
				.ApplyTo(InWidget, RestoreError);
		}
		break;

	default:
		break;
	}
}

FAIWidgetPreviewEntry* FAIWidgetRuntimePreview::FindEntry(const UWidget* InWidget, EAIWidgetOperation InOperation)
{
	return Entries.FindByPredicate(
		[InWidget, InOperation](const FAIWidgetPreviewEntry& Entry)
		{
			return Entry.Operation == InOperation && Entry.Widget.Get() == InWidget;
		});
}

void FAIWidgetRuntimePreview::PruneDeadEntries()
{
	Entries.RemoveAll(
		[](const FAIWidgetPreviewEntry& Entry)
		{
			return !Entry.Widget.IsValid();
		});
}

bool FAIWidgetRuntimePreview::Apply(UWidget* InWidget, const FAIWidgetCommand& InCommand, FText& OutError)
{
	if (!InWidget)
	{
		OutError = LOCTEXT("NoWidget", "적용할 UMG Widget이 없습니다. 선택된 Widget이 순수 Slate일 수 있습니다.");
		return false;
	}

	if (!FAIWidgetCommand::IsRuntimeSupported(InCommand.Operation))
	{
		OutError = FText::Format(
			LOCTEXT("NotWhitelisted", "허용되지 않은 Operation입니다: {0}"),
			FText::FromString(FAIWidgetCommand::GetOperationName(InCommand.Operation)));
		return false;
	}

	// 값이 안 바뀌는 요청은 무시한다.
	//
	// UI 컨트롤은 선택이 바뀌어 값이 처음 채워질 때도 OnValueChanged를 쏘기 때문에,
	// 이 가드가 없으면 Widget을 고르기만 해도 "미리보기 3건"이 잡히고 Revert 버튼이 켜진다.
	// AI가 현재 값과 같은 값을 보내오는 경우도 여기서 걸린다.
	if (IsNoOp(InWidget, InCommand))
	{
		return true;
	}

	PruneDeadEntries();

	// 원본은 이 Widget/Operation을 처음 건드릴 때만 기록한다.
	// 그래야 같은 속성을 여러 번 바꿔도 Revert가 최초 값으로 돌아간다.
	FAIWidgetPreviewEntry* Entry = FindEntry(InWidget, InCommand.Operation);
	const bool bIsNewEntry = (Entry == nullptr);

	FAIWidgetPreviewEntry NewEntry;
	if (bIsNewEntry)
	{
		NewEntry.Widget = InWidget;
		NewEntry.Operation = InCommand.Operation;
		NewEntry.WidgetName = InWidget->GetFName();
		CaptureOriginal(InWidget, NewEntry);
		Entry = &NewEntry;
	}

	if (!InCommand.ApplyTo(InWidget, OutError))
	{
		return false;
	}

	Entry->Description = InCommand.Describe();

	if (bIsNewEntry)
	{
		Entries.Add(MoveTemp(NewEntry));
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("런타임 미리보기 적용: %s.%s"),
		*InWidget->GetName(), *InCommand.Describe());

	ChangedEvent.Broadcast();
	return true;
}

void FAIWidgetRuntimePreview::RevertAll()
{
	if (Entries.IsEmpty())
	{
		return;
	}

	for (const FAIWidgetPreviewEntry& Entry : Entries)
	{
		if (UWidget* Widget = Entry.Widget.Get())
		{
			RestoreOriginal(Widget, Entry);
		}
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("런타임 미리보기 %d건을 되돌렸습니다."), Entries.Num());

	Entries.Reset();
	ChangedEvent.Broadcast();
}

#undef LOCTEXT_NAMESPACE
