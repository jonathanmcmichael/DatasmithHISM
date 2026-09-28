#include "ConVerseDatasmithImportService.h"
#include "ConVerseImportProcessing.h"
#include "ConVerseOptimizedImportCommandlet.h"
#include "ConVerseOptimizedImportManifest.h"
#include "ConVerseSourceMetadata.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DatasmithScene.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ConVersePersistenceAutomation
{
	struct FFixture
	{
		FString Root = TEXT("/Game/__ConVersePersistence/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FConVerseOptimizedImportOptions Options;
		FConVerseOptimizedImportResult Result;
		UConVerseOptimizedImportManifest* Manifest = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("Disposable map tests require -unattended"), FApp::IsUnattended())) return false;
			UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
			if (!Test.TestTrue(TEXT("Create named disposable map"), World && UEditorLoadingAndSavingUtils::SaveMap(World, Root / TEXT("Original")))) return false;
			Options.FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("DatasmithHISM/Tests/Fixtures/Joist16K6/Joist16K6.udatasmith"));
			Options.DestinationPath = Root / TEXT("Import");
			Options.bAutomated = true;
			Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
			Result = FConVerseDatasmithImportService::ImportAndVerify(Options);
			Manifest = Cast<UConVerseOptimizedImportManifest>(Result.ManifestAssetPath.ResolveObject());
			return Test.TestTrue(TEXT("Fixture import verifies"), Result.Status == EConVerseOptimizedImportStatus::Verified)
				&& Test.TestNotNull(TEXT("Fixture manifest"), Manifest);
		}

		bool Save(FAutomationTestBase& Test)
		{
			FString Message;
			bool bVerified = false;
			const bool bSaved = FConVerseDatasmithImportService::SaveImportedResult(Result.ManifestAssetPath, Message, &bVerified);
			Test.AddInfo(Message);
			return Test.TestTrue(TEXT("Fixture saves and stays verified"), bSaved && bVerified);
		}
	};

	// A bounded zero-quota fixture: refuse opening only this map's temporary write.
	// Other package saves use the real filesystem. No project drive is filled.
	class FMapWriteQuota final : public IPlatformFile
	{
	public:
		IPlatformFile* Inner = nullptr;
		FString MapName;
		int32 RefusedWrites = 0;
		virtual bool Initialize(IPlatformFile* InInner, const TCHAR*) override { Inner = InInner; return true; }
		virtual IPlatformFile* GetLowerLevel() override { return Inner; }
		virtual void SetLowerLevel(IPlatformFile* Value) override { Inner = Value; }
		virtual const TCHAR* GetName() const override { return TEXT("ConVerseMapWriteQuota"); }
		virtual bool FileExists(const TCHAR* P) override { return Inner->FileExists(P); }
		virtual int64 FileSize(const TCHAR* P) override { return Inner->FileSize(P); }
		virtual bool DeleteFile(const TCHAR* P) override { return Inner->DeleteFile(P); }
		virtual bool IsReadOnly(const TCHAR* P) override { return Inner->IsReadOnly(P); }
		virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override { return Inner->MoveFile(To, From); }
		virtual bool SetReadOnly(const TCHAR* P, bool Value) override { return Inner->SetReadOnly(P, Value); }
		virtual FDateTime GetTimeStamp(const TCHAR* P) override { return Inner->GetTimeStamp(P); }
		virtual void SetTimeStamp(const TCHAR* P, FDateTime Value) override { Inner->SetTimeStamp(P, Value); }
		virtual FDateTime GetAccessTimeStamp(const TCHAR* P) override { return Inner->GetAccessTimeStamp(P); }
		virtual FString GetFilenameOnDisk(const TCHAR* P) override { return Inner->GetFilenameOnDisk(P); }
		virtual IFileHandle* OpenRead(const TCHAR* P, bool AllowWrite = false) override { return Inner->OpenRead(P, AllowWrite); }
		virtual IFileHandle* OpenWrite(const TCHAR* P, bool Append = false, bool AllowRead = false) override
		{
			const FString Filename = FPaths::GetCleanFilename(P);
			if (Filename.StartsWith(MapName) && Filename.EndsWith(TEXT(".tmp"))) { ++RefusedWrites; return nullptr; }
			return Inner->OpenWrite(P, Append, AllowRead);
		}
		virtual bool DirectoryExists(const TCHAR* P) override { return Inner->DirectoryExists(P); }
		virtual bool CreateDirectory(const TCHAR* P) override { return Inner->CreateDirectory(P); }
		virtual bool DeleteDirectory(const TCHAR* P) override { return Inner->DeleteDirectory(P); }
		virtual FFileStatData GetStatData(const TCHAR* P) override { return Inner->GetStatData(P); }
		virtual bool IterateDirectory(const TCHAR* P, FDirectoryVisitor& V) override { return Inner->IterateDirectory(P, V); }
		virtual bool IterateDirectoryStat(const TCHAR* P, FDirectoryStatVisitor& V) override { return Inner->IterateDirectoryStat(P, V); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseNamedMapSaveAsTest,
	"DatasmithHISM.Persistence.NamedMapSaveAsDoesNotClaimSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseNamedMapSaveAsTest::RunTest(const FString&)
{
	using namespace ConVersePersistenceAutomation;
	FFixture Fixture;
	if (!Fixture.Create(*this) || !Fixture.Save(*this)) return false;
	const FSoftObjectPath OriginalWorld = Fixture.Manifest->WorldPath;
	const FString OriginalSession = Fixture.Manifest->SessionId;
	const FString CopyMap = Fixture.Root / TEXT("Copy");
	TStrongObjectPtr<UConVerseOptimizedImportCommandlet> Commandlet(NewObject<UConVerseOptimizedImportCommandlet>());
	AddExpectedError(TEXT("Named-level Save As"), EAutomationExpectedErrorFlags::Contains, 1);
	const int32 ExitCode = Commandlet->Main(FString::Printf(
		TEXT("-Source=\"%s\" -Destination=%s -Nanite=Preserve -SaveAsMap=%s -Save"),
		*Fixture.Options.FilePath, *Fixture.Options.DestinationPath, *CopyMap));
	TestEqual(TEXT("Unsupported identity-changing map copy returns failure"), ExitCode, 1);
	TestFalse(TEXT("Refusal happens before creating a misleading copy"), FPackageName::DoesPackageExist(CopyMap));
	TestEqual(TEXT("Original owner retained"), Fixture.Manifest->WorldPath, OriginalWorld);
	TestEqual(TEXT("Original session retained"), Fixture.Manifest->SessionId, OriginalSession);
	const auto Again = FConVerseDatasmithImportService::ImportAndVerify(Fixture.Options);
	TestTrue(TEXT("Original result still re-verifies without a new session"), Again.Status == EConVerseOptimizedImportStatus::AlreadyCurrent && Again.bVerificationSucceeded && Again.SessionId.IsEmpty());
	TStrongObjectPtr<UConVerseOptimizedImportManifest> Manifest(Fixture.Manifest);
	TestTrue(TEXT("Exercise native named-map duplication separately"), UEditorLoadingAndSavingUtils::SaveMap(GEditor->GetEditorWorldContext().World(), CopyMap));
	if (!TestNotNull(TEXT("Load the native copy"), UEditorLoadingAndSavingUtils::LoadMap(CopyMap))) return false;
	TSet<FGuid> CopiedGuids;
	for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It) CopiedGuids.Add(It->GetActorGuid());
	TestFalse(TEXT("Native copy does not preserve the original owner GUID"), CopiedGuids.Contains(Manifest->DatasmithSceneActorGuid));
	FString Message;
	bool bVerified = true;
	TestFalse(TEXT("Explicit save refuses the native copy's unproven ownership"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
	TestTrue(TEXT("Refusal explains identity and a recovery action"), Message.Contains(TEXT("Cannot rebind")) && Message.Contains(TEXT("Load the owning map")));
	TestEqual(TEXT("Refused copy leaves manifest bound to original"), Manifest->WorldPath, OriginalWorld);
	TestFalse(TEXT("Refused copy is not verified"), bVerified);
	AddInfo(Message);
	if (!TestNotNull(TEXT("Original owning map remains loadable"), UEditorLoadingAndSavingUtils::LoadMap(OriginalWorld.GetLongPackageName()))) return false;
	const auto Restored = FConVerseDatasmithImportService::ImportAndVerify(Fixture.Options);
	TestTrue(TEXT("Returning to original map restores unchanged-source verification"), Restored.Status == EConVerseOptimizedImportStatus::AlreadyCurrent && Restored.bVerificationSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVersePartialSaveTest,
	"DatasmithHISM.Persistence.PartialSaveAndManualDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVersePartialSaveTest::RunTest(const FString&)
{
	using namespace ConVersePersistenceAutomation;
	FFixture Fixture;
	if (!Fixture.Create(*this)) return false;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	const FString MapFile = FPackageName::LongPackageNameToFilename(World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
	const FString OriginalHash = LexToString(FMD5Hash::HashFile(*MapFile));
	FString Message;
	bool bVerified = true;
	AddExpectedError(TEXT("Error opening file"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Could not create temporary save filename"), EAutomationExpectedErrorFlags::Contains, 1);
	{
		FMapWriteQuota Quota;
		IPlatformFile& Original = FPlatformFileManager::Get().GetPlatformFile();
		Quota.Initialize(&Original, TEXT(""));
		Quota.MapName = World->GetName();
		FPlatformFileManager::Get().SetPlatformFile(Quota);
		ON_SCOPE_EXIT { FPlatformFileManager::Get().SetPlatformFile(Original); };
		TestFalse(TEXT("Simulated map capacity failure cannot report saved"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
		TestTrue(TEXT("Real SavePackage reached the bounded write refusal"), Quota.RefusedWrites > 0);
	}
	AddInfo(TEXT("SIMULATED zero-capacity map write: ") + Message);
	TestFalse(TEXT("Failed persistence does not set verified output"), bVerified);
	TestTrue(TEXT("Failed path is named"), Message.Contains(TEXT("Save incomplete")) && Message.Contains(TEXT("Original.umap")));
	TestEqual(TEXT("Previously saved map bytes unchanged"), LexToString(FMD5Hash::HashFile(*MapFile)), OriginalHash);
	TestTrue(TEXT("Earlier scene package was actually saved before map failure"), FPackageName::DoesPackageExist(Fixture.Manifest->GetOutermost()->GetName()));
	if (!Fixture.Save(*this)) return false;
	const FString AssetFile = FPackageName::LongPackageNameToFilename(Fixture.Manifest->CreatedPackageNames.Last().ToString(), FPackageName::GetAssetPackageExtension());
	const FString AssetHash = LexToString(FMD5Hash::HashFile(*AssetFile));
	{
		const bool bWasReadOnly = IFileManager::Get().IsReadOnly(*AssetFile);
		ON_SCOPE_EXIT { FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, bWasReadOnly); };
		if (!TestTrue(TEXT("Make only a disposable owned asset read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, true))) return false;
		AddExpectedError(TEXT("as it is read only!"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Actual read-only asset failure cannot report saved"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
		AddInfo(TEXT("ACTUAL read-only owned asset: ") + Message);
		TestTrue(TEXT("Actual failure identifies the asset"), Message.Contains(AssetFile));
		TestEqual(TEXT("Failed asset bytes unchanged"), LexToString(FMD5Hash::HashFile(*AssetFile)), AssetHash);
	}
	if (!Fixture.Save(*this)) return false;
	auto* Component = Cast<UInstancedStaticMeshComponent>(Fixture.Manifest->Groups[0].OutputComponentPath.ResolveObject());
	if (!TestNotNull(TEXT("Instanced output"), Component)) return false;
	FTransform Placement;
	Component->GetInstanceTransform(0, Placement, false);
	Placement.AddToTranslation(FVector(75, 0, 0));
	Component->UpdateInstanceTransform(0, Placement, false, true);
	const FString Baseline = Fixture.Manifest->TrackedObjects[0].State;
	TestTrue(TEXT("Edited result may persist"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
	TestFalse(TEXT("Saving drift never claims verification"), bVerified);
	TestTrue(TEXT("Saved drift is explicit"), Message.Contains(TEXT("unverified tracked changes")));
	TestEqual(TEXT("Saving does not replace the tracked baseline"), Fixture.Manifest->TrackedObjects[0].State, Baseline);
	const auto Again = FConVerseDatasmithImportService::ImportAndVerify(Fixture.Options);
	TestTrue(TEXT("Saved drift still fails re-verification"), Again.Status == EConVerseOptimizedImportStatus::AlreadyCurrent && !Again.bVerificationSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseUnnamedMapFirstSaveTest,
	"DatasmithHISM.Persistence.UnnamedMapFirstSaveStaysVerified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseUnnamedMapFirstSaveTest::RunTest(const FString&)
{
	using namespace ConVersePersistenceAutomation;
	// Import into an untitled level, then name it. The editor's Save As also saves the dirty manifest,
	// which lets the engine redirect its soft paths but leaves tracked-state text on the /Temp/ world.
	// Cover both that path and a map-only save that relies on explicit-save rebinding.
	if (!TestTrue(TEXT("Disposable map tests require -unattended"), FApp::IsUnattended())) return false;
	auto Run = [this](bool bSaveManifestWithMap)
	{
		const FString Variant = bSaveManifestWithMap ? TEXT("[map and manifest] ") : TEXT("[map only] ");
		FFixture Fixture;
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!TestTrue(Variant + TEXT("Start in an untitled level"), World && World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))) return;
		Fixture.Options.FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("DatasmithHISM/Tests/Fixtures/Joist16K6/Joist16K6.udatasmith"));
		Fixture.Options.DestinationPath = Fixture.Root / TEXT("Import");
		Fixture.Options.bAutomated = true;
		Fixture.Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
		Fixture.Result = FConVerseDatasmithImportService::ImportAndVerify(Fixture.Options);
		Fixture.Manifest = Cast<UConVerseOptimizedImportManifest>(Fixture.Result.ManifestAssetPath.ResolveObject());
		if (!TestTrue(Variant + TEXT("Import into untitled level verifies"), Fixture.Result.Status == EConVerseOptimizedImportStatus::Verified && Fixture.Manifest)) return;
		FString Message;
		bool bVerified = true;
		TestFalse(Variant + TEXT("Untitled level cannot be saved yet"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
		if (!TestTrue(Variant + TEXT("Name the level through the editor"), UEditorLoadingAndSavingUtils::SaveMap(GEditor->GetEditorWorldContext().World(), Fixture.Root / TEXT("Named")))) return;
		if (bSaveManifestWithMap)
		{
			TestTrue(Variant + TEXT("Save the dirty manifest as the editor does"), UEditorLoadingAndSavingUtils::SavePackages({Fixture.Manifest->GetOutermost()}, false));
			TestFalse(Variant + TEXT("Engine redirected the manifest's world path"), Fixture.Manifest->WorldPath.ToString().StartsWith(TEXT("/Temp/")));
			TestTrue(Variant + TEXT("Tracked-state text still names the untitled world"), Fixture.Manifest->TrackedObjects.ContainsByPredicate(
				[](const FConVerseTrackedObjectState& Record) { return Record.State.Contains(TEXT("/Temp/")); }));
			TArray<FString> Differences;
			const bool bMatches = ConVerseImportProcessing::CheckState(*Fixture.Manifest, Differences);
			for (const FString& Difference : Differences) AddInfo(Difference);
			TestTrue(Variant + TEXT("Naming the level is not tracked drift"), bMatches);
			AActor* Owner = Cast<AActor>(Fixture.Manifest->DatasmithSceneActorPath.ResolveObject());
			const FName SessionTag(*(TEXT("ConVerseImportSession=") + Fixture.Manifest->SessionId));
			if (TestNotNull(Variant + TEXT("Session owner resolves"), Owner) && TestTrue(Variant + TEXT("Owner carries session tag"), Owner->Tags.Contains(SessionTag)))
			{
				Owner->Tags.Remove(SessionTag);
				Differences.Reset();
				TestFalse(Variant + TEXT("Unproven ownership keeps the /Temp/ state as drift"), ConVerseImportProcessing::CheckState(*Fixture.Manifest, Differences));
				Owner->Tags.Add(SessionTag);
				Differences.Reset();
				TestTrue(Variant + TEXT("Restored ownership proof accepts the rename"), ConVerseImportProcessing::CheckState(*Fixture.Manifest, Differences));
			}
		}
		TestTrue(Variant + TEXT("First explicit save succeeds"), FConVerseDatasmithImportService::SaveImportedResult(Fixture.Result.ManifestAssetPath, Message, &bVerified));
		AddInfo(Variant + Message);
		TestTrue(Variant + TEXT("First explicit save stays verified"), bVerified);
		TestFalse(Variant + TEXT("No false drift message"), Message.Contains(TEXT("unverified tracked changes")));
		TestFalse(Variant + TEXT("Saved tracked state names the permanent world"), Fixture.Manifest->TrackedObjects.ContainsByPredicate(
			[](const FConVerseTrackedObjectState& Record) { return Record.State.Contains(TEXT("/Temp/")); }));
		const auto Again = FConVerseDatasmithImportService::ImportAndVerify(Fixture.Options);
		TestTrue(Variant + TEXT("Named level re-verifies without a new session"), Again.Status == EConVerseOptimizedImportStatus::AlreadyCurrent && Again.bVerificationSucceeded && Again.SessionId.IsEmpty());
		auto* Component = Cast<UInstancedStaticMeshComponent>(Fixture.Manifest->Groups[0].OutputComponentPath.ResolveObject());
		if (!TestNotNull(Variant + TEXT("Instanced output resolves in the named level"), Component)) return;
		FTransform Placement;
		Component->GetInstanceTransform(0, Placement, false);
		Placement.AddToTranslation(FVector(75, 0, 0));
		Component->UpdateInstanceTransform(0, Placement, false, true);
		TArray<FString> Differences;
		TestFalse(Variant + TEXT("A real edit after naming is still drift"), ConVerseImportProcessing::CheckState(*Fixture.Manifest, Differences));
	};
	Run(false);
	Run(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseRecoveryPathsTest,
	"DatasmithHISM.Persistence.RecoveryListsKnownPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseRecoveryPathsTest::RunTest(const FString&)
{
	FConVerseOptimizedImportResult Interrupted;
	Interrupted.SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Interrupted.AttemptFolder = TEXT("/Game/__ConVerseRecovery/") + Interrupted.SessionId;
	Interrupted.SourceFilePath = TEXT("SyntheticRecoveryFixture.udatasmith");
	Interrupted.LastCompletedStage = EConVerseOptimizedImportStage::ComponentsConverted;
	Interrupted.TrackedObjects.AddDefaulted_GetRef().ObjectPath = FSoftObjectPath(Interrupted.AttemptFolder / TEXT("Unknown.Unknown"));
	const FString Journal = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Attempts") / (Interrupted.SessionId + TEXT(".json"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Journal); };
	TestTrue(TEXT("Write nonterminal checkpoint"), FConVerseDatasmithImportService::WriteAttemptCheckpoint(Interrupted, false));
	const FString Before = LexToString(FMD5Hash::HashFile(*Journal));
	const FString Diagnostics = FString::Join(FConVerseDatasmithImportService::FindInterruptedAttempts(), TEXT("\n"));
	TestTrue(TEXT("Recovery names journal"), Diagnostics.Contains(Interrupted.SessionId + TEXT(".json")));
	TestTrue(TEXT("Recovery names known destination"), Diagnostics.Contains(Interrupted.AttemptFolder));
	TestTrue(TEXT("Recovery lists observed object paths"), Diagnostics.Contains(Interrupted.TrackedObjects[0].ObjectPath.ToString()));
	TestEqual(TEXT("Discovery never changes the checkpoint"), LexToString(FMD5Hash::HashFile(*Journal)), Before);
	TestTrue(TEXT("Complete attempt checkpoint"), FConVerseDatasmithImportService::WriteAttemptCheckpoint(Interrupted, true));
	TestFalse(TEXT("Successful terminal record is not an interrupted attempt"), FString::Join(FConVerseDatasmithImportService::FindInterruptedAttempts(), TEXT("\n")).Contains(Interrupted.SessionId));
	return true;
}

// Explicit process-interruption fixture, deliberately outside the normal DatasmithHISM suite.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseInterruptedProcessFixture,
	"ConVerseAcceptance.InterruptedProcessFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseInterruptedProcessFixture::RunTest(const FString&)
{
	FString Evidence;
	if (!TestTrue(TEXT("Explicit disposable evidence directory required"), FApp::IsUnattended()
		&& FParse::Value(FCommandLine::Get(), TEXT("ConVerseInterruptEvidence="), Evidence))) return false;
	ConVersePersistenceAutomation::FFixture Fixture;
	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World || !UEditorLoadingAndSavingUtils::SaveMap(World, Fixture.Root / TEXT("Interrupted"))) return false;
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("DatasmithHISM/Tests/Fixtures/Joist16K6/Joist16K6.udatasmith"));
	Options.DestinationPath = Fixture.Root / TEXT("InterruptedImport");
	Options.bAutomated = true;
	Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
	bool bCheckpointReached = false;
	Options.CancelRequested = [&]()
	{
		TArray<FString> Files;
		const FString Attempts = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Attempts");
		IFileManager::Get().FindFiles(Files, *(Attempts / TEXT("*.json")), true, false);
		for (const FString& File : Files)
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			if (!FFileHelper::LoadFileToString(Text, *(Attempts / File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json) continue;
			if (!Json->GetStringField(TEXT("OwnedFolder")).StartsWith(Options.DestinationPath + TEXT("/"))
				|| Json->GetIntegerField(TEXT("Stage")) != int32(EConVerseOptimizedImportStage::Verified) || Json->GetBoolField(TEXT("Terminal"))) continue;
			bCheckpointReached = true;
			// Persist known uncommitted packages as a crash fixture. Ownership remains uncertain.
			TSet<UPackage*> Packages;
			for (const auto& Path : Json->GetArrayField(TEXT("ObservedObjects")))
				if (UObject* Object = FSoftObjectPath(Path->AsString()).ResolveObject()) Packages.Add(Object->GetOutermost());
			Packages.Add(World->GetOutermost());
			for (UPackage* Package : Packages)
			{
				FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
				const bool bMap = Package->ContainsMap();
				const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
				if (!UPackage::SavePackage(Package, bMap ? World : nullptr, *Filename, Args)) { AddError(TEXT("Could not save disposable interruption fixture")); return true; }
			}
			Json->SetStringField(TEXT("JournalFile"), Attempts / File);
			Json->SetStringField(TEXT("Map"), World->GetOutermost()->GetName());
			FString Ready; FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Ready));
			IFileManager::Get().MakeDirectory(*Evidence, true);
			FFileHelper::SaveStringToFile(Ready, *(Evidence / TEXT("ready.json")));
			AddInfo(TEXT("Disposable process ready for termination at verified, uncommitted checkpoint."));
			FPlatformProcess::Sleep(45.0f);
			AddError(TEXT("External interruption did not occur within 45 seconds; cancelling normally."));
			return true;
		}
		return false;
	};
	FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Verified checkpoint reached"), bCheckpointReached);
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseInterruptedRecoveryProbe,
	"ConVerseAcceptance.InterruptedRecoveryProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseInterruptedRecoveryProbe::RunTest(const FString&)
{
	FString Evidence, Text;
	TSharedPtr<FJsonObject> Ready;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ConVerseInterruptEvidence="), Evidence)
		|| !FFileHelper::LoadFileToString(Text, *(Evidence / TEXT("ready.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ready) || !Ready) return false;
	const FString Diagnostics = FString::Join(FConVerseDatasmithImportService::FindInterruptedAttempts(), TEXT("\n"));
	TestTrue(TEXT("Restart discovers the interrupted journal"), Diagnostics.Contains(Ready->GetStringField(TEXT("Session"))));
	TestTrue(TEXT("Restart lists the known attempt folder"), Diagnostics.Contains(Ready->GetStringField(TEXT("OwnedFolder"))));
	for (const auto& Path : Ready->GetArrayField(TEXT("ObservedObjects")))
		TestTrue(TEXT("Restart lists observed path: ") + Path->AsString(), Diagnostics.Contains(Path->AsString()));
	FFileHelper::SaveStringToFile(Diagnostics, *(Evidence / TEXT("recovery.txt")));
	return true;
}
#endif
