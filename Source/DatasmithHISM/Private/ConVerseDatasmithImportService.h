#pragma once

#include "CoreMinimal.h"
#include "ConVerseImportRecipe.h"
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
	// Verification failed and the caller asked to decide rather than roll back immediately. The
	// attempt is still live and owns assets and actors; it MUST be resolved by
	// AcceptFailedVerification or DiscardFailedVerification. Not a terminal state.
	AwaitingFailedVerificationDecision,
	// Verification failed and the user accepted the result anyway. Committed but quarantined: the
	// manifest is marked degraded and optimized reimport is refused against it.
	AcceptedWithFailedVerification,
	CancelledRolledBack,
	FailedRolledBack,
	OptimizedReimportBlocked,
	ManualEditsDetected,
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

/**
 * Test-only seam for forcing the failure paths that protect user data.
 *
 * Rollback only runs when something goes wrong, so without a way to induce a failure it is
 * untestable destructive code. Each value aborts the import at a checkpoint that already routes
 * through RollBackAttempt, so the real unwind runs against real imported assets and actors.
 *
 * Deliberately NOT part of ComputePlanId. Plan identity describes committed output, and every
 * non-None value aborts before any manifest is committed, so no committed output can ever exist
 * whose identity depended on this value. Adding it to the hash would be meaningless noise.
 *
 * The editor panel never sets this. It is reachable only from automation.
 */
enum class EConVerseOptimizedImportFailureInjection : uint8
{
	// Normal operation.
	None,
	// Fail immediately after the Datasmith import succeeds, while the attempt owns assets and actors.
	// Exercises a successful rollback: expect ImportedWithFailuresRolledBack.
	AfterDatasmithImport,
	// Fail at manifest commit, after verification has already passed.
	// Exercises rollback from the latest point that still rolls back.
	BeforeManifestCommit,
	// Fail after the Datasmith import AND prevent the unwind from destroying the actors it created,
	// so RollBackAttempt genuinely finds remaining objects and honestly reports failure.
	// Exercises the RollbackFailed degradation. Leaves partial state behind by design.
	ObstructRollback,
	// Remove one instance from one converted component just before verification runs.
	// Nothing is faked: VerifySession compares against the immutable plan and fails on its own
	// terms with a real instance-count mismatch. Exercises the accept/discard decision path.
	CorruptBeforeVerification,
	// Corrupt real output, park it for an explicit decision, then fail before removing any actor
	// from the previous session. Exercises acceptance rollback without damaging the predecessor.
	CorruptBeforeVerificationAndFailPreviousSessionRemoval
};

/** Transient work notifications. These do not participate in output identity or persistence. */
enum class EConVerseImportWorkPhase : uint8
{
	SourceHash,
	SidecarHash,
	Translation,
	SourceActors,
	Materials,
	TextureHash,
	TextureSearch,
	Dependencies,
	GroupPlanning,
	Report
};

/** Autodesk's shared material texture library tiers in the order Revit exports them: 256, 512, then 1024 px. */
TArray<FString> ConVerseDefaultTextureSearchFolders();

struct FConVerseOptimizedImportOptions
{
	FString FilePath;
	FString DestinationPath = TEXT("/Game/DatasmithOptimized");
	EConVerseOptimizedInstanceType InstanceType = EConVerseOptimizedInstanceType::ISM;
	int32 MinimumInstanceCount = 2;
	// Test and commandlet seam. The editor panel leaves this false so Datasmith import options remain interactive.
	bool bAutomated = false;
	FConVerseTessellationSettings Tessellation;
	FConVerseImportProcessingSettings Processing;
	bool bRebuildFromSource = false;
	bool bReplaceManualEdits = false;
	/**
	 * Missing texture files the user was shown and chose to proceed without, as reported in
	 * FConVerseOptimizedImportResult::MissingTextures. Work continues only when every missing texture
	 * is listed here, so a newly missing file is asked about again. Missing mesh files always fail.
	 * bAllowMissingTextures is the explicit headless equivalent (commandlet -AllowMissingTextures).
	 *
	 * Deliberately not part of ComputePlanId: the import reads the same files either way, and a
	 * texture restored later is reported through the sidecar fingerprint, not a silent rebuild.
	 */
	TArray<FString> AcceptedMissingTextures;
	bool bAllowMissingTextures = false;
	/**
	 * Folders searched, in order, by file name for textures missing beside the source. A match
	 * repoints only the in-memory scene element; source files are never copied or changed. The
	 * folder list is kept out of ComputePlanId; the resolutions it produces participate instead.
	 */
	TArray<FString> TextureSearchFolders = ConVerseDefaultTextureSearchFolders();
	// Cooperative cancellation seam, also used by unattended regression tests.
	TFunction<bool()> CancelRequested;
	TFunction<void(EConVerseImportWorkPhase, int64, int64)> ProgressObserver;
	// Test-only seam. See EConVerseOptimizedImportFailureInjection. Never set outside automation.
	EConVerseOptimizedImportFailureInjection FailureInjection = EConVerseOptimizedImportFailureInjection::None;

