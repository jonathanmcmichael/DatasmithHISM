#include "ConVerseDatasmithImportPanel.h"

#include "DatasmithTranslatorManager.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDesktopPlatform.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConVerseDatasmithImportPanel"

namespace ConVerseDatasmithImportPanel
{
	constexpr float LabelWidth = 180.0f;
	constexpr float RowPadding = 5.0f;

	FSlateColor ErrorColor()
	{
		return FSlateColor(FLinearColor(0.85f, 0.16f, 0.12f));
	}

	FSlateColor SuccessColor()
	{
		return FSlateColor(FLinearColor(0.15f, 0.65f, 0.25f));
	}

	FSlateColor WarningColor()
	{
		return FSlateColor(FLinearColor(0.9f, 0.55f, 0.08f));
	}

	/**
	 * Collects the extensions of every enabled Datasmith translator.
	 *
	 * The manager returns "ext;description" entries, so the set reflects exactly which formats
	 * this installation can actually read. Enumerating rather than hardcoding means enabling a
	 * CAD, Revit, or IFC plugin widens the importer with no code change here.
	 */
	TSet<FString> GatherSupportedExtensions()
	{
		TSet<FString> Extensions;
		for (const FString& Format : FDatasmithTranslatorManager::Get().GetSupportedFormats())
		{
			FString Extension;
			FString Description;
			if (Format.Split(TEXT(";"), &Extension, &Description))
			{
				Extension.TrimStartAndEndInline();
				if (!Extension.IsEmpty())
				{
					Extensions.Add(Extension.ToLower());
				}
			}
		}
		return Extensions;
	}

	bool IsExtensionSupportedByTranslator(const FString& FilePath)
	{
		const FString Extension = FPaths::GetExtension(FilePath).ToLower();
		if (Extension.IsEmpty())
		{
			return false;
		}
		return GatherSupportedExtensions().Contains(Extension);
	}

	/** Builds an OpenFileDialog filter covering every translator-supported format. */
	FString BuildSourceFileDialogFilter()
	{
		TArray<FString> SortedExtensions = GatherSupportedExtensions().Array();
		SortedExtensions.Sort();
		if (SortedExtensions.IsEmpty())
		{
			// No translators resolved. Fall back rather than presenting an empty picker.
			return TEXT("Datasmith scene (*.udatasmith)|*.udatasmith|All files (*.*)|*.*");
		}

		TArray<FString> Patterns;
		Patterns.Reserve(SortedExtensions.Num());
		for (const FString& Extension : SortedExtensions)
		{
			Patterns.Add(FString::Printf(TEXT("*.%s"), *Extension));
		}
		const FString CombinedPatterns = FString::Join(Patterns, TEXT(";"));

		// A single combined entry keeps the picker usable when many CAD plugins are enabled.
		return FString::Printf(
			TEXT("Datasmith supported sources (%s)|%s|All files (*.*)|*.*"),
			*CombinedPatterns,
			*CombinedPatterns);
	}
}

