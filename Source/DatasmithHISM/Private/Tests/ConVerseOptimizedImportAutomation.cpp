#include "ConVerseDatasmithImportService.h"

#include "ConVerseOptimizedImportManifest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorReimportHandler.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DatasmithExportOptions.h"
#include "DatasmithExporterManager.h"
#include "DatasmithMaterialElements.h"
#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"
#include "DatasmithScene.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ConVerseOptimizedImportAutomation
{
	static constexpr int32 ExpectedOptimizedInstances = 2;
	static constexpr int32 ExpectedSourceMeshActors = 3;

	struct FFixtureFiles
	{
		FString RootDirectory;
		FString SceneFile;
		FString MeshFile;
		FString SceneHash;
		FString MeshHash;
	};

	static FString HashFile(const FString& FilePath)
	{
		return LexToString(FMD5Hash::HashFile(*FilePath));
	}

	static void AddMetadata(
		const TSharedRef<IDatasmithScene>& Scene,
		const TSharedRef<IDatasmithMeshActorElement>& Actor,
		const TCHAR* ElementId)
	{
		const TSharedRef<IDatasmithMetaDataElement> Metadata =
			FDatasmithSceneFactory::CreateMetaData(*FString::Printf(TEXT("Metadata_%s"), Actor->GetName()));
		Metadata->SetAssociatedElement(Actor);

		const TSharedRef<IDatasmithKeyValueProperty> Identity =
			FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("ElementId"));
		Identity->SetPropertyType(EDatasmithKeyValuePropertyType::String);
		Identity->SetValue(ElementId);
		Metadata->AddProperty(Identity);

		const TSharedRef<IDatasmithKeyValueProperty> Custom =
			FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("CustomField"));
		Custom->SetPropertyType(EDatasmithKeyValuePropertyType::String);
		Custom->SetValue(TEXT("Preserved"));
		Metadata->AddProperty(Custom);
		Scene->AddMetaData(Metadata);
	}

	static TSharedRef<IDatasmithMeshActorElement> MakeMeshActor(
		const TSharedRef<IDatasmithScene>& Scene,
		const TSharedRef<IDatasmithActorElement>& Parent,
		const TCHAR* Name,
		const TCHAR* Label,
		const TCHAR* MeshName,
		const TCHAR* MaterialName,
		const TCHAR* ElementId,
		const FTransform& WorldTransform)
	{
		const TSharedRef<IDatasmithMeshActorElement> Actor = FDatasmithSceneFactory::CreateMeshActor(Name);
		Actor->SetLabel(Label);
		Actor->SetStaticMeshPathName(MeshName);
		Actor->AddMaterialOverride(MaterialName, 0);
		Actor->SetLayer(TEXT("FixtureLayer"));
		Actor->SetVisibility(true);
		Actor->SetCastShadow(true);
		Actor->SetMobility(EDatasmithActorMobilityType::Static);
		Actor->SetIsAComponent(false);
		Actor->SetTranslation(WorldTransform.GetTranslation());
		Actor->SetRotation(WorldTransform.GetRotation());
		Actor->SetScale(WorldTransform.GetScale3D());
		Actor->AddTag(*FString::Printf(TEXT("FixtureTag=%s"), ElementId));
		AddMetadata(Scene, Actor, ElementId);
		Parent->AddChild(Actor, EDatasmithActorAttachmentRule::KeepWorldTransform);
		return Actor;
	}

	/**
	 * Exports the fixture scene. Passing an ExistingRoot re-exports over the same canonical source path,
	 * and ExtraInstances appends additional eligible instances so the resulting PlanId differs.
	 */
	static bool CreateFixture(
		FFixtureFiles& OutFixture,
		FString& OutError,
		int32 ExtraInstances = 0,
		const FString& ExistingRoot = FString())
	{
		OutFixture.RootDirectory = ExistingRoot.IsEmpty()
			? FPaths::ProjectSavedDir()
				/ TEXT("DatasmithHISM/Automation")
				/ FGuid::NewGuid().ToString(EGuidFormats::Digits)
			: ExistingRoot;
		if (!IFileManager::Get().MakeDirectory(*OutFixture.RootDirectory, true))
		{
			OutError = TEXT("Could not create the temporary fixture directory.");
			return false;
		}

		FDatasmithExporterManager::Initialize();
		const FString SceneName = TEXT("ConVerseOptimizedImportFixture");
		FDatasmithSceneExporter SceneExporter;
		SceneExporter.SetName(*SceneName);
		SceneExporter.SetOutputPath(*OutFixture.RootDirectory);
		SceneExporter.PreExport();
		IFileManager::Get().MakeDirectory(SceneExporter.GetAssetsOutputPath(), true);

		FDatasmithMesh Mesh;
		Mesh.SetName(TEXT("SharedFixtureMesh"));
		Mesh.SetVerticesCount(4);
		Mesh.SetVertex(0, -50.0f, -50.0f, 0.0f);
		Mesh.SetVertex(1,  50.0f, -50.0f, 0.0f);
		Mesh.SetVertex(2,  50.0f,  50.0f, 0.0f);
		Mesh.SetVertex(3, -50.0f,  50.0f, 0.0f);
		Mesh.SetFacesCount(2);
		Mesh.SetFace(0, 0, 1, 2, 0);
		Mesh.SetFace(1, 0, 2, 3, 0);
		for (int32 CornerIndex = 0; CornerIndex < 6; ++CornerIndex)
		{
			Mesh.SetNormal(CornerIndex, 0.0f, 0.0f, 1.0f);
		}
		Mesh.SetUVChannelsCount(1);
		Mesh.SetUVCount(0, 4);
		Mesh.SetUV(0, 0, 0.0, 0.0);
		Mesh.SetUV(0, 1, 1.0, 0.0);
		Mesh.SetUV(0, 2, 1.0, 1.0);
		Mesh.SetUV(0, 3, 0.0, 1.0);
		Mesh.SetFaceUV(0, 0, 0, 1, 2);
		Mesh.SetFaceUV(1, 0, 0, 2, 3);

		FDatasmithMeshExporter MeshExporter;
		TSharedPtr<IDatasmithMeshElement> MeshElement = MeshExporter.ExportToUObject(
			SceneExporter.GetAssetsOutputPath(), Mesh.GetName(), Mesh, nullptr, EDSExportLightmapUV::Never);
		if (!MeshElement.IsValid())
		{
			OutError = FString::Printf(TEXT("Could not export fixture mesh: %s"), *MeshExporter.GetLastError());
			return false;
		}
		OutFixture.MeshFile = MeshElement->GetFile();

		const TSharedRef<IDatasmithUEPbrMaterialElement> Material =
			FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("FixtureMaterial"));
		Material->SetLabel(TEXT("Fixture Material"));
		IDatasmithMaterialExpressionColor* BaseColor =
			Material->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		if (BaseColor == nullptr)
		{
			OutError = TEXT("Could not create the fixture material expression.");
			return false;
		}
		BaseColor->GetColor() = FLinearColor(0.12f, 0.34f, 0.56f, 1.0f);
		BaseColor->ConnectExpression(Material->GetBaseColor());
		MeshElement->SetMaterial(Material->GetName(), 0);

		const TSharedRef<IDatasmithScene> Scene = FDatasmithSceneFactory::CreateScene(*SceneName);
		Scene->SetHost(TEXT("DatasmithHISM Automation"));
		Scene->SetVendor(TEXT("ConVerse"));
		Scene->SetProductName(TEXT("Optimized Import Fixture"));
		Scene->SetProductVersion(TEXT("1"));
		Scene->SetResourcePath(*OutFixture.RootDirectory);
		Scene->AddMaterial(Material);
		Scene->AddMesh(MeshElement);

		const TSharedRef<IDatasmithActorElement> Parent =
			FDatasmithSceneFactory::CreateActor(TEXT("SharedParent"));
		Parent->SetLabel(TEXT("Shared Parent"));
		Parent->SetTranslation(FVector(100.0, 200.0, 25.0));
		Parent->SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(12.0)));
		Scene->AddActor(Parent);

		MakeMeshActor(
			Scene, Parent, TEXT("Instance_A"), TEXT("Supported Instance A"),
			MeshElement->GetName(), Material->GetName(), TEXT("1001"),
			FTransform(FRotator(0.0, 20.0, 0.0), FVector(175.0, 225.0, 25.0), FVector(1.0)));
		MakeMeshActor(
			Scene, Parent, TEXT("Instance_B"), TEXT("Supported Instance B"),
			MeshElement->GetName(), Material->GetName(), TEXT("1002"),
			FTransform(FRotator(0.0, -35.0, 0.0), FVector(315.0, 260.0, 45.0), FVector(1.25)));
		MakeMeshActor(
			Scene, Parent, TEXT("Instance_Mirrored"), TEXT("Mirrored Negative Instance"),
			MeshElement->GetName(), Material->GetName(), TEXT("1003"),
			FTransform(FRotator(0.0, 5.0, 0.0), FVector(455.0, 290.0, 25.0), FVector(-1.0, 1.0, 1.0)));

		for (int32 ExtraIndex = 0; ExtraIndex < ExtraInstances; ++ExtraIndex)
		{
			const FString ExtraName = FString::Printf(TEXT("Instance_Extra_%d"), ExtraIndex);
			const FString ExtraLabel = FString::Printf(TEXT("Supported Extra Instance %d"), ExtraIndex);
			const FString ExtraElementId = FString::Printf(TEXT("200%d"), ExtraIndex);
			MakeMeshActor(
				Scene, Parent, *ExtraName, *ExtraLabel,
				MeshElement->GetName(), Material->GetName(), *ExtraElementId,
				FTransform(
					FRotator(0.0, 45.0 + ExtraIndex, 0.0),
					FVector(600.0 + (140.0 * ExtraIndex), 320.0, 25.0),
					FVector(1.0)));
		}

		SceneExporter.Export(Scene, false);
		OutFixture.SceneFile = OutFixture.RootDirectory / (SceneName + TEXT(".udatasmith"));
		if (!FPaths::FileExists(OutFixture.SceneFile) || !FPaths::FileExists(OutFixture.MeshFile))
		{
			OutError = TEXT("The Datasmith exporter did not produce both the scene and mesh sidecar files.");
			return false;
		}
		OutFixture.SceneHash = HashFile(OutFixture.SceneFile);
		OutFixture.MeshHash = HashFile(OutFixture.MeshFile);
		return true;
	}

	static int32 CountWorldActors(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	static bool HasSkipCount(
		const FConVerseOptimizedImportResult& Result,
		EConVerseOptimizedSkipReason Reason,
		int32 ExpectedCount)
	{
		const FConVerseOptimizedSkipCount* Count = Result.SkipCounts.FindByPredicate(
			[Reason](const FConVerseOptimizedSkipCount& Candidate) { return Candidate.Reason == Reason; });
		return Count != nullptr && Count->Count == ExpectedCount;
	}

	static bool RecordHasMetadata(
		const FConVerseOptimizedImportInstanceRecord& Record,
		const FString& Key,
		const FString& Value)
	{
		return Record.Metadata.ContainsByPredicate([&Key, &Value](const FConVerseOptimizedMetadataPair& Pair)
		{
			return Pair.Key == Key && Pair.Value == Value;
		});
	}

	/**
	 * Datasmith sanitizes actor labels on import, so a source label of "Mirrored Negative Instance"
	 * arrives in the world as "Mirrored_Negative_Instance". Compare on the sanitized form.
	 */
	static AActor* FindCreatedActorByLabel(
		const UConVerseOptimizedImportManifest& Manifest,
		const FString& Label)
	{
		FString ExpectedLabel = Label;
		ExpectedLabel.ReplaceInline(TEXT(" "), TEXT("_"));
		for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest.CreatedActors)
		{
			AActor* Actor = Cast<AActor>(Record.ActorPath.ResolveObject());
			if (IsValid(Actor) && Actor->GetActorLabel() == ExpectedLabel)
			{
				return Actor;
			}
		}
		return nullptr;
	}

	static void CleanupCommittedImport(
		UWorld& World,
		const UConVerseOptimizedImportManifest* Manifest,
		UDatasmithScene* ImportedScene)
	{
		TArray<AActor*> Actors;
		TArray<UObject*> Assets;
		if (Manifest != nullptr)
		{
			for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest->CreatedActors)
			{
				if (AActor* Actor = Cast<AActor>(Record.ActorPath.ResolveObject()))
				{
					Actors.AddUnique(Actor);
				}
			}
			for (const FConVerseOptimizedCreatedAssetRecord& Record : Manifest->CreatedAssets)
			{
				if (UObject* Asset = Record.AssetPath.ResolveObject())
				{
					Assets.AddUnique(Asset);
				}
			}
		}
		for (AActor* Actor : Actors)
		{
			if (IsValid(Actor))
			{
				World.EditorDestroyActor(Actor, true);
			}
		}
		if (IsValid(ImportedScene))
		{
			Assets.AddUnique(ImportedScene);
		}
		if (!Assets.IsEmpty())
		{
			ObjectTools::DeleteObjectsUnchecked(Assets);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportEndToEndTest,
	"DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConVerseOptimizedImportEndToEndTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is available"), World))
	{
		return false;
	}

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FString TestRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const int32 BaselineActorCount = CountWorldActors(*World);
	const bool bBaselineWorldDirty = World->GetOutermost()->IsDirty();

	auto RunMode = [this, World, &Fixture, &TestRunId, BaselineActorCount, bBaselineWorldDirty](
		EConVerseOptimizedInstanceType InstanceType,
		const TCHAR* ModeName,
		UClass* ExpectedComponentClass)
	{
		FConVerseOptimizedImportOptions Options;
		Options.FilePath = Fixture.SceneFile;
		Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/%s"), *TestRunId, ModeName);
		Options.InstanceType = InstanceType;
		Options.MinimumInstanceCount = ExpectedOptimizedInstances;
		Options.bAutomated = true;

		const FConVerseOptimizedImportResult FirstAnalysis = FConVerseDatasmithImportService::Analyze(Options);
		const FConVerseOptimizedImportResult SecondAnalysis = FConVerseDatasmithImportService::Analyze(Options);
		TestTrue(*FString::Printf(TEXT("%s analysis succeeds"), ModeName),
			FirstAnalysis.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded);
		TestEqual(*FString::Printf(TEXT("%s analysis PlanId is deterministic"), ModeName),
			SecondAnalysis.PlanId, FirstAnalysis.PlanId);
		TestEqual(*FString::Printf(TEXT("%s analysis finds one group"), ModeName),
			FirstAnalysis.PlannedGroupCount, 1);
		TestEqual(*FString::Printf(TEXT("%s analysis finds two optimized instances"), ModeName),
			FirstAnalysis.PlannedInstanceCount, ExpectedOptimizedInstances);
		TestEqual(*FString::Printf(TEXT("%s analysis counts all source mesh actors"), ModeName),
			FirstAnalysis.TotalSourceMeshActors, ExpectedSourceMeshActors);
		TestEqual(*FString::Printf(TEXT("%s analysis finds two eligible leaves"), ModeName),
			FirstAnalysis.EligibleSourceActors, ExpectedOptimizedInstances);
		TestTrue(*FString::Printf(TEXT("%s analysis skips the negative determinant actor"), ModeName),
			HasSkipCount(FirstAnalysis, EConVerseOptimizedSkipReason::UnsupportedNegativeScale, 1));
		TestTrue(*FString::Printf(TEXT("%s Analyze does not mutate the world"), ModeName),
			CountWorldActors(*World) == BaselineActorCount
			&& World->GetOutermost()->IsDirty() == bBaselineWorldDirty);

		TArray<FAssetData> AnalysisAssets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get()
			.GetAssetsByPath(FName(*Options.DestinationPath), AnalysisAssets, true, false);
		TestEqual(*FString::Printf(TEXT("%s Analyze creates no assets"), ModeName), AnalysisAssets.Num(), 0);

		const FConVerseOptimizedImportResult ImportResult =
			FConVerseDatasmithImportService::ImportAndVerify(Options);
		TestTrue(*FString::Printf(TEXT("%s import reaches Verified"), ModeName),
			ImportResult.Status == EConVerseOptimizedImportStatus::Verified);
		TestEqual(*FString::Printf(TEXT("%s import consumes the analyzed plan"), ModeName),
			ImportResult.PlanId, FirstAnalysis.PlanId);
		TestEqual(*FString::Printf(TEXT("%s verifies one group"), ModeName),
			ImportResult.VerifiedGroupCount, 1);
		TestEqual(*FString::Printf(TEXT("%s verifies two instances"), ModeName),
			ImportResult.VerifiedInstanceCount, ExpectedOptimizedInstances);

		UDatasmithScene* ImportedScene = Cast<UDatasmithScene>(ImportResult.ImportAssetPath.ResolveObject());
		TestNotNull(*FString::Printf(TEXT("%s imported scene asset exists"), ModeName), ImportedScene);
		UConVerseOptimizedImportManifest* Manifest = ImportedScene != nullptr
			? Cast<UConVerseOptimizedImportManifest>(
				ImportedScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
			: nullptr;
		TestNotNull(*FString::Printf(TEXT("%s persistent manifest exists"), ModeName), Manifest);

		if (Manifest != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("%s manifest is active"), ModeName),
				Manifest->CommitState == EConVerseOptimizedImportCommitState::Active);
			TestEqual(*FString::Printf(TEXT("%s manifest stores the PlanId"), ModeName),
				Manifest->PlanId, ImportResult.PlanId);
			TestEqual(*FString::Printf(TEXT("%s manifest has one group"), ModeName), Manifest->Groups.Num(), 1);
			TestEqual(*FString::Printf(TEXT("%s manifest has one record per instance"), ModeName),
				Manifest->Instances.Num(), ExpectedOptimizedInstances);

			if (Manifest->Groups.Num() == 1)
			{
				UObject* OutputComponent = Manifest->Groups[0].OutputComponentPath.ResolveObject();
				TestNotNull(*FString::Printf(TEXT("%s output component resolves"), ModeName), OutputComponent);
				TestTrue(*FString::Printf(TEXT("%s output uses the exact requested component class"), ModeName),
					OutputComponent != nullptr && OutputComponent->GetClass() == ExpectedComponentClass);
				const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(OutputComponent);
				TestTrue(*FString::Printf(TEXT("%s output component has two instances"), ModeName),
					Instances != nullptr && Instances->GetInstanceCount() == ExpectedOptimizedInstances);
			}

			for (const FConVerseOptimizedImportInstanceRecord& Record : Manifest->Instances)
			{
				TestTrue(*FString::Printf(TEXT("%s instance %d keeps its source tag"), ModeName, Record.InstanceIndex),
					Record.SourceActorTags.ContainsByPredicate([](const FString& Tag)
					{
						return Tag.StartsWith(TEXT("FixtureTag="));
					}));
				TestTrue(*FString::Printf(TEXT("%s instance %d keeps unknown metadata"), ModeName, Record.InstanceIndex),
					RecordHasMetadata(Record, TEXT("CustomField"), TEXT("Preserved")));
				TestTrue(*FString::Printf(TEXT("%s instance %d has explicit source identity"), ModeName, Record.InstanceIndex),
					Record.SourceIdentityStatus == EConVerseOptimizedSourceIdentityStatus::Explicit
					&& Record.SelectedSourceIdentityKey == TEXT("ElementId")
					&& !Record.SelectedSourceIdentityValue.IsEmpty());
			}

			AActor* MirroredActor = FindCreatedActorByLabel(*Manifest, TEXT("Mirrored Negative Instance"));
				TestNotNull(*FString::Printf(TEXT("%s rejected mirrored actor remains imported"), ModeName), MirroredActor);
			if (MirroredActor != nullptr)
			{
				UStaticMeshComponent* RawMesh = MirroredActor->FindComponentByClass<UStaticMeshComponent>();
				TestTrue(*FString::Printf(TEXT("%s mirrored actor remains ordinary geometry"), ModeName),
					RawMesh != nullptr && !RawMesh->IsA<UInstancedStaticMeshComponent>());
			}
		}

		UConVerseOptimizedAssetMarker* Marker = ImportedScene != nullptr
			? Cast<UConVerseOptimizedAssetMarker>(
				ImportedScene->GetAssetUserDataOfClass(UConVerseOptimizedAssetMarker::StaticClass()))
			: nullptr;
		TestNotNull(*FString::Printf(TEXT("%s scene has an ownership marker"), ModeName), Marker);
		if (Marker != nullptr && Manifest != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("%s ownership marker is active and joins the manifest"), ModeName),
				Marker->CommitState == EConVerseOptimizedImportCommitState::Active
				&& Marker->ManifestId == Manifest->ManifestId
				&& Marker->SessionId == Manifest->SessionId);
		}

		TestEqual(*FString::Printf(TEXT("%s leaves .udatasmith bytes unchanged"), ModeName),
			HashFile(Fixture.SceneFile), Fixture.SceneHash);
		TestEqual(*FString::Printf(TEXT("%s leaves .udsmesh bytes unchanged"), ModeName),
			HashFile(Fixture.MeshFile), Fixture.MeshHash);

		CleanupCommittedImport(*World, Manifest, ImportedScene);
		TestEqual(*FString::Printf(TEXT("%s cleanup restores the editor actor count"), ModeName),
			CountWorldActors(*World), BaselineActorCount);
	};

	RunMode(
		EConVerseOptimizedInstanceType::ISM,
		TEXT("ISM"),
		UInstancedStaticMeshComponent::StaticClass());
	RunMode(
		EConVerseOptimizedInstanceType::HISM,
		TEXT("HISM"),
		UHierarchicalInstancedStaticMeshComponent::StaticClass());

	TestEqual(TEXT("The generated .udatasmith hash is unchanged after both modes"),
		HashFile(Fixture.SceneFile), Fixture.SceneHash);
	TestEqual(TEXT("The generated .udsmesh hash is unchanged after both modes"),
		HashFile(Fixture.MeshFile), Fixture.MeshHash);
	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedReimportTest,
	"DatasmithHISM.OptimizedImport.OptimizerAwareReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConVerseOptimizedReimportTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is available"), World))
	{
		return false;
	}

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const int32 BaselineActorCount = CountWorldActors(*World);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/Reimport"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12));
	Options.InstanceType = EConVerseOptimizedInstanceType::ISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	// First import establishes the active session.
	const FConVerseOptimizedImportResult FirstImport = FConVerseDatasmithImportService::ImportAndVerify(Options);
	if (!TestTrue(TEXT("The first optimized import reaches Verified"),
		FirstImport.Status == EConVerseOptimizedImportStatus::Verified))
	{
		AddError(FirstImport.Summary);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	TestFalse(TEXT("The first import is not reported as a reimport"), FirstImport.bWasReimport);
	TestTrue(TEXT("An active optimized import is now detected"),
		FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));

	UDatasmithScene* FirstScene = Cast<UDatasmithScene>(FirstImport.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* FirstManifest = FirstScene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			FirstScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	if (!TestNotNull(TEXT("The first manifest exists"), FirstManifest))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	const FString FirstManifestId = FirstManifest->ManifestId;
	const FString FirstSessionId = FirstManifest->SessionId;
	const int32 CommittedActorCount = CountWorldActors(*World);
	const int32 FirstCreatedActorCount = FirstManifest->CreatedActors.Num();

	// Unchanged source must be a no-op.
	const FConVerseOptimizedImportResult UnchangedReimport =
		FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("An unchanged source reports AlreadyCurrent"),
		UnchangedReimport.Status == EConVerseOptimizedImportStatus::AlreadyCurrent);
	TestTrue(TEXT("AlreadyCurrent is reported as a reimport"), UnchangedReimport.bWasReimport);
	TestEqual(TEXT("AlreadyCurrent names the active manifest"),
		UnchangedReimport.PreviousManifestId, FirstManifestId);
	TestTrue(TEXT("AlreadyCurrent creates no session"), UnchangedReimport.SessionId.IsEmpty());
	TestEqual(TEXT("AlreadyCurrent does not mutate the world"),
		CountWorldActors(*World), CommittedActorCount);
	TestTrue(TEXT("AlreadyCurrent leaves the first manifest active"),
		FirstManifest->CommitState == EConVerseOptimizedImportCommitState::Active);
	// The frozen contract requires AlreadyCurrent to run verification only, not to skip verification.
	TestTrue(TEXT("AlreadyCurrent re-verifies the committed output"),
		UnchangedReimport.bVerificationSucceeded);
	TestEqual(TEXT("AlreadyCurrent re-verifies every committed group"),
		UnchangedReimport.VerifiedGroupCount, FirstImport.VerifiedGroupCount);
	TestEqual(TEXT("AlreadyCurrent re-verifies every committed instance"),
		UnchangedReimport.VerifiedInstanceCount, FirstImport.VerifiedInstanceCount);

	// Changed source must supersede the prior session.
	FFixtureFiles ChangedFixture;
	if (!TestTrue(TEXT("The fixture is re-exported with an additional instance"),
		CreateFixture(ChangedFixture, FixtureError, 1, Fixture.RootDirectory)))
	{
		AddError(FixtureError);
		CleanupCommittedImport(*World, FirstManifest, FirstScene);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	TestNotEqual(TEXT("The re-exported source has different bytes"),
		ChangedFixture.SceneHash, Fixture.SceneHash);

	const FConVerseOptimizedImportResult ChangedReimport =
		FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("A changed source reaches Verified"),
		ChangedReimport.Status == EConVerseOptimizedImportStatus::Verified);
	TestTrue(TEXT("The changed run is reported as a reimport"), ChangedReimport.bWasReimport);
	TestEqual(TEXT("The reimport links the previous manifest"),
		ChangedReimport.PreviousManifestId, FirstManifestId);
	TestEqual(TEXT("The reimport links the previous session"),
		ChangedReimport.PreviousSessionId, FirstSessionId);
	TestNotEqual(TEXT("The reimport builds a different plan"), ChangedReimport.PlanId, FirstImport.PlanId);
	TestEqual(TEXT("The reimport removes every prior-session actor"),
		ChangedReimport.RemovedPreviousActorCount, FirstCreatedActorCount);
	TestEqual(TEXT("The reimport verifies the extra instance"),
		ChangedReimport.VerifiedInstanceCount, ExpectedOptimizedInstances + 1);

	if (FirstManifest != nullptr)
	{
		TestTrue(TEXT("The previous manifest is superseded"),
			FirstManifest->CommitState == EConVerseOptimizedImportCommitState::Superseded);
	}

	UDatasmithScene* SecondScene = Cast<UDatasmithScene>(ChangedReimport.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* SecondManifest = SecondScene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			SecondScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	TestNotNull(TEXT("The reimported manifest exists"), SecondManifest);
	if (SecondManifest != nullptr)
	{
		TestTrue(TEXT("The reimported manifest is active"),
			SecondManifest->CommitState == EConVerseOptimizedImportCommitState::Active);
		TestEqual(TEXT("The reimported manifest records its predecessor"),
			SecondManifest->PreviousManifestId, FirstManifestId);
		TestEqual(TEXT("The reimported manifest records the predecessor session"),
			SecondManifest->PreviousSessionId, FirstSessionId);
	}

	// Superseded asset packages are retained by design in this first implementation.
	TestTrue(TEXT("The superseded scene asset is retained"), IsValid(FirstScene));

	CleanupCommittedImport(*World, SecondManifest, SecondScene);
	CleanupCommittedImport(*World, FirstManifest, FirstScene);
	TestEqual(TEXT("Cleanup restores the editor actor count"),
		CountWorldActors(*World), BaselineActorCount);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportFutureSchemaTest,
	"DatasmithHISM.OptimizedImport.FutureSchemaManifestIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A manifest written by a newer build must never be treated as active, because the supersede path
 * destroys the previous session's actors. Reimporting against a future-versioned manifest must fail
 * closed into a recoverable blocked state rather than destroying output this build cannot interpret.
 */
bool FConVerseOptimizedImportFutureSchemaTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is available"), World))
	{
		return false;
	}

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FString TestRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const int32 BaselineActorCount = CountWorldActors(*World);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/FutureSchema"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	const FConVerseOptimizedImportResult FirstImport = FConVerseDatasmithImportService::ImportAndVerify(Options);
	if (!TestTrue(TEXT("The baseline import reaches Verified"),
		FirstImport.Status == EConVerseOptimizedImportStatus::Verified))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	UDatasmithScene* Scene = Cast<UDatasmithScene>(FirstImport.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* Manifest = Scene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			Scene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	if (!TestNotNull(TEXT("The committed manifest exists"), Manifest))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	// Simulate a session written by a build newer than this one.
	const int32 OriginalSchemaVersion = Manifest->ManifestSchemaVersion;
	Manifest->ManifestSchemaVersion = OriginalSchemaVersion + 1;

	const int32 ActorCountBeforeReimport = CountWorldActors(*World);
	const FConVerseOptimizedImportResult BlockedReimport =
		FConVerseDatasmithImportService::ImportAndVerify(Options);

	TestTrue(TEXT("A future-schema manifest does not produce AlreadyCurrent or Verified"),
		BlockedReimport.Status != EConVerseOptimizedImportStatus::AlreadyCurrent
		&& BlockedReimport.Status != EConVerseOptimizedImportStatus::Verified);
	TestTrue(TEXT("A future-schema manifest is refused with a recoverable blocked status"),
		BlockedReimport.Status == EConVerseOptimizedImportStatus::OptimizedReimportBlocked);

	// The critical guarantee: refusing to match must not destroy the newer session's output.
	TestEqual(TEXT("The blocked reimport destroys no actors"),
		CountWorldActors(*World), ActorCountBeforeReimport);
	TestTrue(TEXT("The future-schema manifest is still active"),
		Manifest->CommitState == EConVerseOptimizedImportCommitState::Active);
	for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest->CreatedActors)
	{
		TestTrue(TEXT("Every actor owned by the future-schema session still resolves"),
			IsValid(Cast<AActor>(Record.ActorPath.ResolveObject())));
	}

	// Restore the real schema version so cleanup and ownership behave normally.
	Manifest->ManifestSchemaVersion = OriginalSchemaVersion;

	CleanupCommittedImport(*World, Manifest, Scene);
	TestEqual(TEXT("Cleanup restores the editor actor count after the blocked reimport"),
		CountWorldActors(*World), BaselineActorCount);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportStockReimportGuardTest,
	"DatasmithHISM.OptimizedImport.StockReimportIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Epic documents that an ordinary Datasmith reimport re-runs the translator and finalizes the full
 * scene, bypassing any pre-import modification. For this plugin that would re-materialize every
 * StaticMeshActor that was deliberately collapsed into a single ISM/HISM component, silently
 * destroying the optimization and duplicating geometry.
 *
 * FConVerseOptimizedReimportHandler guards this by registering above the stock Datasmith factory
 * and UE 5.8's Interchange handler. That priority ordering is the load-bearing part, so this test
 * drives FReimportManager -- the real dispatch path -- rather than calling the handler directly.
 * Calling the handler directly would pass even if priority registration were broken.
 */
bool FConVerseOptimizedImportStockReimportGuardTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is available"), World))
	{
		return false;
	}

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FString TestRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const int32 BaselineActorCount = CountWorldActors(*World);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/StockReimport"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	const FConVerseOptimizedImportResult Import = FConVerseDatasmithImportService::ImportAndVerify(Options);
	if (!TestTrue(TEXT("The baseline import reaches Verified"),
		Import.Status == EConVerseOptimizedImportStatus::Verified))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	UDatasmithScene* Scene = Cast<UDatasmithScene>(Import.ImportAssetPath.ResolveObject());
	if (!TestNotNull(TEXT("The committed Datasmith scene asset exists"), Scene))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	UConVerseOptimizedImportManifest* Manifest = Cast<UConVerseOptimizedImportManifest>(
		Scene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()));
	if (!TestNotNull(TEXT("The committed manifest exists"), Manifest))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const int32 ActorCountBeforeReimport = CountWorldActors(*World);

	// Drive the same entry point the Content Browser's right-click Reimport uses. The optimized
	// handler must win on priority and cancel; if the stock Datasmith factory wins instead, it
	// re-finalizes the scene and the collapsed actors come back.
	const bool bReimportSucceeded = FReimportManager::Instance()->Reimport(
		Scene,
		/*bAskForNewFileIfMissing=*/false,
		/*bShowNotification=*/false);

	TestFalse(
		TEXT("Ordinary reimport of a committed optimized scene does not succeed"),
		bReimportSucceeded);

	// The guarantee that actually matters: the optimization survived.
	TestEqual(TEXT("The blocked reimport creates or destroys no actors"),
		CountWorldActors(*World), ActorCountBeforeReimport);
	TestTrue(TEXT("The optimized session is still active"),
		Manifest->CommitState == EConVerseOptimizedImportCommitState::Active);
	for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest->CreatedActors)
	{
		TestTrue(TEXT("Every actor owned by the optimized session still resolves"),
			IsValid(Cast<AActor>(Record.ActorPath.ResolveObject())));
	}

	CleanupCommittedImport(*World, Manifest, Scene);
	TestEqual(TEXT("Cleanup restores the editor actor count"),
		CountWorldActors(*World), BaselineActorCount);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportTessellationPlanIdTest,
	"DatasmithHISM.OptimizedImport.TessellationAffectsPlanIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Tessellation changes generated geometry without changing the source file, so it must take part
 * in plan identity. If it did not, changing only a tessellation value would produce the same
 * PlanId, match the active manifest, take the AlreadyCurrent branch in ImportAndVerify, and
 * silently discard the user's new settings while reporting success.
 *
 * Analyze is used because it computes a PlanId without mutating the world.
 */
