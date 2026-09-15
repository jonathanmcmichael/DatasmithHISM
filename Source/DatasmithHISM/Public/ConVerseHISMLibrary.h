#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVerseHISMLibrary.generated.h"

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseHISMCreationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 ActorsConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 HISMActorsCreated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 SourceActorsConverted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 SkippedActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 ActorsWithNoEligibleStaticMesh = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 ActorsWithMultipleEligibleStaticMeshes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 ActorsInSingleActorGroups = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 FailedHISMActorCreations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	int32 FailedSourceActorDeletes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HISM")
	FString Summary;
};

UCLASS()
class DATASMITHHISM_API UConVerseHISMLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|HISM", meta = (DisplayName = "Create HISMs From Selection", ToolTip = "Groups selected Datasmith geometry by shared static mesh and materials. Single-mesh actors collapse into one managed actor named after the mesh, while hosted multi-part families stay together under one managed actor named after their wrapper family. Converted source actors are removed when their child hierarchy is fully converted."))
	static FConVerseHISMCreationResult CreateHISMsFromSelection(const FString& NewActorLabelPrefix = TEXT("HISM"));
};
