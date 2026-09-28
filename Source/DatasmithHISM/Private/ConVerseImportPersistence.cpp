#include "ConVerseDatasmithImportService.h"
#include "ConVerseImportProcessing.h"
#include "ConVerseOptimizedImportManifest.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

bool FConVerseDatasmithImportService::SavePreset(const FString& FilePath, const FConVerseOptimizedImportOptions& Options, FString& OutError)
{
	auto Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("Version"), 1);
	Json->SetStringField(TEXT("Source"), Options.FilePath);
	Json->SetStringField(TEXT("Destination"), Options.DestinationPath);
	Json->SetStringField(TEXT("InstanceType"), Options.InstanceType == EConVerseOptimizedInstanceType::ISM ? TEXT("ISM") : TEXT("HISM"));
	Json->SetNumberField(TEXT("MinimumInstances"), Options.MinimumInstanceCount);
	Json->SetNumberField(TEXT("ChordTolerance"), Options.Tessellation.ChordTolerance);
	Json->SetNumberField(TEXT("NormalTolerance"), Options.Tessellation.NormalTolerance);
	Json->SetNumberField(TEXT("MaxEdgeLength"), Options.Tessellation.MaxEdgeLength);
	Json->SetNumberField(TEXT("Stitching"), int32(Options.Tessellation.StitchingTechnique));
	Json->SetStringField(TEXT("Processing"), ConVerseImportProcessing::SettingsJson(Options.Processing));
	TArray<TSharedPtr<FJsonValue>> Folders;
	for (const FString& Folder : Options.TextureSearchFolders) Folders.Add(MakeShared<FJsonValueString>(Folder));
	Json->SetArrayField(TEXT("TextureSearchFolders"), Folders);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
	if (!FFileHelper::SaveStringToFile(Text, *FilePath)) { OutError = TEXT("Could not save import preset: ") + FilePath; return false; }
	return true;
}

bool FConVerseDatasmithImportService::LoadPreset(const FString& FilePath, FConVerseOptimizedImportOptions& Options, FString& OutError)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *FilePath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{ OutError = TEXT("Cannot read import preset: ") + FilePath; return false; }
	double Version = 0;
	FString Processing, Mode;
	FConVerseOptimizedImportOptions Loaded;
	if (!Json->TryGetNumberField(TEXT("Version"), Version) || Version != 1
		|| !Json->TryGetStringField(TEXT("Processing"), Processing)
		|| !FJsonObjectConverter::JsonObjectStringToUStruct(Processing, &Loaded.Processing)
		|| !Json->TryGetStringField(TEXT("Source"), Loaded.FilePath)
		|| !Json->TryGetStringField(TEXT("Destination"), Loaded.DestinationPath)
		|| !Json->TryGetStringField(TEXT("InstanceType"), Mode) || (Mode != TEXT("ISM") && Mode != TEXT("HISM")))
	{ OutError = TEXT("Unsupported or incomplete import preset."); return false; }
	Loaded.InstanceType = Mode == TEXT("ISM") ? EConVerseOptimizedInstanceType::ISM : EConVerseOptimizedInstanceType::HISM;
	int32 Stitching = 0;
	if (!Json->TryGetNumberField(TEXT("MinimumInstances"), Loaded.MinimumInstanceCount)
		|| !Json->TryGetNumberField(TEXT("ChordTolerance"), Loaded.Tessellation.ChordTolerance)
		|| !Json->TryGetNumberField(TEXT("NormalTolerance"), Loaded.Tessellation.NormalTolerance)
		|| !Json->TryGetNumberField(TEXT("MaxEdgeLength"), Loaded.Tessellation.MaxEdgeLength)
		|| !Json->TryGetNumberField(TEXT("Stitching"), Stitching) || Stitching < 0 || Stitching > 2)
	{ OutError = TEXT("Import preset has invalid tessellation or grouping fields."); return false; }
	Loaded.Tessellation.StitchingTechnique = EConVerseStitchingTechnique(Stitching);
	// Optional: presets written before texture search folders keep the defaults.
	const TArray<TSharedPtr<FJsonValue>>* Folders = nullptr;
	if (Json->TryGetArrayField(TEXT("TextureSearchFolders"), Folders))
	{
		Loaded.TextureSearchFolders.Reset();
		for (const TSharedPtr<FJsonValue>& Folder : *Folders)
		{
			FString Path;
			if (!Folder.IsValid() || !Folder->TryGetString(Path)) { OutError = TEXT("Import preset has an invalid texture search folder."); return false; }
			Loaded.TextureSearchFolders.Add(Path);
		}
	}
	Options = MoveTemp(Loaded);
	return true;
}

