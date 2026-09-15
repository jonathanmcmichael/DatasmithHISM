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

	void CollectStaticMeshes(const TArray<UObject*>& Objects, TSet<UStaticMesh*>& OutMeshes);
	FAnalysis AnalyzeMeshes(const TSet<UStaticMesh*>& Meshes, const FOptions& Options);
	int32 ReplaceStaticMeshReferencesInObjects(const TArray<UObject*>& Objects, const TMap<UStaticMesh*, UStaticMesh*>& ReplacementMap);
}
