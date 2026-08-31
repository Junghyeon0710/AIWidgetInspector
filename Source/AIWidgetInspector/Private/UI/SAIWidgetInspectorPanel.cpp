// AI Widget Inspector

#include "UI/SAIWidgetInspectorPanel.h"

#include "AI/AIWidgetContextBuilder.h"
#include "AIWidgetInspectorLog.h"
#include "AIWidgetInspectorCommands.h"
#include "AIWidgetInspectorModule.h"
#include "Commands/AIWidgetCommand.h"
#include "Commands/AIWidgetCommandParser.h"
#include "Commands/AIWidgetPersistentApplier.h"

#include "BaseWidgetBlueprint.h"
#include "Commands/AIWidgetRuntimePreview.h"
#include "Inspection/AIWidgetInspector.h"
#include "Inspection/AIWidgetSelection.h"
#include "Inspection/AIWidgetSourceResolver.h"
#include "UI/SAIWidgetTerminal.h"
#include "WidgetPicking/AIWidgetHighlighter.h"
#include "WidgetPicking/AIWidgetPicker.h"

#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Layout/WidgetPath.h"
#include "Misc/Attribute.h"
#include "Misc/FileHelper.h"
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

	// 패널 전용 단축키. 전역이 아니라 여기 묶여 있어서 패널에 포커스가 있을 때만 듣는다.
	PanelCommands = MakeShared<FUICommandList>();
	PanelCommands->MapAction(
		FAIWidgetInspectorCommands::Get().SaveWidgetAsset,
		FExecuteAction::CreateSP(this, &SAIWidgetInspectorPanel::ExecuteSaveAsset),
		FCanExecuteAction::CreateSP(this, &SAIWidgetInspectorPanel::CanSaveAsset));

	Providers = FAIWidgetInspectorModule::Get().GetProviders();
	if (Providers.Num() > 0)
	{
		ActiveProvider = Providers[0];

		// 쓸 수 있으면 대화형 세션을 기본으로 고른다. 나머지 Provider는 할 수 있는 일이
		// 좁아서, 위젯을 고쳐 달라고 하면 무엇을 못 하는지부터 설명하는 답이 돌아온다.
		// 무엇을 고르느냐로 결과가 갈린다는 걸 처음 쓰는 사람이 알 방법이 없다.
		for (const FProviderPtr& Provider : Providers)
		{
			if (Provider.IsValid() && Provider->IsInteractive() && Provider->IsAvailable())
			{
				ActiveProvider = Provider;
				break;
			}
		}
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
			.Padding(8.0f, 0.0f, 8.0f, 2.0f)
			[
				BuildTerminalSection()
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				MakeSection(LOCTEXT("SectionChangePlan", "Change Plan"), false, BuildChangePlanSection(),
					TAttribute<EVisibility>(this, &SAIWidgetInspectorPanel::GetResponseAreaVisibility))
			]
		]
	];

	// 기본 Provider를 고를 때는 터미널 위젯이 아직 없었다. 여기서 맞춰 두지 않으면 목록에는
	// Codex가 떠 있는데 터미널에서는 claude가 도는 상태가 된다.
	if (ActiveProvider.IsValid() && ActiveProvider->IsInteractive() && TerminalWidget.IsValid())
	{
		TerminalWidget->SetCli(ActiveProvider->GetTerminalCli());
	}

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
				.ToolTipText(LOCTEXT("InspectTooltip", "Turn on Inspect Mode. Click any widget to select it; Esc or right-click cancels."))
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
				.ToolTipText(LOCTEXT("HighlightTooltip", "Keep drawing an outline around the selected widget."))
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

