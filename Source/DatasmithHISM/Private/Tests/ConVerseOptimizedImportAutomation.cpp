#include "ConVerseDatasmithImportService.h"

#include "ConVerseOptimizedImportManifest.h"
#include "ConVerseImportProcessing.h"
#include "ConVerseDatasmithImportPanel.h"
#include "Misc/FileHelper.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "ConVerseSourceMetadata.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/ScopeExit.h"

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
		FString PrimaryMaterialName;
		FString SecondaryMaterialName;
		FString TextureFile;
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
	 * ExtraInstances appends additional eligible instances so the resulting PlanId differs, and
	 * bSecondMaterialSlot gives the shared mesh a second material slot driven by its own material.
	 */
	static bool CreateFixture(
		FFixtureFiles& OutFixture,
		FString& OutError,
		int32 ExtraInstances = 0,
		const FString& ExistingRoot = FString(),
		bool bSecondMaterialSlot = false, bool bLights = false, bool bCustomAppearance = false, bool bTexture = false)
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
		Mesh.SetFace(1, 0, 2, 3, bSecondMaterialSlot ? 1 : 0);
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
		BaseColor->GetColor() = bCustomAppearance ? FLinearColor(0.82f, 0.14f, 0.16f, 1.0f) : FLinearColor(0.12f, 0.34f, 0.56f, 1.0f);
		BaseColor->ConnectExpression(Material->GetBaseColor());
		MeshElement->SetMaterial(Material->GetName(), 0);
		OutFixture.PrimaryMaterialName = Material->GetName();

		TSharedPtr<IDatasmithUEPbrMaterialElement> SecondaryMaterial;
		OutFixture.SecondaryMaterialName.Reset();
		if (bSecondMaterialSlot)
		{
			SecondaryMaterial = FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("FixtureMaterialB"));
			SecondaryMaterial->SetLabel(TEXT("Fixture Material B"));
			IDatasmithMaterialExpressionColor* SecondaryBaseColor =
				SecondaryMaterial->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
			if (SecondaryBaseColor == nullptr)
			{
				OutError = TEXT("Could not create the secondary fixture material expression.");
				return false;
			}
			SecondaryBaseColor->GetColor() = FLinearColor(0.87f, 0.21f, 0.05f, 1.0f);
			SecondaryBaseColor->ConnectExpression(SecondaryMaterial->GetBaseColor());
			MeshElement->SetMaterial(SecondaryMaterial->GetName(), 1);
			OutFixture.SecondaryMaterialName = SecondaryMaterial->GetName();
		}

		const TSharedRef<IDatasmithScene> Scene = FDatasmithSceneFactory::CreateScene(*SceneName);
		Scene->SetHost(TEXT("DatasmithHISM Automation"));
		Scene->SetVendor(TEXT("ConVerse"));
		Scene->SetProductName(TEXT("Optimized Import Fixture"));
		Scene->SetProductVersion(TEXT("1"));
		Scene->SetResourcePath(*OutFixture.RootDirectory);
		Scene->AddMaterial(Material);
		if (SecondaryMaterial.IsValid())
		{
			Scene->AddMaterial(SecondaryMaterial.ToSharedRef());
		}
		Scene->AddMesh(MeshElement);
		if (bTexture)
		{
			// A valid 1024-square, 24-bit BMP crosses three 1 MiB fingerprint checkpoints.
			OutFixture.TextureFile = FString(SceneExporter.GetAssetsOutputPath()) / TEXT("Fingerprint.bmp");
			TArray<uint8> Bytes;
			Bytes.Init(0, 54 + 1024 * 1024 * 3);
			auto Put32 = [&](int32 Offset, uint32 Value) { for (int32 I = 0; I < 4; ++I) Bytes[Offset + I] = uint8(Value >> (8 * I)); };
			Bytes[0] = 'B'; Bytes[1] = 'M'; Put32(2, Bytes.Num()); Put32(10, 54); Put32(14, 40);
			Put32(18, 1024); Put32(22, 1024); Bytes[26] = 1; Bytes[28] = 24; Put32(34, 1024 * 1024 * 3);
			for (int32 I = 54; I < Bytes.Num(); ++I) Bytes[I] = uint8(I % 251);
			if (!FFileHelper::SaveArrayToFile(Bytes, *OutFixture.TextureFile)) { OutError = TEXT("Could not write texture fixture."); return false; }
			const auto Texture = FDatasmithSceneFactory::CreateTexture(TEXT("FingerprintTexture"));
			Texture->SetFile(*OutFixture.TextureFile);
			Scene->AddTexture(Texture);
		}


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

		if (bLights)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				auto Light = FDatasmithSceneFactory::CreatePointLight(*FString::Printf(TEXT("FixtureLight%d"), Index));
				Light->SetIntensityUnits(Index == 0 ? EDatasmithLightUnits::Lumens : Index == 1 ? EDatasmithLightUnits::Candelas : EDatasmithLightUnits::Unitless);
				Light->SetIntensity(1250 + Index * 100);
				Light->SetEnabled(Index != 2);
				Parent->AddChild(Light, EDatasmithActorAttachmentRule::KeepWorldTransform);
			}
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

	static TMap<FString, FString> SnapshotWorld(UWorld& World)
	{
		TMap<FString, FString> Snapshot;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			Snapshot.Add(It->GetPathName(), ConVerseImportProcessing::ObjectState(**It));
			TArray<UActorComponent*> Components;
			It->GetComponents(Components);
			for (UActorComponent* Component : Components) Snapshot.Add(Component->GetPathName(), ConVerseImportProcessing::ObjectState(*Component));
		}
		return Snapshot;
	}

	static TSet<FSoftObjectPath> SnapshotAssets()
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(TEXT("/Game"), Assets, true, false);
		TSet<FSoftObjectPath> Paths;
		for (const auto& Asset : Assets) Paths.Add(Asset.GetSoftObjectPath());
		return Paths;
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
	FConVerseOptimizedMultiMaterialVerificationTest,
	"DatasmithHISM.OptimizedImport.MultiMaterialSlotsVerify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Datasmith renames every imported material slot to its material asset's name
 * (FDatasmithStaticMaterialTemplate::Apply), so verification cannot recover the Datasmith slot id by
 * parsing FStaticMaterial::MaterialSlotName. Doing so mapped every slot to id 0 and reported
 * "material pointer mismatch at slot 1" for any mesh carrying more than one material.
 */
