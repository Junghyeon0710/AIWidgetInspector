// AI Widget Inspector

#include "UI/SAIWidgetInspectorPanel.h"

#include "AI/AIWidgetContextBuilder.h"
#include "AIWidgetInspectorLog.h"
#include "AIWidgetInspectorModule.h"
#include "Commands/AIWidgetCommand.h"
#include "Commands/AIWidgetCommandParser.h"
#include "Commands/AIWidgetPersistentApplier.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetInspector.h"
#include "Inspection/AIWidgetSelection.h"
#include "Inspection/AIWidgetSourceResolver.h"
#include "WidgetPicking/AIWidgetHighlighter.h"
#include "WidgetPicking/AIWidgetPicker.h"

#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Layout/WidgetPath.h"
#include "Misc/Attribute.h"
#include "Misc/Paths.h"
#include "SEnumCombo.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SAIWidgetInspectorPanel"

namespace AIWidgetInspectorPanel
{
	/** 라벨 열 너비. 값이 세로로 가지런히 보이게 고정한다. */
	static constexpr float LabelColumnWidth = 104.0f;

	static FText ToTextOrDash(const FString& InValue)
	{
		return InValue.IsEmpty() ? FText::FromString(TEXT("-")) : FText::FromString(InValue);
	}

	static FText ToTextOrDash(const FName& InValue)
	{
		return InValue.IsNone() ? FText::FromString(TEXT("-")) : FText::FromName(InValue);
	}
}

void SAIWidgetInspectorPanel::Construct(
	const FArguments& InArgs,
	const TSharedRef<FAIWidgetPicker>& InPicker,
	const TSharedRef<FAIWidgetSelection>& InSelection,
	const TSharedRef<FAIWidgetHighlighter>& InHighlighter)
{
	Picker = InPicker;
	Selection = InSelection;
	Highlighter = InHighlighter;

	RuntimePreview = FAIWidgetInspectorModule::Get().GetRuntimePreview();

	Providers = FAIWidgetInspectorModule::Get().GetProviders();
	if (Providers.Num() > 0)
	{
		ActiveProvider = Providers[0];
	}

	SelectionChangedHandle = Selection->OnChanged().AddSP(this, &SAIWidgetInspectorPanel::HandleSelectionChanged);

	// 이 패널을 클릭했을 때 패널 자신이 선택되지 않게 한다.
	Picker->AddIgnoredWidget(SharedThis(this));

	// 창이 짧을 때를 대비해 헤더만 고정하고 나머지는 통째로 스크롤시킨다.
	// 예전에는 경로 목록이 FillHeight였는데, 아래 Ask AI 영역이 붙으면서 남는 높이가 0이 되어
	// 목록이 찌그러지고 그 아래 내용과 겹쳐 그려졌다.
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			BuildHeader()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				MakeSection(LOCTEXT("SectionSlate", "Slate"), false, BuildSlateSection())
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				MakeSection(LOCTEXT("SectionUMG", "UMG"), false, BuildUMGSection())
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				MakeSection(LOCTEXT("SectionSource", "Source"), false, BuildSourceSection())
			]

			// 경로와 미리보기는 늘 보고 있을 내용이 아니라서 접어 둔다.
			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				MakeSection(LOCTEXT("SectionPath", "Widget Path"), true, BuildPathList())
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				MakeSection(LOCTEXT("SectionPreview", "Runtime Preview"), true, BuildPreviewSection())
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				BuildAskAISection()
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				MakeSection(LOCTEXT("SectionChangePlan", "Change Plan"), false, BuildChangePlanSection())
			]
		]
	];

	RebuildPathEntries();
}