TSharedRef<SWidget> SAIWidgetInspectorPanel::MakeSection(const FText& InTitle, bool bInInitiallyCollapsed, TSharedRef<SWidget> InContent,
	TAttribute<EVisibility> InVisibility)
{
	// 모든 섹션이 이 함수를 지난다. 여백과 제목 모양을 여기서만 정하면 섹션마다 어긋날 일이 없다.
	return SNew(SExpandableArea)
		.InitiallyCollapsed(bInInitiallyCollapsed)
		.AreaTitle(InTitle)
		.AreaTitleFont(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 8.0f))
		.Visibility(InVisibility)
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
			.Text(LOCTEXT("AncestorMetaData", "This Slate widget has no UMG metadata, so the values shown come from an ancestor."))
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
				.ToolTipText(LOCTEXT("OpenBlueprintTooltip", "Open the Widget Blueprint asset that created this widget."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanOpenBlueprint)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleOpenBlueprintClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenSource", "Open Source"))
				.ToolTipText(LOCTEXT("OpenSourceTooltip", "Open the C++ location that created this widget in your IDE."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanOpenSource)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleOpenSourceClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveAsset", "Save Asset"))
				.ToolTipText(LOCTEXT("SaveAssetTooltip", "Save the changed Widget Blueprint. Until you save, Ctrl+Z still undoes it."))
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
				.ToolTipText(LOCTEXT("ParentTooltip", "Select the widget one step outwards, for when a click landed on the label instead of the button."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSelectParent)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleSelectParentClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Child", "Child"))
				.ToolTipText(LOCTEXT("ChildTooltip", "Select the widget one step inwards along the path."))
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
		return LOCTEXT("StatusInspecting", "Inspect Mode: click any widget to select it. Esc or right-click cancels.");
	}

	if (!Selection->IsValid())
	{
		return LOCTEXT("StatusEmpty", "No widget is selected.");
	}

	if (!Selection->GetSelectedWidget().IsValid())
	{
		return LOCTEXT("StatusDestroyed", "The selected widget no longer exists.");
	}

	return FText::Format(
		LOCTEXT("StatusSelected", "Step {1} of {0} in the path."),
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
			.Text(LOCTEXT("PreviewNotice", "Applies to the live widget only. The asset is untouched, and the change goes away when the widget is rebuilt."))
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
				.HintText(LOCTEXT("PreviewTextHint", "Select a TextBlock to type here."))
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
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(this, &SAIWidgetInspectorPanel::GetCommitPreviewText)
				.ToolTipText(LOCTEXT("CommitPreviewTooltip",
					"Write the current preview into the Widget Blueprint and save it. "
					"Ctrl+Z undoes it until you save; after saving it is in the file."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanCommitPreview)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleCommitPreviewClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(this, &SAIWidgetInspectorPanel::GetRevertPreviewText)
				.ToolTipText(LOCTEXT("RevertPreviewTooltip", "Restore every preview made this session to its original value."))
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

FText SAIWidgetInspectorPanel::GetCommitPreviewText() const
{
	const int32 PreviewCount = RuntimePreview.IsValid() ? RuntimePreview->Num() : 0;
	return PreviewCount > 0
		? FText::Format(LOCTEXT("CommitPreviewCount", "Save to Asset ({0})"), FText::AsNumber(PreviewCount))
		: LOCTEXT("CommitPreview", "Save to Asset");
}

bool SAIWidgetInspectorPanel::CanCommitPreview() const
{
	return HasPreviews() && FAIWidgetPersistentApplier::CanApply(CachedInspection);
}

FReply SAIWidgetInspectorPanel::HandleCommitPreviewClicked()
{
	if (!RuntimePreview.IsValid())
	{
		return FReply::Handled();
	}

	// 미리보기 항목은 되돌리려고 '처음 값'만 들고 있다. 에셋에 넣을 것은 지금 값이므로
	// 살아 있는 Widget에서 다시 읽는다.
	TArray<FAIWidgetCommand> CommandsToApply;
	for (const FAIWidgetPreviewEntry& Entry : RuntimePreview->GetEntries())
	{
		const UWidget* Widget = Entry.Widget.Get();
		if (!Widget)
		{
			continue;
		}

		FAIWidgetCommand Command;
		if (FAIWidgetCommand::CaptureFrom(Widget, Entry.Operation, Entry.WidgetName, Command))
		{
			CommandsToApply.Add(MoveTemp(Command));
		}
	}

	if (CommandsToApply.IsEmpty())
	{
		ChangePlanStatusText = LOCTEXT("CommitNothing", "There are no previews to write to the asset.");
		return FReply::Handled();
	}

	const FAIWidgetPersistentResult Result = FAIWidgetPersistentApplier::Apply(CommandsToApply, CachedInspection);

	// 컴파일이 선택을 죽이기 전에 저장할 손잡이를 잡아 둔다.
	if (Result.Blueprint.IsValid())
	{
		FAIWidgetInspectorModule::Get().SetLastAppliedBlueprint(Result.Blueprint.Get());
	}

	if (Result.AppliedCount == 0)
	{
		ChangePlanStatusText = Result.Error.IsEmpty()
			? LOCTEXT("CommitNothingApplied", "No changes were applied to the asset.")
			: Result.Error;
		return FReply::Handled();
	}

	FText SaveError;
	if (!FAIWidgetPersistentApplier::SaveAsset(Result.Blueprint.Get(), SaveError))
	{
		// 적용은 됐고 저장만 실패한 상태다. 여기서 "실패"라고만 하면 사용자가
		// 아무 일도 안 일어난 줄 알고 다시 누른다.
		ChangePlanStatusText = FText::Format(
			LOCTEXT("CommitAppliedNotSaved", "Applied {0} change(s) to the asset but could not save: {1}"),
			FText::AsNumber(Result.AppliedCount),
			SaveError);
		return FReply::Handled();
	}

	// 에셋에 들어갔으므로 미리보기는 더 이상 '임시'가 아니다. 목록에 남겨 두면
	// Revert가 방금 저장한 값을 되돌려 에셋과 화면이 어긋난다.
	RuntimePreview->ForgetAll();

	ChangePlanStatusText = FText::Format(
		LOCTEXT("CommitSaved", "Applied {0} change(s) to the asset and saved."),
		FText::AsNumber(Result.AppliedCount));

	return FReply::Handled();
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
	return MakeSection(LOCTEXT("SectionAskAI", "Ask AI"), false,
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
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

			// 남는 자리를 콤보가 가져간다. 이름이 짧은 것과 긴 것이 섞여 있어서, 폭을
			// 내용에 맞추면 고를 때마다 옆의 버튼이 따라 움직인다.
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
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
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CopyContext", "Copy Context"))
				.ToolTipText(LOCTEXT("CopyContextTooltip", "Copy just the widget context, without a question."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::HasSelection)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleCopyContextClicked)
			]
		]

		// 못 쓰는 Provider를 골랐을 때 이유를 여기 띄운다. 회색 버튼만 보고
		// 툴팁을 찾아 마우스를 올려 볼 사람은 드물다.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Visibility(this, &SAIWidgetInspectorPanel::GetProviderWarningVisibility)
			.Padding(8.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(this, &SAIWidgetInspectorPanel::GetProviderWarningText)
				.ColorAndOpacity(FLinearColor(1.0f, 0.72f, 0.35f))
				.AutoWrapText(true)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBox)
			.HeightOverride(64.0f)
			[
				SAssignNew(QuestionTextBox, SMultiLineEditableTextBox)
				.HintText(LOCTEXT("QuestionHint", "e.g. why does this button not respond to clicks?"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RequestChange", "Request Change"))
				.ToolTipText(LOCTEXT("RequestChangeTooltip", "Send the same context, asking for the change to be made instead of prose."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSendRequest)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleRequestChangeClicked)
			]

			// 둘 중 하나에 무게를 준다. 같은 모양으로 나란히 두면 무엇이 기본 동작인지
			// 매번 읽어서 골라야 한다.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText"))
				.Text(this, &SAIWidgetInspectorPanel::GetAskButtonText)
				.ToolTipText(LOCTEXT("AskAITooltip", "Send the widget context and your question to the provider."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanSendRequest)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleAskAIClicked)
			]
		]

		// 아래는 답을 손으로 받아 오는 Provider에만 쓰인다. 대화형에서는 통째로 접힌다.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			.Visibility(this, &SAIWidgetInspectorPanel::GetResponseAreaVisibility)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ResponseLabel", "AI Response"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(120.0f)
				[
					SAssignNew(ResponseTextBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("ResponseHint", "Paste the assistant's reply here, then press Parse Change."))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("ParseChange", "Parse Change"))
				.ToolTipText(LOCTEXT("ParseChangeTooltip", "Read and check the change in the reply. Nothing runs yet."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanParseChange)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleParseChangeClicked)
			]
		]);
}

