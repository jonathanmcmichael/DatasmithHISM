#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Trace/Trace.h"

class FConVerseImportTraceCapture
{
public:
	FConVerseImportTraceCapture() = default;

	~FConVerseImportTraceCapture()
	{
		Stop();
	}

	FConVerseImportTraceCapture(const FConVerseImportTraceCapture&) = delete;
	FConVerseImportTraceCapture& operator=(const FConVerseImportTraceCapture&) = delete;

	bool Start(FString& OutError)
	{
		if (UE::Trace::IsTracing())
		{
			OutError = TEXT("An Unreal Insights trace is already active; it was left unchanged.");
			return false;
		}

		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Profiling");
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = TEXT("Could not create the Saved/Profiling directory.");
			return false;
		}

		TraceFilePath = FPaths::ConvertRelativePathToFull(
			Directory / (TEXT("ConVerseOptimizedImport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".utrace")));
		const TArray<FString> ActiveBefore = GetActiveChannels();
		static const TCHAR* RequiredChannels[] = { TEXT("cpu"), TEXT("frame"), TEXT("bookmark"), TEXT("log") };
		TArray<FString> ChannelsToEnable;
		for (const TCHAR* Channel : RequiredChannels)
		{
			if (!ContainsChannel(ActiveBefore, Channel))
			{
				ChannelsToEnable.Emplace(Channel);
			}
		}

		if (!ChannelsToEnable.IsEmpty())
		{
			FTraceAuxiliary::EnableChannels(*FString::Join(ChannelsToEnable, TEXT(",")));
		}

		const TArray<FString> ActiveAfter = GetActiveChannels();
		TArray<FString> EnabledByCapture;
		for (const FString& Channel : ChannelsToEnable)
		{
			if (ContainsChannel(ActiveAfter, *Channel))
			{
				EnabledByCapture.Add(Channel);
			}
		}
		for (const TCHAR* Channel : RequiredChannels)
		{
			if (!ContainsChannel(ActiveAfter, Channel))
			{
				RestoreChannels(EnabledByCapture);
				TraceFilePath.Reset();
				OutError = FString::Printf(TEXT("Unreal Insights could not enable the %s channel."), Channel);
				return false;
			}
		}
		FTraceAuxiliary::FOptions TraceOptions;
		TraceOptions.bExcludeTail = true;
		if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TraceFilePath, nullptr, &TraceOptions))
		{
			RestoreChannels(EnabledByCapture);
			TraceFilePath.Reset();
			OutError = TEXT("Unreal Insights could not open the trace file.");
			return false;
		}
		if (!FTraceAuxiliary::IsConnected())
		{
			FTraceAuxiliary::Stop();
			RestoreChannels(EnabledByCapture);
			TraceFilePath.Reset();
			OutError = TEXT("Unreal Insights did not establish the requested trace file.");
			return false;
		}
		ChannelsToDisable = MoveTemp(EnabledByCapture);
		bStarted = true;
		return true;
	}

	bool Stop()
	{
		if (!bStarted)
		{
			return false;
		}

		bStarted = false;
		FString ActiveDestination = FTraceAuxiliary::GetTraceDestinationString();
		FPaths::ConvertRelativePathToFull(ActiveDestination);
		FPaths::NormalizeFilename(ActiveDestination);
		FString ExpectedDestination = TraceFilePath;
		FPaths::NormalizeFilename(ExpectedDestination);
		const bool bOwnsActiveTrace = FTraceAuxiliary::IsConnected()
			&& FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File
			&& ActiveDestination.Equals(ExpectedDestination, ESearchCase::IgnoreCase);
		const bool bTraceStopped = bOwnsActiveTrace && FTraceAuxiliary::Stop();
		RestoreChannels(ChannelsToDisable);
		return bTraceStopped && IFileManager::Get().FileExists(*TraceFilePath);
	}

	const FString& GetTraceFilePath() const
	{
		return TraceFilePath;
	}

	bool IsStarted() const
	{
		return bStarted;
	}

private:
	static TArray<FString> GetActiveChannels()
	{
		TStringBuilder<256> ActiveChannelBuilder;
		FTraceAuxiliary::GetActiveChannelsString(ActiveChannelBuilder);
		const FString ActiveChannels = ActiveChannelBuilder.ToString();
		TArray<FString> Channels;
		ActiveChannels.ParseIntoArray(Channels, TEXT(","), true);
		for (FString& Channel : Channels)
		{
			Channel.TrimStartAndEndInline();
		}
		return Channels;
	}

	static bool ContainsChannel(const TArray<FString>& Channels, const TCHAR* Name)
	{
		return Channels.ContainsByPredicate([Name](const FString& Channel)
		{
			return Channel.Equals(Name, ESearchCase::IgnoreCase);
		});
	}

	static void RestoreChannels(TArray<FString>& ChannelsToRestore)
	{
		if (!ChannelsToRestore.IsEmpty())
		{
			FTraceAuxiliary::DisableChannels(*FString::Join(ChannelsToRestore, TEXT(",")));
			ChannelsToRestore.Reset();
		}
	}

	FString TraceFilePath;
	TArray<FString> ChannelsToDisable;
	bool bStarted = false;
};