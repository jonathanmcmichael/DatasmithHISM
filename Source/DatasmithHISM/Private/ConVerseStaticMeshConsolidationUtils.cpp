#include "ConVerseStaticMeshConsolidationUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "MeshDescription.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "StaticMeshAttributes.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "ConVerseStaticMeshConsolidationUtils"

namespace ConVerseStaticMeshConsolidation
{
	namespace
	{
		static void HashBytes(FMD5& Hash, const void* Data, const SIZE_T SizeInBytes)
		{
			Hash.Update(static_cast<const uint8*>(Data), static_cast<int32>(SizeInBytes));
		}

		template <typename ValueType>
		static void HashValue(FMD5& Hash, const ValueType& Value)
		{
			HashBytes(Hash, &Value, sizeof(ValueType));
		}

		static void HashString(FMD5& Hash, const FString& Value)
		{
			const int32 Length = Value.Len();
			HashValue(Hash, Length);
			if (Length > 0)
			{
				HashBytes(Hash, *Value, static_cast<SIZE_T>(Length) * sizeof(TCHAR));
			}
		}

		static uint32 BuildTriangleHash(
			const FMeshDescription& MeshDescription,
			const FStaticMeshConstAttributes& Attributes,
			const FTriangleID TriangleID,
			const TPolygonGroupAttributesConstRef<FName>& PolygonGroupMaterialSlotNames,
			const FVector3f& PositionOrigin)
		{
			uint32 TriangleHash = 0;

			const FPolygonGroupID PolygonGroupID = MeshDescription.GetTrianglePolygonGroup(TriangleID);
			const FString MaterialSlotName = PolygonGroupMaterialSlotNames.IsValid() ? PolygonGroupMaterialSlotNames[PolygonGroupID].ToString() : FString();
			TriangleHash = FCrc::MemCrc32(*MaterialSlotName, MaterialSlotName.Len() * sizeof(TCHAR), TriangleHash);

			const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
			const TVertexInstanceAttributesConstRef<FVector3f> VertexNormals = Attributes.GetVertexInstanceNormals();
			const TVertexInstanceAttributesConstRef<FVector2f> VertexUVs = Attributes.GetVertexInstanceUVs();
			const int32 UVChannelCount = VertexUVs.IsValid() ? VertexUVs.GetNumChannels() : 0;

			for (const FVertexInstanceID VertexInstanceID : MeshDescription.GetTriangleVertexInstances(TriangleID))
			{
				const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
				const FVector3f Position = VertexPositions[VertexID] - PositionOrigin;
				TriangleHash = FCrc::MemCrc32(&Position, sizeof(Position), TriangleHash);

				if (VertexNormals.IsValid())
				{
					const FVector3f Normal = VertexNormals[VertexInstanceID];
					TriangleHash = FCrc::MemCrc32(&Normal, sizeof(Normal), TriangleHash);
				}

				for (int32 UVChannelIndex = 0; UVChannelIndex < UVChannelCount; ++UVChannelIndex)
				{
					const FVector2f UV = VertexUVs.Get(VertexInstanceID, UVChannelIndex);
					TriangleHash = FCrc::MemCrc32(&UV, sizeof(UV), TriangleHash);
				}
			}

			return TriangleHash;
		}

