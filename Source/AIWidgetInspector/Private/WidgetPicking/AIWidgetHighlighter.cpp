// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "WidgetPicking/AIWidgetHighlighter.h"

#include "AIWidgetInspectorLog.h"
#include "Inspection/AIWidgetSelection.h"
#include "WidgetPicking/AIWidgetPicker.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/PaintGeometry.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"

namespace AIWidgetHighlighterColors
{
	/** Hover 경로의 조상들. 얕을수록 붉고 깊을수록 초록. */
	static const FLinearColor HoverAncestorNear(1.0f, 0.25f, 0.0f);
	static const FLinearColor HoverAncestorFar(0.2f, 1.0f, 0.3f);
	static constexpr float HoverAncestorAlpha = 0.35f;

	/** Hover 경로의 가장 깊은 Widget = 클릭하면 잡히는 대상. */
	static const FLinearColor HoverLeaf(0.15f, 1.0f, 0.35f, 1.0f);

	/** 선택 유지 하이라이트. */
	static const FLinearColor Selected(1.0f, 0.55f, 0.1f, 1.0f);
}

FAIWidgetHighlighter::FAIWidgetHighlighter(const TSharedRef<FAIWidgetPicker>& InPicker, const TSharedRef<FAIWidgetSelection>& InSelection)
	: Picker(InPicker)
	, Selection(InSelection)
{
}

void FAIWidgetHighlighter::Register()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetWidgetReflector(AsShared());
	}
}

void FAIWidgetHighlighter::SetHighlightSelected(bool bInHighlightSelected)
{
	bHighlightSelected = bInHighlightSelected;
}

bool FAIWidgetHighlighter::OpenSourceLocation(const FString& InFileName, int32 InLineNumber, int32 InColumnNumber) const
{
	return SourceAccessDelegate.IsBound() && SourceAccessDelegate.Execute(InFileName, InLineNumber, InColumnNumber);
}

bool FAIWidgetHighlighter::OpenAsset(UObject* InAsset) const
{
	return InAsset != nullptr && AssetAccessDelegate.IsBound() && AssetAccessDelegate.Execute(InAsset);
}

bool FAIWidgetHighlighter::OpenDebugObject(UObject* InObject) const
{
	return InObject != nullptr && DebugObjectAccessDelegate.IsBound() && DebugObjectAccessDelegate.Execute(InObject);
}

void FAIWidgetHighlighter::OnWidgetPicked()
{
	// 우리 Input Pre-Processor가 ESC를 먼저 먹기 때문에 보통은 여기까지 오지 않는다.
	// 전처리기가 등록되지 않은 상황을 위한 안전망.
	Picker->ExitInspectMode();
}

bool FAIWidgetHighlighter::IsInPickingMode() const
{
	return Picker->IsInspecting();
}

bool FAIWidgetHighlighter::IsVisualizingLayoutUnderCursor() const
{
	// true를 반환하면 엔진이 커서 아래 경로를 계산해서 Visualize()로 넘겨준다.
	return Picker->IsInspecting();
}

void FAIWidgetHighlighter::SetWidgetsToVisualize(const FWidgetPath& InWidgetsToVisualize)
{
	// Hover 대상은 Picker가 자체 Hit Test로 관리한다. 엔진이 밀어주는 경로는 Visualize()에서 직접 쓴다.
}

bool FAIWidgetHighlighter::ReflectorNeedsToDrawIn(TSharedRef<SWindow> ThisWindow) const
{
	// Inspect Mode가 아니어도 선택된 Widget 하이라이트는 계속 그려야 하므로,
	// 선택 대상이 들어있는 창에 대해서는 그리겠다고 알린다.
	if (!bHighlightSelected || !Selection->IsValid())
	{
		return false;
	}

	const TSharedPtr<SWindow> SelectedWindow = Selection->GetWeakPath().Window.Pin();
	return SelectedWindow == ThisWindow;
}

int32 FAIWidgetHighlighter::Visualize(const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId)
{
	// Inspect Mode 중에는 커서 아래 경로가 우선. 엔진이 이미 살아있는 경로로 만들어 넘겨준다.
	if (Picker->IsInspecting() && InWidgetsToVisualize.IsValid())
	{
		return DrawHoveredPath(InWidgetsToVisualize, OutDrawElements, LayerId);
	}

	return DrawSelectedWidget(OutDrawElements, LayerId);
}