TSharedRef<SWidget> SAIWidgetInspectorPanel::BuildTerminalSection()
{
	return MakeSection(LOCTEXT("SectionTerminal", "CLI Session"), false,
		SAssignNew(TerminalWidget, SAIWidgetTerminal));
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

		// 목록에서 고른 것과 터미널에서 도는 것이 어긋나면, 사용자는 무엇과 이야기하고
		// 있는지 알 수 없다. 대화형을 골랐으면 터미널을 그 CLI로 갈아 끼운다.
		if (InProvider.IsValid() && InProvider->IsInteractive() && TerminalWidget.IsValid())
		{
			TerminalWidget->SetCli(InProvider->GetTerminalCli());
		}
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

FText SAIWidgetInspectorPanel::GetProviderWarningText() const
{
	return ActiveProvider.IsValid() ? ActiveProvider->GetUnavailableReason() : FText::GetEmpty();
}

EVisibility SAIWidgetInspectorPanel::GetResponseAreaVisibility() const
{
	// 대화형은 답이 터미널로 흐르므로 주고받을 칸이 필요 없다.
	const bool bInteractive = ActiveProvider.IsValid() && ActiveProvider->IsInteractive();
	return bInteractive ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SAIWidgetInspectorPanel::GetProviderWarningVisibility() const
{
	// 이유가 없으면 줄 자체를 없앤다. 빈 칸이 남으면 뭔가 잘못된 것처럼 보인다.
	return GetProviderWarningText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
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

	// 대화형 Provider는 응답을 돌려주지 않는다. 답은 터미널에 흐르고, 끝나는 시점도
	// 우리가 알 수 없다. 그래서 요청/응답 경로를 타지 않고 곧장 터미널로 넘긴다.
	if (ActiveProvider->IsInteractive())
	{
		SendToTerminal(InKind);
		return;
	}

	FAIWidgetRequest Request;
	Request.Kind = InKind;
	Request.Context = BuildCurrentContext();
	Request.UserMessage = QuestionTextBox->GetText().ToString();

	bRequestInFlight = true;
	PendingRequestKind = InKind;
	ClearChangePlan();

	ActiveProvider->SendRequest(Request, FOnAIWidgetResponse::CreateSP(this, &SAIWidgetInspectorPanel::HandleAIResponse));
}

void SAIWidgetInspectorPanel::SendToTerminal(EAIWidgetRequestKind InKind)
{
	if (!TerminalWidget.IsValid())
	{
		return;
	}

	const FString ContextPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectIntermediateDir() / TEXT("AIWidgetInspector") / TEXT("WidgetContext.md"));

	// 무엇을 쓸 수 있는지 Context에 같이 적어 준다. 이 안내가 없으면 CLI는 자기가 무엇을
	// 할 수 있는 자리인지 몰라서, 에디터 Tool의 좁은 목록만 보고 "그건 못 한다"로 답하거나
	// 반대로 살아 있는 에디터를 두고 파일만 고친다. 둘 다 반쪽짜리다.
	const FString Capabilities = TEXT(
		"\n[Where you are]\n"
		"The Unreal editor is running and you are attached to it. There are two ways to act, and a job often needs both:\n"
		"- The editor's own tools are on the \"unreal\" MCP server. Start with list_toolsets, then describe_toolset. "
		"The widget toolset changes the selected widget in the running editor and can save the asset.\n"
		"- The working directory is the project root, so the source and the .uproject are right here. "
		"Write or edit C++, add a module dependency, fix the bug, whatever the request actually needs.\n"
		"Do the whole job. If it needs a C++ class the editor tools cannot make, write the files. "
		"If it only takes effect after a compile and an editor restart, say so at the end, "
		"along with anything left to do by hand.\n");

	if (!FFileHelper::SaveStringToFile(BuildCurrentContext() + Capabilities, *ContextPath))
	{
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("Could not write the widget context file: %s"), *ContextPath);

		if (ResponseTextBox.IsValid())
		{
			ResponseTextBox->SetText(FText::Format(
				LOCTEXT("ContextWriteFailed", "Could not write the widget context to {0}."),
				FText::FromString(ContextPath)));
		}
		return;
	}

	const FString Question = QuestionTextBox->GetText().ToString();

	// 답만 받을 것인지 고쳐 달라고 할 것인지를 문장으로 갈라 준다. 대화형 세션에는
	// 원샷 Provider가 쓰던 응답 스키마를 붙이지 않는다. JSON을 파싱할 사람이 없고,
	// CLI는 MCP Tool로 직접 고칠 수 있기 때문이다.
	const FString Instruction = (InKind == EAIWidgetRequestKind::Question)
		? TEXT("answer this about it")
		: TEXT("carry this out");

	const FString Prompt = FString::Printf(
		TEXT("Read \"%s\" for the Unreal widget I have selected, then %s: %s"),
		*ContextPath,
		*Instruction,
		*Question);

	TerminalWidget->SendPrompt(Prompt);

	if (ResponseTextBox.IsValid())
	{
		ResponseTextBox->SetText(LOCTEXT("SentToTerminal",
			"Sent to the CLI Session below. The reply appears there, and you answer its permission prompts with Enter."));
	}
}