		static bool BuildMeshSignature(UStaticMesh* StaticMesh, const FOptions& Options, FString& OutSignature, FText& OutReason)
		{
			OutSignature.Reset();

			if (!IsValid(StaticMesh))
			{
				OutReason = LOCTEXT("InvalidMesh", "Mesh is invalid.");
				return false;
			}

			if (StaticMesh->GetNumSourceModels() == 0)
			{
				OutReason = LOCTEXT("MissingSourceModels", "Mesh has no source models.");
				return false;
			}

			// Try LOD0; fall back to LOD1 if LOD0 is missing or empty (some Datasmith
			// imports omit the LOD0 source model but retain LOD1).
			int32 UsedLODIndex = 0;
			const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
			if ((MeshDescription == nullptr || MeshDescription->Triangles().Num() == 0)
				&& StaticMesh->GetNumSourceModels() > 1)
			{
				MeshDescription = StaticMesh->GetMeshDescription(1);
				UsedLODIndex = 1;
			}

			if (MeshDescription == nullptr)
			{
				OutReason = LOCTEXT("MissingMeshDescription", "Mesh has no accessible mesh description (LOD0 or LOD1).");
				return false;
			}

			if (MeshDescription->Triangles().Num() == 0)
			{
				OutReason = LOCTEXT("NoTriangles", "Mesh has no triangles (LOD0 or LOD1).");
				return false;
			}

			const FStaticMeshConstAttributes Attributes(*MeshDescription);
			const TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			const TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

			FBox3f PositionBounds(ForceInit);
			bool bHasVertexBounds = false;
			for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
			{
				PositionBounds += VertexPositions[VertexID];
				bHasVertexBounds = true;
			}

			const FVector3f PositionOrigin = bHasVertexBounds ? PositionBounds.GetCenter() : FVector3f::ZeroVector;

			TArray<uint32> TriangleHashes;
			TriangleHashes.Reserve(MeshDescription->Triangles().Num());
			for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
			{
				TriangleHashes.Add(BuildTriangleHash(*MeshDescription, Attributes, TriangleID, PolygonGroupMaterialSlotNames, PositionOrigin));
			}
			TriangleHashes.Sort();

			FMD5 SignatureHash;
			HashValue(SignatureHash, UsedLODIndex);
			HashValue(SignatureHash, StaticMesh->GetNumSourceModels());
			HashValue(SignatureHash, MeshDescription->Vertices().Num());
			HashValue(SignatureHash, MeshDescription->VertexInstances().Num());
			HashValue(SignatureHash, MeshDescription->Triangles().Num());
			HashValue(SignatureHash, MeshDescription->PolygonGroups().Num());

			const TVertexInstanceAttributesConstRef<FVector2f> VertexUVs = Attributes.GetVertexInstanceUVs();
			const int32 UVChannelCount = VertexUVs.IsValid() ? VertexUVs.GetNumChannels() : 0;
			HashValue(SignatureHash, UVChannelCount);

			for (const uint32 TriangleHash : TriangleHashes)
			{
				HashValue(SignatureHash, TriangleHash);
			}

			const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
			HashValue(SignatureHash, StaticMaterials.Num());
			for (const FStaticMaterial& StaticMaterial : StaticMaterials)
			{
				HashString(SignatureHash, StaticMaterial.ImportedMaterialSlotName.ToString());
				if (Options.bRequireMatchingMaterials)
				{
					HashString(SignatureHash, GetPathNameSafe(StaticMaterial.MaterialInterface));
				}
			}

			uint8 Digest[16];
			SignatureHash.Final(Digest);
			OutSignature = BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
			return true;
		}

		static void AddMeshIfValid(TSet<UStaticMesh*>& OutMeshes, UStaticMesh* StaticMesh)
		{
			if (IsValid(StaticMesh))
			{
				OutMeshes.Add(StaticMesh);
			}
		}

		static void CollectMeshesFromActor(AActor* Actor, TSet<UStaticMesh*>& OutMeshes)
		{
			if (!IsValid(Actor))
			{
				return;
			}

			TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
			Actor->GetComponents(MeshComponents);

			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if (MeshComponent != nullptr)
				{
					AddMeshIfValid(OutMeshes, MeshComponent->GetStaticMesh());
				}
			}
		}

		static void CollectMeshesFromActorHierarchy(AActor* RootActor, TSet<UStaticMesh*>& OutMeshes, TSet<AActor*>& VisitedActors)
		{
			if (!IsValid(RootActor) || VisitedActors.Contains(RootActor))
			{
				return;
			}

			VisitedActors.Add(RootActor);
			CollectMeshesFromActor(RootActor, OutMeshes);

			TArray<AActor*> AttachedActors;
			RootActor->GetAttachedActors(AttachedActors, true, false);
			for (AActor* AttachedActor : AttachedActors)
			{
				CollectMeshesFromActorHierarchy(AttachedActor, OutMeshes, VisitedActors);
			}
		}

		static void ReplaceStaticMeshReferencesOnActorHierarchy(
			AActor* RootActor,
			const TMap<UStaticMesh*, UStaticMesh*>& ReplacementMap,
			int32& InOutReplacedComponentCount,
			TSet<AActor*>& VisitedActors,
			TSet<UStaticMeshComponent*>& ProcessedComponents)
		{
			if (!IsValid(RootActor) || VisitedActors.Contains(RootActor))
			{
				return;
			}

			VisitedActors.Add(RootActor);

			TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
			RootActor->GetComponents(MeshComponents);
			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if (!IsValid(MeshComponent) || ProcessedComponents.Contains(MeshComponent))
				{
					continue;
				}

				ProcessedComponents.Add(MeshComponent);
				UStaticMesh* CurrentMesh = MeshComponent->GetStaticMesh();
				if (UStaticMesh* const* ReplacementMesh = ReplacementMap.Find(CurrentMesh))
				{
					MeshComponent->Modify();
					MeshComponent->SetStaticMesh(*ReplacementMesh);
					++InOutReplacedComponentCount;
				}
			}

