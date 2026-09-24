#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "ConVerseDatasmithImportService.h"

class SEditableTextBox;

/**
 * Editor panel for analyzing and importing optimized Datasmith scenes.
 *
 * Tab registration is owned by the DatasmithHISM module. This widget only owns
 * user input, validation, operation state, and report presentation.
 */
class SConVerseDatasmithImportPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConVerseDatasmithImportPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply HandleBrowseForSource();
	FReply HandleAnalyze();
	FReply HandleImportAndVerify();

	void HandleSourcePathChanged(const FText& NewText);
	void HandleDestinationPathChanged(const FText& NewText);
	void HandleMinimumInstancesChanged(int32 NewValue);
	void HandleMinimumInstancesCommitted(int32 NewValue, ETextCommit::Type CommitType);
	void HandleISMCheckStateChanged(ECheckBoxState NewState);
	void HandleHISMCheckStateChanged(ECheckBoxState NewState);

	TOptional<float> GetChordTolerance() const;
	TOptional<float> GetMaxEdgeLength() const;
	TOptional<float> GetNormalTolerance() const;
	void HandleChordToleranceCommitted(float NewValue, ETextCommit::Type CommitType);
	void HandleMaxEdgeLengthCommitted(float NewValue, ETextCommit::Type CommitType);
	void HandleNormalToleranceCommitted(float NewValue, ETextCommit::Type CommitType);
	TSharedRef<SWidget> BuildStitchingMenu();
	FText GetStitchingText() const;
	FText GetTessellationSummaryText() const;
	void ResetTessellationToDefaults();

	ECheckBoxState GetISMCheckState() const;
	ECheckBoxState GetHISMCheckState() const;
	TOptional<int32> GetMinimumInstances() const;
	FText GetValidationText() const;
	FSlateColor GetValidationColor() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	FText GetReportText() const;
	FText GetImportButtonText() const;
	FText GetImportButtonToolTipText() const;

	bool CanAnalyze() const;
	bool CanImportAndVerify() const;
	bool IsInputEnabled() const;
	bool ValidateSource(FText& OutError) const;
	bool ValidateDestination(FText& OutError) const;
	void MarkInputsChanged();
	void RefreshActiveImportProbe();
	FConVerseOptimizedImportOptions MakeOptions() const;
	void SetResult(const FConVerseOptimizedImportResult& Result, bool bWasImport);

	enum class EPanelStatus : uint8
	{
		Idle,
		Ready,
		/**
		 * Set while a synchronous operation runs. The modal FScopedSlowTask dialog raised by the
		 * service owns the on-screen progress and Cancel button for that window, so this value is
		 * only observable if an operation leaves without reporting a result.
		 */
		Working,
		Cancelled,
		Blocked,
		Analyzed,
		Verified,
		ImportedWithFailures,
		Failed
	};

	TSharedPtr<SEditableTextBox> SourcePathTextBox;
	FString SourcePath;
	FString DestinationPath = TEXT("/Game/DatasmithOptimized");
	EConVerseOptimizedInstanceType InstanceType = EConVerseOptimizedInstanceType::ISM;
	int32 MinimumInstanceCount = 2;
	/**
	 * Forwarded to CAD-style translators. Ignored by already-tessellated formats such as
	 * .udatasmith, so the panel reports whether the last run actually applied them.
	 */
	FConVerseTessellationSettings Tessellation;
	bool bOperationInProgress = false;
	/** Cached probe result. Refreshed on input change and after each operation, never during paint. */
	bool bActiveImportExists = false;
	/** Whether the most recent run found a translator that accepts tessellation options. */
	bool bLastRunAppliedTessellation = false;
	bool bHasRunSinceInputChange = false;
	EPanelStatus PanelStatus = EPanelStatus::Idle;
	FText StatusDetail;
	FText ReportText;
};
