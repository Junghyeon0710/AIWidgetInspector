// AI Widget Inspector

#pragma once

#include "Commands/AIWidgetCommand.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWidget;

/**
 * 적용된 미리보기 하나.
 *
 * 되돌리려면 원래 값이 필요한데, 런타임 변경은 Transaction을 타지 않으므로
 * Ctrl+Z로는 돌아오지 않는다. 그래서 여기에 직접 들고 있는다.
 */
struct FAIWidgetPreviewEntry
{
	TWeakObjectPtr<UWidget> Widget;
	EAIWidgetOperation Operation = EAIWidgetOperation::None;

	/** Widget이 사라진 뒤에도 목록에 뭐였는지는 보여줄 수 있게 이름을 복사해 둔다. */
	FName WidgetName;

	/** 마지막으로 적용한 값의 설명. */
	FString Description;

	//~ 최초 적용 시점의 값.
	ESlateVisibility OriginalVisibility = ESlateVisibility::Visible;
	bool bOriginalEnabled = true;
	FText OriginalText;
	float OriginalRenderOpacity = 1.0f;
	FVector2D OriginalRenderTranslation = FVector2D::ZeroVector;
	FLinearColor OriginalColorAndOpacity = FLinearColor::White;

	/** 최초 적용 시점에 이 Widget이 고정된 색을 갖고 있었는지. 없었다면 되돌릴 것도 없다. */
	bool bHadColorAndOpacity = false;
};

/**
 * 살아있는 UWidget 인스턴스에만 적용되는 임시 변경.
 *
 * 에셋은 건드리지 않는다. 에디터를 다시 열거나 Widget이 다시 만들어지면 사라진다.
 * "이렇게 바꾸면 어떻게 보이나"를 확인하는 용도이고, 원본을 고치는 건 Phase 7의 일이다.
 *
 * (Widget, Operation) 쌍마다 항목 하나를 두고 최초 값만 원본으로 기억한다.
 * 같은 속성을 여러 번 바꿔도 Revert하면 처음 값으로 정확히 돌아간다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetRuntimePreview
{
public:
	/** 적용한다. 실패하면 이유를 OutError에 담고 false. */
	bool Apply(UWidget* InWidget, const FAIWidgetCommand& InCommand, FText& OutError);

	/** 이 Widget에 이 Operation을 적용할 수 있는지. UI에서 컨트롤을 켜고 끄는 데 쓴다. */
	static bool CanApply(const UWidget* InWidget, EAIWidgetOperation InOperation);

	/** 지금 값과 같은 값을 넣으려는 요청인지. 같으면 미리보기로 기록하지 않는다. */
	static bool IsNoOp(const UWidget* InWidget, const FAIWidgetCommand& InCommand);

	void RevertAll();

	/**
	 * 되돌리지 않고 목록만 비운다.
	 *
	 * 미리보기를 에셋에 써 넣은 뒤에 쓴다. 그 시점에는 미리보기 값이 곧 에셋 값이라
	 * 더 이상 임시가 아니다. 항목을 남겨 두면 Revert가 방금 저장한 값을 처음 값으로
	 * 되돌려서, 화면과 에셋이 어긋난 채로 남는다.
	 */
	void ForgetAll();

	int32 Num() const { return Entries.Num(); }
	const TArray<FAIWidgetPreviewEntry>& GetEntries() const { return Entries; }

	FSimpleMulticastDelegate& OnChanged() { return ChangedEvent; }

private:
	FAIWidgetPreviewEntry* FindEntry(const UWidget* InWidget, EAIWidgetOperation InOperation);
	static void CaptureOriginal(const UWidget* InWidget, FAIWidgetPreviewEntry& OutEntry);
	static void RestoreOriginal(UWidget* InWidget, const FAIWidgetPreviewEntry& InEntry);

	/** 이미 파괴된 Widget의 항목을 걷어낸다. */
	void PruneDeadEntries();

	TArray<FAIWidgetPreviewEntry> Entries;
	FSimpleMulticastDelegate ChangedEvent;
};
