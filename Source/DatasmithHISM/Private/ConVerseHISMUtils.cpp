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
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "ConVerseHISMUtils"

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

		struct FSourceActorData
		{
			TObjectPtr<AActor> Actor = nullptr;
			TObjectPtr<UStaticMeshComponent> Component = nullptr;
			TObjectPtr<AActor> CleanupBoundaryActor = nullptr;
			FTransform WorldTransform = FTransform::Identity;
		};

		// Per-group data: the list of source actors plus the canonical mesh chosen for the ISM.
		// The canonical mesh is the one whose asset path sorts first among all geometrically
		// equivalent meshes in the group, giving a deterministic result across reruns.
		struct FHISMGroupData
		{
			TArray<FSourceActorData> Actors;
			TObjectPtr<UStaticMesh> CanonicalMesh = nullptr;
		};

		// Analysis-only group key for AnalyzeManagedHISMCandidates. Uses the conceptual
		// (CleanupBoundary, FamilyLabel) pair instead of a managed actor pointer so no
		// actors are created or modified during the analysis pass.
		// Must be defined at namespace scope (not function-local) for MSVC ADL to resolve
		// GetTypeHash when used as a TMap key.
		struct FHISMAnalysisKey
		{
			TObjectPtr<AActor> CleanupBoundary = nullptr;
			FString FamilyLabel;
			FString GeometrySignature;
			FString MaterialSignature;

			bool operator==(const FHISMAnalysisKey& Other) const
			{
				return CleanupBoundary == Other.CleanupBoundary
					&& FamilyLabel == Other.FamilyLabel
					&& GeometrySignature == Other.GeometrySignature
					&& MaterialSignature == Other.MaterialSignature;
			}

			friend uint32 GetTypeHash(const FHISMAnalysisKey& Key)
			{
				return HashCombine(
					HashCombine(GetTypeHash(Key.CleanupBoundary), GetTypeHash(Key.FamilyLabel)),
					HashCombine(GetTypeHash(Key.GeometrySignature), GetTypeHash(Key.MaterialSignature)));
			}
		};

		struct FHISMAnalysisGroupData
		{
			int32 ActorCount = 0;
			TObjectPtr<UStaticMesh> CanonicalMesh = nullptr;
		};

		enum class EActorEligibilityFailureReason : uint8
		{
			None,
			NoEligibleStaticMesh,
			MultipleEligibleStaticMeshes,
			HasBehaviorPayload
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

			// Optional storey-boundary patterns. When non-empty, FindCleanupBoundaryActor walks
			// past the default first-non-mesh boundary and stops at the first ancestor whose label
			// contains one of these substrings (case-insensitive). Allows ISMs to be grouped per
			// IFC building storey rather than per space, reducing managed actor count in large
			// multi-storey buildings. Empty = current behavior (first non-mesh ancestor).
			TArray<FString> StoreyBoundaryPatterns;
		};

		static USceneComponent* EnsureRootComponent(AActor* Actor, const EComponentMobility::Type Mobility);
		static FString GetActorFamilyLabel(AActor* Actor);

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

		/**
		 * Conversion deletes the source actor, so anything it carries beyond its mesh is destroyed.
		 * An actor is treated as behavior-bearing when it has a non-native (Blueprint-added) component,
		 * a component that is neither a plain scene component nor the mesh being instanced, or asset
		 * user data. Transform-only scaffolding is not behavior and does not block conversion.
		 */
		static bool HasBehaviorPayload(const AActor* Actor, const UStaticMeshComponent* EligibleComponent)
		{
			if (!IsValid(Actor))
			{
				return false;
			}

			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (const UActorComponent* Component : Components)
			{
				if (!IsValid(Component)
					|| Component == EligibleComponent
					|| Component->IsEditorOnly()
					|| Component->IsVisualizationComponent())
				{
					continue;
				}

				// Blueprint-added components always imply authored intent beyond raw geometry.
				if (Component->CreationMethod != EComponentCreationMethod::Native)
				{
					return true;
				}

				// A bare USceneComponent is transform scaffolding. Any other component type
				// (audio, light, particle, child actor, movement, custom) carries behavior.
				if (Component->GetClass() != USceneComponent::StaticClass())
				{
					return true;
				}
			}

			if (const IInterface_AssetUserData* UserDataInterface = Cast<const IInterface_AssetUserData>(Actor))
			{
				const TArray<UAssetUserData*>* UserDataArray =
					const_cast<IInterface_AssetUserData*>(UserDataInterface)->GetAssetUserDataArray();
				if (UserDataArray != nullptr && UserDataArray->Num() > 0)
				{
					return true;
				}
			}

			return false;
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

			if (HasBehaviorPayload(Actor, EligibleComponents[0]))
			{
				OutFailureReason = EActorEligibilityFailureReason::HasBehaviorPayload;
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

			// Phase 1: find the first non-mesh ancestor — always the baseline boundary.
			AActor* FirstNonMeshAncestor = nullptr;
			while (IsValid(CurrentAncestor))
			{
				UStaticMeshComponent* MeshComponent = nullptr;
				EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
				if (!TryGetEligibleStaticMeshComponent(CurrentAncestor, MeshComponent, FailureReason))
				{
					FirstNonMeshAncestor = CurrentAncestor;
					break;
				}
				CurrentAncestor = CurrentAncestor->GetAttachParentActor();
			}

			// Without storey patterns fall through to the original single-pass behavior.
			if (Cache.StoreyBoundaryPatterns.IsEmpty() || FirstNonMeshAncestor == nullptr)
			{
				Cache.CleanupBoundaryActorBySourceActor.Add(GeometryActor, FirstNonMeshAncestor);
				return FirstNonMeshAncestor;
			}

			// Phase 2: storey-boundary walk. Check whether any ancestor (starting from
			// FirstNonMeshAncestor and walking upward) matches a pattern. The closest match
			// wins so that grouping is as tight as possible while still respecting storeys.
			auto MatchesAnyPattern = [&](AActor* Actor) -> bool
			{
				if (!IsValid(Actor))
				{
					return false;
				}
				const FString Label = GetActorFamilyLabel(Actor);
				for (const FString& Pattern : Cache.StoreyBoundaryPatterns)
				{
					if (!Pattern.IsEmpty() && Label.Contains(Pattern, ESearchCase::IgnoreCase))
					{
						return true;
					}
				}
				return false;
			};

			// Check FirstNonMeshAncestor first; if it matches we're done.
			if (MatchesAnyPattern(FirstNonMeshAncestor))
			{
				Cache.CleanupBoundaryActorBySourceActor.Add(GeometryActor, FirstNonMeshAncestor);
				return FirstNonMeshAncestor;
			}

			// Walk further up until we hit a pattern match. Fall back to FirstNonMeshAncestor
			// if no ancestor matches any pattern, preserving pre-A06 behavior.
			AActor* StoreyBoundary = nullptr;
			for (AActor* Ancestor = FirstNonMeshAncestor->GetAttachParentActor();
				IsValid(Ancestor);
				Ancestor = Ancestor->GetAttachParentActor())
			{
				if (MatchesAnyPattern(Ancestor))
				{
					StoreyBoundary = Ancestor;
					break;
				}
			}

			AActor* BoundaryActor = IsValid(StoreyBoundary) ? StoreyBoundary : FirstNonMeshAncestor;
			Cache.CleanupBoundaryActorBySourceActor.Add(GeometryActor, BoundaryActor);
			return BoundaryActor;
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

			// BuildMaterialSignature groups on effective materials via GetMaterial, so every actor in
			// this group shares the source component's material set. Without copying them the created
			// component would silently fall back to the mesh defaults and discard per-component
			// overrides that the grouping key deliberately preserved.
			const int32 SourceMaterialCount = SourceComponent->GetNumMaterials();
			for (int32 SlotIndex = 0; SlotIndex < SourceMaterialCount; ++SlotIndex)
			{
				UMaterialInterface* SourceMaterial = SourceComponent->GetMaterial(SlotIndex);
				if (SourceMaterial != TargetComponent->GetMaterial(SlotIndex))
				{
					TargetComponent->SetMaterial(SlotIndex, SourceMaterial);
				}
			}
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

	FConVerseHISMAnalysisResult AnalyzeManagedHISMCandidates(
		const TArray<AActor*>& Actors,
		bool bUseHISM,
		int32 MinInstanceCount,
		bool bAutoDetectFromNanite,
		EConVerseGroupingMode GroupingMode,
		const TArray<FString>& StoreyBoundaryPatterns)
	{
		FConVerseHISMAnalysisResult Result;
		Result.ActorsConsidered = Actors.Num();

		TMap<FHISMAnalysisKey, FHISMAnalysisGroupData> Groups;
		TMap<UStaticMesh*, FString> SignatureCache;
		FFamilyTypeLookupCache FamilyTypeLookupCache;
		FamilyTypeLookupCache.StoreyBoundaryPatterns = StoreyBoundaryPatterns;

		for (AActor* Actor : Actors)
		{
			UStaticMeshComponent* MeshComponent = nullptr;
			EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
			if (!TryGetEligibleStaticMeshComponent(Actor, MeshComponent, FailureReason))
			{
				if (FailureReason == EActorEligibilityFailureReason::MultipleEligibleStaticMeshes
					|| FailureReason == EActorEligibilityFailureReason::HasBehaviorPayload
					|| (FailureReason == EActorEligibilityFailureReason::NoEligibleStaticMesh && !HasDirectAttachedChildren(Actor)))
				{
					++Result.SkippedActors;
				}
				continue;
			}

			AActor* CleanupBoundaryActor = FindCleanupBoundaryActor(Actor, FamilyTypeLookupCache);
			AActor* HostedFamilyWrapperActor = FindHostedFamilyWrapperActor(Actor, FamilyTypeLookupCache);

			const FString FamilyLabel = (GroupingMode == EConVerseGroupingMode::MaximumOptimization)
				? TEXT("ISM")
				: GetManagedFamilyLabel(Actor, MeshComponent, HostedFamilyWrapperActor);

			FHISMAnalysisKey Key;
			Key.CleanupBoundary = CleanupBoundaryActor;
			Key.FamilyLabel = FamilyLabel;
			Key.GeometrySignature = GetCachedMeshSignature(MeshComponent->GetStaticMesh(), SignatureCache);
			Key.MaterialSignature = BuildMaterialSignature(MeshComponent);

			FHISMAnalysisGroupData& GroupData = Groups.FindOrAdd(Key);
			++GroupData.ActorCount;

			UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
			if (IsValid(Mesh) && (!IsValid(GroupData.CanonicalMesh) || Mesh->GetPathName() < GroupData.CanonicalMesh->GetPathName()))
			{
				GroupData.CanonicalMesh = Mesh;
			}
		}

		for (const TPair<FHISMAnalysisKey, FHISMAnalysisGroupData>& Pair : Groups)
		{
			const int32 Count = Pair.Value.ActorCount;
			if (Count >= MinInstanceCount)
			{
				++Result.GroupsAboveThreshold;
				Result.ActorsWouldBeConverted += Count;
			}
			else
			{
				++Result.GroupsBelowThreshold;
				Result.ActorsInSmallGroups += Count;
			}
		}

		Result.Summary = FString::Printf(
			TEXT("Analyzed %d actor(s). Would create %d ISM group(s) converting %d actor(s). ")
			TEXT("%d group(s) below threshold (%d actor(s) left in place). %d actor(s) skipped."),
			Result.ActorsConsidered,
			Result.GroupsAboveThreshold,
			Result.ActorsWouldBeConverted,
			Result.GroupsBelowThreshold,
			Result.ActorsInSmallGroups,
			Result.SkippedActors);

		return Result;
	}

	bool EnableNaniteIfNeeded(UStaticMesh* Mesh)
	{
		if (!IsValid(Mesh) || Mesh->GetNaniteSettings().bEnabled)
		{
			return false;
		}

		Mesh->Modify();
		FMeshNaniteSettings NaniteSettings = Mesh->GetNaniteSettings();
		NaniteSettings.bEnabled = true;
		Mesh->SetNaniteSettings(NaniteSettings);
		Mesh->PostEditChange();
		return true;
	}

	void FinalizeSummary(FConVerseHISMCreationResult& Result)
	{
		// Include ISM/HISM type breakdown only when both types were created (bAutoDetectFromNanite path).
		const bool bMixed = Result.ISMOnlyComponentsCreated > 0 && Result.HISMOnlyComponentsCreated > 0;
		const FString ComponentDetail = bMixed
			? FString::Printf(TEXT("%d ISM + %d HISM"), Result.ISMOnlyComponentsCreated, Result.HISMOnlyComponentsCreated)
			: FString::FromInt(Result.ISMComponentsCreated);

		Result.Summary = FString::Printf(
			TEXT("Considered %d actor(s), created %s component(s), converted %d source actor(s), left %d single-instance actor(s) in place. ")
			TEXT("Skipped %d actor(s): %d had no eligible static mesh component, %d had multiple eligible static mesh components, %d carried behavior beyond their mesh. ")
			TEXT("%d ISM component creation(s) failed. %d instance addition(s) failed and those source actors were kept. %d converted actor delete(s) failed."),
			Result.ActorsConsidered,
			*ComponentDetail,
			Result.SourceActorsConverted,
			Result.ActorsInSingleActorGroups,
			Result.SkippedActors,
			Result.ActorsWithNoEligibleStaticMesh,
			Result.ActorsWithMultipleEligibleStaticMeshes,
			Result.ActorsWithBehaviorPayload,
			Result.FailedISMComponentCreations,
			Result.FailedInstanceAdditions,
			Result.FailedSourceActorDeletes);
	}

	FBuildOutput BuildManagedHISMs(const TArray<AActor*>& Actors, const FString& ComponentNamePrefix, bool bUseHISM, int32 MinInstanceCount, bool bAutoDetectFromNanite, EConVerseGroupingMode GroupingMode, const TArray<FString>& StoreyBoundaryPatterns)
	{
		FBuildOutput Output;
		Output.Result.ActorsConsidered = Actors.Num();
		UE_LOG(LogConVerseHISM, Display, TEXT("BuildManagedHISMs: starting with %d actor(s)."), Actors.Num());

		TMap<FHISMGroupKey, FHISMGroupData> Groups;
		TMap<UStaticMesh*, FString> SignatureCache;
		TSet<AActor*> FamilyTypeActors;
		TSet<AActor*> ClearedBoundaryActors;
		FFamilyTypeLookupCache FamilyTypeLookupCache;
		FamilyTypeLookupCache.StoreyBoundaryPatterns = StoreyBoundaryPatterns;

		{
			FScopedSlowTask GroupingTask(
				static_cast<float>(Actors.Num()),
				LOCTEXT("GroupingActors", "Grouping actors by geometry and material..."));
			GroupingTask.MakeDialog(true);

		for (AActor* Actor : Actors)
		{
			if (GroupingTask.ShouldCancel())
			{
				Output.Result.bWasCancelled = true;
				break;
			}

			GroupingTask.EnterProgressFrame(1.f, FText::Format(
				LOCTEXT("GroupingActor", "Grouping: {0}"),
				FText::FromString(IsValid(Actor) ? Actor->GetActorLabel() : TEXT(""))));
			UStaticMeshComponent* MeshComponent = nullptr;
			EActorEligibilityFailureReason FailureReason = EActorEligibilityFailureReason::None;
			if (!TryGetEligibleStaticMeshComponent(Actor, MeshComponent, FailureReason))
			{
				if (FailureReason == EActorEligibilityFailureReason::MultipleEligibleStaticMeshes)
				{
					++Output.Result.SkippedActors;
					++Output.Result.ActorsWithMultipleEligibleStaticMeshes;
				}
				else if (FailureReason == EActorEligibilityFailureReason::HasBehaviorPayload)
				{
					++Output.Result.SkippedActors;
					++Output.Result.ActorsWithBehaviorPayload;
					UE_LOG(
						LogConVerseHISM,
						Verbose,
						TEXT("BuildManagedHISMs: skipped '%s' because it carries behavior beyond its mesh."),
						*Actor->GetActorLabel());
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

			// In MaximumOptimization mode every group under the same cleanup boundary shares
			// one managed actor (family identity is ignored). PreserveBIMHierarchy (default)
			// keeps each Revit/IFC family type in its own managed actor.
			const FString FamilyLabel = (GroupingMode == EConVerseGroupingMode::MaximumOptimization)
				? (ComponentNamePrefix.IsEmpty() ? TEXT("ISM") : ComponentNamePrefix)
				: GetManagedFamilyLabel(Actor, MeshComponent, HostedFamilyWrapperActor);

			AActor* FamilyTypeActor = FindOrCreateManagedFamilyTypeActor(
				CleanupBoundaryActor,
				Actor,
				FamilyLabel,
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
		} // end grouping loop
		} // end grouping FScopedSlowTask scope

		UE_LOG(
			LogConVerseHISM,
			Display,
			TEXT("BuildManagedHISMs: grouped %d actor(s) into %d ISM group(s) across %d family type actor(s). Skipped %d actor(s)."),
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

		{
			FScopedSlowTask BuildTask(
				static_cast<float>(Groups.Num()),
				LOCTEXT("BuildingISMs", "Building ISM components..."));
			BuildTask.MakeDialog(true);

		for (TPair<FHISMGroupKey, FHISMGroupData>& GroupPair : Groups)
		{
			if (BuildTask.ShouldCancel())
			{
				Output.Result.bWasCancelled = true;
				break;
			}

			BuildTask.EnterProgressFrame(1.f);
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

			// Leave groups below the threshold as plain static mesh actors — a small-instance
			// ISM can have higher overhead than the source components.
			if (SourceActors.Num() < MinInstanceCount)
			{
				Output.Result.ActorsInSingleActorGroups += SourceActors.Num();
				continue;
			}

			const EComponentMobility::Type SourceMobility = SourceActors[0].Component->Mobility;
			USceneComponent* RootComponent = EnsureRootComponent(GroupKey.FamilyTypeActor.Get(), SourceMobility);
			if (RootComponent == nullptr)
			{
				++Output.Result.FailedISMComponentCreations;
				continue;
			}

			GroupKey.FamilyTypeActor->Modify();

			const FString BaseName = FString::Printf(
				TEXT("%s_%s_%03d"),
				ComponentNamePrefix.IsEmpty() ? TEXT("ISM") : *ComponentNamePrefix,
				*SanitizeNameFragment(CanonicalMesh->GetName()),
				++GroupIndex);
			const FName ComponentName = MakeUniqueObjectName(GroupKey.FamilyTypeActor.Get(), UInstancedStaticMeshComponent::StaticClass(), *BaseName);

			// Determine component class: explicit override wins; auto-detection checks
			// whether the canonical mesh has Nanite enabled (ISM preferred for Nanite;
			// HISM beneficial for non-Nanite where hierarchical culling helps).
			const bool bResolvedUseHISM = bAutoDetectFromNanite
				? (IsValid(CanonicalMesh) && !CanonicalMesh->GetNaniteSettings().bEnabled)
				: bUseHISM;
			const UClass* ComponentClass = bResolvedUseHISM
				? UHierarchicalInstancedStaticMeshComponent::StaticClass()
				: UInstancedStaticMeshComponent::StaticClass();
			if (!bResolvedUseHISM)
			{
				EnableNaniteIfNeeded(CanonicalMesh);
			}

			UInstancedStaticMeshComponent* ISMComponent =
				NewObject<UInstancedStaticMeshComponent>(
					GroupKey.FamilyTypeActor.Get(),
					ComponentClass,
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
				++Output.Result.FailedISMComponentCreations;
				continue;
			}

			for (const FSourceActorData& SourceActor : SourceActors)
			{
				// AddInstance returns INDEX_NONE on failure. Only queue the source actor for
				// deletion once its geometry is genuinely represented by an instance, otherwise
				// the original would be destroyed with nothing standing in for it.
				const int32 AddedInstanceIndex = ISMComponent->AddInstance(SourceActor.WorldTransform, true);
				if (AddedInstanceIndex == INDEX_NONE)
				{
					++Output.Result.FailedInstanceAdditions;
					UE_LOG(
						LogConVerseHISM,
						Warning,
						TEXT("BuildManagedHISMs: AddInstance failed for '%s'; the source actor was kept and not converted."),
						IsValid(SourceActor.Actor.Get()) ? *SourceActor.Actor->GetActorLabel() : TEXT("<invalid actor>"));
					continue;
				}

				ConvertedActorsAndCleanupBoundaries.Emplace(SourceActor.Actor.Get(), SourceActor.CleanupBoundaryActor.Get());
				++Output.Result.SourceActorsConverted;
			}

			// If every AddInstance failed the component is empty and serves no purpose,
			// so remove it rather than leaving an inert managed component behind.
			if (ISMComponent->GetInstanceCount() == 0)
			{
				GroupKey.FamilyTypeActor->RemoveInstanceComponent(ISMComponent);
				ISMComponent->DestroyComponent();
				++Output.Result.FailedISMComponentCreations;
				continue;
			}

			++Output.Result.ISMComponentsCreated;
			if (bResolvedUseHISM)
			{
				++Output.Result.HISMOnlyComponentsCreated;
			}
			else
			{
				++Output.Result.ISMOnlyComponentsCreated;
			}
		} // end build loop
		} // end build FScopedSlowTask scope

		// Destroy managed family-type actors that were created during grouping but never received
		// an ISM component, so no empty shells are left. This applies on the normal path too:
		// below-threshold groups and failed component creations can both leave a family actor empty.
		for (AActor* FamilyTypeActor : FamilyTypeActors)
		{
			if (!IsValid(FamilyTypeActor))
			{
				continue;
			}

			TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents;
			FamilyTypeActor->GetComponents(ISMComponents);

			bool bHasManagedComponent = false;
			for (const UInstancedStaticMeshComponent* ISMComponent : ISMComponents)
			{
				if (IsValid(ISMComponent) && ISMComponent->ComponentTags.Contains(ManagedHISMTag))
				{
					bHasManagedComponent = true;
					break;
				}
			}

			// Only remove actors this operation created and that own nothing else, so a
			// pre-existing actor reused as a family parent is never destroyed.
			if (!bHasManagedComponent && !HasDirectAttachedChildren(FamilyTypeActor))
			{
				FamilyTypeActor->Modify();
				FamilyTypeActor->Destroy();
			}
		}

		UE_LOG(
			LogConVerseHISM,
			Display,
			TEXT("BuildManagedHISMs: built %d ISM component(s), converted %d source actor(s), skipped %d single-instance group(s), failed %d component creation(s)."),
			Output.Result.ISMComponentsCreated,
			Output.Result.SourceActorsConverted,
			Output.Result.ActorsInSingleActorGroups,
			Output.Result.FailedISMComponentCreations);

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

#undef LOCTEXT_NAMESPACE
