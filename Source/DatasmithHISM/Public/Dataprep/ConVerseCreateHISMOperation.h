#pragma once

#include "CoreMinimal.h"
#include "DataprepOperation.h"
#include "ConVerseHISMLibrary.h"

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
	UPROPERTY(EditAnywhere, Category = "ISM")
	FString NewActorLabelPrefix;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Use HierarchicalInstancedStaticMeshComponent instead of InstancedStaticMeshComponent. HISM adds per-LOD culling and instance clustering, which benefits non-Nanite meshes. Ignored when bAutoDetectFromNanite is true."))
	bool bUseHISM = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Automatically choose ISM for Nanite meshes and HISM for non-Nanite meshes based on each group's canonical mesh. Overrides bUseHISM when enabled."))
	bool bAutoDetectFromNanite = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ClampMin = "1", ToolTip = "Minimum number of instances required to create an ISM component. Groups below this threshold are left as individual static mesh actors."))
	int32 MinInstanceCount = 2;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "PreserveBIMHierarchy groups within Revit/IFC family type boundaries. MaximumOptimization ignores family identity and collapses any actors with identical geometry and materials into one ISM, producing the smallest possible component count."))
	EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy;

	// IFC spatial hierarchy / storey-boundary patterns. When non-empty, the cleanup boundary
	// walker continues past the default first-non-mesh ancestor and stops at the first ancestor
	// whose label contains any of these substrings (case-insensitive). Useful for grouping
	// ISMs per building storey in large multi-storey IFC models (e.g. "Level", "Floor", "Story").
	// Empty = original behavior (first non-mesh ancestor is the cleanup boundary).
	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "IFC storey-boundary substrings. When provided, ISMs are grouped per building storey rather than per space. Typical values: Level, Floor, Story. Leave empty for standard behavior."))
	TArray<FString> StoreyBoundaryPatterns;
};