void SConVerseDatasmithImportPanel::Construct(const FArguments& InArgs)
{
	ReportText = LOCTEXT(
		"InitialReport",
		"Analysis and verification results will appear here. Analyze reads and plans the source scene without creating assets or actors.");
	StatusDetail = LOCTEXT("IdleStatus", "Select a Datasmith-supported source file to begin.");

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
				.Text(LOCTEXT("PanelTitle", "Optimized Datasmith Import"))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"PanelDescription",
					"Analyze a Datasmith scene, replace safe repeated mesh actors in memory, then import and verify ISM or HISM output in the current editor world."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.0f)

				+ SGridPanel::Slot(0, 0)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ConVerseDatasmithImportPanel::LabelWidth)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SourceFileLabel", "Source file"))
					]
				]

				+ SGridPanel::Slot(1, 0)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				[
					SAssignNew(SourcePathTextBox, SEditableTextBox)
					.HintText(LOCTEXT("SourceFileHint", "Choose a Datasmith source file (.udatasmith, CAD, Revit, IFC)"))
					.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
					.OnTextChanged(this, &SConVerseDatasmithImportPanel::HandleSourcePathChanged)
				]

				+ SGridPanel::Slot(2, 0)
				.Padding(8.0f, ConVerseDatasmithImportPanel::RowPadding, 0.0f, ConVerseDatasmithImportPanel::RowPadding)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseButton", "Browse..."))
					.ToolTipText(LOCTEXT("BrowseTooltip", "Select a Datasmith scene file."))
					.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
					.OnClicked(this, &SConVerseDatasmithImportPanel::HandleBrowseForSource)
				]

				+ SGridPanel::Slot(0, 1)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ConVerseDatasmithImportPanel::LabelWidth)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DestinationLabel", "Destination content folder"))
					]
				]

				+ SGridPanel::Slot(1, 1)
				.ColumnSpan(2)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(DestinationPath))
					.HintText(LOCTEXT("DestinationHint", "/Game/DatasmithOptimized"))
					.ToolTipText(LOCTEXT("DestinationTooltip", "Unreal long package path used for the imported Datasmith scene and assets."))
					.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
					.OnTextChanged(this, &SConVerseDatasmithImportPanel::HandleDestinationPathChanged)
				]

				+ SGridPanel::Slot(0, 2)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputComponentLabel", "Output component"))
				]

				+ SGridPanel::Slot(1, 2)
				.ColumnSpan(2)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
						.IsChecked(this, &SConVerseDatasmithImportPanel::GetISMCheckState)
						.OnCheckStateChanged(this, &SConVerseDatasmithImportPanel::HandleISMCheckStateChanged)
						.ToolTipText(LOCTEXT("ISMTooltip", "Create standard Instanced Static Mesh components. This is the default Nanite-first option."))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ISMOption", "ISM"))
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(18.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
						.IsChecked(this, &SConVerseDatasmithImportPanel::GetHISMCheckState)
						.OnCheckStateChanged(this, &SConVerseDatasmithImportPanel::HandleHISMCheckStateChanged)
						.ToolTipText(LOCTEXT("HISMTooltip", "Keep Datasmith's native Hierarchical Instanced Static Mesh components."))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HISMOption", "HISM"))
						]
					]
				]

				+ SGridPanel::Slot(0, 3)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MinimumInstancesLabel", "Minimum instances"))
				]

				+ SGridPanel::Slot(1, 3)
				.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
				.HAlign(HAlign_Left)
				[
					SNew(SBox)
					.WidthOverride(120.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value(this, &SConVerseDatasmithImportPanel::GetMinimumInstances)
						.MinValue(2)
						.MinSliderValue(2)
						.AllowSpin(true)
						.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
						.OnValueChanged(this, &SConVerseDatasmithImportPanel::HandleMinimumInstancesChanged)
						.OnValueCommitted(this, &SConVerseDatasmithImportPanel::HandleMinimumInstancesCommitted)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("TessellationSection", "Tessellation (CAD, Revit, IFC)"))
				.InitiallyCollapsed(true)
				.Padding(FMargin(8.0f, 6.0f, 0.0f, 0.0f))
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(this, &SConVerseDatasmithImportPanel::GetTessellationSummaryText)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SGridPanel)
						.FillColumn(1, 1.0f)

						+ SGridPanel::Slot(0, 0)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(ConVerseDatasmithImportPanel::LabelWidth)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ChordToleranceLabel", "Chord tolerance (cm)"))
							]
						]

						+ SGridPanel::Slot(1, 0)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.Value(this, &SConVerseDatasmithImportPanel::GetChordTolerance)
								.MinValue(0.005f)
								.MinSliderValue(0.005f)
								.MaxSliderValue(1.0f)
								.AllowSpin(true)
								.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
								.OnValueCommitted(this, &SConVerseDatasmithImportPanel::HandleChordToleranceCommitted)
								.ToolTipText(LOCTEXT("ChordToleranceTooltip", "Maximum distance between a generated triangle and the original surface. Smaller values produce more triangles. Minimum 0.005."))
							]
						]

						+ SGridPanel::Slot(0, 1)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NormalToleranceLabel", "Normal tolerance (deg)"))
						]

						+ SGridPanel::Slot(1, 1)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.Value(this, &SConVerseDatasmithImportPanel::GetNormalTolerance)
								.MinValue(5.0f)
								.MaxValue(90.0f)
								.MinSliderValue(5.0f)
								.MaxSliderValue(90.0f)
								.AllowSpin(true)
								.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
								.OnValueCommitted(this, &SConVerseDatasmithImportPanel::HandleNormalToleranceCommitted)
								.ToolTipText(LOCTEXT("NormalToleranceTooltip", "Maximum angle between adjacent triangles. Smaller values produce more triangles. Range 5 to 90 degrees."))
							]
						]

						+ SGridPanel::Slot(0, 2)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("MaxEdgeLengthLabel", "Max edge length (cm)"))
						]

						+ SGridPanel::Slot(1, 2)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SNumericEntryBox<float>)
								.Value(this, &SConVerseDatasmithImportPanel::GetMaxEdgeLength)
								.MinValue(0.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(100.0f)
								.AllowSpin(true)
								.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
								.OnValueCommitted(this, &SConVerseDatasmithImportPanel::HandleMaxEdgeLengthCommitted)
								.ToolTipText(LOCTEXT("MaxEdgeLengthTooltip", "Maximum length of any generated edge. 0 means no constraint; any other value is raised to at least 1.0."))
							]
						]

						+ SGridPanel::Slot(0, 3)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StitchingLabel", "Stitching technique"))
						]

						+ SGridPanel::Slot(1, 3)
						.Padding(0.0f, ConVerseDatasmithImportPanel::RowPadding)
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride(160.0f)
							[
								SNew(SComboButton)
								.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
								.OnGetMenuContent(this, &SConVerseDatasmithImportPanel::BuildStitchingMenu)
								.ToolTipText(LOCTEXT("StitchingTooltip", "Stitching applied before tessellation. Sewing can add or remove objects, which may change how instances group."))
								.ButtonContent()
								[
									SNew(STextBlock)
									.Text(this, &SConVerseDatasmithImportPanel::GetStitchingText)
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					.HAlign(HAlign_Left)
					[
						SNew(SButton)
						.Text(LOCTEXT("ResetTessellation", "Reset to defaults"))
						.IsEnabled(this, &SConVerseDatasmithImportPanel::IsInputEnabled)
						.OnClicked_Lambda([this]()
						{
							ResetTessellationToDefaults();
							return FReply::Handled();
						})
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(this, &SConVerseDatasmithImportPanel::GetValidationText)
				.ColorAndOpacity(this, &SConVerseDatasmithImportPanel::GetValidationColor)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4.0f, 0.0f))

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AnalyzeButton", "Analyze"))
					.ToolTipText(LOCTEXT("AnalyzeTooltip", "Parse and plan the source scene without creating assets or actors."))
					.IsEnabled(this, &SConVerseDatasmithImportPanel::CanAnalyze)
					.HAlign(HAlign_Center)
					.OnClicked(this, &SConVerseDatasmithImportPanel::HandleAnalyze)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(this, &SConVerseDatasmithImportPanel::GetImportButtonText)
					.ToolTipText(this, &SConVerseDatasmithImportPanel::GetImportButtonToolTipText)
					.IsEnabled(this, &SConVerseDatasmithImportPanel::CanImportAndVerify)
					.HAlign(HAlign_Center)
					.OnClicked(this, &SConVerseDatasmithImportPanel::HandleImportAndVerify)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(this, &SConVerseDatasmithImportPanel::GetStatusText)
				.ColorAndOpacity(this, &SConVerseDatasmithImportPanel::GetStatusColor)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReportLabel", "Report"))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(260.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(this, &SConVerseDatasmithImportPanel::GetReportText)
					.IsReadOnly(true)
					.AlwaysShowScrollbars(true)
					.AutoWrapText(false)
					.AllowContextMenu(true)
				]
			]
		]
	];
}