	/**
	 * When verification fails, park the attempt instead of rolling it back immediately, so the
	 * caller can choose to accept the imperfect result.
	 *
	 * This does NOT weaken verification: it only changes who decides what happens next. The attempt
	 * is left live and owning assets and actors, and MUST be resolved by AcceptFailedVerification or
	 * DiscardFailedVerification. Leaving it unresolved leaks the attempt's objects into the level.
	 *
	 * Deliberately not part of ComputePlanId. It changes control flow, not generated output: an
	 * accepted session contains exactly the objects the attempt already built.
	 */
	bool bDeferRollbackOnVerificationFailure = false;
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

/**
 * Which entry point produced a result. Recorded in the import log so a failing analysis is
 * distinguishable from a failing import.
 *
 * ResolveFailedVerification marks the accept/discard decision. It shares a SessionId with the
 * ImportAndVerify row that parked the attempt, so without it the log would appear to contain two
 * rows for one attempt; with it, the pair reads as attempt-then-decision.
 */
enum class EConVerseOptimizedImportOperation : uint8
{
	Analyze,
	ImportAndVerify,
	ResolveFailedVerification
};

struct FConVerseOptimizedImportResult
{
	EConVerseOptimizedImportStatus Status = EConVerseOptimizedImportStatus::InvalidOptions;
	EConVerseOptimizedImportStage LastCompletedStage = EConVerseOptimizedImportStage::None;
	EConVerseOptimizedImportOperation Operation = EConVerseOptimizedImportOperation::Analyze;

	// Recorded for the import log. Normalized where normalization succeeded, raw input otherwise,
	// so an InvalidOptions row still shows what the user actually asked for.
	FString SourceFilePath;
	FString DestinationPath;

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
	TArray<FString> Diagnostics;
	/** Referenced texture/mesh files absent from disk, as "<element>: <path>". */
	TArray<FString> MissingTextures;
	TArray<FString> MissingMeshFiles;
	/** Textures found in TextureSearchFolders, as "<element>: <referenced path> -> <resolved path>". */
	TArray<FString> ResolvedTextures;
	/** Canonical resolution evidence used by PlanId; includes resolved content size and hash. */
	TArray<FString> ResolvedTextureIdentities;
	TArray<FConVerseTrackedObjectState> TrackedObjects;
	TArray<FConVerseImportInspectionRow> InspectionRows;
	TArray<FConVerseAppearanceReviewRow> Appearances;
	TMap<FString, FString> SourceLightDescriptions;
	TMap<FString, FString> MaterialDecisions;
	int32 SourceLightCount = 0;
	int32 SourceMeshAssetCount = 0;
	int32 EnabledLocalLights = 0;
	int32 UnitlessLights = 0;
	int32 IESLights = 0;
	int32 NaniteEnabledMeshes = 0;
	int32 NaniteSkippedMeshes = 0;
	int32 VerifiedOrdinaryMeshes = 0;
	FString ProcessingSettingsJson;
	FString MaterialMappingIdentity;
	FString AttemptFolder;
	double StartedAtSeconds = FPlatformTime::Seconds();
	double DurationSeconds = 0.0;
	double MeshBuildSeconds = 0.0;
	double MeshProcessingSeconds = 0.0;
	uint64 ProcessPeakPhysicalBytes = 0;

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

DECLARE_LOG_CATEGORY_EXTERN(LogConVerseOptimizedImport, Log, All);

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

	/**
	 * Absolute path of the append-only import log: one CSV row per attempt, successes and failures
	 * alike, in chronological order. The per-attempt JSON report named in each row holds the detail.
	 */
	static FString GetImportLogPath();
	static bool SaveImportedResult(const FSoftObjectPath& ManifestPath, FString& OutMessage, bool* OutVerified = nullptr);
	static bool SavePreset(const FString& FilePath, const FConVerseOptimizedImportOptions& Options, FString& OutError);
	static bool LoadPreset(const FString& FilePath, FConVerseOptimizedImportOptions& Options, FString& OutError);
	static TArray<FString> FindInterruptedAttempts();
	static bool WriteAttemptCheckpoint(const FConVerseOptimizedImportResult& Result, bool bTerminal);

	/**
	 * Resolves an attempt parked by bDeferRollbackOnVerificationFailure.
	 *
	 * Accept commits the session despite the failed checks and marks the manifest degraded, so the
	 * imperfect result is usable but can never be superseded by a later optimized reimport.
	 * Discard runs the rollback that would have happened automatically.
	 *
	 * Both take the SessionId from the AwaitingFailedVerificationDecision result. An unknown or
	 * already-resolved SessionId returns InvalidOptions rather than asserting, because the pending
	 * attempt does not survive an editor restart.
	 *
	 * Exactly one of these must be called for every parked attempt. Neither call leaves the attempt
	 * pending: a failed accept falls back to discarding, so no path leaks the attempt's objects.
	 */
	static FConVerseOptimizedImportResult AcceptFailedVerification(const FString& SessionId);
	static FConVerseOptimizedImportResult DiscardFailedVerification(const FString& SessionId);

	/** True when SessionId names an attempt still awaiting an accept/discard decision. */
	static bool HasPendingFailedVerification(const FString& SessionId);
};
