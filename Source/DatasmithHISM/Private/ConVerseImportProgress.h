#pragma once

#include "ConVerseDatasmithImportService.h"
#include "HAL/FileManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"

enum class EConVerseImportWorkResult : uint8
{
	Completed,
	Cancelled,
	Failed
};

/**
 * Aggregate position of a per-file operation (e.g. hashing one sidecar file) within a known set of
 * files, so the progress label can disambiguate "bytes read in this file" from "bytes read overall".
 * Optional: only supplied by callers that enumerated the whole set up front, such as HashDirectory.
 */
struct FConVerseImportOverallByteContext
{
	/** 1-based position of the current file among ItemCount files. */
	int32 ItemIndex = 0;
	int32 ItemCount = 0;
	/** Bytes read in prior files, i.e. the running total excluding the current file's own progress. */
	int64 BytesBeforeCurrentItem = 0;
	/** Sum of all files' sizes in the set, known ahead of time from a pre-pass over the file list. */
	int64 TotalBytes = 0;
};

/** One cooperative cancellation latch for all pre-mutation work in an operation. */
class FConVerseImportProgress
{
public:
	FConVerseImportProgress(const FConVerseOptimizedImportOptions& InOptions, FScopedSlowTask& InTask)
		: Options(InOptions), Task(InTask) {}

	bool IsCancelled()
	{
		bCancelled = bCancelled || Task.ShouldCancel() || (Options.CancelRequested && Options.CancelRequested());
		return bCancelled;
	}

	/**
	 * Pure text formatting, split out so the "which bytes am I looking at" question can be covered by
	 * a plain unit test without a live FScopedSlowTask. Completed/Total are always the current item's
	 * own progress (e.g. bytes read in the file being hashed right now); Overall, when supplied, adds
	 * the file's position and the aggregate byte count across the whole known set, so a label like
	 * "bytes 0 / 3,453" cannot be misread as overall progress on a much larger sidecar.
	 */
	static FText FormatWorkLabel(const FText& Label, int64 Completed, int64 Total, const FConVerseImportOverallByteContext* Overall = nullptr)
	{
		if (Overall && Overall->ItemCount > 0)
		{
			const int64 OverallCompleted = Overall->BytesBeforeCurrentItem + Completed;
			return FText::Format(
				NSLOCTEXT("ConVerseHISM", "WorkCountOverall", "{0}: file {1} of {2} ({3} of {4} bytes)"),
				Label, FText::AsNumber(Overall->ItemIndex), FText::AsNumber(Overall->ItemCount),
				FText::AsNumber(OverallCompleted), FText::AsNumber(Overall->TotalBytes));
		}
		return Total > 0
			? FText::Format(NSLOCTEXT("ConVerseHISM", "WorkCount", "{0}: {1} / {2}"), Label, FText::AsNumber(Completed), FText::AsNumber(Total))
			: FText::Format(NSLOCTEXT("ConVerseHISM", "WorkVisited", "{0}: {1}"), Label, FText::AsNumber(Completed));
	}

	bool Update(EConVerseImportWorkPhase Phase, int64 Completed, int64 Total, const FConVerseImportOverallByteContext* Overall = nullptr)
	{
		if (bCancelled) return false;
		// The observer contract is per-item Completed/Total regardless of Overall, so automation that
		// hooks ProgressObserver (e.g. cancelling once a chunk exceeds a threshold) is unaffected.
		if (Options.ProgressObserver) Options.ProgressObserver(Phase, Completed, Total);
		Task.FrameMessage = FormatWorkLabel(PhaseLabel(Phase), Completed, Total, Overall);
		Task.TickProgress();
		return !IsCancelled();
	}

	EConVerseImportWorkResult HashFile(const FString& File, FString& OutHash, int64& OutSize, EConVerseImportWorkPhase Phase,
		const FConVerseImportOverallByteContext* Overall = nullptr)
	{
		OutHash.Reset();
		OutSize = IFileManager::Get().FileSize(*File);
		if (!Update(Phase, 0, FMath::Max(int64(0), OutSize), Overall)) return EConVerseImportWorkResult::Cancelled;
		if (OutSize < 0) return EConVerseImportWorkResult::Failed;
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
		if (!Reader) return EConVerseImportWorkResult::Failed;
		FMD5 MD5;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		int64 Read = 0;
		while (Read < OutSize)
		{
			if (IsCancelled()) return EConVerseImportWorkResult::Cancelled;
			const int32 Count = int32(FMath::Min<int64>(Buffer.Num(), OutSize - Read));
			Reader->Serialize(Buffer.GetData(), Count);
			if (Reader->IsError()) return EConVerseImportWorkResult::Failed;
			MD5.Update(Buffer.GetData(), Count);
			Read += Count;
			if (!Update(Phase, Read, OutSize, Overall)) return EConVerseImportWorkResult::Cancelled;
		}
		uint8 Digest[16];
		MD5.Final(Digest);
		OutHash = BytesToHex(Digest, 16).ToLower();
		return EConVerseImportWorkResult::Completed;
	}

private:
	static FText PhaseLabel(EConVerseImportWorkPhase Phase)
	{
		switch (Phase)
		{
		case EConVerseImportWorkPhase::SourceHash: return NSLOCTEXT("ConVerseHISM", "WorkSource", "Reading source bytes");
		case EConVerseImportWorkPhase::SidecarHash: return NSLOCTEXT("ConVerseHISM", "WorkSidecar", "Reading supporting asset bytes");
		case EConVerseImportWorkPhase::Translation: return NSLOCTEXT("ConVerseHISM", "WorkTranslation", "Translating source; cancellation is deferred until the translator returns");
		case EConVerseImportWorkPhase::SourceActors: return NSLOCTEXT("ConVerseHISM", "WorkActors", "Analyzing source actors and lights");
		case EConVerseImportWorkPhase::Materials: return NSLOCTEXT("ConVerseHISM", "WorkMaterials", "Analyzing materials");
		case EConVerseImportWorkPhase::TextureHash: return NSLOCTEXT("ConVerseHISM", "WorkTexture", "Fingerprinting texture bytes");
		case EConVerseImportWorkPhase::TextureSearch: return NSLOCTEXT("ConVerseHISM", "WorkTextureSearch", "Searching texture libraries");
		case EConVerseImportWorkPhase::Dependencies: return NSLOCTEXT("ConVerseHISM", "WorkDependencies", "Checking source dependencies");
		case EConVerseImportWorkPhase::GroupPlanning: return NSLOCTEXT("ConVerseHISM", "WorkGroups", "Planning source groups");
		default: return NSLOCTEXT("ConVerseHISM", "WorkReport", "Preparing analysis report");
		}
	}

	const FConVerseOptimizedImportOptions& Options;
	FScopedSlowTask& Task;
	bool bCancelled = false;
};
