#pragma once

#include "CoreMinimal.h"
#include "DataprepOperation.h"
#include "ConVerseHISMLibrary.h"

#include "ConVerseCategoryGroupHISMOperation.generated.h"

// Managed ISMs restricted to actors whose labels match a category substring.
// Useful for pipelines that process Revit/IFC categories (e.g., "Furniture",
// "Structural Framing") as separate passes with different settings.
UCLASS(Category = ActorOperation, Meta = (DisplayName = "Create ISMs By Category", ToolTip = "Groups Datasmith geometry by category (label substring), family boundary, mesh geometry signature, and materials. Only actors whose label matches CategoryFilter are processed. Leave CategoryFilter empty to process all actors (equivalent to Create ISMs)."))
class DATASMITHHISM_API UConVerseCategoryGroupHISMOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:
	UConVerseCategoryGroupHISMOperation();

	virtual FText GetCategory_Implementation() const override;
	virtual FText GetAdditionalKeyword_Implementation() const override;

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;

public:
	// Case-insensitive substring matched against each actor's label.
	// Empty string matches all actors (same behavior as the standard Create ISMs operation).
	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Case-insensitive substring matched against each actor's label. Only matching actors are processed. Leave empty to process all actors."))
	FString CategoryFilter;

	UPROPERTY(EditAnywhere, Category = "ISM")
	FString NewActorLabelPrefix = TEXT("ISM");

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Use HierarchicalInstancedStaticMeshComponent instead of InstancedStaticMeshComponent. Ignored when bAutoDetectFromNanite is true."))
	bool bUseHISM = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Automatically choose ISM for Nanite meshes and HISM for non-Nanite meshes. Overrides bUseHISM when enabled."))
	bool bAutoDetectFromNanite = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ClampMin = "1", ToolTip = "Minimum instance count to create an ISM component. Groups below this threshold are left as individual actors."))
	int32 MinInstanceCount = 2;

	UPROPERTY(EditAnywhere, Category = "ISM")
	EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy;

	// IFC storey-boundary substrings. When non-empty, the cleanup boundary walks past the
	// first non-mesh ancestor and stops at the first ancestor whose label contains any of
	// these substrings (case-insensitive). Mirrors the same parameter on Create ISMs.
	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "IFC storey-boundary substrings (e.g. Level, Floor, Story). Leave empty for standard boundary behavior."))
	TArray<FString> StoreyBoundaryPatterns;
};
