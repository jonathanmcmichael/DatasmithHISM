#include "ConVerseDatasmithImportService.h"

#include "ConVerseImportProcessing.h"
#include "ConVerseOptimizedImportManifest.h"

#include "DatasmithScene.h"
#include "Editor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "StaticMeshCompiler.h"

namespace ConVerseNaniteApply
{
	// The attempt folder is not stored on older manifests, but every owned package carries it as
	// "<Destination>/<Name>_ConVerse_<first 12 of the session id>". Ownership is proven from the
	// recorded package names, never inferred from a folder scan.
	static bool DeriveAttemptFolder(const UConVerseOptimizedImportManifest& Manifest, FString& OutFolder)
	{
		const FString Token = TEXT("_ConVerse_") + Manifest.SessionId.Left(12);
		for (const FName& Package : Manifest.CreatedPackageNames)
		{
			const FString Name = Package.ToString();
			const int32 Index = Name.Find(Token, ESearchCase::CaseSensitive);
			if (Index != INDEX_NONE && Name.StartsWith(Manifest.DestinationContentFolder + TEXT("/")))
			{
				OutFolder = Name.Left(Index + Token.Len());
				return true;
			}
		}
		return false;
	}

	static FConVerseNaniteApplyResult Refuse(FConVerseNaniteApplyResult Result, const FString& Reason)
	{
		Result.bSucceeded = false;
		Result.Summary = TEXT("Nanite was not applied: ") + Reason;
		UE_LOG(LogConVerseOptimizedImport, Warning, TEXT("%s"), *Result.Summary);
		return Result;
	}
}

