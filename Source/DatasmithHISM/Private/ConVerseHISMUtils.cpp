#include "ConVerseHISMUtils.h"

#include "ConVerseStaticMeshConsolidationUtils.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h" // UHierarchicalInstancedStaticMeshComponent is a UInstancedStaticMeshComponent subclass; the include
                                                                  // keeps the full type available so GetComponents<UInstancedStaticMeshComponent> also returns legacy
                                                                  // HISM components created by earlier versions of this plugin when ClearManagedHISMComponents runs.
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogConVerseHISM, Log, All);

namespace ConVerseHISM
{
	namespace
	{
		// Preserve the existing tags so reruns still recognize content created by the old module.
		static const FName ManagedHISMTag(TEXT("ConVerseManagedHISM"));
		static const FName ManagedFamilyTypeTag(TEXT("ConVerseManagedFamilyType"));

		struct FHISMGroupKey
		{
			TObjectPtr<AActor> FamilyTypeActor = nullptr;
			// Stable hash of the mesh's LOD0 geometry (shape only, no material asset paths).
			// Identical geometry from separately imported assets produces the same signature,
			// allowing them to share one ISM. Falls back to the mesh asset path for meshes
			// that cannot be hashed (no source models, no triangles).
			FString GeometrySignature;
			// Ordered list of effective material asset paths from the component (slot 0, 1, …).
			// Actors with identical geometry but different materials produce different keys
			// and get separate ISM components, preserving each variant's appearance.
			FString MaterialSignature;

			bool operator==(const FHISMGroupKey& Other) const
			{
				return FamilyTypeActor == Other.FamilyTypeActor
					&& GeometrySignature == Other.GeometrySignature
					&& MaterialSignature == Other.MaterialSignature;
			}

			friend uint32 GetTypeHash(const FHISMGroupKey& Key)
			{
				return HashCombine(
					HashCombine(GetTypeHash(Key.FamilyTypeActor), GetTypeHash(Key.GeometrySignature)),
					GetTypeHash(Key.MaterialSignature));
			}
		};

		// Per-group data: the list of source actors plus the canonical mesh chosen for the ISM.
		// The canonical mesh is the one whose asset path sorts first among all geometrically
		// equivalent meshes in the group, giving a deterministic result across reruns.
		struct FHISMGroupData
		{
			TArray<FSourceActorData> Actors;
			TObjectPtr<UStaticMesh> CanonicalMesh = nullptr;
		};

		struct FSourceActorData
		{
			TObjectPtr<AActor> Actor = nullptr;
			TObjectPtr<UStaticMeshComponent> Component = nullptr;
			TObjectPtr<AActor> CleanupBoundaryActor = nullptr;
			FTransform WorldTransform = FTransform::Identity;
		};

		enum class EActorEligibilityFailureReason : uint8
		{
			None,
			NoEligibleStaticMesh,
			MultipleEligibleStaticMeshes
		};

		struct FFamilyActorKey
		{
			TObjectPtr<AActor> ParentActor = nullptr;
			FString FamilyLabel;

			bool operator==(const FFamilyActorKey& Other) const
			{
				return ParentActor == Other.ParentActor && FamilyLabel == Other.FamilyLabel;
			}

			friend uint32 GetTypeHash(const FFamilyActorKey& Key)
			{
				return HashCombine(GetTypeHash(Key.ParentActor), GetTypeHash(Key.FamilyLabel));
			}
		};

		struct FFamilyTypeLookupCache
		{
			TMap<TObjectPtr<AActor>, TObjectPtr<AActor>> CleanupBoundaryActorBySourceActor;
			TMap<TObjectPtr<AActor>, TObjectPtr<AActor>> HostedFamilyWrapperActorBySourceActor;
			TMap<FFamilyActorKey, TObjectPtr<AActor>> ManagedFamilyActorsByKey;
		};

		static USceneComponent* EnsureRootComponent(AActor* Actor, const EComponentMobility::Type Mobility);

