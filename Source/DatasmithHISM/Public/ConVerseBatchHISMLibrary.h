#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVerseBatchHISMLibrary.generated.h"

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseBatchHISMResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	int32 RootActorsSelected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	int32 ActorsConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	int32 StaticMeshComponentsConsidered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	int32 EligibleStaticMeshComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Batch HISM")
	FString Summary;
};

UCLASS()
class DATASMITHHISM_API UConVerseBatchHISMLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|Batch HISM", meta = (DisplayName = "Batch Selection To HISMs", ToolTip = "Runs Unreal's built-in Batch Actors instancing path on the current selection, forcing Hierarchical Instanced Static Mesh components and replacing the source actors."))
	static FConVerseBatchHISMResult BatchSelectionToHISMs(int32 InstanceReplacementThreshold = 2);
};
