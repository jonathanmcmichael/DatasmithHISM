#include "ConVerseOptimizedImportCommandlet.h"

#include "ConVerseDatasmithImportService.h"

#include "Misc/Parse.h"
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
		case EConVerseOptimizedImportStatus::CancelledRolledBack: return TEXT("CancelledRolledBack");
		case EConVerseOptimizedImportStatus::FailedRolledBack: return TEXT("FailedRolledBack");
		case EConVerseOptimizedImportStatus::OptimizedReimportBlocked: return TEXT("OptimizedReimportBlocked");
		case EConVerseOptimizedImportStatus::RollbackFailed: return TEXT("RollbackFailed");
		default: return TEXT("Unknown");
		}
	}

	/**
	 * Only a fully successful outcome is a pass. AlreadyCurrent counts as success for a reimport,
	 * but only when re-verification of the committed output also passed.
	 */
	static bool IsSuccess(const FConVerseOptimizedImportResult& Result, bool bAnalyzeOnly)
	{
		if (bAnalyzeOnly)
		{
			return Result.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded;
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

	const FString* Source = Arguments.Find(TEXT("Source"));
	if (Source == nullptr || Source->IsEmpty())
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
			TEXT("Missing -Source=<path>. Usage: -run=ConVerseOptimizedImport -Source=\"C:/Path/Scene.udatasmith\" ")
			TEXT("[-Destination=/Game/DatasmithOptimized] [-InstanceType=ISM|HISM] [-MinInstances=2] [-AnalyzeOnly]")
			TEXT(" [-ChordTolerance=0.2] [-MaxEdgeLength=0] [-NormalTolerance=20] [-Stitching=None|Sewing|Healing]"));
		return 1;
	}

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = *Source;
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

	const bool bAnalyzeOnly = Switches.ContainsByPredicate(
		[](const FString& Switch) { return Switch.Equals(TEXT("AnalyzeOnly"), ESearchCase::IgnoreCase); });

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

	UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("ConVerse optimized import succeeded."));
	return 0;
}