bool FConVerseOptimizedMultiMaterialVerificationTest::RunTest(const FString& Parameters)
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
	const bool bFixtureCreated = CreateFixture(Fixture, FixtureError, 0, FString(), true);
	if (!TestTrue(TEXT("A two-material Datasmith fixture is exported"), bFixtureCreated))
	{
		AddError(FixtureError);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const int32 BaselineActorCount = CountWorldActors(*World);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(
		TEXT("/Game/__ConVerseAutomation/%s/MultiMaterial"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12));
	Options.InstanceType = EConVerseOptimizedInstanceType::ISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;

	const FConVerseOptimizedImportResult ImportResult =
		FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("A multi-material mesh still reaches Verified"),
		ImportResult.Status == EConVerseOptimizedImportStatus::Verified);
	for (const FConVerseOptimizedGroupVerification& Verification : ImportResult.GroupVerification)
	{
		TestTrue(
			*FString::Printf(TEXT("Group %s passes verification (%s)"), *Verification.GroupId, *Verification.Details),
			Verification.bPassed);
	}

	UDatasmithScene* ImportedScene = Cast<UDatasmithScene>(ImportResult.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* Manifest = ImportedScene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			ImportedScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	TestNotNull(TEXT("Persistent manifest exists"), Manifest);

	if (Manifest != nullptr && TestEqual(TEXT("Manifest has one group"), Manifest->Groups.Num(), 1))
	{
		const FConVerseOptimizedImportGroupRecord& GroupRecord = Manifest->Groups[0];
		if (TestEqual(TEXT("Manifest records both material slots"), GroupRecord.MaterialSlots.Num(), 2))
		{
			TestEqual(TEXT("Manifest slot 0 records the primary Datasmith material"),
				GroupRecord.MaterialSlots[0].DatasmithMaterialElementName, Fixture.PrimaryMaterialName);
			TestEqual(TEXT("Manifest slot 1 records the secondary Datasmith material"),
				GroupRecord.MaterialSlots[1].DatasmithMaterialElementName, Fixture.SecondaryMaterialName);
			TestTrue(TEXT("The two slots resolve to different imported materials"),
				GroupRecord.MaterialSlots[0].ImportedMaterialPath
					!= GroupRecord.MaterialSlots[1].ImportedMaterialPath);
		}

		const UInstancedStaticMeshComponent* Component =
			Cast<UInstancedStaticMeshComponent>(GroupRecord.OutputComponentPath.ResolveObject());
		if (TestNotNull(TEXT("Output component resolves"), Component))
		{
			TestEqual(TEXT("Output component exposes both material slots"), Component->GetNumMaterials(), 2);
			if (Component->GetNumMaterials() == 2)
			{
				TestTrue(TEXT("Output component slot 1 is not slot 0's material"),
					Component->GetMaterial(1) != Component->GetMaterial(0));
			}
		}
	}

	CleanupCommittedImport(*World, Manifest, ImportedScene);
	TestEqual(TEXT("Cleanup restores the editor actor count"), CountWorldActors(*World), BaselineActorCount);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportRollbackTest,
	"DatasmithHISM.OptimizedImport.InducedFailureRollsBackCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Rollback only runs when an import fails, and until this test existed no failure had ever been
 * induced, so RollBackAttempt was untested destructive code on the path that exists specifically
 * to protect user data.
 *
 * Both injection points are exercised because they fail at genuinely different depths:
 * AfterDatasmithImport aborts while the attempt owns assets and actors but before verification,
 * and BeforeManifestCommit aborts after verification has already passed - the latest checkpoint
 * that still rolls back. The assertion is the same in both cases: the world and the destination
 * are returned to their pre-operation state and no ownership is claimed.
 */
bool FConVerseOptimizedImportRollbackTest::RunTest(const FString& Parameters)
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
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	auto RunInjection = [this, World, &Fixture, &TestRunId, &Registry](
		EConVerseOptimizedImportFailureInjection Injection,
		const TCHAR* ModeName)
	{
		FConVerseOptimizedImportOptions Options;
		Options.FilePath = Fixture.SceneFile;
		Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/Rollback%s"), *TestRunId, ModeName);
		Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
		Options.MinimumInstanceCount = ExpectedOptimizedInstances;
		Options.bAutomated = true;
		Options.FailureInjection = Injection;

		const int32 BaselineActorCount = CountWorldActors(*World);
		const bool bBaselineWorldDirty = World->GetOutermost()->IsDirty();

		const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::ImportAndVerify(Options);

		TestTrue(*FString::Printf(TEXT("%s reports ImportedWithFailuresRolledBack"), ModeName),
			Result.Status == EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack);
		TestTrue(*FString::Printf(TEXT("%s attempted a rollback"), ModeName), Result.bRollbackAttempted);
		TestTrue(*FString::Printf(TEXT("%s rollback succeeded"), ModeName), Result.bRollbackSucceeded);

		// Without this the test could pass against a no-op rollback that never had anything to undo.
		TestTrue(*FString::Printf(TEXT("%s had real created objects to unwind"), ModeName),
			Result.CreatedObjectCount > 0);
		TestEqual(*FString::Printf(TEXT("%s leaves no remaining objects"), ModeName),
			Result.RemainingObjectCount, 0);

		TestEqual(*FString::Printf(TEXT("%s restores the world actor count"), ModeName),
			CountWorldActors(*World), BaselineActorCount);
		TestEqual(*FString::Printf(TEXT("%s restores the world dirty flag"), ModeName),
			World->GetOutermost()->IsDirty(), bBaselineWorldDirty);
		TestTrue(*FString::Printf(TEXT("%s reaches the RolledBack stage"), ModeName),
			Result.LastCompletedStage == EConVerseOptimizedImportStage::RolledBack);

		TArray<FAssetData> RemainingAssets;
		Registry.GetAssetsByPath(FName(*Options.DestinationPath), RemainingAssets, true, false);
		TestEqual(*FString::Printf(TEXT("%s leaves no assets in the destination"), ModeName),
			RemainingAssets.Num(), 0);

		// A rolled-back attempt must never claim ownership, or the next import would be treated as
		// a reimport of a session that does not exist.
		TestFalse(*FString::Printf(TEXT("%s claims no active manifest"), ModeName),
			FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));

		// The destination must remain usable: a rollback that poisons its own attempt folder would
		// still pass every check above.
		Options.FailureInjection = EConVerseOptimizedImportFailureInjection::None;
		const FConVerseOptimizedImportResult Recovery = FConVerseDatasmithImportService::ImportAndVerify(Options);
		TestTrue(*FString::Printf(TEXT("%s leaves the destination importable"), ModeName),
			Recovery.Status == EConVerseOptimizedImportStatus::Verified);
		if (Recovery.Status != EConVerseOptimizedImportStatus::Verified)
		{
			AddError(Recovery.Summary);
		}

		UDatasmithScene* RecoveredScene = Cast<UDatasmithScene>(Recovery.ImportAssetPath.ResolveObject());
		UConVerseOptimizedImportManifest* RecoveredManifest = RecoveredScene != nullptr
			? Cast<UConVerseOptimizedImportManifest>(
				RecoveredScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
			: nullptr;
		CleanupCommittedImport(*World, RecoveredManifest, RecoveredScene);
	};

	RunInjection(EConVerseOptimizedImportFailureInjection::AfterDatasmithImport, TEXT("PostImport"));
	RunInjection(EConVerseOptimizedImportFailureInjection::BeforeManifestCommit, TEXT("PreCommit"));

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportRollbackFailureTest,
	"DatasmithHISM.OptimizedImport.ObstructedRollbackDegradesToRollbackFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * RollbackFailed is the worst outcome the importer can report: the unwind itself could not restore
 * the pre-operation inventory, so the user is left in an unknown partial state and needs the
 * remaining object paths to clean up by hand. It must never be collapsed into Failed or Cancelled.
 *
 * The obstruction seam skips the destruction pass only. Everything after it is the real code: the
 * verification sweep genuinely finds the actors still present and reports failure on its own terms,
 * so this exercises the degradation rather than asserting a hardcoded return value.
 *
 * This test deliberately leaves partial state behind and cleans it up itself.
 */
bool FConVerseOptimizedImportRollbackFailureTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseOptimizedImportAutomation;
	(void)Parameters;

	// RollbackFailed is deliberately logged at Error, because a user must never miss it. This test
	// induces that state on purpose, so the Error is the expected outcome rather than a failure.
	AddExpectedError(TEXT("Status: RollbackFailed"), EAutomationExpectedErrorFlags::Contains, 0);

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
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/RollbackFailed"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;
	Options.FailureInjection = EConVerseOptimizedImportFailureInjection::ObstructRollback;

	// Captured so the test can clean up exactly what the obstructed rollback refused to remove.
	TSet<AActor*> PreExistingActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		PreExistingActors.Add(*It);
	}

	const FConVerseOptimizedImportResult Result = FConVerseDatasmithImportService::ImportAndVerify(Options);

	TestTrue(TEXT("An obstructed rollback reports RollbackFailed"),
		Result.Status == EConVerseOptimizedImportStatus::RollbackFailed);
	TestTrue(TEXT("The rollback was attempted"), Result.bRollbackAttempted);
	TestFalse(TEXT("The rollback did not succeed"), Result.bRollbackSucceeded);
	TestTrue(TEXT("RollbackFailed reports remaining objects"), Result.RemainingObjectCount > 0);
	TestTrue(TEXT("RollbackFailed does not claim the RolledBack stage"),
		Result.LastCompletedStage != EConVerseOptimizedImportStage::RolledBack);

	// The whole point of this state is that the user can find what was left behind.
	TestTrue(TEXT("The summary lists remaining object paths"),
		Result.Summary.Contains(TEXT("remaining")));

	// A failed rollback must still not have committed ownership.
	TestFalse(TEXT("A failed rollback claims no active manifest"),
		FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));

	// Clean up the partial state this test deliberately created.
	TArray<AActor*> LeftoverActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!PreExistingActors.Contains(*It))
		{
			LeftoverActors.Add(*It);
		}
	}
	for (AActor* Actor : LeftoverActors)
	{
		if (IsValid(Actor))
		{
			World->EditorDestroyActor(Actor, true);
		}
	}

	TArray<FAssetData> LeftoverAssets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get()
		.GetAssetsByPath(FName(*Options.DestinationPath), LeftoverAssets, true, false);
	TArray<UObject*> AssetsToDelete;
	for (const FAssetData& AssetData : LeftoverAssets)
	{
		if (UObject* Asset = AssetData.GetAsset())
		{
			AssetsToDelete.Add(Asset);
		}
	}
	if (!AssetsToDelete.IsEmpty())
	{
		ObjectTools::ForceDeleteObjects(AssetsToDelete, false);
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportDriftTest,
	"DatasmithHISM.OptimizedImport.DeletedActorReportsDriftNotSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An unchanged source takes the read-only AlreadyCurrent branch. The failure mode that branch must
 * not have is reporting cheerful success while the committed output has been tampered with: a user
 * who deleted an optimized actor by hand needs to be told the world no longer matches the manifest,
 * not that everything is current.
 *
 * This was previously listed as a manual-only check. It is a regression test now.
 */
bool FConVerseOptimizedImportDriftTest::RunTest(const FString& Parameters)
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
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/Drift"), *TestRunId);
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

	// Establish the premise: an untouched reimport is clean. Without this, a drift report could
	// simply mean the verifier is broken.
	{
		const FConVerseOptimizedImportResult Untouched =
			FConVerseDatasmithImportService::ImportAndVerify(Options);
		TestTrue(TEXT("An untouched reimport is AlreadyCurrent"),
			Untouched.Status == EConVerseOptimizedImportStatus::AlreadyCurrent);
		TestTrue(TEXT("An untouched reimport verifies cleanly"), Untouched.bVerificationSucceeded);
	}

	// Simulate the user deleting one optimized actor by hand.
	AActor* ActorToDelete = nullptr;
	for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest->CreatedActors)
	{
		if (AActor* Actor = Cast<AActor>(Record.ActorPath.ResolveObject()))
		{
			ActorToDelete = Actor;
			break;
		}
	}
	if (!TestNotNull(TEXT("The manifest records a created actor to delete"), ActorToDelete))
	{
		CleanupCommittedImport(*World, Manifest, Scene);
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}
	World->EditorDestroyActor(ActorToDelete, true);

	const int32 ActorCountAfterDeletion = CountWorldActors(*World);
	const FConVerseOptimizedImportResult AfterDrift =
		FConVerseDatasmithImportService::ImportAndVerify(Options);

	// Still AlreadyCurrent, because the source really is unchanged - but not a success.
	TestTrue(TEXT("A drifted reimport still reports AlreadyCurrent"),
		AfterDrift.Status == EConVerseOptimizedImportStatus::AlreadyCurrent);
	TestFalse(TEXT("A drifted reimport does not report verification success"),
		AfterDrift.bVerificationSucceeded);
	TestTrue(TEXT("The drift is described in the summary"),
		AfterDrift.Summary.Contains(TEXT("drift")));

	// Read-only means read-only: detecting drift must not silently re-create the deleted actor.
	TestEqual(TEXT("A drifted reimport mutates no actors"),
		CountWorldActors(*World), ActorCountAfterDeletion);
	TestEqual(TEXT("A drifted reimport creates no new session"), AfterDrift.SessionId, FString());

	CleanupCommittedImport(*World, Manifest, Scene);
	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportLogTest,
	"DatasmithHISM.OptimizedImport.ImportLogRecordsFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The import log exists so a failure is noticeable after the fact, without hunting through
 * per-attempt JSON reports. Three properties make it worth having, and all three are asserted here:
 *
 * 1. A failure is recorded at all, with a severity that distinguishes it from a success.
 * 2. Repeated failures append rather than overwrite. The report filename previously fell back to
 *    the PlanId, so a source that failed every run destroyed its own history each time.
 * 3. The row names a report artifact that actually exists, or the log is a dead end.
 */
