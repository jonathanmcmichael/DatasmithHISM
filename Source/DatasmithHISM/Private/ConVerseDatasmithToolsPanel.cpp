#include "ConVerseDatasmithToolsPanel.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConVerseDatasmithToolsPanel"

void SConVerseDatasmithToolsPanel::Construct(const FArguments& InArgs)
{
	OnOpenOptimizedImport = InArgs._OnOpenOptimizedImport;
	OnDedupeMeshes = InArgs._OnDedupeMeshes;
	OnCreateManagedISMs = InArgs._OnCreateManagedISMs;
	OnBatchISMs = InArgs._OnBatchISMs;
	OnAnalyzeISMs = InArgs._OnAnalyzeISMs;
	OnEnableNanite = InArgs._OnEnableNanite;
	OnExplodeISMs = InArgs._OnExplodeISMs;
	OnDedupeAndCreateISMs = InArgs._OnDedupeAndCreateISMs;
	OnToggleUseHISM = InArgs._OnToggleUseHISM;
	CanRunSelectionTools = InArgs._CanRunSelectionTools;
	UseHISMEnabled = InArgs._UseHISMEnabled;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PanelTitle", "DatasmithHISM Tools"))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PanelDescription", "Run Datasmith optimization tools on the current actor selection."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("OptimizedImport", "Optimized Datasmith Import"))
				.ToolTipText(LOCTEXT(
					"OptimizedImportTooltip",
					"Use when importing a new Datasmith scene, including CAD, Revit, and IFC sources. Optimizes before individual source actors populate the level, with ISM or HISM output and verification."))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnOpenOptimizedImport); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 6.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SConVerseDatasmithToolsPanel::GetUseHISMCheckState)
				.OnCheckStateChanged(this, &SConVerseDatasmithToolsPanel::HandleUseHISMChanged)
				.ToolTipText(LOCTEXT("UseHISMTooltip", "Use Hierarchical Instanced Static Mesh components for managed conversion and analysis."))
				[
					SNew(STextBlock).Text(LOCTEXT("UseHISM", "Use HISM"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("AnalyzeISMs", "Analyze ISMs"))
				.ToolTipText(LOCTEXT("AnalyzeISMsTooltip", "Preview managed ISM conversion without making changes."))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnAnalyzeISMs); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("CreateManagedISMs", "Managed ISMs"))
				.ToolTipText(LOCTEXT(
					"CreateManagedISMsTooltip",
					"Use when an existing Datasmith or IFC level needs post-import conversion. Groups repeated geometry by family, mesh, and materials into managed ISM or HISM components."))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnCreateManagedISMs); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("BatchISMs", "Batch ISMs"))
				.ToolTipText(LOCTEXT(
					"BatchISMsTooltip",
					"Use when an existing level only needs Unreal's standard same-mesh batching. Faster and simpler than Managed ISMs, but not family-structure-aware."))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnBatchISMs); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 6.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("DedupeMeshes", "Dedupe Meshes"))
				.ToolTipText(LOCTEXT(
					"DedupeMeshesTooltip",
					"Use when an existing level has separate but duplicate static mesh assets and you want Content Browser cleanup. Does not create ISMs or change actor layout."))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnDedupeMeshes); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("DedupeAndCreateISMs", "Dedupe + ISMs"))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnDedupeAndCreateISMs); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("EnableNanite", "Enable Nanite"))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnEnableNanite); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.IsEnabled(CanRunSelectionTools)
				.Text(LOCTEXT("ExplodeISMs", "Explode ISMs"))
				.OnClicked_Lambda([this]() { return ExecuteAction(OnExplodeISMs); })
			]
		]
	];
}

FReply SConVerseDatasmithToolsPanel::ExecuteAction(const FSimpleDelegate& Action)
{
	Action.ExecuteIfBound();
	return FReply::Handled();
}

ECheckBoxState SConVerseDatasmithToolsPanel::GetUseHISMCheckState() const
{
	return UseHISMEnabled.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SConVerseDatasmithToolsPanel::HandleUseHISMChanged(ECheckBoxState NewState)
{
	if ((NewState == ECheckBoxState::Checked) != UseHISMEnabled.Get())
	{
		OnToggleUseHISM.ExecuteIfBound();
	}
}

#undef LOCTEXT_NAMESPACE