FReply SConVerseDatasmithImportPanel::HandleBrowseForSource()
{
	if (bOperationInProgress)
	{
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		PanelStatus = EPanelStatus::Failed;
		StatusDetail = LOCTEXT("DesktopPlatformUnavailable", "The system file picker is unavailable.");
		return FReply::Handled();
	}

	const FString DefaultPath = SourcePath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(SourcePath);
	TArray<FString> SelectedFiles;
	const bool bSelected = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		LOCTEXT("SourceDialogTitle", "Select Datasmith Source").ToString(),
		DefaultPath,
		TEXT(""),
		ConVerseDatasmithImportPanel::BuildSourceFileDialogFilter(),
		EFileDialogFlags::None,
		SelectedFiles);

	if (bSelected && !SelectedFiles.IsEmpty())
	{
		const FString NewPath = FPaths::ConvertRelativePathToFull(SelectedFiles[0]);
		if (NewPath != SourcePath)
		{
			bHasRunSinceInputChange = false;
			bLastRunAppliedTessellation = false;
		}
		SourcePath = NewPath;
		SourcePathTextBox->SetText(FText::FromString(SourcePath));
		MarkInputsChanged();
	}

	return FReply::Handled();
}

FReply SConVerseDatasmithImportPanel::HandleAnalyze()
{
	if (!CanAnalyze())
	{
		return FReply::Handled();
	}

	bOperationInProgress = true;
	PanelStatus = EPanelStatus::Working;
	StatusDetail = LOCTEXT("AnalyzingStatus", "Analyzing the Datasmith scene...");

	const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::Analyze(MakeOptions());
	SetResult(Result, false);
	bOperationInProgress = false;
	return FReply::Handled();
}

