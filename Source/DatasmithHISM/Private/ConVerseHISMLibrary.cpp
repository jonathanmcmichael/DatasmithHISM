#include "ConVerseHISMLibrary.h"

#include "ConVerseHISMUtils.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"

#define LOCTEXT_NAMESPACE "ConVerseHISMLibrary"

namespace
{
	static int32 DeleteManagedOutputObjects(const TArray<UObject*>& ObjectsToDelete)
	{
		if (ObjectsToDelete.IsEmpty())
		{
			return 0;
		}

		TArray<AActor*> ActorsToDelete;
		TArray<UObject*> NonActorObjectsToDelete;
		ActorsToDelete.Reserve(ObjectsToDelete.Num());
		NonActorObjectsToDelete.Reserve(ObjectsToDelete.Num());

		for (UObject* ObjectToDelete : ObjectsToDelete)
		{
			if (AActor* ActorToDelete = Cast<AActor>(ObjectToDelete))
			{
				if (IsValid(ActorToDelete))
				{
					ActorsToDelete.Add(ActorToDelete);
				}
			}
			else if (IsValid(ObjectToDelete))
			{
				NonActorObjectsToDelete.Add(ObjectToDelete);
			}
		}

		int32 DeletedObjectCount = 0;
		if (!ActorsToDelete.IsEmpty() && GEditor != nullptr)
		{
			if (UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
			{
				if (EditorActorSubsystem->DestroyActors(ActorsToDelete))
				{
					DeletedObjectCount += ActorsToDelete.Num();
				}
			}
		}

		if (!NonActorObjectsToDelete.IsEmpty())
		{
			DeletedObjectCount += ObjectTools::DeleteObjects(
				NonActorObjectsToDelete,
				false,
				ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed);
		}

		return DeletedObjectCount;
	}
}

FConVerseHISMCreationResult UConVerseHISMLibrary::CreateHISMsFromSelection(const FString& NewActorLabelPrefix)
{
	FConVerseHISMCreationResult Result;

	if (GEditor == nullptr)
	{
		Result.Summary = TEXT("Editor is not available.");
		return Result;
	}

	TArray<AActor*> SelectedRootActors;
	if (USelection* SelectedActors = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				SelectedRootActors.Add(Actor);
			}
		}
	}

	TArray<AActor*> ActorsToProcess;
	ConVerseHISM::CollectActorsFromRoots(SelectedRootActors, ActorsToProcess);

	// Wrap the entire operation in a single undoable transaction so the user can
	// Ctrl+Z to restore all source actors and remove all created ISM components
	// in one step. All spawned actors and modified components carry RF_Transactional.
	FScopedTransaction Transaction(LOCTEXT("BuildManagedISMs", "Build Managed ISMs"));

	ConVerseHISM::FBuildOutput BuildOutput = ConVerseHISM::BuildManagedHISMs(ActorsToProcess, NewActorLabelPrefix);
	Result = BuildOutput.Result;

	if (!BuildOutput.ObjectsToDelete.IsEmpty())
	{
		const int32 DeletedObjectCount = DeleteManagedOutputObjects(BuildOutput.ObjectsToDelete);
		Result.FailedSourceActorDeletes = BuildOutput.ObjectsToDelete.Num() - DeletedObjectCount;
		ConVerseHISM::FinalizeSummary(Result);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
