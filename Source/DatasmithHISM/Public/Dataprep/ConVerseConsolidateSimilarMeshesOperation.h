#pragma once

#include "CoreMinimal.h"
#include "DataprepOperation.h"

#include "ConVerseConsolidateSimilarMeshesOperation.generated.h"

UCLASS(Category = MeshOperation, Meta = (DisplayName = "Consolidate Similar Static Meshes", ToolTip = "Groups duplicate static mesh assets by mesh data, repoints matching actors to one canonical mesh asset, and deletes the duplicate mesh assets from the Dataprep working set."))
class DATASMITHHISM_API UConVerseConsolidateSimilarMeshesOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:
	virtual FText GetCategory_Implementation() const override;
	virtual FText GetAdditionalKeyword_Implementation() const override;

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;

public:
	UPROPERTY(EditAnywhere, Category = "Mesh Consolidation")
	bool bRequireMatchingMaterials = true;
};
