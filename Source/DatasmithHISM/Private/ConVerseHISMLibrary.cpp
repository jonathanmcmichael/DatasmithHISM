#include "ConVerseHISMLibrary.h"

#include "ConVerseHISMUtils.h"
#include "ConVerseStaticMeshConsolidationUtils.h"

#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
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

FConVerseHISMCreationResult UConVerseHISMLibrary::CreateISMsFromSelection(
	const FString& NewActorLabelPrefix,
	bool bUseHISM,
	int32 MinInstanceCount,
	bool bAutoDetectFromNanite,
	EConVerseGroupingMode GroupingMode)
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

	ConVerseHISM::FBuildOutput BuildOutput = ConVerseHISM::BuildManagedHISMs(
		ActorsToProcess, NewActorLabelPrefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode);
	Result = BuildOutput.Result;

	if (!BuildOutput.ObjectsToDelete.IsEmpty())
	{
		const int32 DeletedObjectCount = DeleteManagedOutputObjects(BuildOutput.ObjectsToDelete);
		Result.FailedSourceActorDeletes = BuildOutput.ObjectsToDelete.Num() - DeletedObjectCount;
		ConVerseHISM::FinalizeSummary(Result);
	}

	return Result;
}

FConVerseHISMCreationResult UConVerseHISMLibrary::CreateHISMsFromSelection(const FString& NewActorLabelPrefix, bool bUseHISM)
{
	return CreateISMsFromSelection(NewActorLabelPrefix, bUseHISM);
}

FConVerseHISMAnalysisResult UConVerseHISMLibrary::AnalyzeISMCandidatesInSelection(
	const FString& NewActorLabelPrefix,
	bool bUseHISM,
	int32 MinInstanceCount,
	bool bAutoDetectFromNanite,
	EConVerseGroupingMode GroupingMode)
{
	FConVerseHISMAnalysisResult Result;

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

	return ConVerseHISM::AnalyzeManagedHISMCandidates(
		ActorsToProcess, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode);
}

FConVerseISMExplodeResult UConVerseHISMLibrary::ExplodeISMsFromSelection()
{
	static const FName ManagedHISMTag(TEXT("ConVerseManagedHISM"));
	static const FName ManagedFamilyTypeTag(TEXT("ConVerseManagedFamilyType"));

	FConVerseISMExplodeResult Result;

	if (GEditor == nullptr)
	{
		Result.Summary = TEXT("Editor is not available.");
		return Result;
	}

	// Collect all actors from the selection and their full subtrees.
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

	TArray<AActor*> AllActors;
	ConVerseHISM::CollectActorsFromRoots(SelectedRootActors, AllActors);

	// Find all managed family-type actors within the selection subtrees.
	TSet<AActor*> ManagedFamilyActors;
	for (AActor* Actor : AllActors)
	{
		if (IsValid(Actor) && Actor->Tags.Contains(ManagedFamilyTypeTag))
		{
			ManagedFamilyActors.Add(Actor);
		}
	}

	if (ManagedFamilyActors.IsEmpty())
	{
		Result.Summary = TEXT("No managed ISM family-type actors found in the selection. Run Managed ISMs first.");
		return Result;
	}

	FScopedTransaction Transaction(LOCTEXT("ExplodeISMs", "Explode ISMs"));

	for (AActor* FamilyActor : ManagedFamilyActors)
	{
		if (!IsValid(FamilyActor))
		{
			continue;
		}

		UWorld* World = FamilyActor->GetWorld();
		AActor* ParentActor = FamilyActor->GetAttachParentActor();

		TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents;
		FamilyActor->GetComponents(ISMComponents);

		for (UInstancedStaticMeshComponent* ISMComponent : ISMComponents)
		{
			if (!IsValid(ISMComponent) || !ISMComponent->ComponentTags.Contains(ManagedHISMTag))
			{
				continue;
			}

			UStaticMesh* Mesh = ISMComponent->GetStaticMesh();
			const int32 InstanceCount = ISMComponent->GetInstanceCount();
			TArray<AActor*> StagedActors;
			StagedActors.Reserve(InstanceCount);
			bool bAllInstancesRecreated = IsValid(Mesh) && IsValid(World);

			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				if (!bAllInstancesRecreated && (!IsValid(Mesh) || !IsValid(World)))
				{
					++Result.FailedSpawns;
					continue;
				}

				FTransform InstanceTransform;
				if (!ISMComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, /*bWorldSpace=*/true))
				{
					++Result.FailedSpawns;
					bAllInstancesRecreated = false;
					continue;
				}

				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = FamilyActor->GetLevel();
				AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), InstanceTransform, SpawnParams);
				if (!IsValid(NewActor))
				{
					++Result.FailedSpawns;
					bAllInstancesRecreated = false;
					continue;
				}

				NewActor->Modify();

				UStaticMeshComponent* NewSMC = NewObject<UStaticMeshComponent>(
					NewActor,
					UStaticMeshComponent::StaticClass(),
					NAME_None,
					RF_Transactional);
				if (!IsValid(NewSMC))
				{
					++Result.FailedSpawns;
					bAllInstancesRecreated = false;
					NewActor->Destroy();
					continue;
				}

				NewActor->AddInstanceComponent(NewSMC);
				NewActor->SetRootComponent(NewSMC);
				NewSMC->SetStaticMesh(Mesh);
				NewSMC->SetMobility(ISMComponent->Mobility);
				NewSMC->SetCollisionEnabled(ISMComponent->GetCollisionEnabled());
				NewSMC->SetCollisionProfileName(ISMComponent->GetCollisionProfileName());
				NewSMC->CastShadow = ISMComponent->CastShadow;
				NewSMC->bCastDynamicShadow = ISMComponent->bCastDynamicShadow;
				NewSMC->bCastStaticShadow = ISMComponent->bCastStaticShadow;

				const int32 MatCount = ISMComponent->GetNumMaterials();
				for (int32 SlotIndex = 0; SlotIndex < MatCount; ++SlotIndex)
				{
					NewSMC->SetMaterial(SlotIndex, ISMComponent->GetMaterial(SlotIndex));
				}

				NewSMC->RegisterComponent();

				bool bReplacementIsValid = NewActor->GetRootComponent() == NewSMC
					&& NewSMC->GetStaticMesh() == Mesh
					&& NewSMC->IsRegistered();
				if (IsValid(ParentActor) && ParentActor->GetRootComponent() != nullptr)
				{
					bReplacementIsValid &= NewActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform);
				}

				if (!bReplacementIsValid)
				{
					++Result.FailedSpawns;
					bAllInstancesRecreated = false;
					NewActor->Destroy();
					continue;
				}

				StagedActors.Add(NewActor);
			}

			if (!bAllInstancesRecreated)
			{
				for (AActor* StagedActor : StagedActors)
				{
					if (IsValid(StagedActor))
					{
						StagedActor->Destroy();
					}
				}
				continue;
			}

			Result.InstancesSpawned += StagedActors.Num();

			// Destroy the ISM component only after every replacement actor is valid.
			FamilyActor->Modify();
			ISMComponent->Modify();
			FamilyActor->RemoveInstanceComponent(ISMComponent);
			ISMComponent->DestroyComponent();
			++Result.ISMComponentsExploded;
		}

		// Destroy the family-type actor if it has no remaining managed components.
		TInlineComponentArray<UInstancedStaticMeshComponent*> RemainingISMs;
		FamilyActor->GetComponents(RemainingISMs);
		const bool bHasManagedComponents = RemainingISMs.ContainsByPredicate([](const UInstancedStaticMeshComponent* C)
		{
			// ManagedHISMTag is a function-local static — accessible without capture.
			static const FName LocalManagedHISMTag(TEXT("ConVerseManagedHISM"));
			return IsValid(C) && C->ComponentTags.Contains(LocalManagedHISMTag);
		});

		if (!bHasManagedComponents)
		{
			FamilyActor->Modify();
			FamilyActor->Destroy();
		}
	}

	Result.Summary = FString::Printf(
		TEXT("Exploded %d ISM component(s) into %d individual actor(s). %d instance(s) failed to spawn."),
		Result.ISMComponentsExploded,
		Result.InstancesSpawned,
		Result.FailedSpawns);

	return Result;
}

