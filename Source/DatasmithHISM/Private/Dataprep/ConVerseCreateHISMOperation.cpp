#include "Dataprep/ConVerseCreateHISMOperation.h"

#include "ConVerseHISMUtils.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace
{
	static int32 DeleteManagedHISMActorOutputs(const TArray<UObject*>& ObjectsToDelete)
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

#define LOCTEXT_NAMESPACE "ConVerseCreateHISMOperation"

UConVerseCreateHISMOperation::UConVerseCreateHISMOperation()
	: NewActorLabelPrefix(TEXT("HISM"))
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

	ConVerseHISM::FBuildOutput BuildOutput = ConVerseHISM::BuildManagedHISMs(ActorsToProcess, NewActorLabelPrefix);

	if (BuildOutput.Result.HISMActorsCreated == 0 && BuildOutput.Result.SourceActorsConverted == 0)
	{
		LogInfo(FText::FromString(BuildOutput.Result.Summary));
		return;
	}

	if (!BuildOutput.ObjectsToDelete.IsEmpty())
	{
		const int32 ObjectsToDeleteCount = BuildOutput.ObjectsToDelete.Num();
		const int32 DeletedObjectCount = DeleteManagedHISMActorOutputs(BuildOutput.ObjectsToDelete);
		BuildOutput.Result.FailedSourceActorDeletes = ObjectsToDeleteCount - DeletedObjectCount;
		ConVerseHISM::FinalizeSummary(BuildOutput.Result);

		LogInfo(FText::FromString(BuildOutput.Result.Summary));
		LogInfo(FText::Format(
			LOCTEXT("DeleteSummary", "Deleted {0} converted source or wrapper actor(s) after building managed HISM components."),
			ObjectsToDeleteCount));
		return;
	}

	LogInfo(FText::FromString(BuildOutput.Result.Summary));
}

#undef LOCTEXT_NAMESPACE
