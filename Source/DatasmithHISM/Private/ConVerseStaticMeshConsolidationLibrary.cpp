#include "ConVerseStaticMeshConsolidationLibrary.h"

#include "ConVerseStaticMeshConsolidationUtils.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
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

	static FString BuildSkippedMeshReport(const TMap<TObjectPtr<UStaticMesh>, FText>& SkippedMeshes)
	{
		TArray<FString> Entries;
		Entries.Reserve(SkippedMeshes.Num());
		for (const TPair<TObjectPtr<UStaticMesh>, FText>& Pair : SkippedMeshes)
		{
			Entries.Add(FString::Printf(TEXT("%s: %s"),
				*GetPathNameSafe(Pair.Key.Get()),
				*Pair.Value.ToString()));
		}
		Entries.Sort();
		return FString::Join(Entries, TEXT("\n"));
	}
}

FConVerseStaticMeshConsolidationResult UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(const bool bRequireMatchingMaterials, const bool bDryRun)
{
	return ConsolidateSimilarStaticMeshes(GetSelectedEditorObjects(), bRequireMatchingMaterials, bDryRun);
}

FConVerseStaticMeshConsolidationResult UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes(const TArray<UObject*>& Objects, const bool bRequireMatchingMaterials, const bool bDryRun)
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
	Result.SkippedMeshReport = BuildSkippedMeshReport(Analysis.SkippedMeshes);

	TMap<UStaticMesh*, UStaticMesh*> CandidateReplacementMap;
	for (const ConVerseStaticMeshConsolidation::FGroup& Group : Analysis.Groups)
	{
		if (!IsValid(Group.CanonicalMesh.Get()))
		{
			continue;
		}

		for (UStaticMesh* DuplicateMesh : Group.DuplicateMeshes)
		{
			if (IsValid(DuplicateMesh))
			{
				CandidateReplacementMap.Add(DuplicateMesh, Group.CanonicalMesh.Get());
			}
		}
	}

	const ConVerseStaticMeshConsolidation::FReferenceAudit ReferenceAudit =
		ConVerseStaticMeshConsolidation::AuditStaticMeshReplacementReferences(Objects, CandidateReplacementMap);
	Result.SkippedMeshes += ReferenceAudit.SkippedMeshes.Num();
	const FString UnsafeSkippedMeshReport = BuildSkippedMeshReport(ReferenceAudit.SkippedMeshes);
	if (!UnsafeSkippedMeshReport.IsEmpty())
	{
		Result.SkippedMeshReport = Result.SkippedMeshReport.IsEmpty()
			? UnsafeSkippedMeshReport
			: Result.SkippedMeshReport + TEXT("\n") + UnsafeSkippedMeshReport;
	}

	// Dry-run: count what would happen and return without making any changes.
	if (bDryRun)
	{
		Result.MeshesConsolidated = ReferenceAudit.SafeReplacementMap.Num();
		Result.Summary = FString::Printf(
			TEXT("[Dry run] Considered %d mesh asset(s), found %d duplicate group(s). "
				"Would consolidate %d duplicate mesh asset(s), skipped %d mesh asset(s). No changes made."),
			Result.MeshesConsidered,
			Result.DuplicateGroupsFound,
			Result.MeshesConsolidated,
			Result.SkippedMeshes);
		return Result;
	}

	TMap<UStaticMesh*, UStaticMesh*> ReplacementMap = ReferenceAudit.SafeReplacementMap;
	TArray<UObject*> DuplicateMeshesToDelete;
	DuplicateMeshesToDelete.Reserve(ReplacementMap.Num());
	for (const TPair<UStaticMesh*, UStaticMesh*>& Pair : ReplacementMap)
	{
		if (IsValid(Pair.Key))
		{
			DuplicateMeshesToDelete.Add(Pair.Key);
		}
	}

	if (!DuplicateMeshesToDelete.IsEmpty())
	{
		// Only show a confirmation dialog in interactive editor sessions — Dataprep pipelines
		// and commandlets run headlessly and should proceed without prompting.
		const bool bIsInteractive = GIsEditor && !IsRunningCommandlet();
		if (bIsInteractive)
		{
			const FText ConfirmMessage = FText::Format(
				LOCTEXT("DedupeConfirm",
					"This will permanently delete {0} duplicate static mesh asset(s) from the Content Browser across {1} duplicate group(s).\n\n"
					"This action cannot be undone. Make sure your level is saved and source control is active.\n\n"
					"Delete the duplicates?"),
				DuplicateMeshesToDelete.Num(),
				Result.DuplicateGroupsFound);

			const EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMessage);
			if (Response != EAppReturnType::Yes)
			{
				Result.Outcome = EConVerseStaticMeshConsolidationOutcome::Cancelled;
				Result.Summary = LOCTEXT("DedupeCancelled", "Consolidation cancelled by user.").ToString();
				return Result;
			}
		}

		ConVerseStaticMeshConsolidation::ReplaceStaticMeshReferencesInObjects(Objects, ReplacementMap);

		const int32 DeletedCount = ObjectTools::DeleteObjects(DuplicateMeshesToDelete, false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed);
		Result.MeshesConsolidated = DeletedCount;
		Result.FailedConsolidations = DuplicateMeshesToDelete.Num() - DeletedCount;
		if (Result.FailedConsolidations > 0)
		{
			Result.Outcome = EConVerseStaticMeshConsolidationOutcome::Failed;
		}
	}

	Result.Summary = BuildSummaryText(Result);
	return Result;
}

#undef LOCTEXT_NAMESPACE
