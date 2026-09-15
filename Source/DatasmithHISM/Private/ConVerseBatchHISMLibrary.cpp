#include "ConVerseBatchHISMLibrary.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MeshMerge/MeshInstancingSettings.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	static void CollectActorsFromRoots(const TArray<AActor*>& RootActors, TArray<AActor*>& OutActors)
	{
		TSet<AActor*> VisitedActors;
		TArray<AActor*> PendingActors = RootActors;

		while (!PendingActors.IsEmpty())
		{
			AActor* Actor = PendingActors.Pop(EAllowShrinking::No);
			if (!IsValid(Actor) || VisitedActors.Contains(Actor))
			{
				continue;
			}

			VisitedActors.Add(Actor);
			OutActors.Add(Actor);

			TArray<AActor*> AttachedActors;
			Actor->GetAttachedActors(AttachedActors, true, false);
			PendingActors.Append(AttachedActors);
		}
	}

	static bool IsEligibleStaticMeshComponent(UStaticMeshComponent* MeshComponent)
	{
		return IsValid(MeshComponent)
			&& !MeshComponent->IsEditorOnly()
			&& !MeshComponent->IsVisualizationComponent()
			&& !MeshComponent->IsA<UInstancedStaticMeshComponent>()
			&& MeshComponent->GetStaticMesh() != nullptr;
	}

	static FString SanitizeNameFragment(const FString& InValue)
	{
		FString Result = InValue;
		Result.TrimStartAndEndInline();
		Result.ReplaceInline(TEXT(" "), TEXT("_"));
		Result.ReplaceInline(TEXT("/"), TEXT("_"));
		Result.ReplaceInline(TEXT("\\"), TEXT("_"));
		Result.ReplaceInline(TEXT(":"), TEXT("_"));
		Result.ReplaceInline(TEXT("."), TEXT("_"));
		Result.ReplaceInline(TEXT("-"), TEXT("_"));
		return Result;
	}

	static TSet<UInstancedStaticMeshComponent*> CollectLevelISMComponents(ULevel* Level)
	{
		TSet<UInstancedStaticMeshComponent*> Components;
		if (!IsValid(Level))
		{
			return Components;
		}

		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor))
			{
				continue;
			}

			TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents;
			Actor->GetComponents(ISMComponents);
			for (UInstancedStaticMeshComponent* ISMComponent : ISMComponents)
			{
				if (IsValid(ISMComponent))
				{
					Components.Add(ISMComponent);
				}
			}
		}

		return Components;
	}

	static void RenameNewlyCreatedISMComponents(
		ULevel* Level,
		const TSet<UInstancedStaticMeshComponent*>& ExistingComponents)
	{
		if (!IsValid(Level))
		{
			return;
		}

		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor))
			{
				continue;
			}

			TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents;
			Actor->GetComponents(ISMComponents);
			for (UInstancedStaticMeshComponent* ISMComponent : ISMComponents)
			{
				if (!IsValid(ISMComponent) || ExistingComponents.Contains(ISMComponent))
				{
					continue;
				}

				UStaticMesh* StaticMesh = ISMComponent->GetStaticMesh();
				if (!IsValid(StaticMesh))
				{
					continue;
				}

				Actor->Modify();
				ISMComponent->Modify();

				const FString BaseName = SanitizeNameFragment(StaticMesh->GetName());
				const FName UniqueName = MakeUniqueObjectName(
					Actor,
					UInstancedStaticMeshComponent::StaticClass(),
					BaseName.IsEmpty() ? TEXT("ISM") : *BaseName);

				ISMComponent->Rename(
					*UniqueName.ToString(),
					Actor,
					REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}
}

FConVerseBatchHISMResult UConVerseBatchHISMLibrary::BatchSelectionToHISMs(const int32 InstanceReplacementThreshold)
{
	FConVerseBatchHISMResult Result;

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

	Result.RootActorsSelected = SelectedRootActors.Num();
	if (SelectedRootActors.IsEmpty())
	{
		Result.Summary = TEXT("Select one or more actors to batch.");
		return Result;
	}

	TArray<AActor*> ActorsToProcess;
	CollectActorsFromRoots(SelectedRootActors, ActorsToProcess);
	Result.ActorsConsidered = ActorsToProcess.Num();

	TArray<UPrimitiveComponent*> ComponentsToBatch;
	TSet<ULevel*> UniqueLevels;
	for (AActor* Actor : ActorsToProcess)
	{
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);

		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			++Result.StaticMeshComponentsConsidered;

			if (!IsEligibleStaticMeshComponent(MeshComponent))
			{
				continue;
			}

			ComponentsToBatch.Add(MeshComponent);
			++Result.EligibleStaticMeshComponents;

			if (ULevel* Level = Actor->GetLevel())
			{
				UniqueLevels.Add(Level);
			}
		}
	}

	if (ComponentsToBatch.IsEmpty())
	{
		Result.Summary = TEXT("The selection does not contain any eligible static mesh components to batch.");
		return Result;
	}

	if (UniqueLevels.Num() != 1)
	{
		Result.Summary = TEXT("The selected actors should be in the same level.");
		return Result;
	}

	UWorld* World = ComponentsToBatch[0] != nullptr ? ComponentsToBatch[0]->GetWorld() : nullptr;
	ULevel* Level = UniqueLevels.Array()[0];
	if (!IsValid(World) || !IsValid(Level))
	{
		Result.Summary = TEXT("Could not determine a valid world and level for batching.");
		return Result;
	}

	FMeshInstancingSettings Settings;
	Settings.ActorClassToUse = AActor::StaticClass();
	Settings.InstanceReplacementThreshold = FMath::Max(2, InstanceReplacementThreshold);
	Settings.bUseHLODVolumes = false;
	Settings.ISMComponentToUse = UInstancedStaticMeshComponent::StaticClass();

	const TSet<UInstancedStaticMeshComponent*> ExistingISMComponents = CollectLevelISMComponents(Level);

	const IMeshMergeUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	FText PredictedResults;
	MeshUtilities.MergeComponentsToInstances(ComponentsToBatch, World, Level, Settings, false, true, &PredictedResults);
	Result.Summary = PredictedResults.IsEmpty() ? TEXT("Batch HISM tool completed.") : PredictedResults.ToString();

	if (PredictedResults.ToString().Contains(TEXT("will not result in any instanced meshes being created")))
	{
		return Result;
	}

	MeshUtilities.MergeComponentsToInstances(ComponentsToBatch, World, Level, Settings, true, true, nullptr);
	RenameNewlyCreatedISMComponents(Level, ExistingISMComponents);
	Result.bSucceeded = true;
	return Result;
}
