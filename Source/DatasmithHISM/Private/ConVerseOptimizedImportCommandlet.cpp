#include "ConVerseOptimizedImportCommandlet.h"

#include "ConVerseDatasmithImportService.h"

#include "Misc/Parse.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "DatasmithScene.h"
#include "DatasmithMeshSerialization.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogConVerseOptimizedImportCommandlet, Log, All);

UConVerseOptimizedImportCommandlet::UConVerseOptimizedImportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace ConVerseOptimizedImportCommandlet
{
	static const TCHAR* ToString(EConVerseOptimizedImportStatus Status)
	{
		switch (Status)
		{
		case EConVerseOptimizedImportStatus::InvalidOptions: return TEXT("InvalidOptions");
		case EConVerseOptimizedImportStatus::SourceLoadFailed: return TEXT("SourceLoadFailed");
		case EConVerseOptimizedImportStatus::SourceChangedAfterAnalysis: return TEXT("SourceChangedAfterAnalysis");
		case EConVerseOptimizedImportStatus::AnalysisSucceeded: return TEXT("AnalysisSucceeded");
		case EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups: return TEXT("AnalysisNoEligibleGroups");
		case EConVerseOptimizedImportStatus::AlreadyCurrent: return TEXT("AlreadyCurrent");
		case EConVerseOptimizedImportStatus::Verified: return TEXT("Verified");
		case EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack: return TEXT("ImportedWithFailuresRolledBack");
		case EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision: return TEXT("AwaitingFailedVerificationDecision");
		case EConVerseOptimizedImportStatus::AcceptedWithFailedVerification: return TEXT("AcceptedWithFailedVerification");
		case EConVerseOptimizedImportStatus::CancelledRolledBack: return TEXT("CancelledRolledBack");
		case EConVerseOptimizedImportStatus::FailedRolledBack: return TEXT("FailedRolledBack");
		case EConVerseOptimizedImportStatus::OptimizedReimportBlocked: return TEXT("OptimizedReimportBlocked");
		case EConVerseOptimizedImportStatus::ManualEditsDetected: return TEXT("ManualEditsDetected");
		case EConVerseOptimizedImportStatus::RollbackFailed: return TEXT("RollbackFailed");
		default: return TEXT("Unknown");
		}
	}

	/**
	 * Only a fully successful outcome is a pass. AlreadyCurrent counts as success for a reimport,
	 * but only when re-verification of the committed output also passed.
	 *
	 * AcceptedWithFailedVerification is deliberately NOT a pass. Accepting a degraded session is an
	 * interactive decision about known-bad output; a headless batch run must still fail so CI does
	 * not go green on an import whose checks did not pass. The commandlet never sets
	 * bDeferRollbackOnVerificationFailure, so it cannot produce that status anyway.
	 */
	static bool IsSuccess(const FConVerseOptimizedImportResult& Result, bool bAnalyzeOnly)
	{
		if (bAnalyzeOnly)
		{
			return Result.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded
				|| Result.Status == EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups;
		}
		if (Result.Status == EConVerseOptimizedImportStatus::Verified)
		{
			return true;
		}
		return Result.Status == EConVerseOptimizedImportStatus::AlreadyCurrent
			&& Result.bVerificationSucceeded;
	}
}

