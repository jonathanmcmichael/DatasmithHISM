#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVerseHISMLibrary.generated.h"

UENUM(BlueprintType)
enum class EConVerseGroupingMode : uint8
{
	// Group within Revit/IFC family type boundaries. Actors from different families are
	// never collapsed together even if their geometry is identical. Preserves BIM structure.
	PreserveBIMHierarchy UMETA(DisplayName = "Preserve BIM Hierarchy"),

	// Group purely by render equivalence. Any actors under the same cleanup boundary with
	// identical geometry and materials collapse into one ISM, regardless of family identity.
	// Produces the smallest possible actor/component count.
	MaximumOptimization UMETA(DisplayName = "Maximum Optimization"),
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseHISMCreationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsConsidered = 0;

	// Total ISM/HISM components created (ISMOnlyComponentsCreated + HISMOnlyComponentsCreated).
	// Kept for backward compat — prefer the split fields for ISM vs HISM breakdowns.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ISMComponentsCreated = 0;

	// Components created as UInstancedStaticMeshComponent (non-Nanite-hierarchical ISM).
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ISMOnlyComponentsCreated = 0;

	// Components created as UHierarchicalInstancedStaticMeshComponent.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 HISMOnlyComponentsCreated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 SourceActorsConverted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 SkippedActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWithNoEligibleStaticMesh = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWithMultipleEligibleStaticMeshes = 0;

	// Actors skipped because they carry behavior beyond their mesh (Blueprint-added components,
	// audio/light/particle/child-actor components, or asset user data). Converting deletes the
	// source actor, so these are left intact rather than silently losing that payload.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWithBehaviorPayload = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsInSingleActorGroups = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedISMComponentCreations = 0;

	// Source actors whose AddInstance call failed. These are deliberately NOT deleted,
	// so the original geometry survives instead of disappearing with no instance to replace it.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedInstanceAdditions = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedSourceActorDeletes = 0;

	// True if the user cancelled the operation via the progress dialog.
	// A partial result may still have been committed; use Ctrl+Z to undo.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	bool bWasCancelled = false;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	FString Summary;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseHISMAnalysisResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsConsidered = 0;

	// Groups with enough instances to produce an ISM (≥ MinInstanceCount).
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 GroupsAboveThreshold = 0;

	// Groups below the minimum instance threshold — would be left as individual actors.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 GroupsBelowThreshold = 0;

	// Actors that would be folded into ISM components.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWouldBeConverted = 0;

	// Actors in groups below the threshold — would stay as individual actors.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsInSmallGroups = 0;

	// Actors skipped (zero or multiple eligible mesh components).
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 SkippedActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	FString Summary;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseISMExplodeResult
{
	GENERATED_BODY()

	// Number of managed ISM components that were processed.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ISMComponentsExploded = 0;

	// Number of static mesh actor instances spawned in place of ISM instances.
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 InstancesSpawned = 0;

	// Instances that could not be spawned (e.g., invalid transform or mesh).
	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedSpawns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	FString Summary;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseEnableNaniteResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 MeshesConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 MeshesAlreadyEnabled = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 MeshesEnabled = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	FString Summary;
};

UCLASS()
class DATASMITHHISM_API UConVerseHISMLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Canonical function. All new callers should use this.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Create ISMs From Selection", ToolTip = "Groups selected Datasmith geometry by shared static mesh shape and materials. Geometrically identical meshes (including separately imported assets with duplicate geometry) collapse into one ISM component per variant. Single-mesh actors group into a managed actor named after the mesh; hosted multi-part families group under a managed actor named after their wrapper family. Converted source actors are removed when their child hierarchy is fully converted."))
	static FConVerseHISMCreationResult CreateISMsFromSelection(
		const FString& NewActorLabelPrefix = TEXT("ISM"),
		bool bUseHISM = false,
		int32 MinInstanceCount = 2,
		bool bAutoDetectFromNanite = false,
		EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy);

	// Dry-run counterpart to CreateISMsFromSelection. Runs Phase 1 grouping only — no actors
	// are created, modified, or deleted. Returns a summary of what would happen.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Analyze ISM Candidates In Selection", ToolTip = "Runs the grouping phase without making any changes. Returns a summary of how many ISM groups would be created and how many actors would be converted."))
	static FConVerseHISMAnalysisResult AnalyzeISMCandidatesInSelection(
		const FString& NewActorLabelPrefix = TEXT("ISM"),
		bool bUseHISM = false,
		int32 MinInstanceCount = 2,
		bool bAutoDetectFromNanite = false,
		EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy);

	// Enables Nanite on all static mesh assets referenced by the current editor selection.
	// Modifies mesh assets in-place; triggers async rebuilds. Does not create or remove actors.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Enable Nanite On Selection", ToolTip = "Enables Nanite on all static mesh assets referenced by the current selection. Mesh rebuilds are queued asynchronously."))
	static FConVerseEnableNaniteResult EnableNaniteOnSelection();

	// Reverse of CreateISMsFromSelection. Finds all ConVerseManagedHISM-tagged components
	// in the selection and its descendant cleanup boundaries. Spawns one static mesh actor
	// per ISM instance at the stored world transform, then destroys the ISM components and
	// any now-empty managed family-type actors. Wrapped in a single FScopedTransaction.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Explode ISMs From Selection", ToolTip = "Reverse of Managed ISMs. Finds all ConVerseManagedHISM components in the selection and spawns one static mesh actor per instance. Destroys the ISM components and empty family-type actors. Wrapped in a single undo transaction."))
	static FConVerseISMExplodeResult ExplodeISMsFromSelection();

	// Replaces OldTagName with NewTagName on every actor and component in the current level.
	// Useful for migrating levels created by an earlier version of the plugin that used
	// different tag strings. Returns the number of tag instances replaced.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Migrate Tags In Current Level", ToolTip = "Replaces OldTagName with NewTagName on every actor and component in the current level. Use to migrate levels created by an earlier plugin version that used different managed-ISM tag strings."))
	static int32 MigrateTagsInCurrentLevel(const FString& OldTagName, const FString& NewTagName);

	// Deprecated. Use CreateISMsFromSelection.
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Create ISMs From Selection (Deprecated)", DeprecatedFunction, DeprecationMessage = "Use CreateISMsFromSelection instead."))
	static FConVerseHISMCreationResult CreateHISMsFromSelection(const FString& NewActorLabelPrefix = TEXT("ISM"), bool bUseHISM = false);
};
