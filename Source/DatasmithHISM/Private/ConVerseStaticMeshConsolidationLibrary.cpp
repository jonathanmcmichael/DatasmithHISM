#include "ConVerseStaticMeshConsolidationLibrary.h"

#include "ConVerseStaticMeshConsolidationUtils.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "ConVerseStaticMeshConsolidationLibrary"

namespace
{
	static FString BuildSummaryText(const FConVerseStaticMeshConsolidationResult& Result)
	{
		return FString::Printf(
			TEXT("Considered %d mesh asset(s), found %d duplicate group(s), consolidated %d duplicate mesh asset(s), failed %d consolidation(s), skipped %d mesh asset(s)."),
			Result.MeshesConsidered,
			Result.DuplicateGroupsFound,
			Result.MeshesConsolidated,
			Result.FailedConsolidations,
			Result.SkippedMeshes);
	}

	static TArray<UObject*> GetSelectedEditorObjects()
	{
		TArray<UObject*> SelectedObjects;
		if (GEditor == nullptr)
		{
			return SelectedObjects;
		}

		TSet<UObject*> UniqueObjects;

		if (USelection* SelectedActors = GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*SelectedActors); It; ++It)
			{
				if (UObject* SelectedObject = Cast<UObject>(*It))
				{
					UniqueObjects.Add(SelectedObject);
				}
			}
		}

		if (USelection* SelectedAssets = GEditor->GetSelectedObjects())
		{
			for (FSelectionIterator It(*SelectedAssets); It; ++It)
			{
				if (UObject* SelectedObject = Cast<UObject>(*It))
				{
					UniqueObjects.Add(SelectedObject);
				}
			}
		}

		SelectedObjects.Reserve(UniqueObjects.Num());
		for (UObject* Object : UniqueObjects)
		{
			SelectedObjects.Add(Object);
		}

		return SelectedObjects;
	}
}

FConVerseStaticMeshConsolidationResult UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(const bool bRequireMatchingMaterials)
{
	return ConsolidateSimilarStaticMeshes(GetSelectedEditorObjects(), bRequireMatchingMaterials);
}

FConVerseStaticMeshConsolidationResult UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes(const TArray<UObject*>& Objects, const bool bRequireMatchingMaterials)
{
	FConVerseStaticMeshConsolidationResult Result;

	TSet<UStaticMesh*> Meshes;
	ConVerseStaticMeshConsolidation::CollectStaticMeshes(Objects, Meshes);
	Result.MeshesConsidered = Meshes.Num();

	if (Meshes.IsEmpty())
	{
		Result.Summary = TEXT("No static mesh assets were found in the supplied objects.");
		return Result;
	}

	ConVerseStaticMeshConsolidation::FOptions Options;
	Options.bRequireMatchingMaterials = bRequireMatchingMaterials;

	const ConVerseStaticMeshConsolidation::FAnalysis Analysis =
		ConVerseStaticMeshConsolidation::AnalyzeMeshes(Meshes, Options);

	Result.DuplicateGroupsFound = Analysis.Groups.Num();
	Result.SkippedMeshes = Analysis.SkippedMeshCount;

	TMap<UStaticMesh*, UStaticMesh*> ReplacementMap;
	TArray<UObject*> DuplicateMeshesToDelete;
	for (const ConVerseStaticMeshConsolidation::FGroup& Group : Analysis.Groups)
	{
		if (!IsValid(Group.CanonicalMesh.Get()))
		{
			continue;
		}

		for (UStaticMesh* DuplicateMesh : Group.DuplicateMeshes)
		{
			if (!IsValid(DuplicateMesh))
			{
				continue;
			}

			ReplacementMap.Add(DuplicateMesh, Group.CanonicalMesh.Get());
			DuplicateMeshesToDelete.Add(DuplicateMesh);
		}
	}

	ConVerseStaticMeshConsolidation::ReplaceStaticMeshReferencesInObjects(Objects, ReplacementMap);

	if (!DuplicateMeshesToDelete.IsEmpty())
	{
		const int32 DeletedCount = ObjectTools::DeleteObjects(DuplicateMeshesToDelete, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed);
		Result.MeshesConsolidated = DeletedCount;
		Result.FailedConsolidations = DuplicateMeshesToDelete.Num() - DeletedCount;
	}

	Result.Summary = BuildSummaryText(Result);
	return Result;
}

#undef LOCTEXT_NAMESPACE