bool FConVerseOptimizedImportTessellationPlanIdTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/TessellationPlanId");
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	const FConVerseOptimizedImportResult Baseline = FConVerseDatasmithImportService::Analyze(Options);
	if (!TestTrue(TEXT("The baseline analysis succeeds"),
		Baseline.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded))
	{
		AddError(Baseline.Summary);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	TestFalse(TEXT("The baseline analysis produces a PlanId"), Baseline.PlanId.IsEmpty());

	// Each field must independently affect identity.
	{
		FConVerseOptimizedImportOptions Changed = Options;
		Changed.Tessellation.ChordTolerance = 0.05f;
		const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::Analyze(Changed);
		TestNotEqual(TEXT("A chord tolerance change produces a different PlanId"),
			Result.PlanId, Baseline.PlanId);
	}
	{
		FConVerseOptimizedImportOptions Changed = Options;
		Changed.Tessellation.NormalTolerance = 45.0f;
		const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::Analyze(Changed);
		TestNotEqual(TEXT("A normal tolerance change produces a different PlanId"),
			Result.PlanId, Baseline.PlanId);
	}
	{
		FConVerseOptimizedImportOptions Changed = Options;
		Changed.Tessellation.MaxEdgeLength = 10.0f;
		const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::Analyze(Changed);
		TestNotEqual(TEXT("A max edge length change produces a different PlanId"),
			Result.PlanId, Baseline.PlanId);
	}
	{
		FConVerseOptimizedImportOptions Changed = Options;
		Changed.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::None;
		const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::Analyze(Changed);
		TestNotEqual(TEXT("A stitching technique change produces a different PlanId"),
			Result.PlanId, Baseline.PlanId);
	}

	// Identity must remain stable when nothing changes, or every reimport would look like a change.
	{
		const FConVerseOptimizedImportResult Repeat = FConVerseDatasmithImportService::Analyze(Options);
		TestEqual(TEXT("Re-analyzing identical options reproduces the same PlanId"),
			Repeat.PlanId, Baseline.PlanId);
	}

	// Clamping must be observable: a sub-minimum value is raised to the floor, so it cannot be
	// distinguished from the floor itself.
	{
		FConVerseOptimizedImportOptions AtFloor = Options;
		AtFloor.Tessellation.ChordTolerance = 0.005f;
		FConVerseOptimizedImportOptions BelowFloor = Options;
		BelowFloor.Tessellation.ChordTolerance = 0.0001f;

		const FConVerseOptimizedImportResult AtFloorResult = FConVerseDatasmithImportService::Analyze(AtFloor);
		const FConVerseOptimizedImportResult BelowFloorResult = FConVerseDatasmithImportService::Analyze(BelowFloor);
		TestEqual(TEXT("A sub-minimum chord tolerance is clamped to the minimum"),
			BelowFloorResult.PlanId, AtFloorResult.PlanId);
	}

	// .udatasmith is already tessellated, so the translator should expose no tessellation options.
	TestFalse(TEXT("A .udatasmith source reports no tessellation options applied"),
		Baseline.bTessellationApplied);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportSidecarChangeTest,
	"DatasmithHISM.OptimizedImport.SidecarChangeWarnsWithoutReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Datasmith source is a primary file plus a "_Assets" sidecar folder, and for CAD, Revit, and
 * IFC the geometry lives in that folder. Re-exporting can rewrite sidecar assets while leaving the
 * primary file byte-identical, which previously produced AlreadyCurrent and silently retained
 * stale geometry.
 *
 * The chosen behavior is warn-first: report the change and leave the world untouched, because
 * superseding is destructive and the decision to rebuild belongs to the user. This test asserts
 * both halves - that the warning fires, and that nothing is mutated.
 */
bool FConVerseOptimizedImportSidecarChangeTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is available"), World))
	{
		return false;
	}

	FFixtureFiles Fixture;
	FString FixtureError;
	if (!TestTrue(TEXT("A valid temporary Datasmith fixture is exported"), CreateFixture(Fixture, FixtureError)))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FString TestRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/Sidecar"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	const FConVerseOptimizedImportResult FirstImport = FConVerseDatasmithImportService::ImportAndVerify(Options);
	if (!TestTrue(TEXT("The baseline import reaches Verified"),
		FirstImport.Status == EConVerseOptimizedImportStatus::Verified))
	{
		AddError(FirstImport.Summary);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	// The fixture exports a real mesh into its sidecar folder, so this should be populated.
	TestTrue(TEXT("The fixture has a sidecar folder"), FirstImport.bSidecarExists);
	TestTrue(TEXT("The sidecar fingerprint is recorded"), !FirstImport.SidecarHash.IsEmpty());
	TestTrue(TEXT("The sidecar contains at least one asset"), FirstImport.SidecarFileCount > 0);

	UDatasmithScene* Scene = Cast<UDatasmithScene>(FirstImport.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* Manifest = Scene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			Scene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	if (!TestNotNull(TEXT("The committed manifest exists"), Manifest))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	TestEqual(TEXT("The manifest persists the sidecar fingerprint"),
		Manifest->SidecarHash, FirstImport.SidecarHash);

	// An immediate reimport with nothing touched must not warn, or the warning is worthless noise.
	{
		const FConVerseOptimizedImportResult Unchanged =
			FConVerseDatasmithImportService::ImportAndVerify(Options);
		TestTrue(TEXT("An untouched source is AlreadyCurrent"),
			Unchanged.Status == EConVerseOptimizedImportStatus::AlreadyCurrent);
		TestFalse(TEXT("An untouched sidecar does not warn"), Unchanged.bSidecarChanged);
	}

	// Mutate a sidecar asset while leaving the primary file completely alone. This is the exact
	// shape of a Revit re-export that rewrites geometry without touching the scene manifest.
	const FString SidecarDirectory = FPaths::Combine(
		FPaths::GetPath(Fixture.SceneFile),
		FPaths::GetBaseFilename(Fixture.SceneFile) + TEXT("_Assets"));

	TArray<FString> SidecarFiles;
	IFileManager::Get().FindFilesRecursive(SidecarFiles, *SidecarDirectory, TEXT("*.*"), true, false);
	if (!TestTrue(TEXT("The sidecar folder contains files to mutate"), SidecarFiles.Num() > 0))
	{
		CleanupCommittedImport(*World, Manifest, Scene);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FDateTime PrimaryBefore = IFileManager::Get().GetTimeStamp(*Fixture.SceneFile);
	const int64 PrimarySizeBefore = IFileManager::Get().FileSize(*Fixture.SceneFile);

	SidecarFiles.Sort();
	{
		TArray<uint8> Payload;
		FFileHelper::LoadFileToArray(Payload, *SidecarFiles[0]);
		Payload.Add(0x42);
		if (!TestTrue(TEXT("The sidecar asset can be rewritten"),
			FFileHelper::SaveArrayToFile(Payload, *SidecarFiles[0])))
		{
			CleanupCommittedImport(*World, Manifest, Scene);
			IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
			return false;
		}
	}

	// Guard the premise of the whole test: if the primary file moved, this proves nothing.
	TestEqual(TEXT("The primary source file size is unchanged"),
		IFileManager::Get().FileSize(*Fixture.SceneFile), PrimarySizeBefore);
	TestTrue(TEXT("The primary source file timestamp is unchanged"),
		IFileManager::Get().GetTimeStamp(*Fixture.SceneFile) == PrimaryBefore);

	const int32 ActorCountBeforeReimport = CountWorldActors(*World);
	const FConVerseOptimizedImportResult AfterSidecarChange =
		FConVerseDatasmithImportService::ImportAndVerify(Options);

	// Warn-first: still AlreadyCurrent, still read-only, but the change is reported.
	TestTrue(TEXT("A sidecar-only change still reports AlreadyCurrent"),
		AfterSidecarChange.Status == EConVerseOptimizedImportStatus::AlreadyCurrent);
	TestTrue(TEXT("A sidecar-only change is detected"), AfterSidecarChange.bSidecarChanged);
	TestNotEqual(TEXT("The sidecar fingerprint changed"),
		AfterSidecarChange.SidecarHash, FirstImport.SidecarHash);
	TestTrue(TEXT("The warning is surfaced in the summary"),
		AfterSidecarChange.Summary.Contains(TEXT("sidecar")));

	// The load-bearing half: warn-first must not have rebuilt anything.
	TestEqual(TEXT("A sidecar-only change mutates no actors"),
		CountWorldActors(*World), ActorCountBeforeReimport);
	TestEqual(TEXT("A sidecar-only change creates no new session"),
		AfterSidecarChange.SessionId, FString());

	CleanupCommittedImport(*World, Manifest, Scene);
	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