SAIWidgetInspectorPanel::~SAIWidgetInspectorPanel()
{
	if (Selection.IsValid())
	{
		Selection->OnChanged().Remove(SelectionChangedHandle);
	}

	// 소멸 중이라 SharedThis()를 쓸 수 없으므로, Picker가 무시 목록에서 죽은 항목을 알아서 걷어내게 둔다.
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildHeader()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(this, &SAIWidgetInspectorPanel::GetInspectButtonText)
				.ToolTipText(LOCTEXT("InspectTooltip", "Inspect Mode를 켠다. UI 위에서 클릭하면 그 Widget이 선택되고, ESC 또는 우클릭이면 취소된다."))
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleToggleInspectClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SAIWidgetInspectorPanel::GetHighlightSelectedState)
				.OnCheckStateChanged(this, &SAIWidgetInspectorPanel::HandleHighlightSelectedChanged)
				.ToolTipText(LOCTEXT("HighlightTooltip", "선택된 Widget의 테두리를 계속 표시한다."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HighlightSelected", "Highlight Selected"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "Clear"))
				.IsEnabled(this, &SAIWidgetInspectorPanel::HasSelection)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleClearClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAIWidgetInspectorPanel::GetStatusText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::MakeSection(const FText& InTitle, bool bInInitiallyCollapsed, TSharedRef<SWidget> InContent)
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(bInInitiallyCollapsed)
		.AreaTitle(InTitle)
		.Padding(FMargin(0.0f, 2.0f, 0.0f, 4.0f))
		.BodyContent()
		[
			InContent
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::MakeDetailRow(const FText& InLabel, TAttribute<FText> InValue)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(AIWidgetInspectorPanel::LabelColumnWidth)
			[
				SNew(STextBlock)
				.Text(InLabel)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(InValue)
			.ToolTipText(InValue)
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildSlateSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelSlate", "Type"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetSlateTypeText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelVisibility", "Visibility"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetVisibilityText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelEnabled", "Enabled"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetEnabledText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelDesiredSize", "DesiredSize"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetDesiredSizeText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelGeometry", "Geometry"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetGeometryText))
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildUMGSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AncestorMetaData", "이 Slate Widget에는 UMG 꼬리표가 없어 상위 Widget의 정보를 표시합니다."))
			.Visibility(this, &SAIWidgetInspectorPanel::GetAncestorMetaDataNoticeVisibility)
			.ColorAndOpacity(FLinearColor(1.0f, 0.75f, 0.35f))
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelName", "Name"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetWidgetNameText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelUMGType", "UMG Type"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetUMGTypeText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelOwner", "Owner"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetOwnerText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelOwnerClass", "Owner Class"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetOwnerClassText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelNative", "Native Parent"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetNativeParentText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelSlot", "Slot"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetSlotTypeText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelUMGParent", "Parent"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetParentWidgetText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelChildren", "Children"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetChildCountText))
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildSourceSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelBlueprint", "Blueprint"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetBlueprintAssetText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelCreatedIn", "C++"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetCreatedInText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeDetailRow(LOCTEXT("LabelNativeSource", "Native"), MakeAttributeSP(this, &SAIWidgetInspectorPanel::GetNativeSourceText))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenBlueprint", "Open Blueprint"))
				.ToolTipText(LOCTEXT("OpenBlueprintTooltip", "이 Widget을 만든 Widget Blueprint 에셋을 연다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanOpenBlueprint)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleOpenBlueprintClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenSource", "Open Source"))
				.ToolTipText(LOCTEXT("OpenSourceTooltip", "이 Slate Widget이 생성된 C++ 위치를 IDE에서 연다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanOpenSource)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleOpenSourceClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveAsset", "Save Asset"))
				.ToolTipText(LOCTEXT("SaveAssetTooltip", "변경된 Widget Blueprint를 저장한다. 저장 전까지는 Ctrl+Z로 되돌릴 수 있다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSaveAsset)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleSaveAssetClicked)
			]
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildPathList()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WidgetPath", "Widget Path"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Parent", "Parent"))
				.ToolTipText(LOCTEXT("ParentTooltip", "한 단계 바깥 Widget을 선택한다. 클릭이 STextBlock을 잡았을 때 SButton으로 올라가는 용도."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSelectParent)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleSelectParentClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Child", "Child"))
				.ToolTipText(LOCTEXT("ChildTooltip", "경로를 따라 한 단계 안쪽 Widget을 선택한다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSelectChild)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleSelectChildClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(180.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(PathListView, SListView<FPathEntryPtr>)
					.ListItemsSource(&PathEntries)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SAIWidgetInspectorPanel::HandleGeneratePathRow)
					.OnSelectionChanged(this, &SAIWidgetInspectorPanel::HandlePathSelectionChanged)
				]
			]
		];
}

TSharedRef<ITableRow> SAIWidgetInspectorPanel::HandleGeneratePathRow(FPathEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(STableRow<FPathEntryPtr>, InOwnerTable)
		[
			SNew(STextBlock)
			.Text(InEntry.IsValid() ? InEntry->Label : FText::GetEmpty())
		];
}

void SAIWidgetInspectorPanel::HandlePathSelectionChanged(FPathEntryPtr InEntry, ESelectInfo::Type InSelectInfo)
{
	if (bUpdatingListSelection || InSelectInfo == ESelectInfo::Direct || !InEntry.IsValid())
	{
		return;
	}

	Selection->SelectIndex(InEntry->Index);
}

void SAIWidgetInspectorPanel::HandleSelectionChanged()
{
	RebuildPathEntries();
}

void SAIWidgetInspectorPanel::RefreshInspection()
{
	const TSharedPtr<SWidget> SelectedWidget = GetSelectedWidget();
	CachedInspection = FAIWidgetInspector::Inspect(SelectedWidget);
	CachedSourceInfo = FAIWidgetSourceResolver::Resolve(CachedInspection, SelectedWidget);
}

void SAIWidgetInspectorPanel::RebuildPathEntries()
{
	PathEntries.Reset();

	const int32 NumWidgets = Selection->Num();
	for (int32 Index = 0; Index < NumWidgets; ++Index)
	{
		const TSharedPtr<SWidget> Widget = Selection->GetWidgetAt(Index);

		FPathEntryPtr Entry = MakeShared<FPathEntry>();
		Entry->Index = Index;

		const FString Indent = FString::ChrN(Index * 2, TEXT(' '));
		Entry->Label = Widget.IsValid()
			? FText::FromString(Indent + FAIWidgetPicker::DescribeWidget(Widget.ToSharedRef()))
			: FText::FromString(Indent + TEXT("<destroyed>"));

		PathEntries.Add(MoveTemp(Entry));
	}

	if (PathListView.IsValid())
	{
		PathListView->RequestListRefresh();
	}

	SyncListSelection();
	RefreshInspection();
}

void SAIWidgetInspectorPanel::SyncListSelection()
{
	if (!PathListView.IsValid())
	{
		return;
	}

	TGuardValue<bool> Guard(bUpdatingListSelection, true);

	const int32 SelectedIndex = Selection->GetSelectedIndex();
	if (PathEntries.IsValidIndex(SelectedIndex))
	{
		PathListView->SetSelection(PathEntries[SelectedIndex], ESelectInfo::Direct);
		PathListView->RequestScrollIntoView(PathEntries[SelectedIndex]);
	}
	else
	{
		PathListView->ClearSelection();
	}
}

