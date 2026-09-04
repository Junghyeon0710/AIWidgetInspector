// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "Components/SlateWrapperTypes.h"
#include "CoreMinimal.h"

class UWidget;

/**
 * Widget에 할 수 있는 변경.
 *
 * 화이트리스트다. AI가 문자열로 무엇을 보내든 여기 없는 것은 실행되지 않는다.
 * 새 Operation을 추가하려면 Executor에 실제 구현을 넣고 여기에 등록해야 한다.
 */
enum class EAIWidgetOperation : uint8
{
	None,
	SetVisibility,
	SetEnabled,
	SetText,
	SetRenderOpacity,
	SetRenderTranslation,
	SetColorAndOpacity,
};

/**
 * 변경 한 건.
 *
 * Operation에 따라 값 필드 중 하나만 의미가 있다. 유니온을 쓰지 않은 건 FText가
 * 자명하지 않은 소멸자를 갖고 있어서이고, 어차피 한 번에 몇 개 안 만든다.
 */
struct FAIWidgetCommand
{
	EAIWidgetOperation Operation = EAIWidgetOperation::None;

	/** 대상 UMG Widget 이름. Phase 6에서 AI가 지목한 이름과 실제 선택을 대조하는 데 쓴다. */
	FName TargetWidgetName;

	ESlateVisibility Visibility = ESlateVisibility::Visible;
	bool bEnabled = true;
	FText Text;
	float RenderOpacity = 1.0f;
	FVector2D RenderTranslation = FVector2D::ZeroVector;
	FLinearColor ColorAndOpacity = FLinearColor::White;

	/**
	 * 이 명령이 가리키는 속성을 Widget에 쓴다.
	 *
	 * 런타임 미리보기(화면에 떠 있는 인스턴스)와 영구 변경(에셋 안의 원본 Widget)이 같은 코드를 쓴다.
	 * 그래야 Operation을 추가할 때 한 곳만 고치면 된다. 트랜잭션·Modify는 호출하는 쪽 책임이다.
	 */
	AIWIDGETINSPECTOR_API bool ApplyTo(UWidget* InWidget, FText& OutError) const;

	/** "SetVisibility(Collapsed)" 형태. 미리보기 목록과 로그에 쓴다. */
	AIWIDGETINSPECTOR_API FString Describe() const;

	/** Operation 이름 없이 값만. "Collapsed", "0.50", "30, 0" 처럼. */
	AIWIDGETINSPECTOR_API FString DescribeValue() const;

	static AIWIDGETINSPECTOR_API const TCHAR* GetOperationName(EAIWidgetOperation InOperation);

	/** 살아있는 인스턴스에 지금 적용할 수 있는 Operation인지. */
	static AIWIDGETINSPECTOR_API bool IsRuntimeSupported(EAIWidgetOperation InOperation);

	//~ 자주 쓰는 조합을 만드는 헬퍼.
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetVisibility(FName InTargetWidgetName, ESlateVisibility InVisibility);
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetEnabled(FName InTargetWidgetName, bool bInEnabled);
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetText(FName InTargetWidgetName, const FText& InText);
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetRenderOpacity(FName InTargetWidgetName, float InOpacity);
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetRenderTranslation(FName InTargetWidgetName, const FVector2D& InTranslation);
	static AIWIDGETINSPECTOR_API FAIWidgetCommand MakeSetColorAndOpacity(FName InTargetWidgetName, const FLinearColor& InColor);

	/**
	 * Widget이 지금 갖고 있는 색. 색을 가진 Widget이 아니면 false.
	 *
	 * ColorAndOpacity는 UWidget이 아니라 몇몇 구체 타입에만 있고, 타입마다 FSlateColor와
	 * FLinearColor로 갈린다. 그 분기를 여기 한 곳에 모아 두고 미리보기와 검사기가 같이 쓴다.
	 */
	static AIWIDGETINSPECTOR_API bool GetColorAndOpacity(const UWidget* InWidget, FLinearColor& OutColor);

	/**
	 * Widget이 지금 갖고 있는 값을 명령으로 만든다.
	 *
	 * 미리보기를 에셋으로 넘길 때 쓴다. 미리보기 항목은 되돌리려고 '처음 값'만 들고 있어서,
	 * 지금 화면에 보이는 값은 Widget에서 직접 읽어야 한다.
	 */
	static AIWIDGETINSPECTOR_API bool CaptureFrom(
		const UWidget* InWidget,
		EAIWidgetOperation InOperation,
		FName InTargetWidgetName,
		FAIWidgetCommand& OutCommand);

	/**
	 * "#RRGGBB" 또는 "#RRGGBBAA"를 읽는다. '#'은 있어도 없어도 된다.
	 *
	 * FColor::FromHex를 그냥 부르지 않는다. 그 함수는 형식이 틀리면 오류 없이 투명한 검정을
	 * 돌려주기 때문에, 오타 하나가 "색이 사라짐"으로 조용히 적용된다. 여기서 먼저 막는다.
	 *
	 * 입력은 sRGB로 보고 선형으로 변환한다. 그러지 않으면 지정한 색보다 밝게 나온다.
	 */
	static AIWIDGETINSPECTOR_API bool ParseHexColor(const FString& InHex, FLinearColor& OutColor);

	/** 선형 색을 "#RRGGBBAA" 표기로. 미리보기 목록과 AI에게 보여 줄 때 쓴다. */
	static AIWIDGETINSPECTOR_API FString ToHexColor(const FLinearColor& InColor);
};
