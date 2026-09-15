#pragma once

#include "CoreMinimal.h"
#include "DataprepOperation.h"

#include "ConVerseCreateHISMOperation.generated.h"

UCLASS(Category = ActorOperation, Meta = (DisplayName = "Create Hierarchical Instanced Static Meshes", ToolTip = "Groups Datasmith geometry by shared static mesh and materials. Single-mesh actors collapse into one managed actor named after the mesh, while hosted multi-part families stay together under one managed actor named after their wrapper family. Converted source actors are removed when their child hierarchy is fully converted."))
class DATASMITHHISM_API UConVerseCreateHISMOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:
	UConVerseCreateHISMOperation();

	virtual FText GetCategory_Implementation() const override;
	virtual FText GetAdditionalKeyword_Implementation() const override;

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;

public:
	UPROPERTY(EditAnywhere, Category = "HISM")
	FString NewActorLabelPrefix;
};