FReply SAIWidgetInspectorPanel::HandleToggleInspectClicked()
{
	Picker->ToggleInspectMode();
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleSelectParentClicked()
{
	Selection->SelectParent();
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleSelectChildClicked()
{
	Selection->SelectChild();
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleClearClicked()
{
	Selection->Clear();
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleOpenBlueprintClicked()
{
	Highlighter->OpenAsset(CachedInspection.BlueprintAsset.Get());
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleOpenSourceClicked()
{
	// 기록된 경로는 엔진 위젯의 경우 Epic 빌드 머신 경로라 이 컴퓨터에 없다.
	// 리졸버가 옮겨 준 경로가 있으면 그쪽을 연다.
	if (CachedSourceInfo.HasResolvedFile())
	{
		FSourceCodeNavigation::OpenSourceFile(CachedSourceInfo.ResolvedFile, CachedSourceInfo.CreatedInLine);
		return FReply::Handled();
	}

	const FName WidgetCreatedIn = CachedInspection.SlateCreatedIn;
	if (!WidgetCreatedIn.IsNone())
	{
		Highlighter->OpenSourceLocation(WidgetCreatedIn.GetPlainNameString(), WidgetCreatedIn.GetNumber());
	}

	return FReply::Handled();
}

bool SAIWidgetInspectorPanel::CanSelectParent() const
{
	return Selection->CanSelectParent();
}

bool SAIWidgetInspectorPanel::CanSelectChild() const
{
	return Selection->CanSelectChild();
}

bool SAIWidgetInspectorPanel::HasSelection() const
{
	return Selection->IsValid();
}

bool SAIWidgetInspectorPanel::CanOpenBlueprint() const
{
	return CachedInspection.BlueprintAsset.IsValid() && Highlighter->CanOpenAsset();
}

bool SAIWidgetInspectorPanel::CanOpenSource() const
{
	return CachedSourceInfo.HasResolvedFile()
		|| (!CachedInspection.SlateCreatedIn.IsNone() && Highlighter->CanOpenSourceLocation());
}

ECheckBoxState SAIWidgetInspectorPanel::GetHighlightSelectedState() const
{
	return Highlighter->IsHighlightingSelected() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SAIWidgetInspectorPanel::HandleHighlightSelectedChanged(ECheckBoxState InNewState)
{
	Highlighter->SetHighlightSelected(InNewState == ECheckBoxState::Checked);
}

TSharedPtr<SWidget> SAIWidgetInspectorPanel::GetSelectedWidget() const
{
	return Selection->GetSelectedWidget();
}

FText SAIWidgetInspectorPanel::GetInspectButtonText() const
{
	return Picker->IsInspecting()
		? LOCTEXT("StopInspect", "Stop Inspect (ESC)")
		: LOCTEXT("StartInspect", "Inspect Widget");
}

FText SAIWidgetInspectorPanel::GetStatusText() const
{
	if (Picker->IsInspecting())
	{
		return LOCTEXT("StatusInspecting", "Inspect Mode: UI 위에서 클릭하면 선택됩니다. ESC 또는 우클릭으로 취소.");
	}

	if (!Selection->IsValid())
	{
		return LOCTEXT("StatusEmpty", "선택된 Widget이 없습니다.");
	}

	if (!Selection->GetSelectedWidget().IsValid())
	{
		return LOCTEXT("StatusDestroyed", "선택했던 Widget이 이미 파괴되었습니다.");
	}

	return FText::Format(
		LOCTEXT("StatusSelected", "경로 깊이 {0} 중 {1}번째."),
		FText::AsNumber(Selection->Num()),
		FText::AsNumber(Selection->GetSelectedIndex() + 1));
}

FText SAIWidgetInspectorPanel::GetSlateTypeText() const
{
	const TSharedPtr<SWidget> Widget = GetSelectedWidget();
	return Widget.IsValid() ? FText::FromString(Widget->GetTypeAsString()) : FText::FromString(TEXT("-"));
}

FText SAIWidgetInspectorPanel::GetVisibilityText() const
{
	const TSharedPtr<SWidget> Widget = GetSelectedWidget();
	return Widget.IsValid() ? FText::FromString(Widget->GetVisibility().ToString()) : FText::FromString(TEXT("-"));
}

FText SAIWidgetInspectorPanel::GetEnabledText() const
{
	const TSharedPtr<SWidget> Widget = GetSelectedWidget();
	if (!Widget.IsValid())
	{
		return FText::FromString(TEXT("-"));
	}

	return Widget->IsEnabled() ? LOCTEXT("True", "true") : LOCTEXT("False", "false");
}

FText SAIWidgetInspectorPanel::GetDesiredSizeText() const
{
	const TSharedPtr<SWidget> Widget = GetSelectedWidget();
	if (!Widget.IsValid())
	{
		return FText::FromString(TEXT("-"));
	}

	const FVector2D WidgetDesiredSize = Widget->GetDesiredSize();
	return FText::FromString(FString::Printf(TEXT("%.0f x %.0f"), WidgetDesiredSize.X, WidgetDesiredSize.Y));
}

FText SAIWidgetInspectorPanel::GetGeometryText() const
{
	if (!Selection->IsValid())
	{
		return FText::FromString(TEXT("-"));
	}

	// Geometry는 Widget 자체가 아니라 배치 결과라서 경로를 다시 계산해야 나온다.
	const FWidgetPath ResolvedPath = Selection->ResolvePath();
	const int32 SelectedIndex = Selection->GetSelectedIndex();
	if (!ResolvedPath.IsValid() || SelectedIndex < 0 || SelectedIndex >= ResolvedPath.Widgets.Num())
	{
		return FText::FromString(TEXT("-"));
	}

	const FGeometry& Geometry = ResolvedPath.Widgets[SelectedIndex].Geometry;
	const FVector2f AbsolutePosition = FVector2f(Geometry.GetAbsolutePosition());
	const FVector2f LocalSize = FVector2f(Geometry.GetLocalSize());

	return FText::FromString(FString::Printf(
		TEXT("pos %.0f, %.0f   size %.0f x %.0f%s"),
		AbsolutePosition.X,
		AbsolutePosition.Y,
		LocalSize.X,
		LocalSize.Y,
		Geometry.HasRenderTransform() ? TEXT("   (render transform)") : TEXT("")));
}

FText SAIWidgetInspectorPanel::GetCreatedInText() const
{
	const FName WidgetCreatedIn = CachedInspection.SlateCreatedIn;
	if (WidgetCreatedIn.IsNone())
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::FromString(FString::Printf(
		TEXT("%s:%d"),
		*FPaths::GetCleanFilename(WidgetCreatedIn.GetPlainNameString()),
		WidgetCreatedIn.GetNumber()));
}

FText SAIWidgetInspectorPanel::GetNativeSourceText() const
{
	// Native 부모 클래스가 어디에 있는지. Blueprint 뒤의 실제 코드를 찾는 출발점이다.
	if (!CachedSourceInfo.NativeHeaderPath.IsEmpty())
	{
		return FText::FromString(FPaths::GetCleanFilename(CachedSourceInfo.NativeHeaderPath));
	}

	return FText::FromString(TEXT("-"));
}

FText SAIWidgetInspectorPanel::GetWidgetNameText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.WidgetName);
}

FText SAIWidgetInspectorPanel::GetUMGTypeText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetUMGTypeName());
}

FText SAIWidgetInspectorPanel::GetOwnerText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetOwnerUserWidgetName());
}

