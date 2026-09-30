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

	bool Update(EConVerseImportWorkPhase Phase, int64 Completed, int64 Total)
	{
		if (bCancelled) return false;
		if (Options.ProgressObserver) Options.ProgressObserver(Phase, Completed, Total);
		const FText Label = PhaseLabel(Phase);
		Task.FrameMessage = Total > 0
			? FText::Format(NSLOCTEXT("ConVerseHISM", "WorkCount", "{0}: {1} / {2}"), Label, FText::AsNumber(Completed), FText::AsNumber(Total))
			: FText::Format(NSLOCTEXT("ConVerseHISM", "WorkVisited", "{0}: {1}"), Label, FText::AsNumber(Completed));
		Task.TickProgress();
		return !IsCancelled();
	}

	EConVerseImportWorkResult HashFile(const FString& File, FString& OutHash, int64& OutSize, EConVerseImportWorkPhase Phase)
	{
		OutHash.Reset();
		OutSize = IFileManager::Get().FileSize(*File);
		if (!Update(Phase, 0, FMath::Max(int64(0), OutSize))) return EConVerseImportWorkResult::Cancelled;
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
			if (!Update(Phase, Read, OutSize)) return EConVerseImportWorkResult::Cancelled;
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
