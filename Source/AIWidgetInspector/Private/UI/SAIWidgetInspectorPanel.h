// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#pragma once

#include "AI/AIWidgetProvider.h"
#include "Commands/AIWidgetCommandValidator.h"
#include "CoreMinimal.h"
#include "Inspection/AIWidgetInspectionResult.h"
#include "Inspection/AIWidgetSourceResolver.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"

class FAIWidgetHighlighter;
class FAIWidgetPicker;
class FAIWidgetRuntimePreview;
class FAIWidgetSelection;
class UWidget;
struct FAIWidgetCommand;
class ITableRow;
class SAIWidgetTerminal;
class SMultiLineEditableTextBox;
class STableViewBase;
template <typename ItemType> class SListView;
template <typename OptionType> class SComboBox;

/**
 * 선택된 Widget의 정보와 경로를 보여주고, 그 Widget에 대해 AI에게 물어보는 패널.
 *
 * Visibility/Enabled처럼 매 프레임 달라질 수 있는 값은 TAttribute로 SWidget에서 바로 읽는다.
 * 반대로 UMG/Blueprint 추적 결과는 선택이 바뀔 때만 달라지므로 한 번 계산해서 들고 있는다.
 */
class SAIWidgetInspectorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAIWidgetInspectorPanel) {}
	SLATE_END_ARGS()

	/** 패널에 포커스가 있을 때 단축키를 받는다. Ctrl+S가 전역 저장과 겹치지 않게 하려는 것이다. */
	virtual FReply OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<FAIWidgetPicker>& InPicker,
		const TSharedRef<FAIWidgetSelection>& InSelection,
		const TSharedRef<FAIWidgetHighlighter>& InHighlighter);

	virtual ~SAIWidgetInspectorPanel() override;