FText SAIWidgetInspectorPanel::GetOwnerClassText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetOwnerClassName());
}

FText SAIWidgetInspectorPanel::GetNativeParentText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetNativeParentClassName());
}

FText SAIWidgetInspectorPanel::GetSlotTypeText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetSlotTypeName());
}

FText SAIWidgetInspectorPanel::GetParentWidgetText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.GetParentWidgetName());
}

FText SAIWidgetInspectorPanel::GetChildCountText() const
{
	return CachedInspection.ChildWidgetCount == INDEX_NONE
		? FText::FromString(TEXT("-"))
		: FText::AsNumber(CachedInspection.ChildWidgetCount);
}

FText SAIWidgetInspectorPanel::GetBlueprintAssetText() const
{
	return AIWidgetInspectorPanel::ToTextOrDash(CachedInspection.SourceAssetPath);
}

EVisibility SAIWidgetInspectorPanel::GetAncestorMetaDataNoticeVisibility() const
{
	return CachedInspection.bMetaDataFromAncestor ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildPreviewSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewNotice", "살아있는 Widget 인스턴스에만 적용됩니다. 에셋은 바뀌지 않고, Widget이 다시 만들어지면 사라집니다."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(AIWidgetInspectorPanel::LabelColumnWidth)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewVisibility", "Visibility"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(160.0f)
				[
					SNew(SEnumComboBox, StaticEnum<ESlateVisibility>())
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreview)
					.CurrentValue(this, &SAIWidgetInspectorPanel::GetPreviewVisibilityValue)
					.OnEnumSelectionChanged(this, &SAIWidgetInspectorPanel::HandlePreviewVisibilityChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreview)
				.IsChecked(this, &SAIWidgetInspectorPanel::GetPreviewEnabledState)
				.OnCheckStateChanged(this, &SAIWidgetInspectorPanel::HandlePreviewEnabledChanged)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewEnabled", "Enabled"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(AIWidgetInspectorPanel::LabelColumnWidth)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewOpacity", "Opacity"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(80.0f)
				[
					SNew(SNumericEntryBox<float>)
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreview)
					.AllowSpin(true)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.Delta(0.05f)
					.Value(this, &SAIWidgetInspectorPanel::GetPreviewOpacity)
					.OnValueChanged(this, &SAIWidgetInspectorPanel::HandlePreviewOpacityChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PreviewTranslation", "Translate"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(76.0f)
				[
					SNew(SNumericEntryBox<float>)
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreview)
					.AllowSpin(true)
					.MinSliderValue(-500.0f)
					.MaxSliderValue(500.0f)
					.Value(this, &SAIWidgetInspectorPanel::GetPreviewTranslationX)
					.OnValueChanged(this, &SAIWidgetInspectorPanel::HandlePreviewTranslationXChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(76.0f)
				[
					SNew(SNumericEntryBox<float>)
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreview)
					.AllowSpin(true)
					.MinSliderValue(-500.0f)
					.MaxSliderValue(500.0f)
					.Value(this, &SAIWidgetInspectorPanel::GetPreviewTranslationY)
					.OnValueChanged(this, &SAIWidgetInspectorPanel::HandlePreviewTranslationYChanged)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(AIWidgetInspectorPanel::LabelColumnWidth)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewText", "Text"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanPreviewText)
				.HintText(LOCTEXT("PreviewTextHint", "TextBlock을 선택하면 입력할 수 있습니다."))
				.Text(this, &SAIWidgetInspectorPanel::GetPreviewText)
				.OnTextCommitted(this, &SAIWidgetInspectorPanel::HandlePreviewTextCommitted)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SAIWidgetInspectorPanel::GetPreviewErrorText)
				.Visibility(this, &SAIWidgetInspectorPanel::GetPreviewErrorVisibility)
				.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.35f))
				.AutoWrapText(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(this, &SAIWidgetInspectorPanel::GetRevertPreviewText)
				.ToolTipText(LOCTEXT("RevertPreviewTooltip", "이 세션에서 적용한 모든 임시 변경을 원래 값으로 되돌린다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::HasPreviews)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleRevertPreviewClicked)
			]
		];
}