bool FConVerseOptimizedImportLogTest::RunTest(const FString& Parameters)
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

	const FString ImportLogFile = FConVerseDatasmithImportService::GetImportLogPath();

	// Count only this test's own rows: the log is append-only and shared with every other test in
	// the run, so an absolute row count would be order-dependent and flaky.
	const FString TestRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const auto CountRowsForThisRun = [&ImportLogFile, &TestRunId]()
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *ImportLogFile))
		{
			return 0;
		}
		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines);
		int32 Count = 0;
		for (const FString& Line : Lines)
		{
			if (Line.Contains(TestRunId))
			{
				++Count;
			}
		}
		return Count;
	};

	TestEqual(TEXT("This run has no log rows before it starts"), CountRowsForThisRun(), 0);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/ImportLog"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;
	Options.FailureInjection = EConVerseOptimizedImportFailureInjection::AfterDatasmithImport;

	const FConVerseOptimizedImportResult FirstFailure = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("The injected failure really failed"),
		FirstFailure.Status == EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack);
	TestEqual(TEXT("The first failure appends one row"), CountRowsForThisRun(), 1);

	// The load-bearing property: a source that fails repeatedly must accumulate history.
	const FConVerseOptimizedImportResult SecondFailure = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("The second injected failure also failed"),
		SecondFailure.Status == EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack);
	TestEqual(TEXT("The second failure appends a second row rather than overwriting"),
		CountRowsForThisRun(), 2);
	TestNotEqual(TEXT("Each failed attempt keeps its own report artifact"),
		SecondFailure.SavedReportPath, FirstFailure.SavedReportPath);
	TestTrue(TEXT("The first failure's report artifact still exists"),
		FPaths::FileExists(FirstFailure.SavedReportPath));
	TestTrue(TEXT("The second failure's report artifact exists"),
		FPaths::FileExists(SecondFailure.SavedReportPath));

	// The sessionless path is where report filenames used to collide. A failed import is assigned a
	// SessionId, so it is unique for free; Analyze is not, and its filename fell back to the bare
	// PlanId. PlanId is deterministic for identical options, so repeating the same analysis
	// overwrote the previous artifact every time. Two identical analyses must still leave two files.
	FConVerseOptimizedImportOptions AnalyzeOptions = Options;
	AnalyzeOptions.FailureInjection = EConVerseOptimizedImportFailureInjection::None;
	const FConVerseOptimizedImportResult FirstAnalyze = FConVerseDatasmithImportService::Analyze(AnalyzeOptions);
	const FConVerseOptimizedImportResult SecondAnalyze = FConVerseDatasmithImportService::Analyze(AnalyzeOptions);
	TestTrue(TEXT("Both analyses resolved to the same deterministic plan"),
		!FirstAnalyze.PlanId.IsEmpty() && FirstAnalyze.PlanId == SecondAnalyze.PlanId);
	TestTrue(TEXT("Sessionless analyses are not tracked by a session id"),
		FirstAnalyze.SessionId.IsEmpty() && SecondAnalyze.SessionId.IsEmpty());
	TestNotEqual(TEXT("Repeating an identical analysis does not overwrite the previous report"),
		SecondAnalyze.SavedReportPath, FirstAnalyze.SavedReportPath);
	TestTrue(TEXT("The first analysis report survived the second"),
		FPaths::FileExists(FirstAnalyze.SavedReportPath));

	FString Contents;
	FFileHelper::LoadFileToString(Contents, *ImportLogFile);
	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines);

	FString FailureRow;
	for (const FString& Line : Lines)
	{
		// Select the failure row explicitly: the Analyze rows above also belong to this run.
		if (Line.Contains(TestRunId) && Line.Contains(TEXT("\"ImportedWithFailuresRolledBack\"")))
		{
			FailureRow = Line;
		}
	}
	TestTrue(TEXT("A row for this run was found"), !FailureRow.IsEmpty());
	TestTrue(TEXT("The row records the failing status"),
		FailureRow.Contains(TEXT("ImportedWithFailuresRolledBack")));
	TestTrue(TEXT("The row is marked as a warning, not a success"),
		FailureRow.Contains(TEXT("\"Warning\"")));
	TestTrue(TEXT("The row records the operation"), FailureRow.Contains(TEXT("ImportAndVerify")));
	TestTrue(TEXT("The row names the source file"),
		FailureRow.Contains(FPaths::GetCleanFilename(Fixture.SceneFile)));

	// A successful attempt must be distinguishable from a failure, or severity is meaningless.
	Options.FailureInjection = EConVerseOptimizedImportFailureInjection::None;
	const FConVerseOptimizedImportResult Success = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("The recovery import verified"),
		Success.Status == EConVerseOptimizedImportStatus::Verified);
	// Two failures, two analyses, and this success.
	TestEqual(TEXT("The successful attempt is logged too"), CountRowsForThisRun(), 5);

	FFileHelper::LoadFileToString(Contents, *ImportLogFile);
	Contents.ParseIntoArrayLines(Lines);
	FString SuccessRow;
	for (const FString& Line : Lines)
	{
		// Match the quoted status field exactly: failure summaries also contain the word "verified".
		if (Line.Contains(TestRunId) && Line.Contains(TEXT("\"Verified\"")))
		{
			SuccessRow = Line;
		}
	}
	TestTrue(TEXT("A success row was found"), !SuccessRow.IsEmpty());
	TestTrue(TEXT("The success row is marked Info"), SuccessRow.Contains(TEXT("\"Info\"")));

	UDatasmithScene* Scene = Cast<UDatasmithScene>(Success.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* Manifest = Scene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			Scene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	CleanupCommittedImport(*World, Manifest, Scene);
	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportAcceptFailedVerificationTest,
	"DatasmithHISM.OptimizedImport.FailedVerificationCanBeAcceptedAndIsQuarantined",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Accepting a failed verification must keep the output WITHOUT ever claiming it passed.
 *
 * The corruption seam removes one real instance from one real component before verification runs,
 * so VerifySession fails on its own terms with a genuine count mismatch rather than being told to
 * fail. That makes this a test of the decision path, not of the injection.
 *
 * The quarantine assertions are the important half. A kept-but-unverified session whose manifest
 * still looked ordinary would let a later optimized reimport supersede it, destroying actors based
 * on records verification could not confirm.
 */
bool FConVerseOptimizedImportAcceptFailedVerificationTest::RunTest(const FString& Parameters)
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
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/AcceptFailed"), *TestRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;
	Options.FailureInjection = EConVerseOptimizedImportFailureInjection::CorruptBeforeVerification;
	Options.bDeferRollbackOnVerificationFailure = true;

	const FConVerseOptimizedImportResult Parked = FConVerseDatasmithImportService::ImportAndVerify(Options);

	TestTrue(TEXT("A failed verification parks the attempt instead of rolling it back"),
		Parked.Status == EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision);
	TestFalse(TEXT("A parked attempt does not claim verification succeeded"), Parked.bVerificationSucceeded);
	TestFalse(TEXT("A parked attempt has not been rolled back"), Parked.bRollbackAttempted);
	TestTrue(TEXT("The parked session is resolvable"),
		FConVerseDatasmithImportService::HasPendingFailedVerification(Parked.SessionId));

	// Nothing may be committed while the decision is outstanding.
	TestFalse(TEXT("A parked attempt owns no active manifest yet"),
		FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));

	const FConVerseOptimizedImportResult Accepted =
		FConVerseDatasmithImportService::AcceptFailedVerification(Parked.SessionId);

	TestTrue(TEXT("Accepting reports AcceptedWithFailedVerification"),
		Accepted.Status == EConVerseOptimizedImportStatus::AcceptedWithFailedVerification);
	// Accepting changes what happens to the output, never the record of whether it passed.
	TestFalse(TEXT("Accepting never claims verification succeeded"), Accepted.bVerificationSucceeded);
	TestFalse(TEXT("The pending attempt is consumed by the decision"),
		FConVerseDatasmithImportService::HasPendingFailedVerification(Parked.SessionId));
	TestTrue(TEXT("An accepted session owns the destination"),
		FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));

	UDatasmithScene* AcceptedScene = Cast<UDatasmithScene>(Accepted.ImportAssetPath.ResolveObject());
	UConVerseOptimizedImportManifest* AcceptedManifest = AcceptedScene != nullptr
		? Cast<UConVerseOptimizedImportManifest>(
			AcceptedScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()))
		: nullptr;
	if (TestNotNull(TEXT("An accepted session commits a manifest"), AcceptedManifest))
	{
		TestTrue(TEXT("The accepted manifest is active"),
			AcceptedManifest->CommitState == EConVerseOptimizedImportCommitState::Active);
		TestTrue(TEXT("The accepted manifest is marked degraded"),
			AcceptedManifest->Verification.bAcceptedWithFailedVerification);
		TestTrue(TEXT("The accepted manifest records why verification failed"),
			!AcceptedManifest->Verification.AcceptedFailureDetails.IsEmpty());
		TestTrue(TEXT("The accepted manifest does not record a passed verification"),
			AcceptedManifest->Verification.State != EConVerseOptimizedVerificationState::Passed);
	}

	// Quarantine: a degraded session must never be superseded automatically.
	FConVerseOptimizedImportOptions ReimportOptions = Options;
	ReimportOptions.FailureInjection = EConVerseOptimizedImportFailureInjection::None;
	ReimportOptions.bDeferRollbackOnVerificationFailure = false;
	const FConVerseOptimizedImportResult Blocked =
		FConVerseDatasmithImportService::ImportAndVerify(ReimportOptions);
	TestTrue(TEXT("Optimized reimport is refused against an accepted-with-failures session"),
		Blocked.Status == EConVerseOptimizedImportStatus::OptimizedReimportBlocked);
	TestTrue(TEXT("The refusal explains that the session failed verification"),
		Blocked.Summary.Contains(TEXT("accepted despite failing verification")));

	CleanupCommittedImport(*World, AcceptedManifest, AcceptedScene);
	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseOptimizedImportDiscardFailedVerificationTest,
	"DatasmithHISM.OptimizedImport.FailedVerificationCanBeDiscarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Declining a parked attempt must land in exactly the state the automatic rollback would have
 * produced. Deferring the decision changes who decides, not what discarding means.
 */
