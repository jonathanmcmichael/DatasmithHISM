#include "Dataprep/ConVerseConsolidateSimilarMeshesOperation.h"

#include "ConVerseStaticMeshConsolidationUtils.h"

#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "ConVerseConsolidateSimilarMeshesOperation"

FText UConVerseConsolidateSimilarMeshesOperation::GetCategory_Implementation() const
{
	return FDataprepOperationCategories::MeshOperation;
}

FText UConVerseConsolidateSimilarMeshesOperation::GetAdditionalKeyword_Implementation() const
{
	return LOCTEXT("Keywords", "revit datasmith duplicate family mesh consolidate deduplicate replace");
}

void UConVerseConsolidateSimilarMeshesOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	TArray<UObject*> ContextObjects;
	ContextObjects.Reserve(InContext.Objects.Num());
	for (UObject* Object : InContext.Objects)
	{
		ContextObjects.Add(Object);
	}

	TSet<UStaticMesh*> Meshes;
	ConVerseStaticMeshConsolidation::CollectStaticMeshes(ContextObjects, Meshes);
	if (Meshes.IsEmpty())
	{
		LogInfo(LOCTEXT("NoMeshes", "No static mesh assets were found in the Dataprep context."));
		return;
	}

	ConVerseStaticMeshConsolidation::FOptions Options;
	Options.bRequireMatchingMaterials = bRequireMatchingMaterials;

	const ConVerseStaticMeshConsolidation::FAnalysis Analysis =
		ConVerseStaticMeshConsolidation::AnalyzeMeshes(Meshes, Options);

	if (Analysis.Groups.IsEmpty())
	{
		LogInfo(FText::Format(
			LOCTEXT("NoDuplicateGroups", "No duplicate static mesh groups were found. Skipped {0} mesh asset(s)."),
			Analysis.SkippedMeshCount));
		return;
	}

	TMap<UStaticMesh*, UStaticMesh*> CandidateReplacementMap;
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

			CandidateReplacementMap.Add(DuplicateMesh, Group.CanonicalMesh.Get());
		}
	}

	const ConVerseStaticMeshConsolidation::FReferenceAudit ReferenceAudit =
		ConVerseStaticMeshConsolidation::AuditStaticMeshReplacementReferences(ContextObjects, CandidateReplacementMap);
	const int32 TotalSkippedMeshCount = Analysis.SkippedMeshCount + ReferenceAudit.SkippedMeshes.Num();
	for (const TPair<TObjectPtr<UStaticMesh>, FText>& SkippedMesh : ReferenceAudit.SkippedMeshes)
	{
		LogInfo(FText::Format(
			LOCTEXT("UnsafeMeshSkipped", "Skipped unsafe duplicate mesh '{0}': {1}"),
			FText::FromString(GetPathNameSafe(SkippedMesh.Key.Get())),
			SkippedMesh.Value));
	}

	TMap<UStaticMesh*, UStaticMesh*> ReplacementMap = ReferenceAudit.SafeReplacementMap;
	if (ReplacementMap.IsEmpty())
	{
		LogInfo(FText::Format(
			LOCTEXT("NoReplacements", "No duplicate static mesh references were eligible for replacement. Skipped {0} mesh asset(s)."),
			TotalSkippedMeshCount));
		return;
	}

	TArray<UObject*> MeshesToDelete;
	MeshesToDelete.Reserve(ReplacementMap.Num());
	for (const TPair<UStaticMesh*, UStaticMesh*>& Pair : ReplacementMap)
	{
		if (IsValid(Pair.Key))
		{
			MeshesToDelete.Add(Pair.Key);
		}
	}

	const int32 ReplacedComponentCount = ConVerseStaticMeshConsolidation::ReplaceStaticMeshReferencesInObjects(ContextObjects, ReplacementMap);
	DeleteObjects(MoveTemp(MeshesToDelete));

	LogInfo(FText::Format(
		LOCTEXT("Summary", "Consolidated {0} duplicate mesh asset(s) into {1} canonical mesh asset(s). Updated {2} component reference(s). Skipped {3} mesh asset(s)."),
		ReplacementMap.Num(),
		Analysis.Groups.Num(),
		ReplacedComponentCount,
		TotalSkippedMeshCount));
}

#undef LOCTEXT_NAMESPACE
