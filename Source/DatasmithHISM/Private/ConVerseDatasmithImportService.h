#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

enum class EConVerseOptimizedInstanceType : uint8
{
	ISM,
	HISM
};

enum class EConVerseOptimizedImportStatus : uint8
{
	InvalidOptions,
	SourceLoadFailed,
	SourceChangedAfterAnalysis,
	AnalysisSucceeded,
	AnalysisNoEligibleGroups,
	AlreadyCurrent,
	Verified,
	ImportedWithFailuresRolledBack,
	CancelledRolledBack,
	FailedRolledBack,
	OptimizedReimportBlocked,
	RollbackFailed
};

enum class EConVerseOptimizedImportStage : uint8
{
	None,
	OptionsValidated,
	SourceHashed,
	SourceLoaded,
	PlanBuilt,
	MutationPreflight,
	SceneTransformed,
	DatasmithImported,
	ComponentsConverted,
	Verified,
	RolledBack
};

enum class EConVerseOptimizedSkipReason : uint8
{
	HasChildren,
	MissingMeshReference,
	UnresolvedMeshReference,
	UnresolvedMaterialBinding,
	NonFiniteTransform,
	UnsupportedNegativeScale,
	BelowMinimumInstanceCount
};

enum class EConVerseStitchingTechnique : uint8
{
	None,
	Sewing,
	Healing
};

/**
 * Tessellation controls forwarded to CAD-style Datasmith translators (CAD, Revit, IFC).
 *
 * These mirror FDatasmithTessellationOptions in DatasmithImportOptions.h, including its clamp
 * ranges. Defaults intentionally match that struct's own constructor defaults so an import that
 * never touches these controls behaves exactly as it did before they existed.
 *
 * Formats whose translator exposes no tessellation options (notably .udatasmith, which is already
 * tessellated at export time) ignore these values.
 */
struct FConVerseTessellationSettings
{
	// Max distance between a generated triangle and the original surface. Lower means more triangles.
	float ChordTolerance = 0.2f;
	// Max length of any generated edge. 0 disables the constraint; otherwise the minimum is 1.0.
	float MaxEdgeLength = 0.0f;
	// Max angle in degrees between adjacent triangles. Lower means more triangles.
	float NormalTolerance = 20.0f;
	EConVerseStitchingTechnique StitchingTechnique = EConVerseStitchingTechnique::Sewing;

	bool operator==(const FConVerseTessellationSettings& Other) const
	{
		return FMath::IsNearlyEqual(ChordTolerance, Other.ChordTolerance)
			&& FMath::IsNearlyEqual(MaxEdgeLength, Other.MaxEdgeLength)
			&& FMath::IsNearlyEqual(NormalTolerance, Other.NormalTolerance)
			&& StitchingTechnique == Other.StitchingTechnique;
	}
	bool operator!=(const FConVerseTessellationSettings& Other) const { return !(*this == Other); }
};

struct FConVerseOptimizedImportOptions
{
	FString FilePath;
	FString DestinationPath = TEXT("/Game/DatasmithOptimized");
	EConVerseOptimizedInstanceType InstanceType = EConVerseOptimizedInstanceType::ISM;
	int32 MinimumInstanceCount = 2;
	// Test and commandlet seam. The editor panel leaves this false so Datasmith import options remain interactive.
	bool bAutomated = false;
	FConVerseTessellationSettings Tessellation;
};

struct FConVerseOptimizedSkipCount
{
	EConVerseOptimizedSkipReason Reason = EConVerseOptimizedSkipReason::HasChildren;
	int32 Count = 0;
};

struct FConVerseOptimizedGroupVerification
{
	FString GroupId;
	bool bPassed = false;
	FString ComponentPath;
	FString ComponentClass;
	int32 ExpectedInstances = 0;
	int32 ActualInstances = 0;
	FString Details;
};

struct FConVerseOptimizedImportResult
{
	EConVerseOptimizedImportStatus Status = EConVerseOptimizedImportStatus::InvalidOptions;
	EConVerseOptimizedImportStage LastCompletedStage = EConVerseOptimizedImportStage::None;

	FString PlanId;
	FString SessionId;
	FString SourceFileHash;

	/**
	 * Aggregate fingerprint of the source's "<BaseName>_Assets" sidecar folder.
	 *
	 * Deliberately NOT part of PlanId. Plan identity drives the AlreadyCurrent short-circuit, so
	 * folding the sidecar into it would make a sidecar-only change look like a different plan and
	 * trigger an automatic destructive reimport. The chosen behavior is to warn instead, so this
	 * is compared separately and reported.
	 */
	FString SidecarHash;
	int64 SidecarTotalSize = 0;
	int32 SidecarFileCount = 0;
	bool bSidecarExists = false;
	/** True when a committed manifest recorded a different sidecar fingerprint than the current one. */
	bool bSidecarChanged = false;
	/** File count the active manifest recorded, for reporting alongside the current count. */
	int32 SidecarFileCountAtCommit = 0;
	int32 PlannedGroupCount = 0;
	int32 PlannedInstanceCount = 0;
	int32 VerifiedGroupCount = 0;
	int32 VerifiedInstanceCount = 0;
	int32 TotalSourceMeshActors = 0;
	int32 EligibleSourceActors = 0;
	int32 BelowThresholdActorCount = 0;
	int32 SkippedActorCount = 0;
	TArray<FConVerseOptimizedSkipCount> SkipCounts;
	TArray<FConVerseOptimizedGroupVerification> GroupVerification;

	FSoftObjectPath ImportAssetPath;
	FSoftObjectPath ManifestAssetPath;

	// Optimizer-aware reimport. Set when an active manifest already owned this source and destination.
	bool bWasReimport = false;
	FString PreviousManifestId;
	FString PreviousSessionId;
	int32 RemovedPreviousActorCount = 0;

	bool bRollbackAttempted = false;
	bool bRollbackSucceeded = false;
	int32 CreatedObjectCount = 0;
	int32 RemainingObjectCount = 0;
	// True when the resolved translator exposed tessellation options and they were applied.
	// False for already-tessellated formats such as .udatasmith, where the controls do not apply.
	bool bTessellationApplied = false;
	FString Summary;
	FString Report;
	FString SavedReportPath;

	// Retained while the Slate panel migrates to Status as the authoritative state.
	bool bSourceLoaded = false;
	bool bImportSucceeded = false;
	bool bVerificationSucceeded = false;
};

class FConVerseDatasmithImportService
{
public:
	static FConVerseOptimizedImportResult Analyze(const FConVerseOptimizedImportOptions& Options);
	static FConVerseOptimizedImportResult ImportAndVerify(const FConVerseOptimizedImportOptions& Options);

	/**
	 * True when an active optimized-import manifest already owns the normalized source and destination,
	 * meaning the next ImportAndVerify call runs as an optimized reimport. Intended for UI affordances;
	 * the authoritative decision is made inside ImportAndVerify.
	 */
	static bool HasActiveOptimizedImport(const FConVerseOptimizedImportOptions& Options);
};