bool FConVerseOptimizedImportDiscardFailedVerificationTest::RunTest(const FString& Parameters)
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

	const FString DiscardRunId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	const int32 ActorsBefore = CountWorldActors(*World);

	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = FString::Printf(TEXT("/Game/__ConVerseAutomation/%s/DiscardFailed"), *DiscardRunId);
	Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Options.MinimumInstanceCount = ExpectedOptimizedInstances;
	Options.bAutomated = true;
	Options.FailureInjection = EConVerseOptimizedImportFailureInjection::CorruptBeforeVerification;
	Options.bDeferRollbackOnVerificationFailure = true;

	const FConVerseOptimizedImportResult Parked = FConVerseDatasmithImportService::ImportAndVerify(Options);
	if (!TestTrue(TEXT("A failed verification parks the attempt"),
		Parked.Status == EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision))
	{
		IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
		return false;
	}

	const FConVerseOptimizedImportResult Discarded =
		FConVerseDatasmithImportService::DiscardFailedVerification(Parked.SessionId);

	TestTrue(TEXT("Discarding rolls the attempt back"),
		Discarded.Status == EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack);
	TestTrue(TEXT("The rollback was attempted"), Discarded.bRollbackAttempted);
	TestTrue(TEXT("The rollback succeeded"), Discarded.bRollbackSucceeded);
	TestFalse(TEXT("A discarded session owns nothing"),
		FConVerseDatasmithImportService::HasActiveOptimizedImport(Options));
	TestEqual(TEXT("A discarded attempt leaves no actors behind"), CountWorldActors(*World), ActorsBefore);

	// Resolving twice must not roll anything back a second time.
	const FConVerseOptimizedImportResult Again =
		FConVerseDatasmithImportService::DiscardFailedVerification(Parked.SessionId);
	TestTrue(TEXT("A resolved session cannot be resolved again"),
		Again.Status == EConVerseOptimizedImportStatus::InvalidOptions);

	IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseAnalyzeCancellationTest,
	"DatasmithHISM.OptimizedImport.AnalyzeCancellationLeavesNoPartialPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseAnalyzeCancellationTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	UWorld* World = GEditor->GetEditorWorldContext().World();
	const int32 Before = CountWorldActors(*World);
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.bAutomated = true;
	int32 Checks = 0;
	Options.CancelRequested = [&Checks]() { return ++Checks >= 4; };
	const auto Result = FConVerseDatasmithImportService::Analyze(Options);
	TestTrue(TEXT("Cancellation is explicit"), Result.Status == EConVerseOptimizedImportStatus::CancelledRolledBack);
	TestEqual(TEXT("No completed groups reported"), Result.PlannedGroupCount, 0);
	TestEqual(TEXT("No actors created"), CountWorldActors(*World), Before);
	TestEqual(TEXT("Source unchanged"), HashFile(Fixture.SceneFile), Fixture.SceneHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseMeshPolicyTest,
	"DatasmithHISM.OptimizedImport.NanitePolicyAndZeroGroupImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseMeshPolicyTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.bAutomated = true;
	Options.MinimumInstanceCount = 50;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto Analysis = FConVerseDatasmithImportService::Analyze(Options);
	Options.Processing.ManyLightThreshold = 50;
	TestEqual(TEXT("Advisory does not change plan identity"), FConVerseDatasmithImportService::Analyze(Options).PlanId, Analysis.PlanId);
	Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
	TestNotEqual(TEXT("Nanite policy changes output identity"), FConVerseDatasmithImportService::Analyze(Options).PlanId, Analysis.PlanId);
	Options.Processing.NanitePolicy = EConVerseNanitePolicy::AllSupportedMeshes;
	const auto Result = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Zero groups can import and verify ordinary geometry"), Result.Status == EConVerseOptimizedImportStatus::Verified);
	TestEqual(TEXT("No instancing required"), Result.PlannedGroupCount, 0);
	TestEqual(TEXT("All ordinary mesh elements accounted for"), Result.VerifiedOrdinaryMeshes, 3);
	TestTrue(TEXT("Ordinary imported assets received Nanite"), Result.NaniteEnabledMeshes > 0);
	auto* Scene = Cast<UDatasmithScene>(Result.ImportAssetPath.ResolveObject());
	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Result.ManifestAssetPath.ResolveObject());
	if (Manifest)
	{
		TestEqual(TEXT("Every source element has a persisted output row"), Manifest->ImportedElements.Num(), 3);
		for (const auto& Row : Manifest->ImportedElements)
		{
			FConVerseSourceRecord Record;
			TestTrue(TEXT("Ordinary source metadata is runtime-readable"), UConVerseSourceMetadata::FindSource(
				Cast<UActorComponent>(Row.ComponentPath.ResolveObject()), INDEX_NONE, Record));
			TestEqual(TEXT("Runtime identity agrees with source"), Record.SourceElement, Row.SourceElement);
		}
	}
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), Manifest, Scene);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseManualEditRebuildTest,
	"DatasmithHISM.OptimizedImport.ManualEditsRequireExplicitRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseManualEditRebuildTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.bAutomated = true;
	Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto First = FConVerseDatasmithImportService::ImportAndVerify(Options);
	auto* FirstScene = Cast<UDatasmithScene>(First.ImportAssetPath.ResolveObject());
	auto* FirstManifest = Cast<UConVerseOptimizedImportManifest>(First.ManifestAssetPath.ResolveObject());
	if (!TestNotNull(TEXT("Manifest"), FirstManifest)) return false;
	auto* Component = Cast<UInstancedStaticMeshComponent>(FirstManifest->Groups[0].OutputComponentPath.ResolveObject());
	FTransform Moved;
	Component->GetInstanceTransform(0, Moved, false);
	Moved.AddToTranslation(FVector(75, 0, 0));
	Component->UpdateInstanceTransform(0, Moved, false, true);
	const auto Drift = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Unchanged source finds transformed instance"), Drift.Status == EConVerseOptimizedImportStatus::AlreadyCurrent && !Drift.bVerificationSucceeded);
	Options.bRebuildFromSource = true;
	const auto Blocked = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Explicit rebuild alone does not authorize discarding edits"), Blocked.Status == EConVerseOptimizedImportStatus::ManualEditsDetected);
	TestTrue(TEXT("Old component survives refusal"), IsValid(Component));
	Component->UpdateInstanceTransform(0, FTransform(Moved.GetRotation(), Moved.GetTranslation() - FVector(75, 0, 0), Moved.GetScale3D()), false, true);
	USceneComponent* Added = NewObject<USceneComponent>(Component->GetOwner(), TEXT("ManualAddition"));
	Component->GetOwner()->AddInstanceComponent(Added); Added->SetupAttachment(Component->GetOwner()->GetRootComponent()); Added->RegisterComponent();
	TArray<FString> AddedDifferences;
	TestFalse(TEXT("Adding an untracked component is detected before its owner could be replaced"), ConVerseImportProcessing::CheckState(*FirstManifest, AddedDifferences));

	Options.bReplaceManualEdits = true;
	const auto Rebuilt = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Authorized replacement verifies"), Rebuilt.Status == EConVerseOptimizedImportStatus::Verified);
	TestNotEqual(TEXT("Forced rebuild creates a new session"), Rebuilt.SessionId, First.SessionId);
	auto* NewScene = Cast<UDatasmithScene>(Rebuilt.ImportAssetPath.ResolveObject());
	auto* NewManifest = Cast<UConVerseOptimizedImportManifest>(Rebuilt.ManifestAssetPath.ResolveObject());
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), NewManifest, NewScene);
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), FirstManifest, FirstScene);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseApprovedMaterialsTest,
	"DatasmithHISM.OptimizedImport.ApprovedMaterialsAndPresetRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseApprovedMaterialsTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.bAutomated = true;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto Analysis = FConVerseDatasmithImportService::Analyze(Options);
	FString Fingerprint;
	for (const FString& Line : Analysis.Diagnostics)
		if (Line.StartsWith(TEXT("Appearance:"))) { FString Left; Line.Split(TEXT("fingerprint="), &Left, &Fingerprint); break; }
	if (!TestFalse(TEXT("Source appearance fingerprint available"), Fingerprint.IsEmpty())) return false;
	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage()));
	Table->RowStruct = FConVerseMaterialMappingRow::StaticStruct();
	FConVerseMaterialMappingRow Row;
	Row.CatalogId = TEXT("Fixture");
	Row.SourceFingerprint = Fingerprint;
	Row.Replacement = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Row.bApproved = true;
	Table->AddRow(TEXT("Fixture"), Row);
	TStrongObjectPtr<UDataTable> Catalog(NewObject<UDataTable>(GetTransientPackage()));
	Catalog->RowStruct = FConVerseAppearanceCatalogRow::StaticStruct();
	FConVerseAppearanceCatalogRow CatalogRow;
	CatalogRow.CatalogId = TEXT("Fixture");
	CatalogRow.ReferenceFingerprint = Fingerprint;
	Catalog->AddRow(TEXT("Fixture"), CatalogRow);
	Options.Processing.AppearanceCatalog = Catalog.Get();
	Options.Processing.MaterialMappings = Table.Get();
	Options.Processing.bApplyApprovedMaterials = true;
	FString Identity;
	TestTrue(TEXT("Approved mapping validates"), ConVerseImportProcessing::ValidateMappings(Options.Processing, Identity, Error));
	const FString PresetFile = Fixture.RootDirectory / TEXT("Preset.json");
	TestTrue(TEXT("Preset saves"), FConVerseDatasmithImportService::SavePreset(PresetFile, Options, Error));
	FConVerseOptimizedImportOptions Loaded;
	TestTrue(TEXT("Preset loads"), FConVerseDatasmithImportService::LoadPreset(PresetFile, Loaded, Error));
	TestEqual(TEXT("Preset source preserved"), Loaded.FilePath, Options.FilePath);
	TestEqual(TEXT("Preset processing preserved"), ConVerseImportProcessing::SettingsJson(Loaded.Processing), ConVerseImportProcessing::SettingsJson(Options.Processing));
	const auto Imported = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Approved material replacement verifies"), Imported.Status == EConVerseOptimizedImportStatus::Verified);
	auto* Scene = Cast<UDatasmithScene>(Imported.ImportAssetPath.ResolveObject());
	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Imported.ManifestAssetPath.ResolveObject());
	if (Manifest)
	{
		for (const auto& Element : Manifest->ImportedElements)
			if (auto* Component = Cast<UStaticMeshComponent>(Element.ComponentPath.ResolveObject()))
				TestEqual(TEXT("Replacement reaches converted and ordinary output"), Component->GetMaterial(0), Row.Replacement.Get());
		FConVerseOptimizedImportOptions PreviewOptions = Options;
		PreviewOptions.Processing.bApplyApprovedMaterials = false;
		const auto Preview = FConVerseDatasmithImportService::Analyze(PreviewOptions);
		TestTrue(TEXT("Disabling a mapping previews the material change before rebuild"), Preview.Diagnostics.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("Material change ")) && S.Contains(TEXT("Imported material")); }));
		Manifest->SourceInventoryVersion = 0;
		const auto OlderPreview = FConVerseDatasmithImportService::Analyze(PreviewOptions);
		TestTrue(TEXT("Older inventories cannot claim a verified change preview"), OlderPreview.Diagnostics.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("Per-light/material change preview is unverified")); }));
		Manifest->SourceInventoryVersion = 1;
	}
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), Manifest, Scene);
	TestTrue(TEXT("External replacement survives cleanup"), IsValid(Row.Replacement.Get()));
	FFixtureFiles ChangedFixture;
	TestTrue(TEXT("Re-export customized material with identical source name"), CreateFixture(ChangedFixture, Error, 0, Fixture.RootDirectory, false, false, true));
	const auto ChangedAnalysis = FConVerseDatasmithImportService::Analyze(Options);
	TestTrue(TEXT("Changed appearance invalidates the prior approval"), ChangedAnalysis.Diagnostics.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("Approved appearance is absent or changed")); }));
	const auto ChangedImport = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Customized appearance imports successfully without inheriting the old approval"), ChangedImport.Status == EConVerseOptimizedImportStatus::Verified);
	auto* ChangedManifest = Cast<UConVerseOptimizedImportManifest>(ChangedImport.ManifestAssetPath.ResolveObject());
	if (ChangedManifest)
		for (const auto& Element : ChangedManifest->ImportedElements)
			if (auto* Component = Cast<UStaticMeshComponent>(Element.ComponentPath.ResolveObject()))
				TestNotEqual(TEXT("Name alone cannot apply an approved stock replacement"), Component->GetMaterial(0), Row.Replacement.Get());
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), ChangedManifest, Cast<UDatasmithScene>(ChangedImport.ImportAssetPath.ResolveObject()));

	Row.Replacement = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Missing.Missing")));
	Table->AddRow(TEXT("Fixture"), Row);
	TestFalse(TEXT("Missing approved target blocks application"), ConVerseImportProcessing::ValidateMappings(Options.Processing, Identity, Error));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseLightFidelityTest,
	"DatasmithHISM.OptimizedImport.PhysicalLightUnitsAndThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseLightFidelityTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error, 0, FString(), false, true))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.bAutomated = true;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Options.Processing.ManyLightThreshold = 2;
	const auto Analysis = FConVerseDatasmithImportService::Analyze(Options);
	TestEqual(TEXT("Only enabled local lights count towards warning"), Analysis.EnabledLocalLights, 2);
	TestEqual(TEXT("Unitless remains unresolved"), Analysis.UnitlessLights, 1);
	TestTrue(TEXT("Threshold boundary warns"), Analysis.Diagnostics.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("Many lights:")); }));
	Options.Processing.ManyLightThreshold = 3;
	TestFalse(TEXT("Below threshold does not warn"), FConVerseDatasmithImportService::Analyze(Options).Diagnostics.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("Many lights:")); }));
	const auto Imported = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Physical units and values verify against source"), Imported.Status == EConVerseOptimizedImportStatus::Verified);
	TestEqual(TEXT("Every imported light has source verification evidence"), Imported.Diagnostics.FilterByPredicate([](const FString& S) { return S.StartsWith(TEXT("PASS light")); }).Num(), 3);
	FFixtureFiles NoLights;
	TestTrue(TEXT("Re-export without the source lights"), CreateFixture(NoLights, Error, 0, Fixture.RootDirectory));
	const auto Preview = FConVerseDatasmithImportService::Analyze(Options);
	TestEqual(TEXT("Light removals appear in the rebuild preview"), Preview.Diagnostics.FilterByPredicate([](const FString& S) { return S.StartsWith(TEXT("Light change ")) && S.Contains(TEXT("<removed>")); }).Num(), 3);
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(),
		Cast<UConVerseOptimizedImportManifest>(Imported.ManifestAssetPath.ResolveObject()), Cast<UDatasmithScene>(Imported.ImportAssetPath.ResolveObject()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseMaterialStateTest,
	"DatasmithHISM.OptimizedImport.MaterialStateIgnoresGeneratedExpressionIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseMaterialStateTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMaterialInstanceConstant> Material(NewObject<UMaterialInstanceConstant>(GetTransientPackage()));
	FVectorParameterValue Value;
	Value.ParameterInfo = FMaterialParameterInfo(TEXT("DiffuseColor"));
	Value.ParameterValue = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Material->VectorParameterValues.Add(Value);
	const FString Before = ConVerseImportProcessing::ObjectState(*Material);
	Material->VectorParameterValues[0].ExpressionGUID = FGuid::NewGuid();
	TestEqual(TEXT("PostLoad expression identity does not create a false manual-edit conflict"), ConVerseImportProcessing::ObjectState(*Material), Before);
	Material->VectorParameterValues[0].ParameterValue = FLinearColor::Red;
	TestNotEqual(TEXT("Actual appearance edits still produce a conflict"), ConVerseImportProcessing::ObjectState(*Material), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseAmbiguousOwnershipTest,
	"DatasmithHISM.OptimizedImport.MultipleActiveOwnersAreRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseAmbiguousOwnershipTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile; Options.bAutomated = true;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto Imported = FConVerseDatasmithImportService::ImportAndVerify(Options);
	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Imported.ManifestAssetPath.ResolveObject());
	auto* Scene = Cast<UDatasmithScene>(Imported.ImportAssetPath.ResolveObject());
	if (!TestNotNull(TEXT("Committed manifest"), Manifest)) return false;
	auto* DuplicateScene = NewObject<UDatasmithScene>(CreatePackage(*(Options.DestinationPath / TEXT("DuplicateOwner"))), TEXT("DuplicateOwner"), RF_Public | RF_Standalone);
	auto* Duplicate = NewObject<UConVerseOptimizedImportManifest>(DuplicateScene);
	Duplicate->CanonicalSourceFilePath = Manifest->CanonicalSourceFilePath;
	Duplicate->DestinationContentFolder = Manifest->DestinationContentFolder;
	Duplicate->ManifestSchemaVersion = Manifest->ManifestSchemaVersion;
	Duplicate->CommitState = EConVerseOptimizedImportCommitState::Active;
	DuplicateScene->AddAssetUserData(Duplicate);
	FAssetRegistryModule::AssetCreated(DuplicateScene);
	const auto Blocked = FConVerseDatasmithImportService::ImportAndVerify(Options);
	TestTrue(TEXT("Ambiguous active ownership blocks the real dispatch before mutation"), Blocked.Status == EConVerseOptimizedImportStatus::OptimizedReimportBlocked);
	TArray<FString> Differences;
	TestTrue(TEXT("Refusal preserves the existing output"), ConVerseImportProcessing::CheckState(*Manifest, Differences));
	TArray<UObject*> DuplicateObjects{DuplicateScene};
	ObjectTools::ForceDeleteObjects(DuplicateObjects, false);
	CleanupCommittedImport(*GEditor->GetEditorWorldContext().World(), Manifest, Scene);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVersePanelInputStateTest,
	"DatasmithHISM.OptimizedImport.PanelInputStateInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVersePanelInputStateTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles First, Second;
	FString Error;
	if (!TestTrue(TEXT("Create first source"), CreateFixture(First, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*First.RootDirectory, false, true); };
	if (!TestTrue(TEXT("Create second source"), CreateFixture(Second, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Second.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = First.SceneFile;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Options.bAutomated = true;
	const auto Imported = FConVerseDatasmithImportService::ImportAndVerify(Options);
	UWorld* World = GEditor->GetEditorWorldContext().World();
	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Imported.ManifestAssetPath.ResolveObject());
	auto* Scene = Cast<UDatasmithScene>(Imported.ImportAssetPath.ResolveObject());
	ON_SCOPE_EXIT { CleanupCommittedImport(*World, Manifest, Scene); };
	if (!TestTrue(TEXT("Seed import verifies"), Imported.bVerificationSucceeded)) return false;
	const auto Panel = SNew(SConVerseDatasmithImportPanel);
	const auto BeforeWorld = SnapshotWorld(*World);
	const auto BeforeAssets = SnapshotAssets();
	const FString SessionFile = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Presets/Session.json");
	const FString SessionHash = HashFile(SessionFile);
	auto Seed = [&]()
	{
		Panel->ApplyInputOptions(Options);
		Panel->LastManifestPath = Imported.ManifestAssetPath;
		Panel->InspectionRows = Imported.InspectionRows;
		Panel->Appearances = Imported.Appearances;
		Panel->ReportText = FText::FromString(TEXT("Previous imported result"));
		Panel->bHasRunSinceInputChange = true;
		Panel->bLastRunAppliedTessellation = true;
		Panel->RefreshInspection();
		if (!Panel->FilteredRows.IsEmpty()) Panel->InspectionList->SetSelection(Panel->FilteredRows[0]);
	};
	auto CheckCleared = [&]()
	{
		TestFalse(TEXT("Old save target cleared"), Panel->LastManifestPath.IsValid());
		TestTrue(TEXT("Old inspection and review rows cleared"), Panel->InspectionRows.IsEmpty() && Panel->Appearances.IsEmpty() && Panel->FilteredRows.IsEmpty());
		TestTrue(TEXT("Old selection cleared"), Panel->InspectionList->GetSelectedItems().IsEmpty());
		TestFalse(TEXT("Old report cleared"), Panel->ReportText.ToString().Contains(TEXT("Previous imported result")));
	};
	Seed();
	bool bCloseRequested = false;
	const auto Review = SNew(SWindow);
	Review->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride::CreateLambda([&](const TSharedRef<SWindow>&) { bCloseRequested = true; }));
	Panel->MaterialReviewWindow = Review;
	FConVerseOptimizedImportOptions Other = Options;
	Other.FilePath = Second.SceneFile;
	Other.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Other.Tessellation.ChordTolerance = 0.35f;
	Other.Processing.ManyLightThreshold = 137;
	const FString Preset = First.RootDirectory / TEXT("Other.json");
	TestTrue(TEXT("Save different-source preset"), FConVerseDatasmithImportService::SavePreset(Preset, Other, Error));
	TestTrue(TEXT("Apply actual preset file"), Panel->LoadPresetFromFile(Preset));
	CheckCleared();
	TestTrue(TEXT("Stale review closed and disabled immediately"), bCloseRequested && !Review->IsEnabled() && !Panel->MaterialReviewWindow.IsValid());
	TestEqual(TEXT("Source restored"), Panel->SourcePath, Other.FilePath);
	TestTrue(TEXT("Mode restored"), Panel->InstanceType == Other.InstanceType);
	TestEqual(TEXT("Tessellation restored"), Panel->Tessellation.ChordTolerance, Other.Tessellation.ChordTolerance);
	TestEqual(TEXT("Processing restored"), Panel->ProcessingRecipe->Processing.ManyLightThreshold, 137);
	TestFalse(TEXT("Source-specific translator state invalidated"), Panel->bHasRunSinceInputChange || Panel->bLastRunAppliedTessellation);
	Seed();
	Panel->ApplySelectedSource(Second.SceneFile);
	CheckCleared();
	Seed();
	Panel->SourcePathTextBox->SetText(FText::FromString(Second.SceneFile));
	CheckCleared();
	Seed();
	Panel->DestinationPathTextBox->SetText(FText::FromString(Options.DestinationPath + TEXT("Other")));
	CheckCleared();
	TestTrue(TEXT("Destination-only change retains translator knowledge"), Panel->bHasRunSinceInputChange);
	Seed();
	Other.FilePath = Options.FilePath;
	TestTrue(TEXT("Save same-identity preset"), FConVerseDatasmithImportService::SavePreset(Preset, Other, Error));
	TestTrue(TEXT("Load same-identity preset"), Panel->LoadPresetFromFile(Preset));
	TestEqual(TEXT("Same identity retains imported association"), Panel->LastManifestPath, Imported.ManifestAssetPath);
	TestTrue(TEXT("New settings require analysis"), Panel->PanelStatus == SConVerseDatasmithImportPanel::EPanelStatus::Ready);
	TestTrue(TEXT("Same identity keeps existing inspection"), !Panel->InspectionRows.IsEmpty());
	FFileHelper::SaveStringToFile(TEXT("invalid preset"), *Preset);
	TestFalse(TEXT("Invalid preset refused"), Panel->LoadPresetFromFile(Preset));
	TestEqual(TEXT("Invalid preset preserves source"), Panel->SourcePath, Other.FilePath);
	TestEqual(TEXT("Invalid preset preserves association"), Panel->LastManifestPath, Imported.ManifestAssetPath);
	TestTrue(TEXT("Restoring inputs does not mutate world"), BeforeWorld.OrderIndependentCompareEqual(SnapshotWorld(*World)));
	const auto AfterAssets = SnapshotAssets();
	TestTrue(TEXT("Restoring inputs does not create/delete assets"), BeforeAssets.Difference(AfterAssets).IsEmpty() && AfterAssets.Difference(BeforeAssets).IsEmpty());
	TestEqual(TEXT("Restoring inputs does not overwrite executed-session preset"), HashFile(SessionFile), SessionHash);
	TestEqual(TEXT("First source unchanged"), HashFile(First.SceneFile), First.SceneHash);
	TestEqual(TEXT("Second source unchanged"), HashFile(Second.SceneFile), Second.SceneHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVersePanelSessionRestoreTest,
	"DatasmithHISM.OptimizedImport.PanelSessionRestoreShowsInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVersePanelSessionRestoreTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	// A restored source that is not shown still drives Analyze/Import, so the field must display it.
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create source"), CreateFixture(Fixture, Error))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	const FString SessionFile = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Presets/Session.json");
	FString OriginalSession;
	const bool bHadSession = FFileHelper::LoadFileToString(OriginalSession, *SessionFile);
	ON_SCOPE_EXIT
	{
		if (bHadSession) FFileHelper::SaveStringToFile(OriginalSession, *SessionFile);
		else IFileManager::Get().Delete(*SessionFile);
	};
	FConVerseOptimizedImportOptions Session;
	Session.FilePath = Fixture.SceneFile;
	Session.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Session.InstanceType = EConVerseOptimizedInstanceType::HISM;
	Session.MinimumInstanceCount = 3;
	if (!TestTrue(TEXT("Write executed-session preset"), FConVerseDatasmithImportService::SavePreset(SessionFile, Session, Error))) return false;
	const auto BeforeAssets = SnapshotAssets();
	const auto Panel = SNew(SConVerseDatasmithImportPanel);
	TestEqual(TEXT("Restored source is the effective input"), Panel->SourcePath, Session.FilePath);
	TestEqual(TEXT("Restored source is visible"), Panel->SourcePathTextBox->GetText().ToString(), Session.FilePath);
	TestEqual(TEXT("Restored destination is visible"), Panel->DestinationPathTextBox->GetText().ToString(), Session.DestinationPath);
	TestTrue(TEXT("Status reflects restored valid inputs"), Panel->PanelStatus == SConVerseDatasmithImportPanel::EPanelStatus::Ready);
	TestFalse(TEXT("Status does not ask for a source that is already set"), Panel->StatusDetail.ToString().Contains(TEXT("Select a Datasmith-supported source")));
	const auto AfterAssets = SnapshotAssets();
	TestTrue(TEXT("Opening the panel creates/deletes no assets"), BeforeAssets.Difference(AfterAssets).IsEmpty() && AfterAssets.Difference(BeforeAssets).IsEmpty());
	TestEqual(TEXT("Source unchanged"), HashFile(Fixture.SceneFile), Fixture.SceneHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseAnalysisPhaseCancellationTest,
	"DatasmithHISM.OptimizedImport.AnalysisPhaseCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseAnalysisPhaseCancellationTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create textured, multi-material, light fixture"), CreateFixture(Fixture, Error, 3, FString(), true, true, false, true))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Options.bAutomated = true;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	const auto BeforeWorld = SnapshotWorld(*World);
	const bool bDirty = World->GetOutermost()->IsDirty();
	const auto BeforeAssets = SnapshotAssets();
	const FString TextureHash = HashFile(Fixture.TextureFile);
	for (bool bImport : {false, true})
	{
		for (const auto Target : {EConVerseImportWorkPhase::SourceHash, EConVerseImportWorkPhase::SidecarHash,
			EConVerseImportWorkPhase::Translation, EConVerseImportWorkPhase::SourceActors, EConVerseImportWorkPhase::Materials,
			EConVerseImportWorkPhase::TextureHash, EConVerseImportWorkPhase::Dependencies, EConVerseImportWorkPhase::GroupPlanning,
			EConVerseImportWorkPhase::Report})
		{
			if (bImport && Target == EConVerseImportWorkPhase::Report) continue;
			bool bCancel = false;
			int32 NotificationsAfterCancel = 0;
			Options.ProgressObserver = [&](EConVerseImportWorkPhase Phase, int64 Completed, int64 Total)
			{
				if (bCancel) ++NotificationsAfterCancel;
				const bool bChunk = Target == EConVerseImportWorkPhase::TextureHash || Target == EConVerseImportWorkPhase::SidecarHash;
				if (Phase == Target && (Target == EConVerseImportWorkPhase::Report || (bChunk ? Completed >= 1024 * 1024 && Completed < Total : Completed > 0))) bCancel = true;
			};
			Options.CancelRequested = [&]() { return bCancel; };
			const FString Case = FString::Printf(TEXT("%s phase %d"), bImport ? TEXT("Import") : TEXT("Analyze"), int32(Target));
			const auto Result = bImport ? FConVerseDatasmithImportService::ImportAndVerify(Options) : FConVerseDatasmithImportService::Analyze(Options);
			TestTrue(*(Case + TEXT(" reached cancellation checkpoint")), bCancel);
			TestTrue(*(Case + TEXT(" cancelled explicitly")), Result.Status == EConVerseOptimizedImportStatus::CancelledRolledBack);
			TestEqual(*(Case + TEXT(" stopped progress immediately")), NotificationsAfterCancel, 0);
			TestTrue(*(Case + TEXT(" no partial plan")), Result.PlanId.IsEmpty() && Result.PlannedGroupCount == 0 && Result.PlannedInstanceCount == 0);
			TestTrue(*(Case + TEXT(" no partial inspection or appearance data")), Result.InspectionRows.IsEmpty() && Result.Appearances.IsEmpty() && Result.MaterialDecisions.IsEmpty() && Result.SourceLightDescriptions.IsEmpty());
			TestTrue(*(Case + TEXT(" no attempted-session output")), Result.SessionId.IsEmpty() && Result.CreatedObjectCount == 0 && !Result.ManifestAssetPath.IsValid());
			TestTrue(*(Case + TEXT(" unchanged world objects")), BeforeWorld.OrderIndependentCompareEqual(SnapshotWorld(*World)));
			TestEqual(*(Case + TEXT(" unchanged world dirty flag")), World->GetOutermost()->IsDirty(), bDirty);
			const auto AfterAssets = SnapshotAssets();
			TestTrue(*(Case + TEXT(" unchanged asset inventory")), BeforeAssets.Difference(AfterAssets).IsEmpty() && AfterAssets.Difference(BeforeAssets).IsEmpty());
			TestEqual(*(Case + TEXT(" unchanged source")), HashFile(Fixture.SceneFile), Fixture.SceneHash);
			TestEqual(*(Case + TEXT(" unchanged mesh")), HashFile(Fixture.MeshFile), Fixture.MeshHash);
			TestEqual(*(Case + TEXT(" unchanged texture")), HashFile(Fixture.TextureFile), TextureHash);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVerseAnalysisProgressEvidenceTest,
	"DatasmithHISM.OptimizedImport.AnalysisProgressPreservesEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVerseAnalysisProgressEvidenceTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create textured fixture"), CreateFixture(Fixture, Error, 3, FString(), true, true, false, true))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Options.bAutomated = true;
	const auto Baseline = FConVerseDatasmithImportService::Analyze(Options);
	TestTrue(TEXT("Uncancelled analysis succeeds"), Baseline.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded);
	int32 TextureChunks = 0;
	Options.ProgressObserver = [&](EConVerseImportWorkPhase Phase, int64 Completed, int64) { if (Phase == EConVerseImportWorkPhase::TextureHash && Completed > 0) ++TextureChunks; };
	const auto Observed = FConVerseDatasmithImportService::Analyze(Options);
	TestTrue(TEXT("Texture fingerprint has several chunk notifications"), TextureChunks >= 3);
	TestEqual(TEXT("Observer does not change plan identity"), Observed.PlanId, Baseline.PlanId);
	TestEqual(TEXT("Source hash unchanged"), Observed.SourceFileHash, Fixture.SceneHash);
	TestEqual(TEXT("Sidecar fingerprint unchanged"), Observed.SidecarHash, Baseline.SidecarHash);
	TestEqual(TEXT("Instance count unchanged"), Observed.PlannedInstanceCount, Baseline.PlannedInstanceCount);
	TestEqual(TEXT("Light count unchanged"), Observed.SourceLightCount, Baseline.SourceLightCount);
	TestTrue(TEXT("Material fingerprints and decisions unchanged"), Observed.MaterialDecisions.OrderIndependentCompareEqual(Baseline.MaterialDecisions));
	FScopedSlowTask Task(1.0f);
	FConVerseImportProgress Progress(Options, Task);
	FString StreamedHash;
	int64 Size = 0;
	TestTrue(TEXT("Streamed fingerprint succeeds"), Progress.HashFile(Fixture.TextureFile, StreamedHash, Size, EConVerseImportWorkPhase::TextureHash) == EConVerseImportWorkResult::Completed);
	TestEqual(TEXT("Streamed bytes match the original engine MD5 implementation"), StreamedHash, HashFile(Fixture.TextureFile));
	const FString Preset = Fixture.RootDirectory / TEXT("Observed.json");
	TestTrue(TEXT("Observed options can be saved"), FConVerseDatasmithImportService::SavePreset(Preset, Options, Error));
	FConVerseOptimizedImportOptions Loaded;
	TestTrue(TEXT("Observed options can be loaded"), FConVerseDatasmithImportService::LoadPreset(Preset, Loaded, Error));
	TestFalse(TEXT("Progress observer is not persisted"), bool(Loaded.ProgressObserver));
	const FString MovedTexture = Fixture.TextureFile + TEXT(".missing-test");
	if (!TestTrue(TEXT("Temporarily remove disposable texture dependency"), IFileManager::Get().Move(*MovedTexture, *Fixture.TextureFile))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().Move(*Fixture.TextureFile, *MovedTexture); };
	for (bool bImport : {false, true})
	{
		const auto Missing = bImport ? FConVerseDatasmithImportService::ImportAndVerify(Options) : FConVerseDatasmithImportService::Analyze(Options);
		TestTrue(TEXT("Missing dependency remains an explicit failure"), Missing.Status == EConVerseOptimizedImportStatus::SourceLoadFailed && Missing.Summary.Contains(TEXT("Missing referenced source files")));
		TestTrue(TEXT("Missing dependency cannot create an import session"), Missing.SessionId.IsEmpty() && Missing.CreatedObjectCount == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConVersePanelMissingTexturesTest,
	"DatasmithHISM.OptimizedImport.MissingTexturesRequireExplicitAcceptance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConVersePanelMissingTexturesTest::RunTest(const FString&)
{
	using namespace ConVerseOptimizedImportAutomation;
	FFixtureFiles Fixture;
	FString Error;
	if (!TestTrue(TEXT("Create textured fixture"), CreateFixture(Fixture, Error, 3, FString(), true, true, false, true))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Fixture.RootDirectory, false, true); };
	const FString MovedTexture = Fixture.TextureFile + TEXT(".missing-test");
	if (!TestTrue(TEXT("Remove disposable texture"), IFileManager::Get().Move(*MovedTexture, *Fixture.TextureFile))) return false;
	FConVerseOptimizedImportOptions Options;
	Options.FilePath = Fixture.SceneFile;
	Options.DestinationPath = TEXT("/Game/__ConVerseAutomation/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Options.bAutomated = true;
	Options.TextureSearchFolders.Reset(); // Hermetic: a real library file must not satisfy this fixture.

	// Search folders: a same-named file (any case) is imported from the folder without touching the source.
	{
		const FString Library = Fixture.RootDirectory / TEXT("Library/Mats");
		const FString LibraryCopy = Library / FPaths::GetCleanFilename(Fixture.TextureFile).ToUpper();
		TestTrue(TEXT("Create disposable texture library"), IFileManager::Get().Copy(*LibraryCopy, *MovedTexture) == COPY_OK);
		FConVerseOptimizedImportOptions Searched = Options;
		Searched.TextureSearchFolders = {Fixture.RootDirectory / TEXT("Empty"), Fixture.RootDirectory / TEXT("Library")};
		const auto Resolved = FConVerseDatasmithImportService::Analyze(Searched);
		TestTrue(TEXT("Texture found in a search folder needs no acceptance"), Resolved.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded);
		TestEqual(TEXT("Resolution is reported"), Resolved.ResolvedTextures.Num(), 1);
		TestTrue(TEXT("Resolution names the library file"), !Resolved.ResolvedTextures.IsEmpty() && Resolved.ResolvedTextures[0].EndsWith(LibraryCopy));
		TestTrue(TEXT("Nothing is left missing"), Resolved.MissingTextures.IsEmpty());
		FConVerseOptimizedImportOptions AcceptedOnly = Options;
		AcceptedOnly.bAllowMissingTextures = true;
		TestNotEqual(TEXT("A resolved texture changes plan identity"), Resolved.PlanId, FConVerseDatasmithImportService::Analyze(AcceptedOnly).PlanId);
		TestTrue(TEXT("Resolution does not restore the source-side file"), !FPaths::FileExists(Fixture.TextureFile));
		UWorld* ResolvedWorld = GEditor->GetEditorWorldContext().World();
		const auto ResolvedImport = FConVerseDatasmithImportService::ImportAndVerify(Searched);
		auto* ResolvedManifest = Cast<UConVerseOptimizedImportManifest>(ResolvedImport.ManifestAssetPath.ResolveObject());
		auto* ResolvedScene = Cast<UDatasmithScene>(ResolvedImport.ImportAssetPath.ResolveObject());
		TestTrue(TEXT("Import using the library texture verifies"), ResolvedImport.Status == EConVerseOptimizedImportStatus::Verified);
		TestTrue(TEXT("Import report names the search-folder texture"), ResolvedImport.Report.Contains(TEXT("Missing texture found in search folder")));
		CleanupCommittedImport(*ResolvedWorld, ResolvedManifest, ResolvedScene);
		TestTrue(TEXT("Source-side texture is still absent after import"), !FPaths::FileExists(Fixture.TextureFile));
	}

	// Service: textures fail until the exact missing set is accepted; geometry never can be.
	const auto Refused = FConVerseDatasmithImportService::Analyze(Options);
	TestTrue(TEXT("Unaccepted missing texture fails"), Refused.Status == EConVerseOptimizedImportStatus::SourceLoadFailed && Refused.Summary.Contains(TEXT("Missing referenced source files")));
	TestEqual(TEXT("Missing texture is reported separately"), Refused.MissingTextures.Num(), 1);
	TestTrue(TEXT("No mesh file is missing"), Refused.MissingMeshFiles.IsEmpty());
	FConVerseOptimizedImportOptions Other = Options;
	Other.AcceptedMissingTextures = {TEXT("SomeOtherTexture: C:/NotThisFile.png")};
	TestTrue(TEXT("Accepting a different texture does not proceed"), FConVerseDatasmithImportService::Analyze(Other).Status == EConVerseOptimizedImportStatus::SourceLoadFailed);
	FConVerseOptimizedImportOptions Accepted = Options;
	Accepted.AcceptedMissingTextures = Refused.MissingTextures;
	const auto Analyzed = FConVerseDatasmithImportService::Analyze(Accepted);
	TestTrue(TEXT("Accepted missing texture proceeds"), Analyzed.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded);
	TestTrue(TEXT("Accepted texture is listed in the report"), Analyzed.Diagnostics.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Proceeding without missing texture")); }));
	FConVerseOptimizedImportOptions Headless = Options;
	Headless.bAllowMissingTextures = true;
	TestTrue(TEXT("Headless opt-in proceeds"), FConVerseDatasmithImportService::Analyze(Headless).Status == EConVerseOptimizedImportStatus::AnalysisSucceeded);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	const auto Imported = FConVerseDatasmithImportService::ImportAndVerify(Accepted);
	auto* Manifest = Cast<UConVerseOptimizedImportManifest>(Imported.ManifestAssetPath.ResolveObject());
	auto* Scene = Cast<UDatasmithScene>(Imported.ImportAssetPath.ResolveObject());
	ON_SCOPE_EXIT { CleanupCommittedImport(*World, Manifest, Scene); };
	TestTrue(TEXT("Import without the accepted texture verifies"), Imported.Status == EConVerseOptimizedImportStatus::Verified);
	TestTrue(TEXT("Import report lists the accepted texture"), Imported.Report.Contains(TEXT("Proceeding without missing texture")));

	const FString MovedMesh = Fixture.MeshFile + TEXT(".missing-test");
	if (TestTrue(TEXT("Remove disposable mesh"), IFileManager::Get().Move(*MovedMesh, *Fixture.MeshFile)))
	{
		const auto NoMesh = FConVerseDatasmithImportService::Analyze(Headless);
		TestTrue(TEXT("Missing mesh fails even with textures allowed"), NoMesh.Status == EConVerseOptimizedImportStatus::SourceLoadFailed && !NoMesh.MissingMeshFiles.IsEmpty());
		IFileManager::Get().Move(*Fixture.MeshFile, *MovedMesh);
	}

	// Panel: Yes/No prompt, exact acceptance, and reset on source change.
	const FString SessionFile = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/Presets/Session.json");
	FString OriginalSession;
	const bool bHadSession = FFileHelper::LoadFileToString(OriginalSession, *SessionFile);
	ON_SCOPE_EXIT
	{
		if (bHadSession) FFileHelper::SaveStringToFile(OriginalSession, *SessionFile);
		else IFileManager::Get().Delete(*SessionFile);
	};
	const auto Panel = SNew(SConVerseDatasmithImportPanel);
	Panel->ApplyInputOptions(Options);
	int32 Prompts = 0;
	FString LastPrompt;
	bool bAnswer = false;
	Panel->AskYesNo = [&](const FText& Prompt) { ++Prompts; LastPrompt = Prompt.ToString(); return bAnswer; };
	Panel->HandleAnalyze();
	TestEqual(TEXT("Panel asks once"), Prompts, 1);
	TestTrue(TEXT("Prompt names the missing file"), LastPrompt.Contains(FPaths::GetCleanFilename(Fixture.TextureFile)));
	TestTrue(TEXT("No leaves nothing accepted"), Panel->AcceptedMissingTextures.IsEmpty());
	TestTrue(TEXT("No leaves the analysis failed"), Panel->PanelStatus != SConVerseDatasmithImportPanel::EPanelStatus::Analyzed);
	bAnswer = true;
	Panel->HandleAnalyze();
	TestEqual(TEXT("Yes accepts exactly the reported textures"), Panel->AcceptedMissingTextures, Refused.MissingTextures);
	TestTrue(TEXT("Yes completes the analysis"), Panel->PanelStatus == SConVerseDatasmithImportPanel::EPanelStatus::Analyzed);
	const int32 PromptsBeforeRepeat = Prompts;
	Panel->HandleAnalyze();
	TestEqual(TEXT("Accepted textures are not asked again for the same source"), Prompts, PromptsBeforeRepeat);
	FFixtureFiles Second;
	if (TestTrue(TEXT("Create second source"), CreateFixture(Second, Error)))
	{
		ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Second.RootDirectory, false, true); };
		Panel->ApplySelectedSource(Second.SceneFile);
		TestTrue(TEXT("Changing source clears the acceptance"), Panel->AcceptedMissingTextures.IsEmpty());
	}
	IFileManager::Get().Move(*Fixture.TextureFile, *MovedTexture);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