bool FConVerseDatasmithImportService::SaveImportedResult(const FSoftObjectPath& ManifestPath, FString& OutMessage, bool* OutVerified)
{
	if (OutVerified) *OutVerified = false;
	UConVerseOptimizedImportManifest* Manifest = Cast<UConVerseOptimizedImportManifest>(ManifestPath.ResolveObject());
	if (!Manifest || Manifest->CommitState != EConVerseOptimizedImportCommitState::Active)
	{ OutMessage = TEXT("No active imported result is available to save."); return false; }
	if (Manifest->ManifestSchemaVersion > 2 || Manifest->ManifestSchemaVersion < 1)
	{ OutMessage = TEXT("This manifest schema cannot be safely saved by this plugin version."); return false; }
	UWorld* World = Cast<UWorld>(Manifest->WorldPath.ResolveObject());
	if (!World && GEditor)
	{
		// Save As changes object paths. Rebind only on an explicit save and only after every
		// actor GUID and the session-owner tag agree. A different/partial world remains blocked.
		UWorld* Candidate = GEditor->GetEditorWorldContext().World();
		TMap<FGuid, AActor*> Actors;
		if (Candidate)
			for (TActorIterator<AActor> It(Candidate); It; ++It) Actors.Add(It->GetActorGuid(), *It);
		AActor* Owner = Actors.FindRef(Manifest->DatasmithSceneActorGuid);
		bool bMatches = Owner && Owner->Tags.Contains(FName(*(TEXT("ConVerseImportSession=") + Manifest->SessionId)));
		for (const auto& Record : Manifest->CreatedActors)
		{
			const AActor* Actor = Actors.FindRef(Record.ActorGuid);
			bMatches &= Actor && FSoftObjectPath(Actor).GetSubPathString() == Record.ActorPath.GetSubPathString();
		}
		if (bMatches && Candidate && !Candidate->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))
		{
			const FString OldPrefix = Manifest->WorldPath.ToString();
			const FString NewPrefix = Candidate->GetPathName();
			auto Rebase = [&](FSoftObjectPath& Path)
			{
				const FString Old = Path.ToString();
				if (Old == OldPrefix || Old.StartsWith(OldPrefix + TEXT(":"))) Path = FSoftObjectPath(NewPrefix + Old.Mid(OldPrefix.Len()));
			};
			Manifest->Modify();
			Rebase(Manifest->WorldPath); Rebase(Manifest->LevelPath); Rebase(Manifest->DatasmithSceneActorPath);
			for (auto& Record : Manifest->CreatedActors) Rebase(Record.ActorPath);
			for (auto& Record : Manifest->ImportedElements) Rebase(Record.ComponentPath);
			for (auto& Record : Manifest->Groups) { Rebase(Record.OutputComponentPath); Rebase(Record.OutputOwnerActorPath); }
			for (auto& Record : Manifest->TrackedObjects)
			{
				Rebase(Record.ObjectPath);
				Record.State.ReplaceInline(*(OldPrefix + TEXT(":")), *(NewPrefix + TEXT(":")), ESearchCase::CaseSensitive);
			}
			World = Candidate;
		}
		else if (Candidate && !Candidate->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))
		{
			OutMessage = FString::Printf(TEXT("Cannot rebind the imported result to %s: actor identity or session ownership does not match. ")
				TEXT("Load the owning map %s and save there, or import an independent copy into a new map and destination. No packages were saved."),
				*Candidate->GetPathName(), *Manifest->WorldPath.ToString());
			return false;
		}
	}
	if (!World || World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))
	{ OutMessage = TEXT("Save the owning level with a permanent name before saving the imported result."); return false; }
	// The engine may already have redirected the soft paths above; tracked-state text still needs the same move.
	bool bRebasedState = false;
	for (auto& Record : Manifest->TrackedObjects)
	{
		const FString Rebased = ConVerseImportProcessing::RebaseTemporaryWorld(*Manifest, Record.State);
		if (Rebased == Record.State) continue;
		if (!bRebasedState) Manifest->Modify();
		bRebasedState = true;
		Record.State = Rebased;
	}
	TArray<UPackage*> Packages;
	for (const FName& Name : Manifest->CreatedPackageNames)
	{
		UPackage* Package = FindPackage(nullptr, *Name.ToString());
		if (!Package) { OutMessage = TEXT("A session package is not loaded; save is incomplete: ") + Name.ToString(); return false; }
		Packages.AddUnique(Package);
	}
	Packages.AddUnique(Manifest->GetOutermost());
	Packages.AddUnique(World->GetOutermost());
	for (UPackage* Package : Packages)
	{
		const bool bMap = Package->ContainsMap();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, bMap ? World : nullptr, *Filename, Args))
		{ OutMessage = TEXT("Save incomplete; the import remains in memory. Could not save: ") + Filename; return false; }
	}
	OutMessage = Manifest->Verification.bAcceptedWithFailedVerification
		? TEXT("Saved, degraded: verification failures remain recorded.") : TEXT("Saved imported result and owning level.");
	TArray<FString> Differences;
	const bool bMatches = ConVerseImportProcessing::CheckState(*Manifest, Differences);
	if (!bMatches) OutMessage = TEXT("Saved, with unverified tracked changes. Saving does not accept a new verification baseline.");
	if (OutVerified) *OutVerified = bMatches && !Manifest->Verification.bAcceptedWithFailedVerification;
	return true;
}