			TArray<AActor*> AttachedActors;
			RootActor->GetAttachedActors(AttachedActors, true, false);
			for (AActor* AttachedActor : AttachedActors)
			{
				ReplaceStaticMeshReferencesOnActorHierarchy(
					AttachedActor,
					ReplacementMap,
					InOutReplacedComponentCount,
					VisitedActors,
					ProcessedComponents);
			}
		}

		static void CollectStaticMeshComponentsFromActorHierarchy(
			AActor* RootActor,
			TSet<UStaticMeshComponent*>& OutComponents,
			TSet<AActor*>& VisitedActors)
		{
			if (!IsValid(RootActor) || VisitedActors.Contains(RootActor))
			{
				return;
			}

			VisitedActors.Add(RootActor);

			TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
			RootActor->GetComponents(MeshComponents);
			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if (IsValid(MeshComponent))
				{
					OutComponents.Add(MeshComponent);
				}
			}

			TArray<AActor*> AttachedActors;
			RootActor->GetAttachedActors(AttachedActors, true, false);
			for (AActor* AttachedActor : AttachedActors)
			{
				CollectStaticMeshComponentsFromActorHierarchy(AttachedActor, OutComponents, VisitedActors);
			}
		}

		static void CollectScopedStaticMeshComponents(
			const TArray<UObject*>& Objects,
			TSet<UStaticMeshComponent*>& OutComponents,
			TSet<FName>& OutPackageNames)
		{
			TSet<AActor*> VisitedActors;

			for (UObject* Object : Objects)
			{
				if (!IsValid(Object))
				{
					continue;
				}

				if (UPackage* Package = Object->GetOutermost())
				{
					OutPackageNames.Add(Package->GetFName());
				}

				if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Object))
				{
					OutComponents.Add(MeshComponent);
				}
				else if (AActor* Actor = Cast<AActor>(Object))
				{
					CollectStaticMeshComponentsFromActorHierarchy(Actor, OutComponents, VisitedActors);
				}
			}

			for (UStaticMeshComponent* MeshComponent : OutComponents)
			{
				if (IsValid(MeshComponent))
				{
					if (UPackage* Package = MeshComponent->GetOutermost())
					{
						OutPackageNames.Add(Package->GetFName());
					}
				}
			}
		}
	}

	void CollectStaticMeshes(const TArray<UObject*>& Objects, TSet<UStaticMesh*>& OutMeshes)
	{
		TSet<AActor*> VisitedActors;

		for (UObject* Object : Objects)
		{
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
			{
				AddMeshIfValid(OutMeshes, StaticMesh);
			}
			else if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Object))
			{
				AddMeshIfValid(OutMeshes, MeshComponent->GetStaticMesh());
			}
			else if (AActor* Actor = Cast<AActor>(Object))
			{
				CollectMeshesFromActorHierarchy(Actor, OutMeshes, VisitedActors);
			}
		}
	}

	FAnalysis AnalyzeMeshes(const TSet<UStaticMesh*>& Meshes, const FOptions& Options)
	{
		FAnalysis Analysis;
		Analysis.MeshesConsidered = Meshes.Num();

		TMap<FString, TArray<UStaticMesh*>> SignatureToMeshes;
		for (UStaticMesh* StaticMesh : Meshes)
		{
			FString Signature;
			FText SkipReason;
			if (!BuildMeshSignature(StaticMesh, Options, Signature, SkipReason))
			{
				++Analysis.SkippedMeshCount;
				Analysis.SkippedMeshes.Add(StaticMesh, SkipReason);
				continue;
			}

			++Analysis.MeshesWithValidSignatures;
			SignatureToMeshes.FindOrAdd(Signature).Add(StaticMesh);
		}

		for (TPair<FString, TArray<UStaticMesh*>>& SignaturePair : SignatureToMeshes)
		{
			TArray<UStaticMesh*>& GroupMeshes = SignaturePair.Value;
			if (GroupMeshes.Num() < 2)
			{
				continue;
			}

			GroupMeshes.Sort([](const UStaticMesh& Left, const UStaticMesh& Right)
			{
				return Left.GetPathName() < Right.GetPathName();
			});

			FGroup& Group = Analysis.Groups.AddDefaulted_GetRef();
			Group.Signature = SignaturePair.Key;
			Group.CanonicalMesh = GroupMeshes[0];
			for (int32 MeshIndex = 1; MeshIndex < GroupMeshes.Num(); ++MeshIndex)
			{
				Group.DuplicateMeshes.Add(GroupMeshes[MeshIndex]);
			}
		}

		Analysis.Groups.Sort([](const FGroup& Left, const FGroup& Right)
		{
			return IsValid(Left.CanonicalMesh.Get()) && IsValid(Right.CanonicalMesh.Get())
				? Left.CanonicalMesh->GetPathName() < Right.CanonicalMesh->GetPathName()
				: IsValid(Left.CanonicalMesh.Get());
		});

		return Analysis;
	}

	FReferenceAudit AuditStaticMeshReplacementReferences(
		const TArray<UObject*>& Objects,
		const TMap<UStaticMesh*, UStaticMesh*>& CandidateReplacementMap)
	{
		FReferenceAudit Audit;
		if (CandidateReplacementMap.IsEmpty())
		{
			return Audit;
		}

		TSet<UStaticMeshComponent*> ScopedComponents;
		TSet<FName> ScopedPackageNames;
		CollectScopedStaticMeshComponents(Objects, ScopedComponents, ScopedPackageNames);

		TSet<UStaticMesh*> UnsafeMeshes;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* MeshComponent = *It;
			if (!IsValid(MeshComponent) || ScopedComponents.Contains(MeshComponent))
			{
				continue;
			}

			if (CandidateReplacementMap.Contains(MeshComponent->GetStaticMesh()))
			{
				UnsafeMeshes.Add(MeshComponent->GetStaticMesh());
				Audit.SkippedMeshes.FindOrAdd(MeshComponent->GetStaticMesh()) = FText::Format(
					LOCTEXT("LoadedExternalComponentReference",
						"Skipped because loaded static mesh component '{0}' is outside the supplied context."),
					FText::FromString(MeshComponent->GetPathName()));
			}
		}

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		for (const TPair<UStaticMesh*, UStaticMesh*>& Pair : CandidateReplacementMap)
		{
			UStaticMesh* DuplicateMesh = Pair.Key;
			if (!IsValid(DuplicateMesh) || UnsafeMeshes.Contains(DuplicateMesh))
			{
				continue;
			}

			UPackage* DuplicatePackage = DuplicateMesh->GetOutermost();
			if (DuplicatePackage == nullptr)
			{
				UnsafeMeshes.Add(DuplicateMesh);
				Audit.SkippedMeshes.FindOrAdd(DuplicateMesh) =
					LOCTEXT("MissingDuplicatePackage", "Skipped because the duplicate mesh package could not be determined.");
				continue;
			}

			TArray<FName> ReferencerPackageNames;
			AssetRegistry.GetReferencers(DuplicatePackage->GetFName(), ReferencerPackageNames);
			for (const FName ReferencerPackageName : ReferencerPackageNames)
			{
				if (!ScopedPackageNames.Contains(ReferencerPackageName))
				{
					UnsafeMeshes.Add(DuplicateMesh);
					Audit.SkippedMeshes.FindOrAdd(DuplicateMesh) = FText::Format(
						LOCTEXT("ExternalPackageReference",
							"Skipped because package '{0}' references this mesh outside the supplied context."),
						FText::FromName(ReferencerPackageName));
					break;
				}
			}
		}

		for (const TPair<UStaticMesh*, UStaticMesh*>& Pair : CandidateReplacementMap)
		{
			if (!UnsafeMeshes.Contains(Pair.Key) && IsValid(Pair.Key) && IsValid(Pair.Value))
			{
				Audit.SafeReplacementMap.Add(Pair.Key, Pair.Value);
			}
		}

		return Audit;
	}

	int32 ReplaceStaticMeshReferencesInObjects(const TArray<UObject*>& Objects, const TMap<UStaticMesh*, UStaticMesh*>& ReplacementMap)
	{
		if (ReplacementMap.IsEmpty())
		{
			return 0;
		}

		int32 ReplacedComponentCount = 0;
		TSet<AActor*> VisitedActors;
		TSet<UStaticMeshComponent*> ProcessedComponents;

		auto ReplaceOnComponent = [&ReplacementMap, &ReplacedComponentCount, &ProcessedComponents](UStaticMeshComponent* MeshComponent)
		{
			if (!IsValid(MeshComponent) || ProcessedComponents.Contains(MeshComponent))
			{
				return;
			}
			ProcessedComponents.Add(MeshComponent);

			UStaticMesh* CurrentMesh = MeshComponent->GetStaticMesh();
			if (UStaticMesh* const* ReplacementMesh = ReplacementMap.Find(CurrentMesh))
			{
				MeshComponent->Modify();
				MeshComponent->SetStaticMesh(*ReplacementMesh);
				++ReplacedComponentCount;
			}
		};

		for (UObject* Object : Objects)
		{
			if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Object))
			{
				ReplaceOnComponent(MeshComponent);
			}
			else if (AActor* Actor = Cast<AActor>(Object))
			{
				ReplaceStaticMeshReferencesOnActorHierarchy(
					Actor,
					ReplacementMap,
					ReplacedComponentCount,
					VisitedActors,
					ProcessedComponents);
			}
		}

		return ReplacedComponentCount;
	}

	bool GetMeshGeometrySignature(UStaticMesh* Mesh, FString& OutSignature)
	{
		FText Reason;
		FOptions Options;
		Options.bRequireMatchingMaterials = false;
		return BuildMeshSignature(Mesh, Options, OutSignature, Reason);
	}
}

#undef LOCTEXT_NAMESPACE
