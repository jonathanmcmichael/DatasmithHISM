#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVerseHISMLibrary.generated.h"

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseHISMCreationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ISMComponentsCreated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 SourceActorsConverted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 SkippedActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWithNoEligibleStaticMesh = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsWithMultipleEligibleStaticMeshes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 ActorsInSingleActorGroups = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedISMComponentCreations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	int32 FailedSourceActorDeletes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ISM")
	FString Summary;
};

UCLASS()
class DATASMITHHISM_API UConVerseHISMLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|ISM", meta = (DisplayName = "Create ISMs From Selection", ToolTip = "Groups selected Datasmith geometry by shared static mesh shape and materials. Geometrically identical meshes (including separately imported assets with duplicate geometry) collapse into one ISM component per variant. Single-mesh actors group into a managed actor named after the mesh; hosted multi-part families group under a managed actor named after their wrapper family. Converted source actors are removed when their child hierarchy is fully converted."))
	static FConVerseHISMCreationResult CreateHISMsFromSelection(const FString& NewActorLabelPrefix = TEXT("ISM"));
};