int32 FAIWidgetHighlighter::DrawHoveredPath(const FWidgetPath& InWidgetPath, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!InWidgetPath.TopLevelWindow.IsValid())
	{
		return LayerId;
	}

	const TSharedRef<SWindow> TopLevelWindow = InWidgetPath.TopLevelWindow.ToSharedRef();
	const int32 NumWidgets = InWidgetPath.Widgets.Num();

	for (int32 Index = 0; Index < NumWidgets; ++Index)
	{
		const bool bIsLeaf = (Index == NumWidgets - 1);

		FLinearColor Color;
		if (bIsLeaf)
		{
			Color = AIWidgetHighlighterColors::HoverLeaf;
		}
		else
		{
			const float Factor = (NumWidgets > 1) ? static_cast<float>(Index) / static_cast<float>(NumWidgets - 1) : 0.0f;
			Color = FMath::Lerp(AIWidgetHighlighterColors::HoverAncestorNear, AIWidgetHighlighterColors::HoverAncestorFar, Factor);
			Color.A = AIWidgetHighlighterColors::HoverAncestorAlpha;
		}

		DrawWidgetOutline(InWidgetPath.Widgets[Index], TopLevelWindow, Color, OutDrawElements, LayerId);
	}

	return LayerId;
}

int32 FAIWidgetHighlighter::DrawSelectedWidget(FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!bHighlightSelected || !Selection->IsValid())
	{
		return LayerId;
	}

	const FWidgetPath SelectedPath = Selection->ResolvePath();
	if (!SelectedPath.IsValid() || !SelectedPath.TopLevelWindow.IsValid())
	{
		return LayerId;
	}

	// 창마다 Visualize가 불리므로 지금 그리고 있는 창이 아니면 건너뛴다.
	if (SelectedPath.TopLevelWindow.Get() != OutDrawElements.GetPaintWindow())
	{
		return LayerId;
	}

	// FArrangedChildren::IsValidIndex()는 const가 아니라서 직접 범위를 본다.
	const int32 SelectedIndex = Selection->GetSelectedIndex();
	if (SelectedIndex < 0 || SelectedIndex >= SelectedPath.Widgets.Num())
	{
		return LayerId;
	}

	DrawWidgetOutline(
		SelectedPath.Widgets[SelectedIndex],
		SelectedPath.TopLevelWindow.ToSharedRef(),
		AIWidgetHighlighterColors::Selected,
		OutDrawElements,
		LayerId);

	return LayerId;
}

void FAIWidgetHighlighter::DrawWidgetOutline(
	const FArrangedWidget& InArrangedWidget,
	const TSharedRef<SWindow>& InTopLevelWindow,
	const FLinearColor& InColor,
	FSlateWindowElementList& OutDrawElements,
	int32& InOutLayerId)
{
	FPaintGeometry WindowSpaceGeometry = InArrangedWidget.Geometry.ToPaintGeometry();
	WindowSpaceGeometry.AppendTransform(TransformCast<FSlateLayoutTransform>(Inverse(InTopLevelWindow->GetPositionInScreen())));
	WindowSpaceGeometry.CommitTransformsIfUsingLegacyConstructor();

	const FVector2D LocalSize = FVector2D(WindowSpaceGeometry.GetLocalSize());

	// 크기가 0인 Widget은 박스로는 보이지 않으므로 대각선을 긋는다. (SWidgetReflector와 동일)
	if (FMath::IsNearlyZero(LocalSize.X) || FMath::IsNearlyZero(LocalSize.Y))
	{
		TArray<FVector2D> LinePoints;
		LinePoints.Add(FVector2D::ZeroVector);
		LinePoints.Add(LocalSize);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++InOutLayerId,
			WindowSpaceGeometry,
			LinePoints,
			ESlateDrawEffect::None,
			InColor,
			true,
			2.0f);
	}
	else
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++InOutLayerId,
			WindowSpaceGeometry,
			FCoreStyle::Get().GetBrush(TEXT("Debug.Border")),
			ESlateDrawEffect::None,
			InColor);
	}
}
