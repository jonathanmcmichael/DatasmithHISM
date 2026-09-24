#pragma once

#include "CoreMinimal.h"
#include "DataprepOperation.h"
#include "ConVerseHISMLibrary.h"

#include "ConVerseAnalyzeHISMOperation.generated.h"

UCLASS(Category = ActorOperation, Meta = (DisplayName = "Analyze ISM Candidates", ToolTip = "Dry-run of Create ISMs. Reports how many ISM groups would be created and how many actors would be converted, without making any changes. Use as a pre-flight step before the Create ISMs operation."))
class DATASMITHHISM_API UConVerseAnalyzeHISMOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:
	virtual FText GetCategory_Implementation() const override;
	virtual FText GetAdditionalKeyword_Implementation() const override;

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;

public:
	UPROPERTY(EditAnywhere, Category = "ISM")
	FString NewActorLabelPrefix = TEXT("ISM");

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Matches the bUseHISM setting you intend to use in the Create ISMs operation, so the analysis reflects the correct grouping behavior."))
	bool bUseHISM = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "Matches the bAutoDetectFromNanite setting you intend to use in the Create ISMs operation."))
	bool bAutoDetectFromNanite = false;

	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ClampMin = "1", ToolTip = "Minimum instance count threshold — groups below this count would be left as individual actors."))
	int32 MinInstanceCount = 2;

	UPROPERTY(EditAnywhere, Category = "ISM")
	EConVerseGroupingMode GroupingMode = EConVerseGroupingMode::PreserveBIMHierarchy;

	// Matches the StoreyBoundaryPatterns you intend to use in the Create ISMs operation,
	// so the analysis reflects the correct storey-boundary grouping.
	UPROPERTY(EditAnywhere, Category = "ISM", meta = (ToolTip = "IFC storey-boundary substrings to match against ancestor actor labels. Mirrors the StoreyBoundaryPatterns setting on the Create ISMs operation. Leave empty to analyze with default (first non-mesh ancestor) boundary behavior."))
	TArray<FString> StoreyBoundaryPatterns;
};