static FConVerseNaniteApplyResult RunApplyNanite(const FConVerseNaniteApplyOptions& Options)
{
	using namespace ConVerseNaniteApply;
	TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_ApplyNanite);
	FConVerseNaniteApplyResult Result;

	if (Options.Policy == EConVerseNanitePolicy::PreserveImported)
		return Refuse(Result, TEXT("the preserve-imported policy has nothing to apply."));

	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Options.ManifestPath.ResolveObject());
	if (!Manifest)
		return Refuse(Result, TEXT("the import's manifest is not loaded."));
	if (Manifest->CommitState != EConVerseOptimizedImportCommitState::Active)
		return Refuse(Result, TEXT("the import is not the active committed session."));
	if (Manifest->Verification.bAcceptedWithFailedVerification)
		return Refuse(Result, TEXT("the import was accepted with failed verification; rebuild it before applying Nanite."));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || Manifest->WorldPath.ResolveObject() != World)
		return Refuse(Result, TEXT("the owning level is not the loaded level. Load the level that owns this import."));

	auto* Scene = Cast<UDatasmithScene>(Manifest->ImportedDatasmithSceneAssetPath.ResolveObject());
	if (!Scene)
		return Refuse(Result, TEXT("the imported Datasmith scene asset is not loaded."));

	FString AttemptFolder;
	if (!DeriveAttemptFolder(*Manifest, AttemptFolder))
		return Refuse(Result, TEXT("this manifest does not record the packages needed to prove mesh ownership."));

	// Fail closed on any tracked change: applying Nanite rewrites the baseline, so doing it over
	// manual edits would silently bless them.
	TArray<FString> Differences;
	if (!ConVerseImportProcessing::CheckState(*Manifest, Differences))
	{
		Result.Diagnostics = Differences;
		return Refuse(Result, TEXT("tracked objects changed since the import (manual edits or an older manifest). Rebuild the import first."));
	}

	// Everything the step may change, so a failure or cancel can put it back exactly.
	TMap<UStaticMesh*, bool> PreviousEnabled;
	for (const auto& Pair : Scene->StaticMeshes)
	{
		if (UStaticMesh* Mesh = Pair.Value.LoadSynchronous())
			if (Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/")))
				PreviousEnabled.Add(Mesh, Mesh->GetNaniteSettings().bEnabled);
	}

	FConVerseOptimizedImportOptions ProcessingOptions;
	ProcessingOptions.Processing.NanitePolicy = Options.Policy;
	ProcessingOptions.Processing.MaxNaniteMeshes = FMath::Max(0, Options.MaxNaniteMeshes);
	ProcessingOptions.Processing.DisableNaniteMeshElements = Options.DisableNaniteMeshElements;
	ProcessingOptions.CancelRequested = Options.CancelRequested;
	ProcessingOptions.NaniteOnlyMeshElements = Options.OnlyMeshElements;

	// Crash-visible record of each mesh Nanite setting, as during import (Amendment 13).
	const FString ProgressLog = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/ImportProgress")
		/ (TEXT("NaniteApply_") + Manifest->SessionId.Left(12) + TEXT(".log"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProgressLog), true);
	const TFunction<void(const FString&, bool)> WriteProgress = [&ProgressLog](const FString& Event, bool)
	{
		FFileHelper::SaveStringToFile(FDateTime::UtcNow().ToIso8601() + TEXT(" ") + Event + TEXT("\n"), *ProgressLog,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	};
	WriteProgress(FString::Printf(TEXT("Apply Nanite started policy=%d budget=%d meshes=%d"),
		int32(Options.Policy), ProcessingOptions.Processing.MaxNaniteMeshes, PreviousEnabled.Num()), true);

	// The dialog owns Cancel; ProcessMeshes sees it through its own nested slow task.
	FScopedSlowTask ProgressDialog(1.0f, NSLOCTEXT("ConVerseHISM", "ApplyNaniteProgress", "Applying Nanite..."));
	if (!Options.bAutomated) ProgressDialog.MakeDialog(true);

	FConVerseOptimizedImportResult Processing;
	FString Error;
	const bool bProcessed = ConVerseImportProcessing::ProcessMeshes(
		*World, *Scene, AttemptFolder, ProcessingOptions, Processing, Error, WriteProgress);
	Result.MeshBuildSeconds = Processing.MeshBuildSeconds;
	Result.Diagnostics.Append(Processing.Diagnostics);
	Result.NaniteRebuiltMeshes = Processing.NaniteRebuiltMeshes;
	Result.NaniteBudgetSkippedMeshes = Processing.NaniteBudgetSkippedMeshes;
	Result.NaniteSkippedMeshes = Processing.NaniteSkippedMeshes;

	if (!bProcessed)
	{
		Result.bCancelled = Error.StartsWith(TEXT("Mesh processing cancelled"));
		TArray<UStaticMesh*> Restored;
		for (const auto& Pair : PreviousEnabled)
		{
			if (Pair.Key->GetNaniteSettings().bEnabled == Pair.Value) continue;
			Pair.Key->Modify();
			Pair.Key->GetNaniteSettings().bEnabled = Pair.Value;
			Pair.Key->PostEditChange();
			Restored.Add(Pair.Key);
		}
		FStaticMeshCompilingManager::Get().FinishCompilation(Restored);
		WriteProgress(FString::Printf(TEXT("Apply Nanite %s; restored %d meshes: %s"),
			Result.bCancelled ? TEXT("cancelled") : TEXT("failed"), Restored.Num(), *Error), true);
		Result.Summary = FString::Printf(TEXT("Nanite was not applied: %s %d meshes were restored; the import is unchanged."),
			*Error, Restored.Num());
		UE_LOG(LogConVerseOptimizedImport, Warning, TEXT("%s"), *Result.Summary);
		return Result;
	}

	Result.NaniteEnabledMeshes = Processing.NaniteEnabledMeshes;

	// New baseline for exactly the objects this step may have changed; every other record is untouched.
	Manifest->Modify();
	for (FConVerseTrackedObjectState& Record : Manifest->TrackedObjects)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Record.ObjectPath.ResolveObject()))
			Record.State = ConVerseImportProcessing::ObjectState(*Mesh);
	}
	FConVerseImportProcessingSettings Applied;
	Applied.NanitePolicy = Options.Policy;
	Applied.MaxNaniteMeshes = ProcessingOptions.Processing.MaxNaniteMeshes;
	Applied.DisableNaniteMeshElements = Options.DisableNaniteMeshElements;
	// The selection is part of what was applied, so it is recorded with the settings.
	FString AppliedJson = ConVerseImportProcessing::SettingsJson(Applied);
	if (!Options.OnlyMeshElements.IsEmpty()) AppliedJson += TEXT("\nOnlyMeshElements=") + FString::Join(Options.OnlyMeshElements, TEXT(","));
	Manifest->NaniteApplySettingsJson = AppliedJson;
	Manifest->NaniteApplyEnabledMeshes = Result.NaniteEnabledMeshes;
	Manifest->MarkPackageDirty();

	WriteProgress(FString::Printf(TEXT("Apply Nanite completed enabled=%d rebuilt=%d budget_skipped=%d"),
		Result.NaniteEnabledMeshes, Result.NaniteRebuiltMeshes, Result.NaniteBudgetSkippedMeshes), true);
	Result.bSucceeded = true;
	Result.Summary = FString::Printf(
		TEXT("Nanite applied: %d meshes enabled, %d rebuilt, %d left out by the budget, %d excluded for material compatibility. Save the import to keep it."),
		Result.NaniteEnabledMeshes, Result.NaniteRebuiltMeshes, Result.NaniteBudgetSkippedMeshes, Result.NaniteSkippedMeshes);
	return Result;
}

