#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Views/SListView.h"

#include "ConVerseDatasmithImportService.h"

class SEditableTextBox;
class SWindow;
class UInstancedStaticMeshComponent;

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
	virtual ~SConVerseDatasmithImportPanel() override;

private:
	friend class FConVersePanelInputStateTest;
	friend class FConVersePanelSessionRestoreTest;
	friend class FConVersePanelMissingTexturesTest;
	friend class FConVersePanelSaveUpdatesReportTest;
	friend class FConVerseFocusClearsPriorSelectionTest;
	friend class FConVersePanelTraceOptionTest;
	friend class FConVersePanelApplyNaniteTest;
	void ApplyInputOptions(const FConVerseOptimizedImportOptions& Options);
	/** Asks whether to proceed without the result's missing textures; on Yes, records them as accepted. */
	bool ConfirmMissingTextures(const FConVerseOptimizedImportResult& Result);
	void SetTextureSearchFolders(const TArray<FString>& Folders);
	void ApplySelectedSource(const FString& SelectedFile);
	bool LoadPresetFromFile(const FString& FilePath);
	FReply HandleReviewMaterials();
	void CloseMaterialReview();
	FReply HandleBrowseForSource();
	FReply HandleAnalyze();
	FReply HandleImportAndVerify();
	FReply HandleRebuild();
	FReply HandleSaveResult();
	FReply HandleApplyNanite();
	FReply HandleAnalyzeNanite();
	TSharedRef<SWidget> BuildNaniteScopeMenu();
	FText GetNaniteScopeText() const;
	FReply HandleSavePreset();
	FReply HandleLoadPreset();
	void RefreshInspection();
	TSharedRef<ITableRow> MakeInspectionRow(TSharedPtr<FConVerseImportInspectionRow> Row, const TSharedRef<STableViewBase>& Owner);
	FReply FocusInspection();
	FReply OpenInspectionMesh();
	FReply SetInspectionException(bool bDisableNanite);

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

	TArray<FConVerseImportInspectionRow> InspectionRows;
	TArray<FConVerseAppearanceReviewRow> Appearances;
	TArray<TSharedPtr<FConVerseImportInspectionRow>> FilteredRows;
	TSharedPtr<SListView<TSharedPtr<FConVerseImportInspectionRow>>> InspectionList;
	FString InspectionSearch;
	TStrongObjectPtr<UConVerseImportRecipe> ProcessingRecipe;
	TWeakPtr<SWindow> MaterialReviewWindow;
	/**
	 * The ISM/HISM component whose per-instance SelectInstance bit was last set by FocusInspection, if
	 * any. SelectInstance bits are not cleared by GEditor->SelectNone, so an earlier focus otherwise
	 * stays highlighted on its own component after a later focus moves to a different row.
	 */
	TWeakObjectPtr<UInstancedStaticMeshComponent> LastFocusedInstanceComponent;
	void ClearFocusedInstanceSelection();
	bool bApplyingInputs = false;
	bool bForceRebuildNext = false;
	bool bProfileNextImport = false;
	/** Apply Nanite step settings (Amendment 16). Kept apart from the import settings so changing them never invalidates the import. */
	EConVerseNanitePolicy ApplyNanitePolicy = EConVerseNanitePolicy::AllSupportedMeshes;
	int32 ApplyNaniteBudget = 16384;
	/** Nanite analysis (read-only). The recommendation is cleared whenever the import it describes changes. */
	int32 NaniteCoveragePercent = 95;
	int32 NaniteMinTriangles = 1000;
	TArray<FString> NaniteRecommended;
	bool bApplyRecommendedOnly = false;
	FSoftObjectPath LastManifestPath;
	TSharedPtr<SEditableTextBox> SourcePathTextBox;
	TSharedPtr<SEditableTextBox> DestinationPathTextBox;
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
	/** Missing textures the user chose to proceed without for the current source. Cleared when the source changes. */
	TArray<FString> AcceptedMissingTextures;
	/** Yes/No prompt seam; automation replaces the modal dialog. */
	TFunction<bool(const FText&)> AskYesNo;
	EPanelStatus PanelStatus = EPanelStatus::Idle;
	FText StatusDetail;
	FText ReportText;
	/**
	 * Outcome of the most recent explicit "Save imported result", shown ahead of ReportText so the
	 * summary box does not keep displaying pre-save text. Cleared whenever ReportText is replaced by
	 * a fresh operation result or invalidated, so it cannot outlive the report it was about.
	 */
	FText SaveOutcomeText;
};