FReply SAIWidgetInspectorPanel::HandleAskAIClicked()
{
	SendRequest(EAIWidgetRequestKind::Question);
	return FReply::Handled();
}

FReply SAIWidgetInspectorPanel::HandleRequestChangeClicked()
{
	// Provider가 에디터 Tool을 부를 수 있으면 JSON을 받아 우리가 적용할 이유가 없다.
	const bool bUsesTools = ActiveProvider.IsValid() && ActiveProvider->UsesEditorTools();

	SendRequest(bUsesTools
		? EAIWidgetRequestKind::ToolChangeRequest
		: EAIWidgetRequestKind::ChangeRequest);

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
		UE_LOG(LogAIWidgetInspector, Warning, TEXT("AI request failed: %s"), *InResponse.Message.ToString());
		return;
	}

	// Tool 모드에서는 답이 왔을 때 변경이 이미 끝나 있다. 본문에서 JSON을 찾으면
	// 설명에 섞인 값을 명령으로 오해해 같은 변경을 한 번 더 적용하게 된다.
	if (PendingRequestKind == EAIWidgetRequestKind::ToolChangeRequest)
	{
		UE_LOG(LogAIWidgetInspector, Log, TEXT("The assistant handled it directly through the editor tools."));
		return;
	}

	// 질문에 대한 답은 읽으라고 보여 줄 뿐이다. 여기서 뭔가 실행하면
	// "물어봤을 뿐인데 화면이 바뀌는" 일이 생긴다.
	if (PendingRequestKind != EAIWidgetRequestKind::ChangeRequest)
	{
		return;
	}

	// 변경 요청이었으면 여기까지 자동으로 온다. 응답을 읽어 계획을 만들고,
	// 살아있는 인스턴스에 적용해 눈으로 볼 수 있게 한다.
	//
	// 에셋에는 쓰지 않는다. 미리보기는 Revert 한 번으로 되돌아가지만 에셋 변경은
	// 파일을 건드리는 일이라, 사람이 계획을 보고 누르는 단계를 남겨 둔다.
	if (BuildChangePlanFromResponse() <= 0)
	{
		return;
	}

	const int32 AppliedCount = ApplyChangePlanToPreview();

	// 계획은 남겨 둔다. 방금 적용한 그 계획을 그대로 Apply to Asset으로 넘길 수 있어야 한다.
	if (ChangePlanListView.IsValid())
	{
		ChangePlanListView->RequestListRefresh();
	}

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Applied %d change(s) from the reply as a preview."), AppliedCount);
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
				.ToolTipText(LOCTEXT("ApplyPreviewTooltip", "Apply the changes that passed validation to the live widget. The asset is untouched."))
				.IsEnabled(this, &SAIWidgetInspectorPanel::CanApplyChange)
				.OnClicked(this, &SAIWidgetInspectorPanel::HandleApplyChangeClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyToAsset", "Apply to Asset"))
				.ToolTipText(LOCTEXT("ApplyToAssetTooltip", "Edit and recompile the Widget Blueprint. Ctrl+Z undoes it; saving is a separate step."))
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
			LOCTEXT("RejectedChange", "{0}   {1}   rejected: {2}"),
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
	BuildChangePlanFromResponse();
	return FReply::Handled();
}