int32 UConVerseOptimizedImportCommandlet::Main(const FString& Params)
{
	using namespace ConVerseOptimizedImportCommandlet;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Arguments;
	ParseCommandLine(*Params, Tokens, Switches, Arguments);

	if (const FString* MeshFile = Arguments.Find(TEXT("InspectMesh")))
	{
		const FString* Directory = Arguments.Find(TEXT("GeometryDirectory"));
		if (!Directory) return 1;
		const FDatasmithPackedMeshes Packed = GetDatasmithMeshFromFile(*MeshFile);
		if (Packed.Meshes.IsEmpty()) return 1;
		IFileManager::Get().MakeDirectory(**Directory, true);
		for (int32 Model = 0; Model < Packed.Meshes.Num(); ++Model)
		{
			for (int32 LOD = 0; LOD < Packed.Meshes[Model].SourceModels.Num(); ++LOD)
			{
				const FMeshDescription& Description = Packed.Meshes[Model].SourceModels[LOD];
				const auto Positions = FStaticMeshConstAttributes(Description).GetVertexPositions();
				TMap<FVertexID, int32> Indices;
				FString Obj;
				int32 Index = 1;
				for (FVertexID Vertex : Description.Vertices().GetElementIDs())
				{
					Indices.Add(Vertex, Index++);
					const FVector3f P = Positions[Vertex];
					Obj += FString::Printf(TEXT("v %.9g %.9g %.9g\n"), P.X, P.Y, P.Z);
				}
				for (FTriangleID Triangle : Description.Triangles().GetElementIDs())
				{
					const auto Vertices = Description.GetTriangleVertices(Triangle);
					Obj += FString::Printf(TEXT("f %d %d %d\n"), Indices[Vertices[0]], Indices[Vertices[1]], Indices[Vertices[2]]);
				}
				UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Exported payload model=%d LOD=%d vertices=%d triangles=%d collision=%d"),
					Model, LOD, Description.Vertices().Num(), Description.Triangles().Num(), Packed.Meshes[Model].bIsCollisionMesh);
				if (!FFileHelper::SaveStringToFile(Obj, *(*Directory / FString::Printf(TEXT("Payload_%d_LOD%d.obj"), Model, LOD)))) return 1;
			}
		}
		return 0;
	}

	const FString* Source = Arguments.Find(TEXT("Source"));
	if (Source == nullptr || Source->IsEmpty())
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
			TEXT("Missing -Source=<path>. Usage: -run=ConVerseOptimizedImport -Source=\"C:/Path/Scene.udatasmith\" ")
			TEXT("[-Destination=/Game/DatasmithOptimized] [-InstanceType=ISM|HISM] [-MinInstances=2] [-AnalyzeOnly]")
			TEXT(" [-ChordTolerance=0.2] [-MaxEdgeLength=0] [-NormalTolerance=20] [-Stitching=None|Sewing|Healing] [-AllowMissingTextures] [-TextureSearchFolders=\"C:/A;C:/B\"]"));
		return 1;
	}

	FConVerseOptimizedImportOptions Options;
	if (const FString* Preset = Arguments.Find(TEXT("Preset")))
	{
		FString Error;
		if (!FConVerseDatasmithImportService::LoadPreset(*Preset, Options, Error)) { UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("%s"), *Error); return 1; }
	}
	Options.FilePath = *Source;
	Options.bRebuildFromSource = FParse::Param(*Params, TEXT("Rebuild"));
	Options.bReplaceManualEdits = FParse::Param(*Params, TEXT("ReplaceManualEdits"));
	// Headless equivalent of answering Yes to the panel's missing-texture prompt. Missing mesh files still fail.
	Options.bAllowMissingTextures = FParse::Param(*Params, TEXT("AllowMissingTextures"));
	// Semicolon-separated; an empty value disables the default Autodesk library search.
	if (const FString* Folders = Arguments.Find(TEXT("TextureSearchFolders")))
	{
		Options.TextureSearchFolders.Reset();
		Folders->ParseIntoArray(Options.TextureSearchFolders, TEXT(";"), true);
	}
	if (const FString* Policy = Arguments.Find(TEXT("Nanite")))
	{
		if (*Policy == TEXT("All")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::AllSupportedMeshes;
		else if (*Policy == TEXT("ISM")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::ConvertedISMOnly;
		else if (*Policy == TEXT("Preserve")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
		else { UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Nanite must be All, ISM, or Preserve.")); return 1; }
	}
	if (const FString* Threshold = Arguments.Find(TEXT("ManyLightThreshold"))) Options.Processing.ManyLightThreshold = FCString::Atoi(**Threshold);
	if (const FString* Names = Arguments.Find(TEXT("KeepOrdinary"))) Names->ParseIntoArray(Options.Processing.KeepOrdinaryMeshElements, TEXT(","), true);
	if (const FString* Names = Arguments.Find(TEXT("DisableNanite"))) Names->ParseIntoArray(Options.Processing.DisableNaniteMeshElements, TEXT(","), true);
	// Never prompt in a headless run. This is the documented automation seam.
	Options.bAutomated = true;

	if (const FString* Destination = Arguments.Find(TEXT("Destination")))
	{
		Options.DestinationPath = *Destination;
	}
	if (const FString* InstanceType = Arguments.Find(TEXT("InstanceType")))
	{
		if (InstanceType->Equals(TEXT("HISM"), ESearchCase::IgnoreCase))
		{
			Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
		}
		else if (InstanceType->Equals(TEXT("ISM"), ESearchCase::IgnoreCase))
		{
			Options.InstanceType = EConVerseOptimizedInstanceType::ISM;
		}
		else
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Unrecognized -InstanceType=%s. Expected ISM or HISM."), **InstanceType);
			return 1;
		}
	}
	if (const FString* MinInstances = Arguments.Find(TEXT("MinInstances")))
	{
		Options.MinimumInstanceCount = FCString::Atoi(**MinInstances);
	}

	// Tessellation switches. Values are clamped by the service, so CI cannot drive the
	// translator outside the engine's supported ranges.
	if (const FString* ChordTolerance = Arguments.Find(TEXT("ChordTolerance")))
	{
		Options.Tessellation.ChordTolerance = FCString::Atof(**ChordTolerance);
	}
	if (const FString* MaxEdgeLength = Arguments.Find(TEXT("MaxEdgeLength")))
	{
		Options.Tessellation.MaxEdgeLength = FCString::Atof(**MaxEdgeLength);
	}
	if (const FString* NormalTolerance = Arguments.Find(TEXT("NormalTolerance")))
	{
		Options.Tessellation.NormalTolerance = FCString::Atof(**NormalTolerance);
	}
	if (const FString* Stitching = Arguments.Find(TEXT("Stitching")))
	{
		if (Stitching->Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::None;
		}
		else if (Stitching->Equals(TEXT("Sewing"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::Sewing;
		}
		else if (Stitching->Equals(TEXT("Healing"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::Healing;
		}
		else
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Unrecognized -Stitching=%s. Expected None, Sewing, or Healing."), **Stitching);
			return 1;
		}
	}

	if (const FString* Map = Arguments.Find(TEXT("NewMap")))
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("NewMap must be a new /Game map path; existing maps are never overwritten.")); return 1; }
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!World || !UEditorLoadingAndSavingUtils::SaveMap(World, *Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not create validation map.")); return 1; }
	}
	if (const FString* Map = Arguments.Find(TEXT("LoadMap")))
	{
		if (!UEditorLoadingAndSavingUtils::LoadMap(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not load map.")); return 1; }
	}

	const bool bAnalyzeOnly = Switches.ContainsByPredicate(
		[](const FString& Switch) { return Switch.Equals(TEXT("AnalyzeOnly"), ESearchCase::IgnoreCase); });
	if (const FString* Map = Arguments.Find(TEXT("SaveAsMap")); Map && !bAnalyzeOnly)
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("SaveAsMap requires a new writable /Game map path.")); return 1; }
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World && FPackageName::DoesPackageExist(World->GetOutermost()->GetName()))
		{
			// UE duplicates an already-saved world, including a new actor identity domain, while
			// leaving this world loaded. Saving its manifest would falsely bless the new copy.
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Named-level Save As is blocked before import or copying: ownership cannot be proven for a duplicated map. ")
				TEXT("Continue with -LoadMap=%s -Save. For an independent copy, import into a new map and a new destination."),
				*World->GetOutermost()->GetName());
			return 1;
		}
	}

	UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
		TEXT("ConVerse optimized %s: source '%s', destination '%s', %s, minimum %d instances."),
		bAnalyzeOnly ? TEXT("analysis") : TEXT("import"),
		*Options.FilePath,
		*Options.DestinationPath,
		Options.InstanceType == EConVerseOptimizedInstanceType::HISM ? TEXT("HISM") : TEXT("ISM"),
		Options.MinimumInstanceCount);

	const FConVerseOptimizedImportResult Result = bAnalyzeOnly
		? FConVerseDatasmithImportService::Analyze(Options)
		: FConVerseDatasmithImportService::ImportAndVerify(Options);

	const bool bSucceeded = IsSuccess(Result, bAnalyzeOnly);
	UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
		TEXT("Status=%s PlanId=%s Groups=%d/%d Instances=%d/%d. %s"),
		ToString(Result.Status),
		*Result.PlanId,
		Result.VerifiedGroupCount, Result.PlannedGroupCount,
		Result.VerifiedInstanceCount, Result.PlannedInstanceCount,
		*Result.Summary);

	if (!Result.SavedReportPath.IsEmpty())
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Report: %s"), *Result.SavedReportPath);
	}

	if (Result.bSidecarChanged)
	{
		// Logged as a warning, not an error: the run itself succeeded and nothing was mutated.
		// Escalating to a non-zero exit would break CI on a condition the user chose to be advisory.
		UE_LOG(LogConVerseOptimizedImportCommandlet, Warning,
			TEXT("Sidecar assets changed while the primary source file did not (%d files now, %d recorded). ")
			TEXT("The committed geometry may be stale. Force a reimport to pick up the new assets."),
			Result.SidecarFileCount,
			Result.SidecarFileCountAtCommit);
	}

	if (!bSucceeded)
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
			TEXT("ConVerse optimized import did not succeed (status %s). See the report and log above."),
			ToString(Result.Status));
		return 1;
	}

	if (const FString* Directory = Arguments.Find(TEXT("GeometryDirectory")))
	{
		if (auto* Scene = Cast<UDatasmithScene>(Result.ImportAssetPath.ResolveObject()))
		{
			IFileManager::Get().MakeDirectory(**Directory, true);
			FString Inventory = TEXT("Element,Vertices,Triangles,NaniteEnabled,NaniteData\n");
			for (const auto& Pair : Scene->StaticMeshes)
			{
				UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
				const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
				if (!Description) continue;
				FStaticMeshConstAttributes Attributes(*Description);
				const auto Positions = Attributes.GetVertexPositions();
				FString Obj;
				TMap<FVertexID, int32> Indices;
				int32 Index = 1;
				for (FVertexID Vertex : Description->Vertices().GetElementIDs())
				{
					Indices.Add(Vertex, Index++);
					const FVector3f P = Positions[Vertex];
					Obj += FString::Printf(TEXT("v %.9g %.9g %.9g\n"), P.X, P.Y, P.Z);
				}
				for (FTriangleID Triangle : Description->Triangles().GetElementIDs())
				{
					const auto Vertices = Description->GetTriangleVertices(Triangle);
					Obj += FString::Printf(TEXT("f %d %d %d\n"), Indices[Vertices[0]], Indices[Vertices[1]], Indices[Vertices[2]]);
				}
				Inventory += FString::Printf(TEXT("%s,%d,%d,%d,%d\n"), *Pair.Key.ToString(), Description->Vertices().Num(), Description->Triangles().Num(), Mesh->GetNaniteSettings().bEnabled, Mesh->HasValidNaniteData());
				if (!FFileHelper::SaveStringToFile(Obj, *(*Directory / (Pair.Key.ToString() + TEXT(".obj"))))) return 1;
			}
			if (!FFileHelper::SaveStringToFile(Inventory, *(*Directory / TEXT("meshes.csv")))) return 1;
		}
	}
	if (const FString* Map = Arguments.Find(TEXT("SaveAsMap")); Map && !bAnalyzeOnly)
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map)
			|| !UEditorLoadingAndSavingUtils::SaveMap(GEditor->GetEditorWorldContext().World(), *Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("SaveAsMap requires a new writable /Game map path.")); return 1; }
	}
	if (FParse::Param(*Params, TEXT("Save")) && !bAnalyzeOnly)
	{
		FString Message;
		if (!FConVerseDatasmithImportService::SaveImportedResult(Result.ManifestAssetPath, Message))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("%s"), *Message); return 1; }
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("%s"), *Message);
	}
	UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("ConVerse optimized import succeeded."));
	return 0;
}