FConVerseEnableNaniteResult UConVerseHISMLibrary::EnableNaniteOnSelection()
{
	FConVerseEnableNaniteResult Result;

	if (GEditor == nullptr)
	{
		Result.Summary = TEXT("Editor is not available.");
		return Result;
	}

	TArray<UObject*> SelectedObjects;
	if (USelection* SelectedActors = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (UObject* Obj = Cast<UObject>(*It))
			{
				SelectedObjects.Add(Obj);
			}
		}
	}
	if (USelection* SelectedAssets = GEditor->GetSelectedObjects())
	{
		for (FSelectionIterator It(*SelectedAssets); It; ++It)
		{
			if (UObject* Obj = Cast<UObject>(*It))
			{
				SelectedObjects.Add(Obj);
			}
		}
	}

	TSet<UStaticMesh*> Meshes;
	ConVerseStaticMeshConsolidation::CollectStaticMeshes(SelectedObjects, Meshes);
	Result.MeshesConsidered = Meshes.Num();

	FScopedTransaction Transaction(LOCTEXT("EnableNanite", "Enable Nanite On Selection"));

	for (UStaticMesh* Mesh : Meshes)
	{
		if (!IsValid(Mesh))
		{
			continue;
		}

		if (Mesh->GetNaniteSettings().bEnabled)
		{
			++Result.MeshesAlreadyEnabled;
			continue;
		}

		if (ConVerseHISM::EnableNaniteIfNeeded(Mesh))
		{
			++Result.MeshesEnabled;
		}
	}

	Result.Summary = FString::Printf(
		TEXT("Considered %d mesh asset(s). Enabled Nanite on %d. Already enabled: %d."),
		Result.MeshesConsidered,
		Result.MeshesEnabled,
		Result.MeshesAlreadyEnabled);

	return Result;
}

int32 UConVerseHISMLibrary::MigrateTagsInCurrentLevel(const FString& OldTagName, const FString& NewTagName)
{
	if (GEditor == nullptr || OldTagName.IsEmpty() || OldTagName == NewTagName)
	{
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!IsValid(World))
	{
		return 0;
	}

	const FName OldTag(*OldTagName);
	const FName NewTag(*NewTagName);

	int32 MigratedCount = 0;
	FScopedTransaction Transaction(LOCTEXT("MigrateTags", "Migrate Managed ISM Tags"));

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->Tags.Contains(OldTag))
		{
			Actor->Modify();
			Actor->Tags.Remove(OldTag);
			Actor->Tags.AddUnique(NewTag);
			++MigratedCount;
		}

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component) && Component->ComponentTags.Contains(OldTag))
			{
				Component->Modify();
				Component->ComponentTags.Remove(OldTag);
				Component->ComponentTags.AddUnique(NewTag);
				++MigratedCount;
			}
		}
	}

	return MigratedCount;
}

#undef LOCTEXT_NAMESPACE