		static bool HasDirectAttachedChildren(AActor* Actor)
		{
			if (!IsValid(Actor))
			{
				return false;
			}

			TArray<AActor*> AttachedActors;
			Actor->GetAttachedActors(AttachedActors, true, false);
			return AttachedActors.Num() > 0;
		}

		static int32 GetActorDepth(AActor* Actor)
		{
			return Actor ? 1 + GetActorDepth(Actor->GetAttachParentActor()) : 0;
		}

		static bool TryGetEligibleStaticMeshComponent(AActor* Actor, UStaticMeshComponent*& OutComponent, EActorEligibilityFailureReason& OutFailureReason)
		{
			OutComponent = nullptr;
			OutFailureReason = EActorEligibilityFailureReason::None;

			if (!IsValid(Actor))
			{
				OutFailureReason = EActorEligibilityFailureReason::NoEligibleStaticMesh;
				return false;
			}

			TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
			Actor->GetComponents(MeshComponents);

			TArray<UStaticMeshComponent*> EligibleComponents;
			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if (MeshComponent == nullptr || MeshComponent->IsEditorOnly() || MeshComponent->IsVisualizationComponent())
				{
					continue;
				}

				if (MeshComponent->IsA<UInstancedStaticMeshComponent>())
				{
					continue;
				}

				if (MeshComponent->ComponentTags.Contains(ManagedHISMTag))
				{
					continue;
				}

				UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
				if (StaticMesh == nullptr || StaticMesh->GetNumSourceModels() == 0)
				{
					continue;
				}

				EligibleComponents.Add(MeshComponent);
			}

			if (EligibleComponents.Num() == 0)
			{
				OutFailureReason = EActorEligibilityFailureReason::NoEligibleStaticMesh;
				return false;
			}

			if (EligibleComponents.Num() > 1)
			{
				OutFailureReason = EActorEligibilityFailureReason::MultipleEligibleStaticMeshes;
				return false;
			}

			OutComponent = EligibleComponents[0];
			return true;
		}

		static AActor* FindHostedFamilyWrapperActor(AActor* GeometryActor, FFamilyTypeLookupCache& Cache)
		{
			if (!IsValid(GeometryActor))
			{
				return nullptr;
			}

			if (TObjectPtr<AActor>* CachedActor = Cache.HostedFamilyWrapperActorBySourceActor.Find(GeometryActor))
			{
				return CachedActor->Get();
			}

			AActor* HighestNonGeometryAncestorBelowHost = nullptr;
			AActor* CurrentAncestor = GeometryActor->GetAttachParentActor();
			while (IsValid(CurrentAncestor))
			{
				UStaticMeshComponent* MeshComponent = nullptr;
				EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
				if (TryGetEligibleStaticMeshComponent(CurrentAncestor, MeshComponent, FailureReason))
				{
					Cache.HostedFamilyWrapperActorBySourceActor.Add(GeometryActor, HighestNonGeometryAncestorBelowHost);
					return HighestNonGeometryAncestorBelowHost;
				}

				HighestNonGeometryAncestorBelowHost = CurrentAncestor;
				CurrentAncestor = CurrentAncestor->GetAttachParentActor();
			}

			Cache.HostedFamilyWrapperActorBySourceActor.Add(GeometryActor, nullptr);
			return nullptr;
		}

		static AActor* FindCleanupBoundaryActor(AActor* GeometryActor, FFamilyTypeLookupCache& Cache)
		{
			if (!IsValid(GeometryActor))
			{
				return nullptr;
			}

			if (TObjectPtr<AActor>* CachedActor = Cache.CleanupBoundaryActorBySourceActor.Find(GeometryActor))
			{
				return CachedActor->Get();
			}

			AActor* CurrentAncestor = FindHostedFamilyWrapperActor(GeometryActor, Cache);
			CurrentAncestor = IsValid(CurrentAncestor) ? CurrentAncestor->GetAttachParentActor() : GeometryActor->GetAttachParentActor();

			while (IsValid(CurrentAncestor))
			{
				UStaticMeshComponent* MeshComponent = nullptr;
				EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
				if (!TryGetEligibleStaticMeshComponent(CurrentAncestor, MeshComponent, FailureReason))
				{
					Cache.CleanupBoundaryActorBySourceActor.Add(GeometryActor, CurrentAncestor);
					return CurrentAncestor;
				}

				CurrentAncestor = CurrentAncestor->GetAttachParentActor();
			}

			Cache.CleanupBoundaryActorBySourceActor.Add(GeometryActor, nullptr);
			return nullptr;
		}

