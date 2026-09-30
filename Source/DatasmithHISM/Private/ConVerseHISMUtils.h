#pragma once

#include "CoreMinimal.h"

#include "ConVerseHISMLibrary.h"

class AActor;
class UStaticMesh;

namespace ConVerseHISM
{
	struct FBuildOutput
	{
		FConVerseHISMCreationResult Result;
		TArray<UObject*> ObjectsToDelete;
	};

	void CollectActorsFromRoots(const TArray<AActor*>& RootActors, TArray<AActor*>& OutActors);
	bool EnableNaniteIfNeeded(UStaticMesh* Mesh);
	int32 DeleteManagedActorOutputs(const TArray<UObject*>& ObjectsToDelete);
	FBuildOutput BuildManagedHISMs(
		const TArray<AActor*>& Actors,
		const FString& ComponentNamePrefix,
		bool bUseHISM = false,
		int32 MinInstanceCount = 2,
		bool bAutoDetectFromNanite = false,
		EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy,
		const TArray<FString>& StoreyBoundaryPatterns = TArray<FString>(),
		// Interactive library callers retain cancellable slow-task dialogs. Dataprep and
		// other unattended callers must pass false.
		bool bShowProgressDialog = true);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetProgressDialogRequestCountForTesting();
	int32 GetProgressDialogRequestCountForTesting();
	void SetManagedActorDeleteObstructionForTesting(AActor* Actor);
	void ResetManagedActorDeleteResultForTesting();
	int32 GetManagedActorDeleteRequestedCountForTesting();
	int32 GetManagedActorDeleteSucceededCountForTesting();
#endif

	// Dry-run equivalent of BuildManagedHISMs Phase 1. Runs grouping logic without creating,
	// modifying, or deleting any actors or components. Safe to call at any time.
	FConVerseHISMAnalysisResult AnalyzeManagedHISMCandidates(
		const TArray<AActor*>& Actors,
		bool bUseHISM = false,
		int32 MinInstanceCount = 2,
		bool bAutoDetectFromNanite = false,
		EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy,
		const TArray<FString>& StoreyBoundaryPatterns = TArray<FString>());

	void FinalizeSummary(FConVerseHISMCreationResult& Result);
}