private:
	/** 경로 목록의 한 줄. */
	struct FPathEntry
	{
		int32 Index = INDEX_NONE;
		FText Label;
	};
	using FPathEntryPtr = TSharedPtr<FPathEntry>;
	using FProviderPtr = TSharedPtr<IAIWidgetProvider>;

	/** 변경 계획 목록의 한 줄. 검사에 떨어진 항목도 이유와 함께 남긴다. */
	struct FChangePlanEntry
	{
		FAIWidgetCommand Command;
		FAIWidgetCommandValidation Validation;
	};
	using FChangePlanEntryPtr = TSharedPtr<FChangePlanEntry>;

	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildSlateSection();
	TSharedRef<SWidget> BuildUMGSection();
	TSharedRef<SWidget> BuildSourceSection();
	TSharedRef<SWidget> BuildPathList();
	TSharedRef<SWidget> BuildPreviewSection();
	TSharedRef<SWidget> BuildAskAISection();
	TSharedRef<SWidget> BuildTerminalSection();
	TSharedRef<SWidget> BuildChangePlanSection();

	/** 섹션 하나를 접었다 펼 수 있게 감싼다. 패널이 길어져 아래쪽이 스크롤 밖으로 밀리는 걸 막는다. */
	static TSharedRef<SWidget> MakeSection(const FText& InTitle, bool bInInitiallyCollapsed, TSharedRef<SWidget> InContent,
		TAttribute<EVisibility> InVisibility = EVisibility::Visible);
	static TSharedRef<SWidget> MakeDetailRow(const FText& InLabel, TAttribute<FText> InValue);

	TSharedRef<ITableRow> HandleGeneratePathRow(FPathEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwnerTable);
	void HandlePathSelectionChanged(FPathEntryPtr InEntry, ESelectInfo::Type InSelectInfo);

	void HandleSelectionChanged();
	void RebuildPathEntries();
	void SyncListSelection();
	void RefreshInspection();

	FReply HandleToggleInspectClicked();
	FReply HandleSelectParentClicked();
	FReply HandleSelectChildClicked();
	FReply HandleClearClicked();
	FReply HandleOpenBlueprintClicked();
	FReply HandleOpenSourceClicked();
	FReply HandleSaveAssetClicked();

	bool CanSelectParent() const;
	bool CanSelectChild() const;
	bool HasSelection() const;
	bool CanOpenBlueprint() const;
	bool CanOpenSource() const;
	bool CanSaveAsset() const;
	void ExecuteSaveAsset();

	/** 패널 안에서만 듣는 단축키. */
	TSharedPtr<FUICommandList> PanelCommands;

	ECheckBoxState GetHighlightSelectedState() const;
	void HandleHighlightSelectedChanged(ECheckBoxState InNewState);

	//~ Runtime Preview
	/** 미리보기를 적용할 UMG Widget. 순수 Slate Widget이 선택됐으면 nullptr. */
	UWidget* GetPreviewTargetWidget() const;

	bool CanPreview() const;
	bool CanPreviewText() const;
	bool HasPreviews() const;

	void ApplyPreviewCommand(const FAIWidgetCommand& InCommand);

	int32 GetPreviewVisibilityValue() const;
	void HandlePreviewVisibilityChanged(int32 InNewValue, ESelectInfo::Type InSelectInfo);

	ECheckBoxState GetPreviewEnabledState() const;
	void HandlePreviewEnabledChanged(ECheckBoxState InNewState);

	TOptional<float> GetPreviewOpacity() const;
	void HandlePreviewOpacityChanged(float InNewValue);

	TOptional<float> GetPreviewTranslationX() const;
	TOptional<float> GetPreviewTranslationY() const;
	void HandlePreviewTranslationXChanged(float InNewValue);
	void HandlePreviewTranslationYChanged(float InNewValue);

	FText GetPreviewText() const;
	void HandlePreviewTextCommitted(const FText& InText, ETextCommit::Type InCommitType);

	FText GetRevertPreviewText() const;
	FReply HandleRevertPreviewClicked();

	/** 지금 미리보기 중인 값들을 에셋에 쓰고 저장까지 한 번에 한다. */
	FReply HandleCommitPreviewClicked();
	bool CanCommitPreview() const;
	FText GetCommitPreviewText() const;

	FText GetPreviewErrorText() const { return PreviewErrorText; }
	EVisibility GetPreviewErrorVisibility() const;
	//~ End Runtime Preview

	//~ AI
	TSharedRef<SWidget> HandleGenerateProviderItem(FProviderPtr InProvider);
	void HandleProviderChanged(FProviderPtr InProvider, ESelectInfo::Type InSelectInfo);
	FText GetActiveProviderText() const;
	FText GetActiveProviderTooltip() const;

	//~ Provider를 지금 쓸 수 없을 때 패널에 띄우는 안내.
	FText GetProviderWarningText() const;
	/**
	 * 헤더 상태줄의 색.
	 *
	 * Inspect Mode는 클릭의 뜻이 달라지는 상태다. 회색 한 줄로만 알리면, 켜 두고도 모른 채
	 * 위젯을 집게 된다. 선택한 위젯이 사라진 것도 지금 보고 있는 값이 옛것이라는 뜻이라
	 * 눈에 띄어야 한다.
	 */
	FSlateColor GetStatusColor() const;

	EVisibility GetProviderWarningVisibility() const;

	/**
	 * 답을 주고받는 칸을 보일지.
	 *
	 * 대화형 Provider는 답이 터미널에 흐른다. 그런데도 이 칸이 남아 있으면, 한 줄짜리
	 * 안내만 담은 빈 상자와 아무것도 파싱할 것이 없는 버튼을 늘 보게 된다. 무엇을
	 * 하라는 것인지 헷갈리는 UI가 화면의 절반을 차지한다.
	 */
	EVisibility GetResponseAreaVisibility() const;

	FReply HandleCopyContextClicked();
	FReply HandleAskAIClicked();
	FReply HandleRequestChangeClicked();
	bool CanSendRequest() const;
	void HandleAIResponse(const FAIWidgetResponse& InResponse);
	void SendRequest(EAIWidgetRequestKind InKind);

	/**
	 * Context를 파일로 쓰고, 그 파일을 읽으라는 한 줄을 터미널의 CLI에 보낸다.
	 *
	 * Context를 그대로 치지 않는 이유는 두 가지다. 여러 줄이라 TUI가 첫 줄에서 전송해
	 * 버리고, 길어서 화면을 다 덮는다. 파일로 넘기면 대화에는 질문만 남는다.
	 */
	void SendToTerminal(EAIWidgetRequestKind InKind);
	//~ End AI

	//~ Change plan
	TSharedRef<ITableRow> HandleGenerateChangePlanRow(FChangePlanEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwnerTable);

	FReply HandleParseChangeClicked();

	/** 응답 상자의 내용을 계획으로 만든다. 적용 가능한 건수를 돌려준다. */
	int32 BuildChangePlanFromResponse();

	/** 계획 중 통과한 것들을 살아있는 인스턴스에 적용한다. 에셋은 건드리지 않는다. */
	int32 ApplyChangePlanToPreview();

	FReply HandleApplyChangeClicked();
	FReply HandleApplyToAssetClicked();
	FReply HandleCancelChangeClicked();

	bool CanParseChange() const;
	bool CanApplyChange() const;
	bool CanApplyToAsset() const;
	EVisibility GetChangePlanVisibility() const;
	FText GetChangePlanStatusText() const { return ChangePlanStatusText; }

	void ClearChangePlan();

	FText GetAskButtonText() const;

	/** 지금 선택된 Widget에 대한 Context 문자열. 선택이 없으면 빈 문자열. */
	FString BuildCurrentContext() const;

	/** 현재 선택된 SWidget. 파괴되었으면 nullptr. */
	TSharedPtr<SWidget> GetSelectedWidget() const;

	FText GetInspectButtonText() const;
	FText GetStatusText() const;

	//~ Slate. 살아있는 Widget에서 바로 읽는다.
	FText GetSlateTypeText() const;
	FText GetVisibilityText() const;
	FText GetEnabledText() const;
	FText GetDesiredSizeText() const;
	FText GetGeometryText() const;
	FText GetCreatedInText() const;
	FText GetNativeSourceText() const;

	//~ UMG. 캐시된 추적 결과에서 읽는다.
	FText GetWidgetNameText() const;
	FText GetUMGTypeText() const;
	FText GetOwnerText() const;
	FText GetOwnerClassText() const;
	FText GetNativeParentText() const;
	FText GetSlotTypeText() const;
	FText GetParentWidgetText() const;
	FText GetChildCountText() const;
	FText GetBlueprintAssetText() const;
	EVisibility GetAncestorMetaDataNoticeVisibility() const;

	TSharedPtr<FAIWidgetPicker> Picker;
	TSharedPtr<FAIWidgetSelection> Selection;
	TSharedPtr<FAIWidgetHighlighter> Highlighter;

	/** 선택이 바뀔 때만 다시 계산한다. */
	FAIWidgetInspectionResult CachedInspection;

	/** 소스 파일을 뒤지는 작업이라 선택이 바뀔 때만 한다. */
	FAIWidgetSourceInfo CachedSourceInfo;

	TArray<FPathEntryPtr> PathEntries;
	TSharedPtr<SListView<FPathEntryPtr>> PathListView;

	/** 목록 -> 선택 -> 목록으로 되돌아오는 재진입을 막는다. */
	bool bUpdatingListSelection = false;

	TSharedPtr<FAIWidgetRuntimePreview> RuntimePreview;

	/** 마지막 미리보기 실패 이유. 비어 있으면 안내를 숨긴다. */
	FText PreviewErrorText;

	//~ AI
	TArray<FProviderPtr> Providers;
	FProviderPtr ActiveProvider;
	TSharedPtr<SComboBox<FProviderPtr>> ProviderComboBox;
	TSharedPtr<SMultiLineEditableTextBox> QuestionTextBox;

	/** AI 응답 칸. 읽기 전용이 아니다. Clipboard Provider를 쓸 때 여기에 답을 붙여넣는다. */
	TSharedPtr<SMultiLineEditableTextBox> ResponseTextBox;

	/** 패널 안에서 돌아가는 CLI. Provider가 대화형일 때 여기로 프롬프트가 간다. */
	TSharedPtr<SAIWidgetTerminal> TerminalWidget;

	/** 응답을 기다리는 동안 중복 전송을 막는다. */
	bool bRequestInFlight = false;

	/** 지금 날아간 요청이 무엇이었는지. 응답이 오면 이걸 보고 계획으로 만들지 정한다. */
	EAIWidgetRequestKind PendingRequestKind = EAIWidgetRequestKind::Question;

	TArray<FChangePlanEntryPtr> ChangePlanEntries;
	TSharedPtr<SListView<FChangePlanEntryPtr>> ChangePlanListView;
	FText ChangePlanStatusText;
	//~ End AI

	FDelegateHandle SelectionChangedHandle;
};