bool FConVerseDatasmithImportService::WriteAttemptCheckpoint(const FConVerseOptimizedImportResult& Result, bool bTerminal)
{
	if (Result.SessionId.IsEmpty() || Result.AttemptFolder.IsEmpty()) return true;
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Attempts");
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
	auto Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Session"), Result.SessionId);
	Json->SetStringField(TEXT("Source"), Result.SourceFilePath);
	Json->SetStringField(TEXT("OwnedFolder"), Result.AttemptFolder);
	Json->SetStringField(TEXT("Manifest"), Result.ManifestAssetPath.ToString());
	Json->SetStringField(TEXT("Report"), Result.SavedReportPath);
	Json->SetStringField(TEXT("Summary"), Result.Summary);
	Json->SetStringField(TEXT("TimestampUtc"), FDateTime::UtcNow().ToIso8601());
	Json->SetNumberField(TEXT("Stage"), int32(Result.LastCompletedStage));
	Json->SetBoolField(TEXT("Terminal"), bTerminal);
	Json->SetBoolField(TEXT("RecoveryRequired"), !bTerminal || Result.Status == EConVerseOptimizedImportStatus::RollbackFailed
		|| Result.Status == EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision);
	TArray<TSharedPtr<FJsonValue>> Objects;
	for (const auto& Record : Result.TrackedObjects) Objects.Add(MakeShared<FJsonValueString>(Record.ObjectPath.ToString()));
	Json->SetArrayField(TEXT("ObservedObjects"), Objects);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	const FString File = Directory / (Result.SessionId + TEXT(".json"));
	const FString Temporary = File + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Text, *Temporary) && IFileManager::Get().Move(*File, *Temporary, true, false);
}

TArray<FString> FConVerseDatasmithImportService::FindInterruptedAttempts()
{
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Attempts");
	TArray<FString> Files, Results;
	IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		bool bRecovery = true;
		const bool bReadable = FFileHelper::LoadFileToString(Text, *(Directory / File))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
		if (bReadable)
			Json->TryGetBoolField(TEXT("RecoveryRequired"), bRecovery);
		if (!bRecovery) continue;
		FString Diagnostic = TEXT("Review interrupted or incomplete attempt; no automatic cleanup: ") + Directory / File;
		if (!bReadable)
		{
			Diagnostic += TEXT("\n  Checkpoint is unreadable; object ownership is unknown.");
		}
		else
		{
			for (const TCHAR* Field : { TEXT("Session"), TEXT("Source"), TEXT("OwnedFolder"), TEXT("Manifest"), TEXT("Report"), TEXT("Summary") })
			{
				FString Value;
				if (Json->TryGetStringField(Field, Value) && !Value.IsEmpty())
					Diagnostic += FString::Printf(TEXT("\n  Recorded %s: %s"), Field, *Value);
			}
			const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
			if (Json->TryGetArrayField(TEXT("ObservedObjects"), Objects))
				for (const auto& Object : *Objects)
				{
					FString Path;
					if (Object && Object->TryGetString(Path) && !Path.IsEmpty())
						Diagnostic += TEXT("\n  Observed object (ownership unproven): ") + Path;
				}
		}
		Results.Add(MoveTemp(Diagnostic));
	}
	return Results;
}