FConVerseNaniteApplyResult FConVerseDatasmithImportService::ApplyNanite(const FConVerseNaniteApplyOptions& Options)
{
	// Timed here, not in a scope guard: a guard would run after the result is already copied out.
	const double Started = FPlatformTime::Seconds();
	FConVerseNaniteApplyResult Result = RunApplyNanite(Options);
	Result.DurationSeconds = FPlatformTime::Seconds() - Started;
	return Result;
}

FConVerseNaniteAnalysisResult FConVerseDatasmithImportService::AnalyzeNanite(const FConVerseNaniteAnalysisOptions& Options)
{
	using namespace ConVerseNaniteApply;
	FConVerseNaniteAnalysisResult Result;
	const auto Fail = [&Result](const FString& Reason)
	{
		Result.Summary = TEXT("Nanite analysis could not run: ") + Reason;
		UE_LOG(LogConVerseOptimizedImport, Warning, TEXT("%s"), *Result.Summary);
		return Result;
	};

	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Options.ManifestPath.ResolveObject());
	if (!Manifest) return Fail(TEXT("the import's manifest is not loaded."));
	if (Manifest->CommitState != EConVerseOptimizedImportCommitState::Active)
		return Fail(TEXT("the import is not the active committed session."));
	auto* Scene = Cast<UDatasmithScene>(Manifest->ImportedDatasmithSceneAssetPath.ResolveObject());
	if (!Scene) return Fail(TEXT("the imported Datasmith scene asset is not loaded."));
	FString AttemptFolder;
	if (!DeriveAttemptFolder(*Manifest, AttemptFolder))
		return Fail(TEXT("this manifest does not record the packages needed to prove mesh ownership."));

	// Element name per owned mesh. The first key wins, matching the exception and selection lists.
	TMap<UStaticMesh*, FString> ElementOf;
	for (const auto& Pair : Scene->StaticMeshes)
	{
		UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
		if (Mesh && Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/")) && !ElementOf.Contains(Mesh))
			ElementOf.Add(Mesh, Pair.Key.ToString());
	}

	// Placements come from this import's own actors, so unrelated content in the level cannot skew them.
	const auto IsNaniteBlend = [](const UMaterialInterface* Material)
	{
		return Material && Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked;
	};
	TMap<UStaticMesh*, int32> Placements;
	TSet<UStaticMesh*> IncompatibleOverride;
	for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest->CreatedActors)
	{
		const AActor* Actor = Cast<AActor>(Record.ActorPath.ResolveObject());
		if (!Actor) continue;
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (UStaticMeshComponent* Component : Components)
		{
			UStaticMesh* Mesh = Component->GetStaticMesh();
			if (!Mesh || !ElementOf.Contains(Mesh)) continue;
			const auto* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
			Placements.FindOrAdd(Mesh) += Instanced ? FMath::Max(1, Instanced->GetInstanceCount()) : 1;
			for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
				if (IsNaniteBlend(Component->GetMaterial(Slot))) IncompatibleOverride.Add(Mesh);
		}
	}

	const TSet<FString> Disabled(Options.DisableNaniteMeshElements);
	for (const auto& Pair : ElementOf)
	{
		UStaticMesh* Mesh = Pair.Key;
		FConVerseNaniteAnalysisRow& Row = Result.Rows.AddDefaulted_GetRef();
		Row.MeshElement = Pair.Value;
		Row.Triangles = Mesh->GetNumTriangles(0);
		Row.Placements = Placements.FindRef(Mesh);
		Row.PlacedTriangles = int64(Row.Triangles) * Row.Placements;
		Row.bNaniteEnabled = Mesh->GetNaniteSettings().bEnabled;
		bool bIncompatible = IncompatibleOverride.Contains(Mesh);
		for (const auto& Slot : Mesh->GetStaticMaterials()) bIncompatible |= IsNaniteBlend(Slot.MaterialInterface);
		if (Disabled.Contains(Row.MeshElement)) { Row.bEligible = false; Row.Note = TEXT("Nanite disabled by an exception"); }
		else if (bIncompatible) { Row.bEligible = false; Row.Note = TEXT("incompatible material (translucent or additive)"); }
		Result.TotalPlacedTriangles += Row.PlacedTriangles;
	}
	Result.Rows.Sort([](const FConVerseNaniteAnalysisRow& Left, const FConVerseNaniteAnalysisRow& Right)
	{
		return Left.PlacedTriangles != Right.PlacedTriangles ? Left.PlacedTriangles > Right.PlacedTriangles : Left.MeshElement < Right.MeshElement;
	});

	// Candidates are eligible meshes big enough to be worth it; the coverage target is among them.
	for (const FConVerseNaniteAnalysisRow& Row : Result.Rows)
	{
		if (Row.bEligible) ++Result.EligibleMeshes;
		if (Row.bEligible && Row.Triangles >= Options.MinTriangles)
		{
			++Result.CandidateMeshes;
			Result.CandidatePlacedTriangles += Row.PlacedTriangles;
		}
	}
	const double Target = FMath::Clamp(Options.CoverageTarget, 0.0, 1.0) * double(Result.CandidatePlacedTriangles);
	int64 Covered = 0;
	for (FConVerseNaniteAnalysisRow& Row : Result.Rows)
	{
		if (!Row.bEligible || Row.Triangles < Options.MinTriangles) continue;
		if (double(Covered) < Target)
		{
			Row.bRecommended = true;
			Covered += Row.PlacedTriangles;
			Result.RecommendedElements.Add(Row.MeshElement);
		}
		Row.CumulativeShare = Result.CandidatePlacedTriangles > 0 ? double(Covered) / double(Result.CandidatePlacedTriangles) : 0.0;
	}
	Result.RecommendedPlacedTriangles = Covered;

	Result.bSucceeded = true;
	Result.Summary = FString::Printf(
		TEXT("Nanite analysis: %d meshes, %d eligible, %d candidates with at least %d triangles. %d meshes cover %.0f%% of the candidates' %lld placed triangles. Thresholds are heuristics, not measured Nanite break-even points."),
		Result.Rows.Num(), Result.EligibleMeshes, Result.CandidateMeshes, Options.MinTriangles, Result.RecommendedElements.Num(),
		Result.CandidatePlacedTriangles > 0 ? 100.0 * double(Covered) / double(Result.CandidatePlacedTriangles) : 0.0,
		Result.CandidatePlacedTriangles);
	return Result;
}