int32 SAIWidgetInspectorPanel::BuildChangePlanFromResponse()
{
	ClearChangePlan();

	if (!ResponseTextBox.IsValid())
	{
		return 0;
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
		StatusBuilder.AppendLine(LOCTEXT("NothingParsed", "There was no change to read."));
	}
	else
	{
		StatusBuilder.AppendLine(FText::Format(
			LOCTEXT("ParsedSummary", "{1} of {0} can be applied."),
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

	UE_LOG(LogAIWidgetInspector, Log, TEXT("Change plan: %d parsed, %d applicable, %d malformed."),
		ChangePlanEntries.Num(), ValidCount, ParseErrors.Num());

	return ValidCount;
}

FReply SAIWidgetInspectorPanel::HandleApplyChangeClicked()
{
	ApplyChangePlanToPreview();

	// 손으로 누른 경우에는 계획을 비운다. 같은 계획을 두 번 눌러 중복 적용되는 걸 막는다.
	const FText StatusToKeep = ChangePlanStatusText;
	ClearChangePlan();
	ChangePlanStatusText = StatusToKeep;

	return FReply::Handled();
}

int32 SAIWidgetInspectorPanel::ApplyChangePlanToPreview()
{
	if (!RuntimePreview.IsValid())
	{
		return 0;
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
			LOCTEXT("AppliedWithFailures", "{0} applied, {1} failed. Revert Preview undoes them."),
			FText::AsNumber(AppliedCount),
			FText::AsNumber(FailedCount))
		: FText::Format(
			LOCTEXT("Applied", "Applied {0}. The asset is unchanged; Revert Preview undoes them."),
			FText::AsNumber(AppliedCount));

	return AppliedCount;
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

	// 컴파일이 선택을 죽이기 전에 저장할 손잡이를 남긴다.
	if (Result.Blueprint.IsValid())
	{
		FAIWidgetInspectorModule::Get().SetLastAppliedBlueprint(Result.Blueprint.Get());
	}

	FText Status;
	if (Result.AppliedCount == 0)
	{
		Status = Result.Error.IsEmpty()
			? LOCTEXT("AssetNothingApplied", "No changes were applied to the asset.")
			: Result.Error;
	}
	else
	{
		Status = FText::Format(
			LOCTEXT("AssetApplied", "Applied {0} to the asset and recompiled. Ctrl+Z undoes it; press Save Asset under Source to save."),
			FText::AsNumber(Result.AppliedCount));

		if (Result.FailedCount > 0)
		{
			Status = FText::Format(
				LOCTEXT("AssetAppliedWithFailures", "{0} ({1} failed: {2})"),
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
	if (FAIWidgetPersistentApplier::IsAssetDirty(CachedInspection))
	{
		return true;
	}

	// 에셋에 적용하면 Blueprint가 재컴파일되고 화면의 Widget이 파괴된다. 그러면 선택이
	// 죽어 CachedInspection으로는 에셋을 못 찾고, 방금 바꿔 놓고도 버튼이 회색이 된다.
	return FAIWidgetPersistentApplier::IsAssetDirty(FAIWidgetInspectorModule::Get().GetLastAppliedBlueprint());
}

void SAIWidgetInspectorPanel::ExecuteSaveAsset()
{
	HandleSaveAssetClicked();
}

FReply SAIWidgetInspectorPanel::OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (PanelCommands.IsValid() && PanelCommands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(InGeometry, InKeyEvent);
}

FReply SAIWidgetInspectorPanel::HandleSaveAssetClicked()
{
	// CanSaveAsset과 같은 순서로 찾는다. 선택이 살아 있으면 그쪽, 죽었으면 마지막에 건드린 것.
	UBaseWidgetBlueprint* Blueprint = FAIWidgetPersistentApplier::GetWidgetBlueprint(CachedInspection);
	if (!Blueprint)
	{
		Blueprint = FAIWidgetInspectorModule::Get().GetLastAppliedBlueprint();
	}

	FText Error;
	if (FAIWidgetPersistentApplier::SaveAsset(Blueprint, Error))
	{
		ChangePlanStatusText = LOCTEXT("AssetSaved", "Asset saved.");
	}
	else
	{
		ChangePlanStatusText = Error;
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