UWidget* SAIWidgetInspectorPanel::GetPreviewTargetWidget() const
{
	return CachedInspection.SourceWidget.Get();
}

bool SAIWidgetInspectorPanel::CanPreview() const
{
	return RuntimePreview.IsValid() && GetPreviewTargetWidget() != nullptr;
}

bool SAIWidgetInspectorPanel::CanPreviewText() const
{
	return RuntimePreview.IsValid()
		&& FAIWidgetRuntimePreview::CanApply(GetPreviewTargetWidget(), EAIWidgetOperation::SetText);
}

bool SAIWidgetInspectorPanel::HasPreviews() const
{
	return RuntimePreview.IsValid() && RuntimePreview->Num() > 0;
}

void SAIWidgetInspectorPanel::ApplyPreviewCommand(const FAIWidgetCommand& InCommand)
{
	UWidget* TargetWidget = GetPreviewTargetWidget();
	if (!RuntimePreview.IsValid() || !TargetWidget)
	{
		return;
	}

	FText Error;
	if (!RuntimePreview->Apply(TargetWidget, InCommand, Error))
	{
		PreviewErrorText = Error;
		return;
	}

	PreviewErrorText = FText::GetEmpty();
}

int32 SAIWidgetInspectorPanel::GetPreviewVisibilityValue() const
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	return TargetWidget
		? static_cast<int32>(TargetWidget->GetVisibility())
		: static_cast<int32>(ESlateVisibility::Visible);
}

void SAIWidgetInspectorPanel::HandlePreviewVisibilityChanged(int32 InNewValue, ESelectInfo::Type InSelectInfo)
{
	ApplyPreviewCommand(FAIWidgetCommand::MakeSetVisibility(
		CachedInspection.WidgetName,
		static_cast<ESlateVisibility>(InNewValue)));
}

ECheckBoxState SAIWidgetInspectorPanel::GetPreviewEnabledState() const
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	const bool bIsWidgetEnabled = TargetWidget ? TargetWidget->GetIsEnabled() : true;
	return bIsWidgetEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SAIWidgetInspectorPanel::HandlePreviewEnabledChanged(ECheckBoxState InNewState)
{
	ApplyPreviewCommand(FAIWidgetCommand::MakeSetEnabled(
		CachedInspection.WidgetName,
		InNewState == ECheckBoxState::Checked));
}

TOptional<float> SAIWidgetInspectorPanel::GetPreviewOpacity() const
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	return TargetWidget ? TOptional<float>(TargetWidget->GetRenderOpacity()) : TOptional<float>();
}

void SAIWidgetInspectorPanel::HandlePreviewOpacityChanged(float InNewValue)
{
	ApplyPreviewCommand(FAIWidgetCommand::MakeSetRenderOpacity(CachedInspection.WidgetName, InNewValue));
}

TOptional<float> SAIWidgetInspectorPanel::GetPreviewTranslationX() const
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	return TargetWidget
		? TOptional<float>(static_cast<float>(TargetWidget->GetRenderTransform().Translation.X))
		: TOptional<float>();
}

TOptional<float> SAIWidgetInspectorPanel::GetPreviewTranslationY() const
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	return TargetWidget
		? TOptional<float>(static_cast<float>(TargetWidget->GetRenderTransform().Translation.Y))
		: TOptional<float>();
}

void SAIWidgetInspectorPanel::HandlePreviewTranslationXChanged(float InNewValue)
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	if (!TargetWidget)
	{
		return;
	}

	FVector2D Translation = TargetWidget->GetRenderTransform().Translation;
	Translation.X = InNewValue;
	ApplyPreviewCommand(FAIWidgetCommand::MakeSetRenderTranslation(CachedInspection.WidgetName, Translation));
}

void SAIWidgetInspectorPanel::HandlePreviewTranslationYChanged(float InNewValue)
{
	const UWidget* TargetWidget = GetPreviewTargetWidget();
	if (!TargetWidget)
	{
		return;
	}

	FVector2D Translation = TargetWidget->GetRenderTransform().Translation;
	Translation.Y = InNewValue;
	ApplyPreviewCommand(FAIWidgetCommand::MakeSetRenderTranslation(CachedInspection.WidgetName, Translation));
}

FText SAIWidgetInspectorPanel::GetPreviewText() const
{
	const UTextBlock* AsTextBlock = Cast<UTextBlock>(GetPreviewTargetWidget());
	return AsTextBlock ? AsTextBlock->GetText() : FText::GetEmpty();
}

void SAIWidgetInspectorPanel::HandlePreviewTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnCleared)
	{
		return;
	}

	ApplyPreviewCommand(FAIWidgetCommand::MakeSetText(CachedInspection.WidgetName, InText));
}

FText SAIWidgetInspectorPanel::GetRevertPreviewText() const
{
	const int32 PreviewCount = RuntimePreview.IsValid() ? RuntimePreview->Num() : 0;
	return PreviewCount > 0
		? FText::Format(LOCTEXT("RevertPreviewCount", "Revert Preview ({0})"), FText::AsNumber(PreviewCount))
		: LOCTEXT("RevertPreview", "Revert Preview");
}

FReply SAIWidgetInspectorPanel::HandleRevertPreviewClicked()
{
	if (RuntimePreview.IsValid())
	{
		RuntimePreview->RevertAll();
	}

	PreviewErrorText = FText::GetEmpty();
	return FReply::Handled();
}