		static FString GetActorFamilyLabel(AActor* Actor)
		{
			if (!IsValid(Actor))
			{
				return FString();
			}

			FString Label = Actor->GetActorLabel();
			if (Label.IsEmpty())
			{
				Label = Actor->GetName();
			}

			Label.TrimStartAndEndInline();
			return Label;
		}

		static FString GetManagedFamilyLabel(AActor* Actor, UStaticMeshComponent* MeshComponent, AActor* HostedFamilyWrapperActor)
		{
			if (IsValid(HostedFamilyWrapperActor))
			{
				const FString WrapperLabel = GetActorFamilyLabel(HostedFamilyWrapperActor);
				if (!WrapperLabel.IsEmpty())
				{
					return WrapperLabel;
				}
			}

			if (IsValid(MeshComponent))
			{
				if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
				{
					FString MeshLabel = StaticMesh->GetName();
					MeshLabel.TrimStartAndEndInline();
					if (!MeshLabel.IsEmpty())
					{
						return MeshLabel;
					}
				}
			}

			return GetActorFamilyLabel(Actor);
		}

		static AActor* FindExistingManagedFamilyTypeActor(AActor* ParentActor, const FString& FamilyLabel)
		{
			if (!IsValid(ParentActor))
			{
				return nullptr;
			}

			TArray<AActor*> DirectChildren;
			ParentActor->GetAttachedActors(DirectChildren, true, false);

			for (AActor* ChildActor : DirectChildren)
			{
				if (!IsValid(ChildActor) || !ChildActor->Tags.Contains(ManagedFamilyTypeTag))
				{
					continue;
				}

				if (GetActorFamilyLabel(ChildActor).Equals(FamilyLabel, ESearchCase::CaseSensitive))
				{
					return ChildActor;
				}
			}

			return nullptr;
		}

		static AActor* FindOrCreateManagedFamilyTypeActor(
			AActor* ParentActor,
			AActor* SourceActor,
			const FString& FamilyLabel,
			const EComponentMobility::Type Mobility,
			FFamilyTypeLookupCache& Cache)
		{
			const FFamilyActorKey Key{ ParentActor, FamilyLabel };
			if (TObjectPtr<AActor>* CachedActor = Cache.ManagedFamilyActorsByKey.Find(Key))
			{
				return CachedActor->Get();
			}

			AActor* FamilyActor = FindExistingManagedFamilyTypeActor(ParentActor, FamilyLabel);
			if (!IsValid(FamilyActor))
			{
				UWorld* World = IsValid(ParentActor) ? ParentActor->GetWorld() : (IsValid(SourceActor) ? SourceActor->GetWorld() : nullptr);
				if (!IsValid(World))
				{
					Cache.ManagedFamilyActorsByKey.Add(Key, nullptr);
					return nullptr;
				}

				const FTransform SpawnTransform = IsValid(ParentActor)
					? ParentActor->GetActorTransform()
					: (IsValid(SourceActor) ? SourceActor->GetActorTransform() : FTransform::Identity);

				FActorSpawnParameters SpawnParameters;
				SpawnParameters.OverrideLevel = IsValid(ParentActor)
					? ParentActor->GetLevel()
					: (IsValid(SourceActor) ? SourceActor->GetLevel() : nullptr);

				FamilyActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform, SpawnParameters);
				if (!IsValid(FamilyActor))
				{
					Cache.ManagedFamilyActorsByKey.Add(Key, nullptr);
					return nullptr;
				}
			}

			FamilyActor->Modify();
			FamilyActor->Tags.AddUnique(ManagedFamilyTypeTag);
			FamilyActor->SetActorLabel(FamilyLabel, false);

