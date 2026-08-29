// AI Widget Inspector

#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "AIWidgetInspectorToolset.generated.h"

/**
 * 선택된 Widget을 AI가 직접 다룰 수 있게 열어 주는 Toolset.
 *
 * 엔진의 ModelContextProtocol 플러그인이 이 함수들을 MCP Tool로 노출한다. 그래서 AI CLI는
 * JSON을 돌려주고 우리가 그걸 읽어 적용하는 대신, 여기 함수를 직접 부르고 결과를 보고
 * 다음 수를 정한다. 서버를 따로 띄우지 않는 이유는 엔진이 이미 하나 갖고 있기 때문이다.
 *
 * 화이트리스트는 그대로다. AI가 부를 수 있는 것은 여기 있는 함수뿐이고, 값은 응답 JSON
 * 경로와 똑같은 파서·검사기를 지난다. 달라진 건 AI가 실패 이유를 읽고 다시 시도할 수
 * 있다는 점이다.
 *
 * 함수 앞의 주석이 곧 AI가 읽는 설명이다. 여기 적힌 내용이 부정확하면 AI가 잘못 고른다.
 */
UCLASS(BlueprintType, MinimalAPI)
class UAIWidgetInspectorToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * 에디터에서 지금 선택된 UMG/Slate Widget의 정보를 돌려준다.
	 *
	 * 타입, 현재 상태(Visibility/Enabled/크기/배치), 슬롯, 부모, 소속 Widget Blueprint,
	 * 그리고 그 Widget을 만든 C++ 위치와 주변 코드까지 담겨 있다.
	 * 무엇을 바꿀지 정하기 전에 먼저 부른다.
	 * 선택된 것이 없으면 그렇게 알려 준다. 그때는 사용자에게 Widget을 클릭해 달라고 해야 한다.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString GetSelectedWidget();

	/**
	 * 선택된 Widget이 속한 UserWidget 안의 Widget을 모두 나열한다.
	 *
	 * 이름, 클래스, 현재 Visibility를 준다. 변경 함수의 TargetWidget에 넣을 수 있는 이름은
	 * 이 목록에 있는 것뿐이므로, 선택된 것 말고 다른 Widget을 바꾸려면 먼저 이걸 부른다.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString ListWidgetTree();

	/**
	 * 화면에 떠 있는 Widget 인스턴스에 변경을 적용한다. 에셋 파일은 바뀌지 않는다.
	 *
	 * RevertPreview로 처음 값까지 되돌릴 수 있으므로 되돌리기 비용이 낮다.
	 * 색이나 투명도처럼 눈으로 봐야 하는 변경은 항상 이걸 먼저 쓴다.
	 *
	 * @param Operation      SetVisibility / SetEnabled / SetText / SetRenderOpacity / SetRenderTranslation / SetColorAndOpacity 중 하나. 그 외는 거부된다.
	 * @param TargetWidget   대상 Widget 이름. ListWidgetTree가 돌려준 이름이어야 한다.
	 * @param ValueJson      Operation이 정하는 JSON 값. SetVisibility는 "Visible"/"Collapsed"/"Hidden"/"HitTestInvisible"/"SelfHitTestInvisible" 중 하나를 따옴표에 넣은 문자열, SetEnabled는 true 또는 false, SetText는 따옴표 친 문자열, SetRenderOpacity는 0~1 숫자, SetRenderTranslation은 {"x":30,"y":0}, SetColorAndOpacity는 "#RRGGBB" 또는 "#RRGGBBAA"(sRGB).
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString PreviewWidgetChange(
		const FString& Operation,
		const FString& TargetWidget,
		const FString& ValueJson);

	/**
	 * Widget Blueprint 에셋 원본에 변경을 쓴다. 되돌리려면 에디터에서 Ctrl+Z를 눌러야 한다.
	 *
	 * 저장까지 하지는 않으므로 사용자가 직접 저장해야 파일에 남는다.
	 * 미리보기와 달리 파일을 건드리는 일이므로, 사용자가 분명히 그렇게 요청했을 때만 부른다.
	 * 어떻게 보일지 확인하려는 것이라면 PreviewWidgetChange를 쓴다.
	 *
	 * @param Operation      PreviewWidgetChange와 같다.
	 * @param TargetWidget   PreviewWidgetChange와 같다.
	 * @param ValueJson      PreviewWidgetChange와 같다.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString ApplyWidgetChangeToAsset(
		const FString& Operation,
		const FString& TargetWidget,
		const FString& ValueJson);

	/** PreviewWidgetChange로 바꾼 것을 모두 처음 값으로 되돌린다. 에셋에 쓴 변경은 대상이 아니다. */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString RevertPreview();
};
