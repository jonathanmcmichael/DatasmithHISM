#include "Dataprep/ConVerseCategoryGroupHISMOperation.h"

#include "ConVerseHISMUtils.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace
{
	static int32 DeleteManagedCategoryActorOutputs(const TArray<UObject*>& ObjectsToDelete)
	{
		if (ObjectsToDelete.IsEmpty() || GEditor == nullptr)
		{
			return 0;
		}

		TArray<AActor*> ActorsToDelete;
		ActorsToDelete.Reserve(ObjectsToDelete.Num());
		for (UObject* ObjectToDelete : ObjectsToDelete)
		{
			if (AActor* ActorToDelete = Cast<AActor>(ObjectToDelete))
			{
				if (IsValid(ActorToDelete))
				{
					ActorsToDelete.Add(ActorToDelete);
				}
			}
		}

		if (ActorsToDelete.IsEmpty())
		{
			return 0;
		}

		if (UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
		{
			return EditorActorSubsystem->DestroyActors(ActorsToDelete) ? ActorsToDelete.Num() : 0;
		}

		return 0;
	}
}

#define LOCTEXT_NAMESPACE "ConVerseCategoryGroupHISMOperation"

UConVerseCategoryGroupHISMOperation::UConVerseCategoryGroupHISMOperation()
	: NewActorLabelPrefix(TEXT("ISM"))
{
}

FText UConVerseCategoryGroupHISMOperation::GetCategory_Implementation() const
{
	return FDataprepOperationCategories::ActorOperation;
}

FText UConVerseCategoryGroupHISMOperation::GetAdditionalKeyword_Implementation() const
{
	return LOCTEXT("Keywords", "category filter hierarchical instanced static mesh family type datasmith revit ifc furniture structure");
}

void UConVerseCategoryGroupHISMOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect root actors that pass the category filter. An empty filter accepts all actors.
	TArray<AActor*> RootActors;
	RootActors.Reserve(InContext.Objects.Num());
	for (UObject* Object : InContext.Objects)
	{
		AActor* Actor = Cast<AActor>(Object);
		if (!IsValid(Actor))
		{
			continue;
		}

		if (!CategoryFilter.IsEmpty())
		{
			const FString ActorLabel = Actor->GetActorLabel();
			if (!ActorLabel.Contains(CategoryFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		RootActors.Add(Actor);
	}

	if (RootActors.IsEmpty())
	{
		LogInfo(FText::Format(
			LOCTEXT("NoMatchingActors", "No actors matched category filter '{0}'. Nothing to process."),
			FText::FromString(CategoryFilter)));
		return;
	}

	TArray<AActor*> ActorsToProcess;
	ConVerseHISM::CollectActorsFromRoots(RootActors, ActorsToProcess);

	LogInfo(FText::Format(
		LOCTEXT("FilteredCount", "Category filter '{0}' matched {1} root actor(s); {2} total actor(s) in subtrees."),
		FText::FromString(CategoryFilter.IsEmpty() ? TEXT("(all)") : CategoryFilter),
		RootActors.Num(),
		ActorsToProcess.Num()));

	ConVerseHISM::FBuildOutput BuildOutput = ConVerseHISM::BuildManagedHISMs(
		ActorsToProcess, NewActorLabelPrefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode, StoreyBoundaryPatterns);

	if (!BuildOutput.ObjectsToDelete.IsEmpty())
	{
		const int32 ObjectsToDeleteCount = BuildOutput.ObjectsToDelete.Num();
		const int32 DeletedObjectCount = DeleteManagedCategoryActorOutputs(BuildOutput.ObjectsToDelete);
		BuildOutput.Result.FailedSourceActorDeletes = ObjectsToDeleteCount - DeletedObjectCount;
		ConVerseHISM::FinalizeSummary(BuildOutput.Result);
	}

	LogInfo(FText::FromString(BuildOutput.Result.Summary));
}

#undef LOCTEXT_NAMESPACE