EVisibility SAIWidgetInspectorPanel::GetPreviewErrorVisibility() const
{
	return PreviewErrorText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildAskAISection()
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.AreaTitle(LOCTEXT("SectionAskAI", "Ask AI"))
		.BodyContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LabelProvider", "Provider"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(ProviderComboBox, SComboBox<FProviderPtr>)
					.OptionsSource(&Providers)
					.InitiallySelectedItem(ActiveProvider)
					.OnGenerateWidget(this, &SAIWidgetInspectorPanel::HandleGenerateProviderItem)
					.OnSelectionChanged(this, &SAIWidgetInspectorPanel::HandleProviderChanged)
					.ToolTipText(this, &SAIWidgetInspectorPanel::GetActiveProviderTooltip)
					[
						SNew(STextBlock)
						.Text(this, &SAIWidgetInspectorPanel::GetActiveProviderText)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyContext", "Copy Context"))
					.ToolTipText(LOCTEXT("CopyContextTooltip", "질문 없이 Widget 정보만 클립보드에 복사한다."))
					.IsEnabled(this, &SAIWidgetInspectorPanel::HasSelection)
					.OnClicked(this, &SAIWidgetInspectorPanel::HandleCopyContextClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBox)
				.HeightOverride(56.0f)
				[
					SAssignNew(QuestionTextBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("QuestionHint", "예) 왜 이 버튼 클릭이 안 돼?"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(this, &SAIWidgetInspectorPanel::GetAskButtonText)
					.ToolTipText(LOCTEXT("AskAITooltip", "Widget 정보와 질문을 묶어 Provider에게 보낸다."))
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanSendRequest)
					.OnClicked(this, &SAIWidgetInspectorPanel::HandleAskAIClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("RequestChange", "Request Change"))
					.ToolTipText(LOCTEXT("RequestChangeTooltip", "같은 내용을 보내되, 실행 가능한 JSON 변경안으로 답하라는 지시를 덧붙인다."))
					.IsEnabled(this, &SAIWidgetInspectorPanel::CanSendRequest)
					.OnClicked(this, &SAIWidgetInspectorPanel::HandleRequestChangeClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ResponseLabel", "AI Response — Clipboard Provider를 쓸 때는 AI가 준 답을 여기에 붙여넣으세요."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(120.0f)
				[
					SAssignNew(ResponseTextBox, SMultiLineEditableTextBox)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("ParseChange", "Parse Change"))
				.ToolTipText(LOCTEXT("ParseChangeTooltip", "응답에서 변경안을 읽고 검사한다. 실행은 아직 하지 않는다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanParseChange)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleParseChangeClicked)
			]
		];
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::HandleGenerateProviderItem(FProviderPtr InProvider)
{
	// 이름은 변하지 않지만 설명은 변한다. CLI를 찾았는지 여부가 거기 들어가는데,
	// 목록을 만든 뒤에 CLI를 설치하는 일이 실제로 있다. 값을 굳혀 두면 이미 깔린
	// 도구를 두고 "PATH에서 찾지 못했습니다"가 계속 떠서 설치가 실패한 것처럼 보인다.
	TWeakPtr<IAIWidgetProvider> WeakProvider = InProvider;

	return SNew(STextBlock)
		.Text(InProvider.IsValid() ? InProvider->GetDisplayName() : FText::GetEmpty())
		.ToolTipText_Lambda([WeakProvider]()
		{
			// Provider는 모듈이 소유한다. 목록 위젯이 그 수명을 붙들지 않도록 약한 참조로 본다.
			const TSharedPtr<IAIWidgetProvider> Provider = WeakProvider.Pin();
			return Provider.IsValid() ? Provider->GetDescription() : FText::GetEmpty();
		});
}

void SAIWidgetInspectorPanel::HandleProviderChanged(FProviderPtr InProvider, ESelectInfo::Type InSelectInfo)
{
	if (InProvider.IsValid())
	{
		ActiveProvider = InProvider;
	}
}

FText SAIWidgetInspectorPanel::GetActiveProviderText() const
{
	return ActiveProvider.IsValid() ? ActiveProvider->GetDisplayName() : LOCTEXT("NoProvider", "None");
}

FText SAIWidgetInspectorPanel::GetActiveProviderTooltip() const
{
	return ActiveProvider.IsValid() ? ActiveProvider->GetDescription() : FText::GetEmpty();
}

FText SAIWidgetInspectorPanel::GetAskButtonText() const
{
	return bRequestInFlight ? LOCTEXT("Asking", "Asking...") : LOCTEXT("AskAI", "Ask AI");
}

FString SAIWidgetInspectorPanel::BuildCurrentContext() const
{
	if (!Selection.IsValid())
	{
		return FString();
	}

	return FAIWidgetContextBuilder::BuildContext(*Selection, CachedInspection, CachedSourceInfo);
}

bool SAIWidgetInspectorPanel::CanSendRequest() const
{
	if (bRequestInFlight || !ActiveProvider.IsValid() || !ActiveProvider->IsAvailable() || !Selection->IsValid())
	{
		return false;
	}

	// 빈 질문을 보내봐야 쓸모 있는 답이 안 나온다.
	return QuestionTextBox.IsValid() && !QuestionTextBox->GetText().IsEmptyOrWhitespace();
}

FReply SAIWidgetInspectorPanel::HandleCopyContextClicked()
{
	if (!ActiveProvider.IsValid())
	{
		return FReply::Handled();
	}

	// 질문 없이 Context만. Provider가 Clipboard면 그대로 붙여넣을 수 있는 형태가 된다.
	FAIWidgetRequest Request;
	Request.Kind = EAIWidgetRequestKind::Question;
	Request.Context = BuildCurrentContext();
	Request.UserMessage = FString();

	bRequestInFlight = true;
	ActiveProvider->SendRequest(Request, FOnAIWidgetResponse::CreateSP(this, &SAIWidgetInspectorPanel::HandleAIResponse));

	return FReply::Handled();
}

void SAIWidgetInspectorPanel::SendRequest(EAIWidgetRequestKind InKind)
{
	if (!CanSendRequest())
	{
		return;
	}

	FAIWidgetRequest Request;
	Request.Kind = InKind;
	Request.Context = BuildCurrentContext();
	Request.UserMessage = QuestionTextBox->GetText().ToString();

	bRequestInFlight = true;
	ClearChangePlan();

	ActiveProvider->SendRequest(Request, FOnAIWidgetResponse::CreateSP(this, &SAIWidgetInspectorPanel::HandleAIResponse));
}

FReply SAIWidgetInspectorPanel::HandleAskAIClicked()
{
	SendRequest(EAIWidgetRequestKind::Question);
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleRequestChangeClicked()
{
	SendRequest(EAIWidgetRequestKind::ChangeRequest);
	return FReply::Handled();
}

void SAIWidgetInspectorPanel::HandleAIResponse(const FAIWidgetResponse& InResponse)
{
	bRequestInFlight = false;

	if (ResponseTextBox.IsValid())
	{
		ResponseTextBox->SetText(InResponse.Message);
	}

	if (!InResponse.bSuccess)
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("AI 요청 실패: %s"), *InResponse.Message.ToString());
	}
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildChangePlanSection()
{
	return SNew(SVerticalBox)
		.Visibility(this, &SAIWidgetInspectorPanel::GetChangePlanVisibility)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(110.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(ChangePlanListView, SListView<FChangePlanEntryPtr>)
					.ListItemsSource(&ChangePlanEntries)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SAIWidgetInspectorPanel::HandleGenerateChangePlanRow)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SAIWidgetInspectorPanel::GetChangePlanStatusText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyPreview", "Apply Preview"))
				.ToolTipText(LOCTEXT("ApplyPreviewTooltip", "검사를 통과한 변경만 살아있는 Widget에 적용한다. 에셋은 바뀌지 않는다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanApplyChange)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleApplyChangeClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyToAsset", "Apply to Asset"))
				.ToolTipText(LOCTEXT("ApplyToAssetTooltip", "Widget Blueprint 원본을 고치고 컴파일한다. Ctrl+Z로 되돌릴 수 있고, 저장은 따로 눌러야 한다."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanApplyToAsset)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleApplyToAssetClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("CancelChange", "Cancel"))
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleCancelChangeClicked)
			]
		];
}

TSharedRef<ITableRow> SAIWidgetInspectorPanel::HandleGenerateChangePlanRow(FChangePlanEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwnerTable)
{
	const bool bIsValid = InEntry.IsValid() && InEntry->Validation.bIsValid;

	FText RowText;
	if (!InEntry.IsValid())
	{
		RowText = FText::GetEmpty();
	}
	else if (bIsValid)
	{
		RowText = FText::FromString(InEntry->Validation.PlanLine);
	}
	else
	{
		RowText = FText::Format(
			LOCTEXT("RejectedChange", "{0}   {1}   거부: {2}"),
			FText::FromName(InEntry->Command.TargetWidgetName),
			FText::FromString(FAIWidgetCommand::GetOperationName(InEntry->Command.Operation)),
			InEntry->Validation.Error);
	}

	return SNew(STableRow<FChangePlanEntryPtr>, InOwnerTable)
		[
			SNew(STextBlock)
			.Text(RowText)
			.ColorAndOpacity(bIsValid ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(1.0f, 0.55f, 0.35f)))
		];
}

void SAIWidgetInspectorPanel::ClearChangePlan()
{
	ChangePlanEntries.Reset();
	ChangePlanStatusText = FText::GetEmpty();

	if (ChangePlanListView.IsValid())
	{
		ChangePlanListView->RequestListRefresh();
	}
}

bool SAIWidgetInspectorPanel::CanParseChange() const
{
	return ResponseTextBox.IsValid() && !ResponseTextBox->GetText().IsEmptyOrWhitespace();
}

bool SAIWidgetInspectorPanel::CanApplyChange() const
{
	return ChangePlanEntries.ContainsByPredicate(
		[](const FChangePlanEntryPtr& Entry)
		{
			return Entry.IsValid() && Entry->Validation.bIsValid && Entry->Validation.TargetWidget.IsValid();
		});
}

EVisibility SAIWidgetInspectorPanel::GetChangePlanVisibility() const
{
	// 적용이 끝나 목록을 비운 뒤에도 결과 문구는 남겨서 보여준다.
	const bool bIsEmpty = ChangePlanEntries.IsEmpty() && ChangePlanStatusText.IsEmpty();
	return bIsEmpty ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply SAIWidgetInspectorPanel::HandleParseChangeClicked()
{
	ClearChangePlan();

	if (!ResponseTextBox.IsValid())
	{
		return FReply::Handled();
	}

	TArray<FAIWidgetCommand> Commands;
	TArray<FText> ParseErrors;
	FAIWidgetCommandParser::Parse(ResponseTextBox->GetText().ToString(), Commands, ParseErrors);

	// 형식을 통과한 명령만 대상 검사로 넘긴다.
	int32 ValidCount = 0;
	for (const FAIWidgetCommand& Command : Commands)
	{
		FChangePlanEntryPtr Entry = MakeShared<FChangePlanEntry>();
		Entry->Command = Command;
		Entry->Validation = FAIWidgetCommandValidator::Validate(Command, CachedInspection);

		ValidCount += Entry->Validation.bIsValid ? 1 : 0;
		ChangePlanEntries.Add(MoveTemp(Entry));
	}

	// 형식 단계에서 떨어진 것들은 대상이 없으므로 목록 대신 상태줄에 모아 보여준다.
	FTextBuilder StatusBuilder;
	if (ChangePlanEntries.IsEmpty() && ParseErrors.IsEmpty())
	{
		StatusBuilder.AppendLine(LOCTEXT("NothingParsed", "읽어낼 변경이 없습니다."));
	}
	else
	{
		StatusBuilder.AppendLine(FText::Format(
			LOCTEXT("ParsedSummary", "{0}건 중 {1}건이 적용 가능합니다."),
			FText::AsNumber(ChangePlanEntries.Num()),
			FText::AsNumber(ValidCount)));
	}

	for (const FText& ParseError : ParseErrors)
	{
		StatusBuilder.AppendLine(ParseError);
	}

	ChangePlanStatusText = StatusBuilder.ToText();

	if (ChangePlanListView.IsValid())
	{
		ChangePlanListView->RequestListRefresh();
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("변경 계획: %d건 파싱, %d건 적용 가능, %d건 형식 오류."),
		ChangePlanEntries.Num(), ValidCount, ParseErrors.Num());

	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleApplyChangeClicked()
{
	if (!RuntimePreview.IsValid())
	{
		return FReply::Handled();
	}

	int32 AppliedCount = 0;
	int32 FailedCount = 0;

	for (const FChangePlanEntryPtr& Entry : ChangePlanEntries)
	{
		if (!Entry.IsValid() || !Entry->Validation.bIsValid)
		{
			continue;
		}

		UWidget* TargetWidget = Entry->Validation.TargetWidget.Get();
		if (!TargetWidget)
		{
			++FailedCount;
			continue;
		}

		FText Error;
		if (RuntimePreview->Apply(TargetWidget, Entry->Command, Error))
		{
			++AppliedCount;
		}
		else
		{
			++FailedCount;
			PreviewErrorText = Error;
		}
	}

	ChangePlanStatusText = FailedCount > 0
		? FText::Format(
			LOCTEXT("AppliedWithFailures", "{0}건 적용, {1}건 실패. Revert Preview로 되돌릴 수 있습니다."),
			FText::AsNumber(AppliedCount),
			FText::AsNumber(FailedCount))
		: FText::Format(
			LOCTEXT("Applied", "{0}건 적용했습니다. 에셋은 바뀌지 않았습니다. Revert Preview로 되돌릴 수 있습니다."),
			FText::AsNumber(AppliedCount));

	// 적용이 끝나면 계획은 비운다. 같은 계획을 두 번 눌러 중복 적용되는 걸 막는다.
	const FText StatusToKeep = ChangePlanStatusText;
	ClearChangePlan();
	ChangePlanStatusText = StatusToKeep;

	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleCancelChangeClicked()
{
	ClearChangePlan();
	return FReply::Handled();
}

bool SAIWidgetInspectorPanel::CanApplyToAsset() const
{
	return FAIWidgetPersistentApplier::CanApply(CachedInspection) && CanApplyChange();
}

FReply SAIWidgetInspectorPanel::HandleApplyToAssetClicked()
{
	// 검사를 통과한 것만 에셋에 쓴다. 거부된 항목은 애초에 여기까지 오지 않는다.
	TArray<FAIWidgetCommand> CommandsToApply;
	for (const FChangePlanEntryPtr& Entry : ChangePlanEntries)
	{
		if (Entry.IsValid() && Entry->Validation.bIsValid)
		{
			CommandsToApply.Add(Entry->Command);
		}
	}

	const FAIWidgetPersistentResult Result = FAIWidgetPersistentApplier::Apply(CommandsToApply, CachedInspection);

	FText Status;
	if (Result.AppliedCount == 0)
	{
		Status = Result.Error.IsEmpty()
			? LOCTEXT("AssetNothingApplied", "에셋에 적용된 변경이 없습니다.")
			: Result.Error;
	}
	else
	{
		Status = FText::Format(
			LOCTEXT("AssetApplied", "에셋에 {0}건 적용하고 컴파일했습니다. Ctrl+Z로 되돌릴 수 있습니다. 저장하려면 Source의 Save Asset을 누르세요."),
			FText::AsNumber(Result.AppliedCount));

		if (Result.FailedCount > 0)
		{
			Status = FText::Format(
				LOCTEXT("AssetAppliedWithFailures", "{0} ({1}건 실패: {2})"),
				Status,
				FText::AsNumber(Result.FailedCount),
				Result.Error);
		}
	}

	ClearChangePlan();
	ChangePlanStatusText = Status;

	// 에셋이 바뀌었으니 UMG/Source 표시도 다시 읽는다.
	RefreshInspection();

	return FReply::Handled();
}

bool SAIWidgetInspectorPanel::CanSaveAsset() const
{
	return FAIWidgetPersistentApplier::IsAssetDirty(CachedInspection);
}

FReply SAIWidgetInspectorPanel::HandleSaveAssetClicked()
{
	FText Error;
	if (FAIWidgetPersistentApplier::SaveAsset(CachedInspection, Error))
	{
		ChangePlanStatusText = LOCTEXT("AssetSaved", "에셋을 저장했습니다.");
	}
	else
	{
		ChangePlanStatusText = Error;
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