FReply SConVerseDatasmithImportPanel::HandleImportAndVerify()
{
	if (!CanImportAndVerify())
	{
		return FReply::Handled();
	}

	bOperationInProgress = true;
	PanelStatus = EPanelStatus::Working;
	StatusDetail = LOCTEXT("ImportingStatus", "Importing the optimized scene and verifying the result...");

	const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::ImportAndVerify(MakeOptions());
	SetResult(Result, true);
	bOperationInProgress = false;
	return FReply::Handled();
}

void SConVerseDatasmithImportPanel::HandleSourcePathChanged(const FText& NewText)
{
	const FString NewPath = NewText.ToString().TrimStartAndEnd();
	if (NewPath != SourcePath)
	{
		// Whether tessellation applies is a property of the source format, so a new source
		// invalidates the previous answer. Changing tessellation values alone does not.
		bHasRunSinceInputChange = false;
		bLastRunAppliedTessellation = false;
	}
	SourcePath = NewPath;
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleDestinationPathChanged(const FText& NewText)
{
	DestinationPath = NewText.ToString().TrimStartAndEnd();
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleMinimumInstancesChanged(int32 NewValue)
{
	MinimumInstanceCount = FMath::Max(2, NewValue);
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleMinimumInstancesCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	MinimumInstanceCount = FMath::Max(2, NewValue);
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleISMCheckStateChanged(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked && InstanceType != EConVerseOptimizedInstanceType::ISM)
	{
		InstanceType = EConVerseOptimizedInstanceType::ISM;
		MarkInputsChanged();
	}
}

void SConVerseDatasmithImportPanel::HandleHISMCheckStateChanged(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked && InstanceType != EConVerseOptimizedInstanceType::HISM)
	{
		InstanceType = EConVerseOptimizedInstanceType::HISM;
		MarkInputsChanged();
	}
}

ECheckBoxState SConVerseDatasmithImportPanel::GetISMCheckState() const
{
	return InstanceType == EConVerseOptimizedInstanceType::ISM ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SConVerseDatasmithImportPanel::GetHISMCheckState() const
{
	return InstanceType == EConVerseOptimizedInstanceType::HISM ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

TOptional<int32> SConVerseDatasmithImportPanel::GetMinimumInstances() const
{
	return MinimumInstanceCount;
}

FText SConVerseDatasmithImportPanel::GetValidationText() const
{
	FText Error;
	if (!ValidateSource(Error))
	{
		return Error;
	}

	if (!ValidateDestination(Error))
	{
		return FText::Format(
			LOCTEXT("DestinationValidation", "Source is ready for analysis. Import is unavailable: {0}"),
			Error);
	}

	return LOCTEXT("InputsValid", "Inputs are valid. Ready to analyze or import.");
}

FSlateColor SConVerseDatasmithImportPanel::GetValidationColor() const
{
	FText Error;
	if (!ValidateSource(Error))
	{
		return SourcePath.IsEmpty() ? FSlateColor::UseSubduedForeground() : ConVerseDatasmithImportPanel::ErrorColor();
	}

	return ValidateDestination(Error)
		? ConVerseDatasmithImportPanel::SuccessColor()
		: ConVerseDatasmithImportPanel::WarningColor();
}

FText SConVerseDatasmithImportPanel::GetStatusText() const
{
	return StatusDetail;
}

FSlateColor SConVerseDatasmithImportPanel::GetStatusColor() const
{
	switch (PanelStatus)
	{
	case EPanelStatus::Verified:
	case EPanelStatus::Analyzed:
		return ConVerseDatasmithImportPanel::SuccessColor();

	case EPanelStatus::ImportedWithFailures:
	case EPanelStatus::Cancelled:
	case EPanelStatus::Blocked:
		return ConVerseDatasmithImportPanel::WarningColor();

	case EPanelStatus::Failed:
		return ConVerseDatasmithImportPanel::ErrorColor();

	default:
		return FSlateColor::UseForeground();
	}
}

FText SConVerseDatasmithImportPanel::GetReportText() const
{
	return ReportText;
}

FText SConVerseDatasmithImportPanel::GetImportButtonText() const
{
	return bActiveImportExists
		? LOCTEXT("ReimportButton", "Optimized Reimport and Verify")
		: LOCTEXT("ImportButton", "Import and Verify");
}

FText SConVerseDatasmithImportPanel::GetImportButtonToolTipText() const
{
	return bActiveImportExists
		? LOCTEXT("ReimportTooltip", "An active optimized import already owns this source and destination. Reimport verifies a new session first, then removes the previous session's actors. Superseded assets are retained.")
		: LOCTEXT("ImportTooltip", "Import the optimized scene into the current editor world and verify every planned group.");
}

bool SConVerseDatasmithImportPanel::CanAnalyze() const
{
	FText Error;
	return !bOperationInProgress && ValidateSource(Error);
}

bool SConVerseDatasmithImportPanel::CanImportAndVerify() const
{
	FText Error;
	return !bOperationInProgress && ValidateSource(Error) && ValidateDestination(Error);
}

bool SConVerseDatasmithImportPanel::IsInputEnabled() const
{
	return !bOperationInProgress;
}

bool SConVerseDatasmithImportPanel::ValidateSource(FText& OutError) const
{
	if (SourcePath.IsEmpty())
	{
		OutError = LOCTEXT("SourceRequired", "Select a Datasmith-supported source file.");
		return false;
	}

	// Cheap pre-check against the enabled translators so the user gets an immediate, specific
	// error instead of waiting for the service to fail. The service still treats translator
	// resolution as authoritative, since only it can confirm the file actually parses.
	if (!ConVerseDatasmithImportPanel::IsExtensionSupportedByTranslator(SourcePath))
	{
		OutError = FText::Format(
			LOCTEXT("SourceExtensionUnsupported", "No enabled Datasmith translator handles '.{0}' files. Check that the plugin for this format is enabled."),
			FText::FromString(FPaths::GetExtension(SourcePath)));
		return false;
	}

	if (!FPaths::FileExists(SourcePath))
	{
		OutError = LOCTEXT("SourceMissing", "The selected source file does not exist.");
		return false;
	}

	if (MinimumInstanceCount < 2)
	{
		OutError = LOCTEXT("MinimumInvalid", "Minimum instances must be at least 2.");
		return false;
	}

	return true;
}

bool SConVerseDatasmithImportPanel::ValidateDestination(FText& OutError) const
{
	if (DestinationPath.IsEmpty())
	{
		OutError = LOCTEXT("DestinationRequired", "Enter a destination content folder.");
		return false;
	}

	FText PackageError;
	if (!FPackageName::IsValidLongPackageName(DestinationPath, false, &PackageError))
	{
		OutError = FText::Format(
			LOCTEXT("DestinationInvalid", "Enter a valid Unreal content path such as /Game/DatasmithOptimized. {0}"),
			PackageError);
		return false;
	}

	return true;
}

void SConVerseDatasmithImportPanel::RefreshActiveImportProbe()
{
	FText Error;
	bActiveImportExists = ValidateSource(Error)
		&& ValidateDestination(Error)
		&& FConVerseDatasmithImportService::HasActiveOptimizedImport(MakeOptions());
}

void SConVerseDatasmithImportPanel::MarkInputsChanged()
{
	if (bOperationInProgress)
	{
		return;
	}

	RefreshActiveImportProbe();

	FText Error;
	if (ValidateSource(Error))
	{
		PanelStatus = EPanelStatus::Ready;
		StatusDetail = LOCTEXT("ReadyStatus", "Input options changed. Run Analyze to inspect the current plan.");
	}
	else
	{
		PanelStatus = EPanelStatus::Idle;
		StatusDetail = SourcePath.IsEmpty()
			? LOCTEXT("IdleStatusAfterChange", "Select a Datasmith-supported source file to begin.")
			: LOCTEXT("InvalidStatus", "Correct the source input before running an operation.");
	}
}

FConVerseOptimizedImportOptions SConVerseDatasmithImportPanel::MakeOptions() const
{
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = SourcePath;
	Options.DestinationPath = DestinationPath;
	Options.InstanceType = InstanceType;
	Options.MinimumInstanceCount = MinimumInstanceCount;
	Options.Tessellation = Tessellation;
	return Options;
}

TOptional<float> SConVerseDatasmithImportPanel::GetChordTolerance() const
{
	return Tessellation.ChordTolerance;
}

TOptional<float> SConVerseDatasmithImportPanel::GetMaxEdgeLength() const
{
	return Tessellation.MaxEdgeLength;
}

TOptional<float> SConVerseDatasmithImportPanel::GetNormalTolerance() const
{
	return Tessellation.NormalTolerance;
}

void SConVerseDatasmithImportPanel::HandleChordToleranceCommitted(float NewValue, ETextCommit::Type CommitType)
{
	// The service clamps authoritatively; clamp here too so the box never displays a value
	// that differs from what the next run would actually use.
	Tessellation.ChordTolerance = FMath::Max(NewValue, 0.005f);
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleMaxEdgeLengthCommitted(float NewValue, ETextCommit::Type CommitType)
{
	Tessellation.MaxEdgeLength = NewValue <= 0.0f ? 0.0f : FMath::Max(NewValue, 1.0f);
	MarkInputsChanged();
}

void SConVerseDatasmithImportPanel::HandleNormalToleranceCommitted(float NewValue, ETextCommit::Type CommitType)
{
	Tessellation.NormalTolerance = FMath::Clamp(NewValue, 5.0f, 90.0f);
	MarkInputsChanged();
}

FText SConVerseDatasmithImportPanel::GetStitchingText() const
{
	switch (Tessellation.StitchingTechnique)
	{
	case EConVerseStitchingTechnique::Sewing:  return LOCTEXT("StitchingSewing", "Sewing");
	case EConVerseStitchingTechnique::Healing: return LOCTEXT("StitchingHealing", "Healing");
	default:                                   return LOCTEXT("StitchingNone", "None");
	}
}

TSharedRef<SWidget> SConVerseDatasmithImportPanel::BuildStitchingMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	auto AddEntry = [this, &MenuBuilder](EConVerseStitchingTechnique Technique, const FText& Label, const FText& ToolTip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Technique]()
				{
					Tessellation.StitchingTechnique = Technique;
					MarkInputsChanged();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, Technique]()
				{
					return Tessellation.StitchingTechnique == Technique;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	AddEntry(
		EConVerseStitchingTechnique::None,
		LOCTEXT("StitchingNone", "None"),
		LOCTEXT("StitchingNoneTooltip", "No stitching applied."));
	AddEntry(
		EConVerseStitchingTechnique::Sewing,
		LOCTEXT("StitchingSewing", "Sewing"),
		LOCTEXT("StitchingSewingTooltip", "Connect surfaces sharing a boundary across objects. Can add or remove objects, which may change instance grouping."));
	AddEntry(
		EConVerseStitchingTechnique::Healing,
		LOCTEXT("StitchingHealing", "Healing"),
		LOCTEXT("StitchingHealingTooltip", "Connect surfaces sharing a boundary within a single object."));

	return MenuBuilder.MakeWidget();
}

void SConVerseDatasmithImportPanel::ResetTessellationToDefaults()
{
	Tessellation = FConVerseTessellationSettings();
	MarkInputsChanged();
}

FText SConVerseDatasmithImportPanel::GetTessellationSummaryText() const
{
	// Report only what has been observed. Before a run there is no translator to ask, so the
	// panel must not claim these settings will or will not take effect.
	if (!bHasRunSinceInputChange)
	{
		return LOCTEXT(
			"TessellationUnknown",
			"Applies to formats tessellated at import, such as CAD, Revit, and IFC. Formats that are already tessellated, including .udatasmith, ignore these values. Run Analyze to see which applies to the selected source.");
	}
	return bLastRunAppliedTessellation
		? LOCTEXT("TessellationApplied", "The translator for this source accepted these tessellation settings.")
		: LOCTEXT("TessellationIgnored", "The translator for this source exposes no tessellation options, so these values were ignored.");
}

void SConVerseDatasmithImportPanel::SetResult(const FConVerseOptimizedImportResult& Result, bool bWasImport)
{
	ReportText = FText::FromString(Result.Report);
	RefreshActiveImportProbe();

	// Only a run that actually loaded the source can tell us whether the translator takes
	// tessellation options. A failure before load leaves the previous answer untouched.
	if (Result.bSourceLoaded)
	{
		bLastRunAppliedTessellation = Result.bTessellationApplied;
		bHasRunSinceInputChange = true;
	}

	if (!bWasImport)
	{
		if (Result.bSourceLoaded)
		{
			PanelStatus = EPanelStatus::Analyzed;
			StatusDetail = FText::Format(
				LOCTEXT("AnalysisComplete", "Analysis complete: {0} groups and {1} instances planned."),
				FText::AsNumber(Result.PlannedGroupCount),
				FText::AsNumber(Result.PlannedInstanceCount));
		}
		else
		{
			PanelStatus = EPanelStatus::Failed;
			StatusDetail = LOCTEXT("AnalysisFailed", "Analysis failed. See the report and Output Log for details.");
		}
		return;
	}

	if (Result.Status == EConVerseOptimizedImportStatus::AlreadyCurrent)
	{
		// Nothing was imported because the active manifest already matches this source,
		// but the committed output was re-verified and may have drifted.
		if (Result.bVerificationSucceeded)
		{
			if (Result.bSidecarChanged)
			{
				// The primary file is unchanged but the sidecar assets are not, so the committed
				// geometry may be stale. Reported rather than acted on: superseding is destructive,
				// so rebuilding is the user's call. This must not read as a clean pass.
				PanelStatus = EPanelStatus::ImportedWithFailures;
				StatusDetail = FText::Format(
					LOCTEXT("AlreadyCurrentSidecarChanged",
						"The primary source file is unchanged, but its sidecar assets changed ({0} files now, {1} recorded). "
						"The committed geometry may be out of date. Force a reimport to pick up the new assets. The world was not modified."),
					FText::AsNumber(Result.SidecarFileCount),
					FText::AsNumber(Result.SidecarFileCountAtCommit));
			}
			else
			{
				PanelStatus = EPanelStatus::Verified;
				StatusDetail = FText::Format(
					LOCTEXT("AlreadyCurrent", "The optimized output is already current: {0} groups and {1} instances re-verified. The world was not modified."),
					FText::AsNumber(Result.VerifiedGroupCount),
					FText::AsNumber(Result.VerifiedInstanceCount));
			}
		}
		else
		{
			PanelStatus = EPanelStatus::ImportedWithFailures;
			StatusDetail = LOCTEXT("AlreadyCurrentDrift",
				"The source is unchanged, but re-verification found drift in the committed output. See the report. The world was not modified.");
		}
		return;
	}

	if (Result.Status == EConVerseOptimizedImportStatus::CancelledRolledBack)
	{
		// Cancellation is a clean, expected outcome: the service rolled the attempt back.
		PanelStatus = EPanelStatus::Cancelled;
		StatusDetail = LOCTEXT("ImportCancelled",
			"The import was cancelled and all changes were rolled back. The world and content were left unchanged.");
		return;
	}

	if (Result.Status == EConVerseOptimizedImportStatus::OptimizedReimportBlocked)
	{
		// Nothing was mutated. The summary explains what owns the destination.
		PanelStatus = EPanelStatus::Blocked;
		StatusDetail = Result.Summary.IsEmpty()
			? LOCTEXT("ReimportBlocked", "The optimized reimport was blocked. Nothing was changed.")
			: FText::FromString(Result.Summary);
		return;
	}

	if (Result.bVerificationSucceeded)
	{
		PanelStatus = EPanelStatus::Verified;
		StatusDetail = Result.bWasReimport
			? FText::Format(
				LOCTEXT("ReimportComplete", "Optimized reimport verified: {0} of {1} optimized groups passed. {2} prior-session actors were removed."),
				FText::AsNumber(Result.VerifiedGroupCount),
				FText::AsNumber(Result.PlannedGroupCount),
				FText::AsNumber(Result.RemovedPreviousActorCount))
			: FText::Format(
				LOCTEXT("VerificationComplete", "Import verified: {0} of {1} optimized groups passed."),
				FText::AsNumber(Result.VerifiedGroupCount),
				FText::AsNumber(Result.PlannedGroupCount));
	}
	else if (Result.bImportSucceeded)
	{
		PanelStatus = EPanelStatus::ImportedWithFailures;
		StatusDetail = FText::Format(
			LOCTEXT("VerificationFailed", "Import completed with verification failures: {0} of {1} optimized groups passed."),
			FText::AsNumber(Result.VerifiedGroupCount),
			FText::AsNumber(Result.PlannedGroupCount));
	}
	else
	{
		PanelStatus = EPanelStatus::Failed;
		StatusDetail = LOCTEXT("ImportFailed", "Import failed or was cancelled. See the report and Output Log for details.");
	}
}

#undef LOCTEXT_NAMESPACE
