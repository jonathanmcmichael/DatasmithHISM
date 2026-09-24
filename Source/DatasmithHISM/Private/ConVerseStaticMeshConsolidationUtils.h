#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

namespace ConVerseStaticMeshConsolidation
{
	struct FOptions
	{
		bool bRequireMatchingMaterials = true;
	};

	struct FGroup
	{
		TObjectPtr<UStaticMesh> CanonicalMesh = nullptr;
		TArray<TObjectPtr<UStaticMesh>> DuplicateMeshes;
		FString Signature;
	};

	struct FAnalysis
	{
		int32 MeshesConsidered = 0;
		int32 MeshesWithValidSignatures = 0;
		int32 SkippedMeshCount = 0;
		TArray<FGroup> Groups;
		TMap<TObjectPtr<UStaticMesh>, FText> SkippedMeshes;
	};

	struct FReferenceAudit
	{
		TMap<UStaticMesh*, UStaticMesh*> SafeReplacementMap;
		TMap<TObjectPtr<UStaticMesh>, FText> SkippedMeshes;
	};

	void CollectStaticMeshes(const TArray<UObject*>& Objects, TSet<UStaticMesh*>& OutMeshes);
	FAnalysis AnalyzeMeshes(const TSet<UStaticMesh*>& Meshes, const FOptions& Options);
	FReferenceAudit AuditStaticMeshReplacementReferences(
		const TArray<UObject*>& Objects,
		const TMap<UStaticMesh*, UStaticMesh*>& CandidateReplacementMap);
	int32 ReplaceStaticMeshReferencesInObjects(const TArray<UObject*>& Objects, const TMap<UStaticMesh*, UStaticMesh*>& ReplacementMap);

	// Returns true and fills OutSignature with a stable hash of the mesh's LOD0 source geometry
	// (vertex positions relative to centroid, normals, UVs, polygon group/material slot names).
	// Material asset paths are NOT included — the signature captures shape only.
	// Returns false when the mesh has no source models or no triangles.
	bool GetMeshGeometrySignature(UStaticMesh* Mesh, FString& OutSignature);
}
