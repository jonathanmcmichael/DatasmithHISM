#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ConVerseSourceMetadata.generated.h"

USTRUCT(BlueprintType)
struct DATASMITHHISMRUNTIME_API FConVerseSourceRecord
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	FName ComponentName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	int32 InstanceIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	FString SourceElement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	FString SourceLabel;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	FString SourceMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	FString SourceDocument;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	TMap<FString, FString> Metadata;
};

/** Cookable source identity. Import ownership and editor recovery remain in the editor module. */
UCLASS(BlueprintType, ClassGroup=(ConVerse))
class DATASMITHHISMRUNTIME_API UConVerseSourceMetadata : public UActorComponent
{
	GENERATED_BODY()
public:
	UConVerseSourceMetadata();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
	TArray<FConVerseSourceRecord> Records;
	/** For an ordinary mesh use INDEX_NONE; for an instance use the hit-result Item index. */
	UFUNCTION(BlueprintPure, Category="ConVerse|Source")
	static bool FindSource(const UActorComponent* MeshComponent, int32 InstanceIndex, FConVerseSourceRecord& OutRecord);
};
