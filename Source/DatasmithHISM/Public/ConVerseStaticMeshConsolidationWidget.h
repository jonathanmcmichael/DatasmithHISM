#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"

#include "ConVerseStaticMeshConsolidationLibrary.h"
#include "ConVerseStaticMeshConsolidationWidget.generated.h"

UCLASS(Blueprintable, Meta = (DisplayName = "Datasmith HISM Static Mesh Consolidation Widget"))
class DATASMITHHISM_API UConVerseStaticMeshConsolidationWidget : public UEditorUtilityWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|Static Meshes")
	FConVerseStaticMeshConsolidationResult ConsolidateSelection();

	UFUNCTION(BlueprintPure, Category = "DatasmithHISM|Static Meshes")
	bool HasSelection() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DatasmithHISM|Static Meshes")
	bool bRequireMatchingMaterials = true;

	UPROPERTY(BlueprintReadOnly, Category = "DatasmithHISM|Static Meshes")
	FConVerseStaticMeshConsolidationResult LastResult;
};
