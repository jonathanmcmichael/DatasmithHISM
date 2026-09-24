#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVerseStaticMeshConsolidationLibrary.generated.h"

UENUM(BlueprintType)
enum class EConVerseStaticMeshConsolidationOutcome : uint8
{
	Completed,
	Cancelled,
	Failed,
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseStaticMeshConsolidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	EConVerseStaticMeshConsolidationOutcome Outcome = EConVerseStaticMeshConsolidationOutcome::Completed;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	int32 MeshesConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	int32 DuplicateGroupsFound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	int32 MeshesConsolidated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	int32 FailedConsolidations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	int32 SkippedMeshes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	FString SkippedMeshReport;

	UPROPERTY(BlueprintReadOnly, Category = "Mesh Consolidation")
	FString Summary;
};

UCLASS()
class DATASMITHHISM_API UConVerseStaticMeshConsolidationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|Static Meshes", meta = (DisplayName = "Consolidate Similar Static Meshes In Selection", ToolTip = "Finds duplicate static mesh assets referenced by the current editor selection and consolidates each duplicate set down to one canonical mesh asset. When bDryRun is true, reports what would happen without making any changes."))
	static FConVerseStaticMeshConsolidationResult ConsolidateSimilarStaticMeshesInSelection(bool bRequireMatchingMaterials = true, bool bDryRun = false);

	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|Static Meshes", meta = (DisplayName = "Consolidate Similar Static Meshes", ToolTip = "Finds duplicate static mesh assets referenced by the supplied actors, components, or static mesh assets and consolidates each duplicate set down to one canonical mesh asset. When bDryRun is true, reports what would happen without making any changes."))
	static FConVerseStaticMeshConsolidationResult ConsolidateSimilarStaticMeshes(const TArray<UObject*>& Objects, bool bRequireMatchingMaterials = true, bool bDryRun = false);
};
