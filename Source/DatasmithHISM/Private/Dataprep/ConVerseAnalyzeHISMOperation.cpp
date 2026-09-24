#include "Dataprep/ConVerseAnalyzeHISMOperation.h"

#include "ConVerseHISMUtils.h"

#define LOCTEXT_NAMESPACE "ConVerseAnalyzeHISMOperation"

FText UConVerseAnalyzeHISMOperation::GetCategory_Implementation() const
{
	return FDataprepOperationCategories::ActorOperation;
}

FText UConVerseAnalyzeHISMOperation::GetAdditionalKeyword_Implementation() const
{
	return LOCTEXT("Keywords", "analyze dry run preview hierarchical instanced static mesh family type datasmith");
}

void UConVerseAnalyzeHISMOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	TArray<AActor*> RootActors;
	RootActors.Reserve(InContext.Objects.Num());
	for (UObject* Object : InContext.Objects)
	{
		if (AActor* Actor = Cast<AActor>(Object))
		{
			RootActors.Add(Actor);
		}
	}

	TArray<AActor*> ActorsToProcess;
	ConVerseHISM::CollectActorsFromRoots(RootActors, ActorsToProcess);

	const FConVerseHISMAnalysisResult AnalysisResult = ConVerseHISM::AnalyzeManagedHISMCandidates(
		ActorsToProcess, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode, StoreyBoundaryPatterns);

	LogInfo(FText::FromString(AnalysisResult.Summary));
	LogInfo(FText::Format(
		LOCTEXT("BreakdownLog",
			"Groups above threshold: {0} (would convert {1} actor(s)). "
			"Groups below threshold: {2} ({3} actor(s) left in place). "
			"Skipped: {4} actor(s)."),
		AnalysisResult.GroupsAboveThreshold,
		AnalysisResult.ActorsWouldBeConverted,
		AnalysisResult.GroupsBelowThreshold,
		AnalysisResult.ActorsInSmallGroups,
		AnalysisResult.SkippedActors));
}

#undef LOCTEXT_NAMESPACE
