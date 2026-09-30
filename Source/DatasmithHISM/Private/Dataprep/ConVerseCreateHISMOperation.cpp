#include "Dataprep/ConVerseCreateHISMOperation.h"

#include "ConVerseHISMUtils.h"

#define LOCTEXT_NAMESPACE "ConVerseCreateHISMOperation"

UConVerseCreateHISMOperation::UConVerseCreateHISMOperation()
	: NewActorLabelPrefix(TEXT("ISM"))
{
}

FText UConVerseCreateHISMOperation::GetCategory_Implementation() const
{
	return FDataprepOperationCategories::ActorOperation;
}

FText UConVerseCreateHISMOperation::GetAdditionalKeyword_Implementation() const
{
	return LOCTEXT("Keywords", "hierarchical instanced static mesh batch actor merge instance datasmith family type");
}

void UConVerseCreateHISMOperation::OnExecution_Implementation(const FDataprepContext& InContext)
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

	ConVerseHISM::FBuildOutput BuildOutput = ConVerseHISM::BuildManagedHISMs(
		ActorsToProcess, NewActorLabelPrefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode, StoreyBoundaryPatterns, false);

	if (BuildOutput.Result.ISMComponentsCreated == 0 && BuildOutput.Result.SourceActorsConverted == 0)
	{
		LogInfo(FText::FromString(BuildOutput.Result.Summary));
		return;
	}

	if (!BuildOutput.ObjectsToDelete.IsEmpty())
	{
		const int32 ObjectsToDeleteCount = BuildOutput.ObjectsToDelete.Num();
		const int32 DeletedObjectCount = ConVerseHISM::DeleteManagedActorOutputs(BuildOutput.ObjectsToDelete);
		BuildOutput.Result.FailedSourceActorDeletes = ObjectsToDeleteCount - DeletedObjectCount;
		ConVerseHISM::FinalizeSummary(BuildOutput.Result);

		LogInfo(FText::FromString(BuildOutput.Result.Summary));
		LogInfo(FText::Format(
			LOCTEXT("DeleteSummary", "Deleted {0} converted source or wrapper actor(s) after building managed ISM components."),
			DeletedObjectCount));
		if (DeletedObjectCount != ObjectsToDeleteCount)
		{
			LogWarning(FText::Format(
				LOCTEXT("DeleteFailureSummary", "Failed to delete {0} of {1} converted source or wrapper actor(s); retained actors remain reported in the conversion summary."),
				ObjectsToDeleteCount - DeletedObjectCount,
				ObjectsToDeleteCount));
		}
		return;
	}

	LogInfo(FText::FromString(BuildOutput.Result.Summary));
}

#undef LOCTEXT_NAMESPACE