			USceneComponent* RootComponent = EnsureRootComponent(FamilyActor, Mobility);
			if (RootComponent != nullptr)
			{
				RootComponent->Modify();
			}

			if (IsValid(ParentActor) && ParentActor->GetRootComponent() != nullptr)
			{
				FamilyActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform);
			}

			Cache.ManagedFamilyActorsByKey.Add(Key, FamilyActor);
			return FamilyActor;
		}

		static FString GetCachedMeshSignature(
			UStaticMesh* Mesh,
			TMap<UStaticMesh*, FString>& SignatureCache)
		{
			if (!IsValid(Mesh))
			{
				return FString();
			}

			if (const FString* Cached = SignatureCache.Find(Mesh))
			{
				return *Cached;
			}

			FString Signature;
			if (!ConVerseStaticMeshConsolidation::GetMeshGeometrySignature(Mesh, Signature))
			{
				// Fall back to the asset path so the actor still gets instanced with
				// others referencing the exact same (unhashable) mesh asset.
				Signature = Mesh->GetPathName();
			}

			SignatureCache.Add(Mesh, Signature);
			return Signature;
		}

		static FString BuildMaterialSignature(const UStaticMeshComponent* Component)
		{
			FString Signature;
			const int32 MaterialCount = Component ? Component->GetNumMaterials() : 0;
			Signature.AppendInt(MaterialCount);
			Signature.AppendChar(TEXT(':'));
			for (int32 Index = 0; Index < MaterialCount; ++Index)
			{
				const UMaterialInterface* Material = Component->GetMaterial(Index);
				Signature += Material ? Material->GetPathName() : TEXT("<None>");
				Signature.AppendChar(TEXT('|'));
			}
			return Signature;
		}

		static FHISMGroupKey BuildGroupKey(
			AActor* FamilyTypeActor,
			UStaticMeshComponent* MeshComponent,
			TMap<UStaticMesh*, FString>& SignatureCache)
		{
			FHISMGroupKey Key;
			Key.FamilyTypeActor = FamilyTypeActor;
			Key.GeometrySignature = GetCachedMeshSignature(MeshComponent->GetStaticMesh(), SignatureCache);
			Key.MaterialSignature = BuildMaterialSignature(MeshComponent);
			return Key;
		}

		static FTransform GetSourceWorldTransform(AActor* Actor, UStaticMeshComponent* MeshComponent)
		{
			if (!IsValid(Actor) || !IsValid(MeshComponent))
			{
				return FTransform::Identity;
			}

			// Revit/Datasmith imports in this workflow are flattened, so the source object transform is
			// the actor transform itself. Using actor space here avoids depending on stale or identity
			// component-to-world values from Dataprep preview actors.
			return Actor->GetActorTransform();
		}

		static void CopyRelevantComponentProperties(const UStaticMeshComponent* SourceComponent, UInstancedStaticMeshComponent* TargetComponent)
		{
			TargetComponent->SetMobility(SourceComponent->Mobility);
			TargetComponent->SetCollisionEnabled(SourceComponent->GetCollisionEnabled());
			TargetComponent->SetCollisionProfileName(SourceComponent->GetCollisionProfileName());
			TargetComponent->SetGenerateOverlapEvents(SourceComponent->GetGenerateOverlapEvents());
			TargetComponent->SetCanEverAffectNavigation(SourceComponent->CanEverAffectNavigation());
			TargetComponent->CastShadow = SourceComponent->CastShadow;
			TargetComponent->bCastDynamicShadow = SourceComponent->bCastDynamicShadow;
			TargetComponent->bCastStaticShadow = SourceComponent->bCastStaticShadow;
			TargetComponent->bReceivesDecals = SourceComponent->bReceivesDecals;
			TargetComponent->bAffectDistanceFieldLighting = SourceComponent->bAffectDistanceFieldLighting;
			TargetComponent->bCastContactShadow = SourceComponent->bCastContactShadow;
			TargetComponent->SetCullDistances(
				FMath::Max(0, FMath::RoundToInt(SourceComponent->MinDrawDistance)),
				FMath::Max(0, FMath::RoundToInt(SourceComponent->LDMaxDrawDistance)));
		}

		static USceneComponent* EnsureRootComponent(AActor* Actor, const EComponentMobility::Type Mobility)
		{
			if (!IsValid(Actor))
			{
				return nullptr;
			}

			USceneComponent* RootComponent = Actor->GetRootComponent();
			if (RootComponent == nullptr)
			{
				RootComponent = NewObject<USceneComponent>(Actor, USceneComponent::StaticClass(), TEXT("Root"), RF_Transactional);
				Actor->AddInstanceComponent(RootComponent);
				Actor->SetRootComponent(RootComponent);
				RootComponent->SetMobility(Mobility);
				RootComponent->RegisterComponent();
				return RootComponent;
			}

			RootComponent->Modify();
			RootComponent->SetMobility(Mobility);
			return RootComponent;
		}

		static void ClearManagedHISMComponents(AActor* FamilyTypeActor)
		{
			if (!IsValid(FamilyTypeActor))
			{
				return;
			}

			// Search for UInstancedStaticMeshComponent, which covers both old HISM outputs
			// (UHierarchicalInstancedStaticMeshComponent is a subclass) and new ISM outputs.
			// The ManagedHISMTag check limits removal to components we actually created.
			TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents;
			FamilyTypeActor->GetComponents(ISMComponents);

			for (UInstancedStaticMeshComponent* ISMComponent : ISMComponents)
			{
				if (!IsValid(ISMComponent) || !ISMComponent->ComponentTags.Contains(ManagedHISMTag))
				{
					continue;
				}

				FamilyTypeActor->Modify();
				ISMComponent->Modify();
				FamilyTypeActor->RemoveInstanceComponent(ISMComponent);
				ISMComponent->DestroyComponent();
			}
		}

		static void ClearManagedFamilyTypeActors(AActor* ParentActor)
		{
			if (!IsValid(ParentActor))
			{
				return;
			}

			TArray<AActor*> DirectChildren;
			ParentActor->GetAttachedActors(DirectChildren, true, false);

			for (AActor* ChildActor : DirectChildren)
			{
				if (!IsValid(ChildActor) || !ChildActor->Tags.Contains(ManagedFamilyTypeTag))
				{
					continue;
				}

				ChildActor->Modify();
				ChildActor->Destroy();
			}
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

		static TArray<AActor*> CollectSortedDeleteCandidates(const TArray<TPair<AActor*, AActor*>>& ConvertedActorsAndCleanupBoundaries)
		{
			TSet<AActor*> SourceActors;
			TSet<AActor*> CandidateIntermediateActors;

			for (const TPair<AActor*, AActor*>& Entry : ConvertedActorsAndCleanupBoundaries)
			{
				AActor* SourceActor = Entry.Key;
				AActor* CleanupBoundaryActor = Entry.Value;
				if (!IsValid(SourceActor))
				{
					continue;
				}

				SourceActors.Add(SourceActor);

				for (AActor* Ancestor = SourceActor->GetAttachParentActor(); IsValid(Ancestor) && Ancestor != CleanupBoundaryActor; Ancestor = Ancestor->GetAttachParentActor())
				{
					CandidateIntermediateActors.Add(Ancestor);
				}
			}

			struct FActorAndDepth
			{
				TObjectPtr<AActor> Actor = nullptr;
				int32 Depth = 0;
			};

			TSet<AActor*> DeleteCandidates = SourceActors;
			DeleteCandidates.Append(CandidateIntermediateActors);

			TArray<FActorAndDepth> SortedCandidates;
			for (AActor* CandidateActor : DeleteCandidates)
			{
				if (IsValid(CandidateActor))
				{
					SortedCandidates.Add({ CandidateActor, GetActorDepth(CandidateActor) });
				}
			}

			SortedCandidates.Sort([](const FActorAndDepth& Left, const FActorAndDepth& Right)
			{
				return Left.Depth > Right.Depth;
			});

			TSet<AActor*> DeletableActors;
			for (const FActorAndDepth& Entry : SortedCandidates)
			{
				AActor* CandidateActor = Entry.Actor.Get();
				if (!IsValid(CandidateActor))
				{
					continue;
				}

				const bool bIsSourceActor = SourceActors.Contains(CandidateActor);
				if (bIsSourceActor)
				{
					DeletableActors.Add(CandidateActor);
					continue;
				}

				if (!bIsSourceActor)
				{
					UStaticMeshComponent* IgnoredComponent = nullptr;
					EActorEligibilityFailureReason IgnoredReason = EActorEligibilityFailureReason::None;
					if (TryGetEligibleStaticMeshComponent(CandidateActor, IgnoredComponent, IgnoredReason))
					{
						continue;
					}
				}

				TArray<AActor*> DirectChildren;
				CandidateActor->GetAttachedActors(DirectChildren, true, false);

				bool bAllChildrenDeleting = true;
				for (AActor* DirectChild : DirectChildren)
				{
					if (!DeleteCandidates.Contains(DirectChild) || !DeletableActors.Contains(DirectChild))
					{
						bAllChildrenDeleting = false;
						break;
					}
				}

				if (bAllChildrenDeleting)
				{
					DeletableActors.Add(CandidateActor);
				}
			}

			TArray<FActorAndDepth> SortedActorsToDelete;
			SortedActorsToDelete.Reserve(DeletableActors.Num());
			for (AActor* ActorToDelete : DeletableActors)
			{
				if (IsValid(ActorToDelete))
				{
					SortedActorsToDelete.Add({ ActorToDelete, GetActorDepth(ActorToDelete) });
				}
			}

			SortedActorsToDelete.Sort([](const FActorAndDepth& Left, const FActorAndDepth& Right)
			{
				return Left.Depth > Right.Depth;
			});

			TArray<AActor*> ActorsToDelete;
			ActorsToDelete.Reserve(SortedActorsToDelete.Num());
			for (const FActorAndDepth& Entry : SortedActorsToDelete)
			{
				ActorsToDelete.Add(Entry.Actor.Get());
			}

			return ActorsToDelete;
		}
	}

	void CollectActorsFromRoots(const TArray<AActor*>& RootActors, TArray<AActor*>& OutActors)
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

			TArray<AActor*> DirectChildren;
			Actor->GetAttachedActors(DirectChildren, true, false);
			PendingActors.Append(DirectChildren);
		}
	}

	void FinalizeSummary(FConVerseHISMCreationResult& Result)
	{
		Result.Summary = FString::Printf(
			TEXT("Considered %d actor(s), created %d HISM component(s), converted %d source actor(s). ")
			TEXT("Skipped %d actor(s): %d had no eligible static mesh component, %d had multiple eligible static mesh components. ")
			TEXT("%d HISM component creation(s) failed. %d converted actor delete(s) failed."),
			Result.ActorsConsidered,
			Result.HISMActorsCreated,
			Result.SourceActorsConverted,
			Result.SkippedActors,
			Result.ActorsWithNoEligibleStaticMesh,
			Result.ActorsWithMultipleEligibleStaticMeshes,
			Result.FailedHISMActorCreations,
			Result.FailedSourceActorDeletes);
	}

	FBuildOutput BuildManagedHISMs(const TArray<AActor*>& Actors, const FString& ComponentNamePrefix)
	{
		FBuildOutput Output;
		Output.Result.ActorsConsidered = Actors.Num();
		UE_LOG(LogConVerseHISM, Display, TEXT("BuildManagedHISMs: starting with %d actor(s)."), Actors.Num());

		TMap<FHISMGroupKey, FHISMGroupData> Groups;
		TMap<UStaticMesh*, FString> SignatureCache;
		TSet<AActor*> FamilyTypeActors;
		TSet<AActor*> ClearedBoundaryActors;
		FFamilyTypeLookupCache FamilyTypeLookupCache;

		for (AActor* Actor : Actors)
		{
			UStaticMeshComponent* MeshComponent = nullptr;
			EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
			if (!TryGetEligibleStaticMeshComponent(Actor, MeshComponent, FailureReason))
			{
				if (FailureReason == EActorEligibilityFailureReason::MultipleEligibleStaticMeshes)
				{
					++Output.Result.SkippedActors;
					++Output.Result.ActorsWithMultipleEligibleStaticMeshes;
				}
				else if (FailureReason == EActorEligibilityFailureReason::NoEligibleStaticMesh && !HasDirectAttachedChildren(Actor))
				{
					++Output.Result.SkippedActors;
					++Output.Result.ActorsWithNoEligibleStaticMesh;
				}
				continue;
			}

			AActor* CleanupBoundaryActor = FindCleanupBoundaryActor(Actor, FamilyTypeLookupCache);
			AActor* HostedFamilyWrapperActor = FindHostedFamilyWrapperActor(Actor, FamilyTypeLookupCache);
			if (IsValid(CleanupBoundaryActor) && !ClearedBoundaryActors.Contains(CleanupBoundaryActor))
			{
				ClearManagedFamilyTypeActors(CleanupBoundaryActor);
				ClearManagedHISMComponents(CleanupBoundaryActor);
				ClearedBoundaryActors.Add(CleanupBoundaryActor);
			}

			AActor* FamilyTypeActor = FindOrCreateManagedFamilyTypeActor(
				CleanupBoundaryActor,
				Actor,
				GetManagedFamilyLabel(Actor, MeshComponent, HostedFamilyWrapperActor),
				MeshComponent->Mobility,
				FamilyTypeLookupCache);
			if (!IsValid(FamilyTypeActor))
			{
				++Output.Result.SkippedActors;
				++Output.Result.ActorsWithNoEligibleStaticMesh;
				continue;
			}

			FamilyTypeActors.Add(FamilyTypeActor);

			const FHISMGroupKey GroupKey = BuildGroupKey(FamilyTypeActor, MeshComponent, SignatureCache);
			FHISMGroupData& GroupData = Groups.FindOrAdd(GroupKey);

			FSourceActorData& Entry = GroupData.Actors.AddDefaulted_GetRef();
			Entry.Actor = Actor;
			Entry.Component = MeshComponent;
			Entry.CleanupBoundaryActor = CleanupBoundaryActor;
			Entry.WorldTransform = GetSourceWorldTransform(Actor, MeshComponent);

			// Track the canonical mesh: alphabetically first asset path among equivalent meshes
			// in this group so the choice is deterministic across reruns.
			UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
			if (!IsValid(GroupData.CanonicalMesh) ||
				Mesh->GetPathName() < GroupData.CanonicalMesh->GetPathName())
			{
				GroupData.CanonicalMesh = Mesh;
			}
		}

		UE_LOG(
			LogConVerseHISM,
			Display,
			TEXT("BuildManagedHISMs: grouped %d actor(s) into %d HISM group(s) across %d family type actor(s). Skipped %d actor(s)."),
			Output.Result.ActorsConsidered - Output.Result.SkippedActors,
			Groups.Num(),
			FamilyTypeActors.Num(),
			Output.Result.SkippedActors);

		UE_LOG(
			LogConVerseHISM,
			Display,
			TEXT("BuildManagedHISMs: cleared prior managed outputs on %d cleanup boundary actor(s) and will build %d family type actor(s)."),
			ClearedBoundaryActors.Num(),
			FamilyTypeActors.Num());

		TArray<TPair<AActor*, AActor*>> ConvertedActorsAndCleanupBoundaries;
		int32 GroupIndex = 0;
		int32 ProcessedGroupCount = 0;

		for (TPair<FHISMGroupKey, FHISMGroupData>& GroupPair : Groups)
		{
			++ProcessedGroupCount;
			if ((ProcessedGroupCount % 250) == 0)
			{
				UE_LOG(
					LogConVerseHISM,
					Display,
					TEXT("BuildManagedHISMs: processed %d / %d group(s). Converted %d source actor(s) so far."),
					ProcessedGroupCount,
					Groups.Num(),
					Output.Result.SourceActorsConverted);
			}

			const FHISMGroupKey& GroupKey = GroupPair.Key;
			FHISMGroupData& GroupData = GroupPair.Value;
			const TArray<FSourceActorData>& SourceActors = GroupData.Actors;
			UStaticMesh* CanonicalMesh = GroupData.CanonicalMesh.Get();

			if (!IsValid(GroupKey.FamilyTypeActor.Get()) || !IsValid(CanonicalMesh) || SourceActors.IsEmpty())
			{
				continue;
			}

			const EComponentMobility::Type SourceMobility = SourceActors[0].Component->Mobility;
			USceneComponent* RootComponent = EnsureRootComponent(GroupKey.FamilyTypeActor.Get(), SourceMobility);
			if (RootComponent == nullptr)
			{
				++Output.Result.FailedHISMActorCreations;
				continue;
			}

			GroupKey.FamilyTypeActor->Modify();

			const FString BaseName = FString::Printf(
				TEXT("%s_%s_%03d"),
				ComponentNamePrefix.IsEmpty() ? TEXT("ISM") : *ComponentNamePrefix,
				*SanitizeNameFragment(CanonicalMesh->GetName()),
				++GroupIndex);
			const FName ComponentName = MakeUniqueObjectName(GroupKey.FamilyTypeActor.Get(), UInstancedStaticMeshComponent::StaticClass(), *BaseName);

			UInstancedStaticMeshComponent* ISMComponent =
				NewObject<UInstancedStaticMeshComponent>(
					GroupKey.FamilyTypeActor.Get(),
					UInstancedStaticMeshComponent::StaticClass(),
					ComponentName,
					RF_Transactional);
			GroupKey.FamilyTypeActor->AddInstanceComponent(ISMComponent);
			ISMComponent->ComponentTags.AddUnique(ManagedHISMTag);
			ISMComponent->SetMobility(SourceMobility);
			ISMComponent->SetupAttachment(RootComponent);
			ISMComponent->SetStaticMesh(CanonicalMesh);

			CopyRelevantComponentProperties(SourceActors[0].Component.Get(), ISMComponent);
			ISMComponent->RegisterComponent();

			if (!IsValid(ISMComponent) || ISMComponent->GetAttachParent() != RootComponent)
			{
				GroupKey.FamilyTypeActor->RemoveInstanceComponent(ISMComponent);
				ISMComponent->DestroyComponent();
				++Output.Result.FailedHISMActorCreations;
				continue;
			}

			for (const FSourceActorData& SourceActor : SourceActors)
			{
				ISMComponent->AddInstance(SourceActor.WorldTransform, true);
				ConvertedActorsAndCleanupBoundaries.Emplace(SourceActor.Actor.Get(), SourceActor.CleanupBoundaryActor.Get());
				++Output.Result.SourceActorsConverted;
			}

			++Output.Result.HISMActorsCreated;
		}

		UE_LOG(
			LogConVerseHISM,
			Display,
			TEXT("BuildManagedHISMs: built %d HISM component(s), converted %d source actor(s), failed %d component creation(s)."),
			Output.Result.HISMActorsCreated,
			Output.Result.SourceActorsConverted,
			Output.Result.FailedHISMActorCreations);

		UE_LOG(LogConVerseHISM, Display, TEXT("BuildManagedHISMs: collecting delete candidates from %d converted actor(s)."), ConvertedActorsAndCleanupBoundaries.Num());
		const TArray<AActor*> ActorsToDelete = CollectSortedDeleteCandidates(ConvertedActorsAndCleanupBoundaries);
		Output.ObjectsToDelete.Reserve(ActorsToDelete.Num());
		for (AActor* ActorToDelete : ActorsToDelete)
		{
			Output.ObjectsToDelete.Add(ActorToDelete);
		}
		UE_LOG(LogConVerseHISM, Display, TEXT("BuildManagedHISMs: collected %d actor(s) for deletion."), Output.ObjectsToDelete.Num());

		FinalizeSummary(Output.Result);
		UE_LOG(LogConVerseHISM, Display, TEXT("BuildManagedHISMs: %s"), *Output.Result.Summary);
		return Output;
	}
}
