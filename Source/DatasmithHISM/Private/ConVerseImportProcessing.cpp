#include "ConVerseImportProcessing.h"
#include "ConVerseOptimizedImportManifest.h"
#include "ConVerseSourceMetadata.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "DatasmithScene.h"
#include "DatasmithSceneActor.h"
#include "DatasmithAssetImportData.h"
#include "DatasmithAssetUserData.h"
#include "Engine/TextureLightProfile.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IDatasmithSceneElements.h"
#include "JsonObjectConverter.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "StaticMeshCompiler.h"
#include "UObject/UnrealType.h"

namespace ConVerseImportProcessing
{
	static FString Hash(const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	FString SettingsJson(const FConVerseImportProcessingSettings& Settings)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(Settings, Json);
		return Json;
	}

	bool ValidateMappings(const FConVerseImportProcessingSettings& Settings, FString& OutIdentity, FString& OutError)
	{
		OutIdentity.Reset();
		if (!Settings.bApplyApprovedMaterials) return true;
		UDataTable* Table = Settings.MaterialMappings.LoadSynchronous();
		if (!Table || Table->GetRowStruct() != FConVerseMaterialMappingRow::StaticStruct())
		{
			OutError = TEXT("Approved material replacement requires a ConVerseMaterialMappingRow DataTable.");
			return false;
		}
		UDataTable* Catalog = Settings.AppearanceCatalog.LoadSynchronous();
		TSet<FString> CatalogIds;
		if (Catalog && Catalog->GetRowStruct() == FConVerseAppearanceCatalogRow::StaticStruct())
			for (const auto& Pair : Catalog->GetRowMap())
			{
				const FString& Id = reinterpret_cast<const FConVerseAppearanceCatalogRow*>(Pair.Value)->CatalogId;
				if (Id.IsEmpty() || CatalogIds.Contains(Id)) { OutError = TEXT("Appearance catalog IDs must be nonempty and unique."); return false; }
				CatalogIds.Add(Id);
			}
		TArray<FString> Rows;
		TSet<FString> Fingerprints;
		for (const auto& Pair : Table->GetRowMap())
		{
			const auto& Row = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
			if (!Row.bApproved) continue;
			if (Row.SourceFingerprint.IsEmpty() || !CatalogIds.Contains(Row.CatalogId) || Fingerprints.Contains(Row.SourceFingerprint)
				|| !Row.Replacement.LoadSynchronous())
			{
				OutError = FString::Printf(TEXT("Approved mapping '%s' has missing identity/target or duplicate source approval."), *Pair.Key.ToString());
				return false;
			}
			Fingerprints.Add(Row.SourceFingerprint);
			Rows.Add(FString::Printf(TEXT("%s|%s|%s|%d"), *Row.CatalogId, *Row.SourceFingerprint,
				*Row.Replacement.ToSoftObjectPath().ToString(), Row.Revision));
		}
		Rows.Sort();
		OutIdentity = Hash(FString::Join(Rows, TEXT("\n")));
		return true;
	}

	bool ApproveAppearance(const FConVerseAppearanceReviewRow& Appearance, const FString& CatalogId, UMaterialInterface* Target,
		const FConVerseImportProcessingSettings& Settings, FString& OutError)
	{
		UDataTable* Catalog = Settings.AppearanceCatalog.LoadSynchronous();
		UDataTable* Mappings = Settings.MaterialMappings.LoadSynchronous();
		if (!Catalog || Catalog->GetRowStruct() != FConVerseAppearanceCatalogRow::StaticStruct()
			|| !Mappings || Mappings->GetRowStruct() != FConVerseMaterialMappingRow::StaticStruct()
			|| !Target || Appearance.Fingerprint.IsEmpty())
		{ OutError = TEXT("Choose valid catalog/mapping tables and a target. Missing texture evidence cannot be approved."); return false; }
		int32 CatalogMatches = 0;
		for (const auto& Pair : Catalog->GetRowMap())
			CatalogMatches += reinterpret_cast<const FConVerseAppearanceCatalogRow*>(Pair.Value)->CatalogId == CatalogId ? 1 : 0;
		if (CatalogId.IsEmpty() || CatalogMatches != 1) { OutError = TEXT("Choose one unique catalog ID before approving this appearance variant."); return false; }
		FName Key(*Appearance.Fingerprint);
		FConVerseMaterialMappingRow Row;
		for (const auto& Pair : Mappings->GetRowMap())
		{
			const auto& Existing = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
			if (Existing.SourceFingerprint == Appearance.Fingerprint) { Key = Pair.Key; Row = Existing; break; }
		}
		Row.CatalogId = CatalogId;
		Row.SourceFingerprint = Appearance.Fingerprint;
		Row.Replacement = Target;
		Row.bApproved = true;
		++Row.Revision;
		Mappings->Modify();
		Mappings->AddRow(Key, Row);
		Mappings->MarkPackageDirty();
		return true;
	}

	static FString TextureFingerprint(const FString& File, FConVerseImportProgress* Progress)
	{
		if (!Progress) return LexToString(FMD5Hash::HashFile(*File));
		FString Content;
		int64 Size = 0;
		return Progress->HashFile(File, Content, Size, EConVerseImportWorkPhase::TextureHash) == EConVerseImportWorkResult::Completed ? Content : FString();
	}

	static FString AppearanceFingerprint(const TSharedRef<IDatasmithScene>& Scene, IDatasmithBaseMaterialElement& Material, const FString& SourceFile, FConVerseImportProgress* Progress = nullptr)
	{
		// Revit exports material instances. Their typed properties include UV transforms and texture references.
		// Display names are deliberately excluded so a rename does not approve a changed appearance.
		if (!Material.IsA(EDatasmithElementType::MaterialInstance))
		{
			FString Evidence = LexToString(Material.CalculateElementHash(true));
			// Conservatively bind non-Revit graphs to every scene texture until graph-specific traversal is available.
			for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
			{
				const auto& Texture = Scene->GetTexture(Index);
				if (!Texture.IsValid()) return FString();
				FString File = Texture->GetFile();
				if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SourceFile), File);
				const FString Content = TextureFingerprint(File, Progress);
				if (Content.IsEmpty()) return FString();
				Evidence += Content + LexToString(Texture->CalculateElementHash(true));
			}
			return Hash(Evidence);
		}
		auto& Instance = static_cast<IDatasmithMaterialInstanceElement&>(Material);
		TArray<FString> Properties;
		Properties.Add(FString::Printf(TEXT("Type=%d|Quality=%d|Parent=%s"), int32(Instance.GetMaterialType()),
			int32(Instance.GetQuality()), Instance.GetCustomMaterialPathName()));
		for (int32 Index = 0; Index < Instance.GetPropertiesCount(); ++Index)
		{
			if (Progress && Progress->IsCancelled()) return FString();
			const auto& Property = Instance.GetProperty(Index);
			if (!Property.IsValid()) continue;
			FString Value = Property->GetValue();
			if (Property->GetPropertyType() == EDatasmithKeyValuePropertyType::Texture)
			{
				bool bResolved = false;
				for (int32 TextureIndex = 0; TextureIndex < Scene->GetTexturesCount(); ++TextureIndex)
				{
					const auto& Texture = Scene->GetTexture(TextureIndex);
					if (Texture.IsValid() && Value == Texture->GetName())
					{
						FString File = Texture->GetFile();
						if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SourceFile), File);
						const FString Content = TextureFingerprint(File, Progress);
						if (Content.IsEmpty()) return FString();
						Value = Content + TEXT("|") + LexToString(Texture->CalculateElementHash(true));
						bResolved = true;
						break;
					}
				}
				if (!bResolved && !Value.IsEmpty()) return FString();
			}
			Properties.Add(FString::Printf(TEXT("%s|%d|%d:%s"), Property->GetName(), int32(Property->GetPropertyType()), Value.Len(), *Value));
		}
		Properties.Sort();
		return Hash(FString::Join(Properties, TEXT("\n")));
	}

	EConVerseImportWorkResult AnalyzeScene(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FConVerseImportProgress& Progress)
	{
		Result.ProcessingSettingsJson = SettingsJson(Options.Processing);
		Result.SourceMeshAssetCount = Scene->GetMeshesCount();
		TSet<FString> MeshNames;
		for (int32 Index = 0; Index < Scene->GetMeshesCount(); ++Index)
		{
			if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
			const auto& Mesh = Scene->GetMesh(Index);
			if (Mesh.IsValid()) MeshNames.Add(Mesh->GetName());
		}
		for (const FString& Name : Options.Processing.KeepOrdinaryMeshElements)
			if (!MeshNames.Contains(Name)) Result.Diagnostics.Add(TEXT("Unmatched keep-ordinary exception: ") + Name);
		for (const FString& Name : Options.Processing.DisableNaniteMeshElements)
			if (!MeshNames.Contains(Name)) Result.Diagnostics.Add(TEXT("Unmatched Nanite exception: ") + Name);
		int64 Visited = 0;
		TFunction<void(const TSharedPtr<IDatasmithActorElement>&)> Visit;
		Visit = [&](const TSharedPtr<IDatasmithActorElement>& Actor)
		{
			if (!Progress.Update(EConVerseImportWorkPhase::SourceActors, Visited++, 0) || !Actor.IsValid()) return;
			if (Actor->IsA(EDatasmithElementType::PointLight) || Actor->IsA(EDatasmithElementType::DirectionalLight))
			{
				++Result.SourceLightCount;
				const auto Light = StaticCastSharedPtr<IDatasmithLightActorElement>(Actor);
				const int32 Units = Actor->IsA(EDatasmithElementType::PointLight) ? int32(StaticCastSharedPtr<IDatasmithPointLightElement>(Actor)->GetIntensityUnits()) : INDEX_NONE;
				Result.SourceLightDescriptions.Add(Actor->GetName(), FString::Printf(TEXT("%s | enabled=%d intensity=%.9g units=%d IES=%d IES-brightness=%d scale=%.9g | source hash=%s"),
					Actor->GetLabel(), Light->IsEnabled(), Light->GetIntensity(), Units, Light->GetUseIes(), Light->GetUseIesBrightness(), Light->GetIesBrightnessScale(), *LexToString(Actor->CalculateElementHash(true))));
			}
			if (Actor->IsA(EDatasmithElementType::PointLight))
			{
				const auto Light = StaticCastSharedPtr<IDatasmithPointLightElement>(Actor);
				Result.EnabledLocalLights += Light->IsEnabled() ? 1 : 0;
				Result.UnitlessLights += Light->GetIntensityUnits() == EDatasmithLightUnits::Unitless ? 1 : 0;
				Result.IESLights += Light->GetUseIes() ? 1 : 0;
			}
			for (int32 Index = 0; Index < Actor->GetChildrenCount() && !Progress.IsCancelled(); ++Index) Visit(Actor->GetChild(Index));
		};
		for (int32 Index = 0; Index < Scene->GetActorsCount() && !Progress.IsCancelled(); ++Index) Visit(Scene->GetActor(Index));
		if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
		if (Result.UnitlessLights)
			Result.Diagnostics.Add(FString::Printf(TEXT("%d source lights declare Unitless. Values are preserved; Revit photometric calibration is unresolved."), Result.UnitlessLights));
		const IConsoleVariable* MegaLights = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
		if (Result.EnabledLocalLights >= Options.Processing.ManyLightThreshold)
			Result.Diagnostics.Add(FString::Printf(TEXT("Many lights: %d enabled local lights (advisory threshold %d). MegaLights configuration: %s. Review Project Settings > Rendering; effective platform/volume/light support is not established."),
				Result.EnabledLocalLights, Options.Processing.ManyLightThreshold,
				MegaLights ? (MegaLights->GetInt() ? TEXT("enabled") : TEXT("disabled; enabling it is recommended")) : TEXT("unknown")));
		for (int32 Index = 0; Index < Scene->GetMaterialsCount(); ++Index)
		{
			if (!Progress.Update(EConVerseImportWorkPhase::Materials, Index, Scene->GetMaterialsCount())) return EConVerseImportWorkResult::Cancelled;
			const auto& Material = Scene->GetMaterial(Index);
			if (!Material.IsValid()) continue;
			auto& Row = Result.Appearances.AddDefaulted_GetRef();
			Row.SourceElement = Material->GetName();
			Row.Name = Material->GetLabel();
			Row.Fingerprint = AppearanceFingerprint(Scene, *Material, Options.FilePath, &Progress);
			if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
			Row.Evidence = FString::Printf(TEXT("Source: %s %s; exporter %s. Appearance variant: %s.\n"), Scene->GetProductName(), Scene->GetProductVersion(), Scene->GetExporterVersion(), *Row.Fingerprint);
			if (Material->IsA(EDatasmithElementType::MaterialInstance))
			{
				const auto Instance = StaticCastSharedPtr<IDatasmithMaterialInstanceElement>(Material);
				for (int32 P = 0; P < Instance->GetPropertiesCount(); ++P)
					if (Instance->GetProperty(P).IsValid()) Row.Evidence += FString(Instance->GetProperty(P)->GetName()) + TEXT("=") + Instance->GetProperty(P)->GetValue() + TEXT("\n");
			}
			for (int32 M = 0; M < Scene->GetMeshesCount(); ++M)
			{
				if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
				if (Scene->GetMesh(M).IsValid())
					for (int32 Slot = 0; Slot < Scene->GetMesh(M)->GetMaterialSlotCount(); ++Slot)
						if (Scene->GetMesh(M)->GetMaterialSlotAt(Slot).IsValid() && Row.SourceElement == Scene->GetMesh(M)->GetMaterialSlotAt(Slot)->GetName()) ++Row.ReferencedSlots;
			}
			UDataTable* Catalog = Options.Processing.AppearanceCatalog.LoadSynchronous();
			if (Catalog && Catalog->GetRowStruct() == FConVerseAppearanceCatalogRow::StaticStruct())
				for (const auto& Pair : Catalog->GetRowMap())
				{
					const auto& Entry = *reinterpret_cast<const FConVerseAppearanceCatalogRow*>(Pair.Value);
					if ((!Row.Fingerprint.IsEmpty() && Entry.ReferenceFingerprint == Row.Fingerprint) || Entry.AppearanceName.Equals(Row.Name, ESearchCase::IgnoreCase)
						|| Entry.Aliases.ContainsByPredicate([&](const FString& Alias) { return Alias.Equals(Row.Name, ESearchCase::IgnoreCase); }))
						Row.CatalogCandidates.Add(Entry.CatalogId);
				}
			FString Target = TEXT("Imported material");
			if (UDataTable* Mappings = Options.Processing.MaterialMappings.LoadSynchronous(); Options.Processing.bApplyApprovedMaterials && Mappings && Mappings->GetRowStruct() == FConVerseMaterialMappingRow::StaticStruct())
				for (const auto& Pair : Mappings->GetRowMap())
				{
					const auto& Mapping = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
					if (Mapping.bApproved && !Row.Fingerprint.IsEmpty() && Mapping.SourceFingerprint == Row.Fingerprint) Target = Mapping.Replacement.ToSoftObjectPath().ToString();
				}
			Result.MaterialDecisions.Add(Row.SourceElement, Row.Name + TEXT(" | appearance=") + Row.Fingerprint + TEXT(" | target=") + Target);
			Result.Diagnostics.Add(FString::Printf(TEXT("Appearance: %s | element=%s | fingerprint=%s"), *Row.Name, *Row.SourceElement, *Row.Fingerprint));
			if (!Progress.Update(EConVerseImportWorkPhase::Materials, Index + 1, Scene->GetMaterialsCount())) return EConVerseImportWorkResult::Cancelled;
		}
		if (UDataTable* Mappings = Options.Processing.MaterialMappings.LoadSynchronous(); Options.Processing.bApplyApprovedMaterials && Mappings && Mappings->GetRowStruct() == FConVerseMaterialMappingRow::StaticStruct())
			for (const auto& Pair : Mappings->GetRowMap())
			{
				const auto& Mapping = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
				if (Mapping.bApproved && !Result.Appearances.ContainsByPredicate([&](const FConVerseAppearanceReviewRow& Row) { return Row.Fingerprint == Mapping.SourceFingerprint; }))
					Result.Diagnostics.Add(TEXT("Approved appearance is absent or changed; imported material is retained: ") + Mapping.CatalogId);
			}

		return Progress.IsCancelled() ? EConVerseImportWorkResult::Cancelled : EConVerseImportWorkResult::Completed;
	}

	// Exporters sometimes reference library textures (for example Autodesk's shared material images)
	// without copying them beside the source. Only the in-memory element is repointed; nothing is copied.
	void ResolveMissingTextures(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result)
	{
		Result.ResolvedTextures.Reset();
		TMap<FString, FString> ByName;
		bool bIndexed = false;
		for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
		{
			const TSharedPtr<IDatasmithTextureElement> Texture = Scene->GetTexture(Index);
			if (!Texture.IsValid()) continue;
			FString Path(Texture->GetFile());
			if (Path.IsEmpty() || Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/"))) continue;
			if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Options.FilePath), Path);
			if (FPaths::FileExists(Path)) continue;
			if (!bIndexed)
			{
				// First folder wins, then sorted path order, so the choice is deterministic.
				for (const FString& Folder : Options.TextureSearchFolders)
				{
					if (Folder.IsEmpty() || !FPaths::DirectoryExists(Folder)) continue;
					TArray<FString> Files;
					IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*"), true, false);
					Files.Sort();
					for (const FString& File : Files)
						if (!ByName.Contains(FPaths::GetCleanFilename(File).ToLower())) ByName.Add(FPaths::GetCleanFilename(File).ToLower(), File);
				}
				bIndexed = true;
			}
			if (const FString* Found = ByName.Find(FPaths::GetCleanFilename(Path).ToLower()))
			{
				Texture->SetFile(**Found);
				Texture->SetFileHash(FMD5Hash::HashFile(**Found));
				Result.ResolvedTextures.Add(FString(Texture->GetName()) + TEXT(": ") + Path + TEXT(" -> ") + *Found);
			}
		}
		for (const FString& Resolved : Result.ResolvedTextures)
			Result.Diagnostics.Add(TEXT("Missing texture found in search folder: ") + Resolved);
	}

	EConVerseImportWorkResult ValidateDependencies(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress)
	{
		Result.MissingTextures.Reset();
		Result.MissingMeshFiles.Reset();
		auto Check = [&](const TCHAR* File, const TCHAR* Element, TArray<FString>& Missing)
		{
			FString Path(File);
			if (Path.IsEmpty() || Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/"))) return;
			if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Options.FilePath), Path);
			if (!FPaths::FileExists(Path)) Missing.Add(FString(Element) + TEXT(": ") + Path);
		};
		const int64 Total = int64(Scene->GetMeshesCount()) + Scene->GetTexturesCount();
		int64 Completed = 0;
		if (!Progress.Update(EConVerseImportWorkPhase::Dependencies, 0, Total)) return EConVerseImportWorkResult::Cancelled;
		for (int32 Index = 0; Index < Scene->GetMeshesCount(); ++Index)
		{
			if (Scene->GetMesh(Index).IsValid()) Check(Scene->GetMesh(Index)->GetFile(), Scene->GetMesh(Index)->GetName(), Result.MissingMeshFiles);
			if (!Progress.Update(EConVerseImportWorkPhase::Dependencies, ++Completed, Total)) return EConVerseImportWorkResult::Cancelled;
		}
		for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
		{
			if (Scene->GetTexture(Index).IsValid()) Check(Scene->GetTexture(Index)->GetFile(), Scene->GetTexture(Index)->GetName(), Result.MissingTextures);
			if (!Progress.Update(EConVerseImportWorkPhase::Dependencies, ++Completed, Total)) return EConVerseImportWorkResult::Cancelled;
		}
		// Missing geometry always fails. A missing texture proceeds only when the user explicitly accepted
		// that exact file, so the decision never silently extends to textures they were not shown.
		TArray<FString> Unaccepted;
		for (const FString& Texture : Result.MissingTextures)
			if (!Options.bAllowMissingTextures && !Options.AcceptedMissingTextures.Contains(Texture)) Unaccepted.Add(Texture);
		OutError.Reset();
		if (!Result.MissingMeshFiles.IsEmpty() || !Unaccepted.IsEmpty())
		{
			TArray<FString> Missing = Result.MissingMeshFiles;
			Missing.Append(Unaccepted);
			OutError = TEXT("Missing referenced source files:\n") + FString::Join(Missing, TEXT("\n"));
			return EConVerseImportWorkResult::Failed;
		}
		for (const FString& Texture : Result.MissingTextures)
			Result.Diagnostics.Add(TEXT("Proceeding without missing texture (accepted by user): ") + Texture);
		return EConVerseImportWorkResult::Completed;
	}

	bool VerifyLights(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const TSet<const AActor*>& ExistingActors, FConVerseOptimizedImportResult& Result)
	{
		if (const auto* ImportData = Cast<UDatasmithSceneImportData>(ImportedScene.AssetImportData))
		{
			if (!ImportData->BaseOptions.bIncludeLight)
			{
				Result.Diagnostics.Add(TEXT("Light import was disabled in Datasmith options; source lights are explicitly excluded."));
				return true;
			}
		}
		TMap<FString, ULightComponent*> ByElement;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (ExistingActors.Contains(*It)) continue;
			TInlineComponentArray<ULightComponent*> Components(*It);
			for (ULightComponent* Light : Components)
			{
				const FString Id = UDatasmithAssetUserData::GetDatasmithUserDataValueForKey(Light, FName(UDatasmithAssetUserData::UniqueIdMetaDataKey));
				if (!Id.IsEmpty()) ByElement.Add(Id, Light);
			}
			if (auto* SceneActor = Cast<ADatasmithSceneActor>(*It))
				for (const auto& Pair : SceneActor->RelatedActors)
					if (Pair.Value.IsValid())
						if (auto* Light = Pair.Value->FindComponentByClass<ULightComponent>()) ByElement.Add(Pair.Key.ToString(), Light);
		}
		bool bPassed = true;
		TFunction<void(const TSharedPtr<IDatasmithActorElement>&)> Visit;
		Visit = [&](const TSharedPtr<IDatasmithActorElement>& Actor)
		{
			if (!Actor.IsValid()) return;
			if (Actor->IsA(EDatasmithElementType::PointLight) || Actor->IsA(EDatasmithElementType::DirectionalLight))
			{
				const auto Light = StaticCastSharedPtr<IDatasmithLightActorElement>(Actor);
				ULightComponent* const* Found = ByElement.Find(Actor->GetName());
				ULightComponent* Component = Found ? *Found : nullptr;
				bool bMatch = Component && FMath::IsFinite(Light->GetIntensity()) && Light->GetIntensity() >= 0.0
					&& FMath::IsNearlyEqual(double(Component->Intensity), Light->GetIntensity(), FMath::Max(0.001, FMath::Abs(Light->GetIntensity()) * 0.00001))
					&& Component->GetVisibleFlag() == Light->IsEnabled();
				if (Actor->IsA(EDatasmithElementType::PointLight))
				{
					const auto Local = StaticCastSharedPtr<IDatasmithPointLightElement>(Actor);
					ELightUnits Expected = ELightUnits::Unitless;
					switch (Local->GetIntensityUnits())
					{
					case EDatasmithLightUnits::Lumens: Expected = ELightUnits::Lumens; break;
					case EDatasmithLightUnits::Candelas: Expected = ELightUnits::Candelas; break;
					case EDatasmithLightUnits::EV: Expected = ELightUnits::EV; break;
					default: break;
					}
					bMatch &= Component && Component->GetLightUnits() == Expected;
				}
				if (Light->GetUseIes())
					bMatch &= Component && Component->IESTexture && Component->bUseIESBrightness == Light->GetUseIesBrightness()
						&& FMath::IsNearlyEqual(double(Component->IESBrightnessScale), Light->GetIesBrightnessScale(), 0.0001);
				auto& Inspection = Result.InspectionRows.AddDefaulted_GetRef();
				Inspection.SourceElement = Actor->GetName(); Inspection.Label = Actor->GetLabel();
				Inspection.Outcome = bMatch ? TEXT("Light: source values preserved") : TEXT("FAIL: light settings differ from source");
				Inspection.ComponentPath = FSoftObjectPath(Component);
				if (Component)
				{
					AActor* Owner = Component->GetOwner();
					auto* Data = Owner->FindComponentByClass<UConVerseSourceMetadata>();
					if (!Data)
					{
						Data = NewObject<UConVerseSourceMetadata>(Owner);
						Owner->AddInstanceComponent(Data); Data->RegisterComponent();
					}
					auto& Record = Data->Records.AddDefaulted_GetRef();
					Record.ComponentName = Component->GetFName(); Record.SourceElement = Actor->GetName(); Record.SourceLabel = Actor->GetLabel();
					Record.SourceDocument = Hash(Result.SourceFilePath);
					for (int32 Tag = 0; Tag < Actor->GetTagsCount(); ++Tag)
					{
						const FString Value = Actor->GetTag(Tag);
						Record.Metadata.Add(FString::Printf(TEXT("SourceTag%d"), Tag), Value);
						if (Value.StartsWith(TEXT("Revit.Element.Id."))) Inspection.SourceIdentity = Value;
					}
					for (int32 Index = 0; Index < Source->GetMetaDataCount(); ++Index)
					{
						const auto& Meta = Source->GetMetaData(Index);
						if (!Meta.IsValid() || Meta->GetAssociatedElement().Get() != Actor.Get()) continue;
						for (int32 Property = 0; Property < Meta->GetPropertiesCount(); ++Property)
							if (Meta->GetProperty(Property).IsValid()) Record.Metadata.Add(Meta->GetProperty(Property)->GetName(), Meta->GetProperty(Property)->GetValue());
					}
				}
				Result.Diagnostics.Add(FString::Printf(TEXT("%s light %s | source intensity=%.9g | source IES=%d | applied=%s"),
					bMatch ? TEXT("PASS") : TEXT("FAIL"), Actor->GetName(), Light->GetIntensity(), Light->GetUseIes(), *GetPathNameSafe(Component)));
				bPassed &= bMatch;
			}
			for (int32 Index = 0; Index < Actor->GetChildrenCount(); ++Index) Visit(Actor->GetChild(Index));
		};
		for (int32 Index = 0; Index < Source->GetActorsCount(); ++Index) Visit(Source->GetActor(Index));
		return bPassed;
	}

	bool ApplyMaterials(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const FString& AttemptFolder, const FConVerseImportProcessingSettings& Settings,
		FConVerseOptimizedImportResult& Result, FString& OutError)
	{
		for (auto& Appearance : Result.Appearances)
			if (const auto* Material = ImportedScene.Materials.Find(FName(*Appearance.SourceElement))) Appearance.ImportedMaterial = Material->ToSoftObjectPath();
		if (!Settings.bApplyApprovedMaterials) return true;
		FString Identity;
		if (!ValidateMappings(Settings, Identity, OutError)) return false;
		UDataTable* Table = Settings.MaterialMappings.Get();
		TMap<UMaterialInterface*, UMaterialInterface*> Replacements;
		for (int32 Index = 0; Index < Source->GetMaterialsCount(); ++Index)
		{
			const auto& Material = Source->GetMaterial(Index);
			if (!Material.IsValid()) continue;
			const FString Fingerprint = AppearanceFingerprint(Source, *Material, Result.SourceFilePath);
			for (const auto& Pair : Table->GetRowMap())
			{
				const auto& Row = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
				if (!Row.bApproved || Fingerprint.IsEmpty() || Row.SourceFingerprint != Fingerprint) continue;
				auto* Imported = ImportedScene.Materials.Find(FName(Material->GetName()));
				UMaterialInterface* Original = Imported ? Imported->LoadSynchronous() : nullptr;
				UMaterialInterface* Replacement = Row.Replacement.Get();
				if (!Original || !Replacement) { OutError = TEXT("An approved material mapping could not resolve its imported source or target."); return false; }
				if (UMaterialInterface** Existing = Replacements.Find(Original); Existing && *Existing != Replacement)
				{ OutError = TEXT("Different approved targets resolve to one shared imported material. Resolve this ambiguity before replacement."); return false; }
				Replacements.Add(Original, Replacement);
				Result.Diagnostics.Add(FString::Printf(TEXT("Approved material: %s | original=%s | replacement=%s | fingerprint=%s"),
					*Row.CatalogId, *Original->GetPathName(), *Replacement->GetPathName(), *Fingerprint));
				*Imported = Replacement;
			}
		}
		for (auto& Pair : ImportedScene.StaticMeshes)
		{
			UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
			if (!Mesh || !Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/"))) continue;
			for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
				if (UMaterialInterface** Target = Replacements.Find(Mesh->GetMaterial(Slot))) Mesh->SetMaterial(Slot, *Target);
		}
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || !Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/"))) continue;
				for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
					if (UMaterialInterface** Target = Replacements.Find(Component->GetMaterial(Slot))) Component->SetMaterial(Slot, *Target);
			}
		}
		return true;
	}

	bool ProcessMeshes(UWorld& World, UDatasmithScene& Scene, const FString& AttemptFolder,
		const FConVerseOptimizedImportOptions& Options, FConVerseOptimizedImportResult& Result, FString& OutError)
	{
		const double ProcessingStarted = FPlatformTime::Seconds();
		ON_SCOPE_EXIT { Result.MeshProcessingSeconds += FPlatformTime::Seconds() - ProcessingStarted; };
		UE_LOG(LogConVerseOptimizedImport, Display, TEXT("Processing imported meshes and waiting for compilation."));
		TSet<UStaticMesh*> Converted;
		TSet<UStaticMesh*> Incompatible;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				if (Component->GetClass() == UInstancedStaticMeshComponent::StaticClass()) Converted.Add(Component->GetStaticMesh());
				for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
				{
					UMaterialInterface* Material = Component->GetMaterial(Slot);
					if (Material && (Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked))
						Incompatible.Add(Component->GetStaticMesh());
				}
			}
		}
		TSet<UStaticMesh*> Disabled;
		for (const auto& Pair : Scene.StaticMeshes)
			if (Options.Processing.DisableNaniteMeshElements.Contains(Pair.Key.ToString())) Disabled.Add(Pair.Value.LoadSynchronous());
		FScopedSlowTask Progress(Scene.StaticMeshes.Num(), NSLOCTEXT("ConVerseHISM", "MeshProcessing", "Processing imported meshes..."));
		TArray<UStaticMesh*> Meshes;
		TSet<UStaticMesh*> Seen;
		for (const auto& Pair : Scene.StaticMeshes)
		{
			Progress.EnterProgressFrame(1, FText::FromString(Pair.Key.ToString()));
			if (Progress.ShouldCancel() || (Options.CancelRequested && Options.CancelRequested()))
			{ OutError = TEXT("Mesh processing cancelled."); return false; }
			UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
			if (!Mesh) { OutError = TEXT("Imported scene contains an unresolved static mesh."); return false; }
			if (Seen.Contains(Mesh)) continue;
			Seen.Add(Mesh);
			if (!Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/")))
			{
				OutError = TEXT("Refusing to change a mesh outside this import's owned folder: ") + Mesh->GetPathName();
				return false;
			}
			bool bEnable = Options.Processing.NanitePolicy == EConVerseNanitePolicy::AllSupportedMeshes
				|| (Options.Processing.NanitePolicy == EConVerseNanitePolicy::ConvertedISMOnly && Converted.Contains(Mesh));
			bool bDisable = Disabled.Contains(Mesh);
			for (const auto& Slot : Mesh->GetStaticMaterials())
				if (Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() != BLEND_Opaque && Slot.MaterialInterface->GetBlendMode() != BLEND_Masked)
					Incompatible.Add(Mesh);
			if (Incompatible.Contains(Mesh) && bEnable)
			{
				bEnable = false;
				bDisable = true;
				++Result.NaniteSkippedMeshes;
				Result.Diagnostics.Add(TEXT("Nanite excluded due to incompatible effective material: ") + Pair.Key.ToString());
			}
			if (bDisable) bEnable = false;
			if ((bEnable || bDisable) && Mesh->GetNaniteSettings().bEnabled != bEnable)
			{
				Mesh->Modify();
				Mesh->GetNaniteSettings().bEnabled = bEnable;
				Mesh->PostEditChange();
			}
			Meshes.Add(Mesh);
		}
		const double BuildStarted = FPlatformTime::Seconds();
		FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
		Result.MeshBuildSeconds += FPlatformTime::Seconds() - BuildStarted;
		if (Progress.ShouldCancel() || (Options.CancelRequested && Options.CancelRequested()))
		{ OutError = TEXT("Mesh processing cancelled after compilation completed."); return false; }
		for (UStaticMesh* Mesh : Meshes)
		{
			if (Mesh->GetNaniteSettings().bEnabled)
			{
				if (!Mesh->HasValidNaniteData()) { OutError = TEXT("Nanite build produced no valid data: ") + Mesh->GetPathName(); return false; }
				++Result.NaniteEnabledMeshes;
			}
		}
		return true;
	}

	// PostLoad may populate expression IDs without changing a parameter name or value.
	static FString CanonicalState(const FString& Input)
	{
		TArray<FString> Lines;
		Input.ParseIntoArrayLines(Lines);
		static const FRegexPattern ExpressionGuid(TEXT(",ExpressionGUID=[0-9A-Fa-f]{32}"));
		for (FString& Line : Lines)
		{
			FString Key, Value;
			if (!Line.Split(TEXT("="), &Key, &Value) || !Key.Contains(TEXT("Parameter"))) continue;
			for (;;)
			{
				FRegexMatcher Match(ExpressionGuid, Value);
				if (!Match.FindNext()) break;
				Value.RemoveAt(Match.GetMatchBeginning(), Match.GetMatchEnding() - Match.GetMatchBeginning());
			}
			Line = Key + TEXT("=") + Value;
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString ObjectState(UObject& Object)
	{
		// Only properties we promise to preserve. Derived/transient render state must not cause false drift.
		static const TCHAR* Properties[] = { TEXT("RelativeLocation"), TEXT("RelativeRotation"), TEXT("RelativeScale3D"),
			TEXT("Mobility"), TEXT("bVisible"), TEXT("bHiddenInGame"), TEXT("CastShadow"), TEXT("bCastDynamicShadow"),
			TEXT("bCastStaticShadow"), TEXT("bReceivesDecals"), TEXT("bAffectDistanceFieldLighting"), TEXT("bCanEverAffectNavigation"),
			TEXT("bGenerateOverlapEvents"), TEXT("MinDrawDistance"), TEXT("LDMaxDrawDistance"), TEXT("InstanceStartCullDistance"),
			TEXT("InstanceEndCullDistance"), TEXT("Intensity"), TEXT("IntensityUnits"), TEXT("LightColor"), TEXT("Temperature"),
			TEXT("bUseTemperature"), TEXT("bUseIESBrightness"), TEXT("IESBrightnessScale"), TEXT("IESTexture"),
			TEXT("bUseInverseSquaredFalloff"), TEXT("AttenuationRadius"), TEXT("InnerConeAngle"), TEXT("OuterConeAngle"),
			TEXT("SourceWidth"), TEXT("SourceHeight"), TEXT("SourceRadius"), TEXT("SourceLength"),
			TEXT("Parent"), TEXT("ScalarParameterValues"), TEXT("VectorParameterValues"), TEXT("TextureParameterValues"),
			TEXT("BasePropertyOverrides"), TEXT("StaticParametersRuntime"), TEXT("StateId"), TEXT("LightmapResolution"),
			TEXT("LightMapCoordinateIndex"), TEXT("NaniteSettings") };
		FString State = Object.GetClass()->GetPathName();
		for (const TCHAR* Name : Properties)
		{
			if (const FProperty* Property = Object.GetClass()->FindPropertyByName(Name))
			{
				FString Value;
				Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(&Object), nullptr, &Object, PPF_None);
				State += FString::Printf(TEXT("\n%s=%s"), Name, *Value);
			}
		}
		if (auto* Actor = Cast<AActor>(&Object))
		{
			State += TEXT("\nLabel=") + Actor->GetActorLabel();
			State += FString::Printf(TEXT("\nHidden=%d|%d"), Actor->IsHidden(), Actor->IsTemporarilyHiddenInEditor());
			TArray<FString> Names;
			TInlineComponentArray<USceneComponent*> Components(Actor);
			for (const auto* Component : Components) Names.Add(Component->GetName());
			TArray<AActor*> Children;
			Actor->GetAttachedActors(Children);
			for (const auto* Child : Children) Names.Add(TEXT("Child=") + Child->GetPathName());
			Names.Sort();
			State += TEXT("\nMembers=") + FString::Join(Names, TEXT("|"));
		}
		if (auto* Component = Cast<USceneComponent>(&Object))
		{
			State += TEXT("\nParent=") + GetPathNameSafe(Component->GetAttachParent());
			State += TEXT("\nWorld=") + Component->GetComponentTransform().ToString();
		}
		if (auto* Component = Cast<UStaticMeshComponent>(&Object))
		{
			State += FString::Printf(TEXT("\nMesh=%s\nCollision=%d|%s"), *GetPathNameSafe(Component->GetStaticMesh()),
				int32(Component->GetCollisionEnabled()), *Component->GetCollisionProfileName().ToString());
			for (int32 Channel = 0; Channel < ECC_MAX; ++Channel)
				State += FString::Printf(TEXT("|%d"), int32(Component->GetCollisionResponseToChannel(ECollisionChannel(Channel))));
			for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
				State += TEXT("\nMaterial=") + GetPathNameSafe(Component->GetMaterial(Slot));
		}
		if (auto* Instances = Cast<UInstancedStaticMeshComponent>(&Object))
		{
			for (int32 Index = 0; Index < Instances->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Instances->GetInstanceTransform(Index, Transform, false);
				State += TEXT("\nInstance=") + Transform.ToString();
			}
		}
		if (auto* Mesh = Cast<UStaticMesh>(&Object))
		{
			State += FString::Printf(TEXT("\nNanite=%d"), Mesh->GetNaniteSettings().bEnabled);
			for (int32 LOD = 0; LOD < Mesh->GetNumSourceModels(); ++LOD)
				if (const auto* Bulk = Mesh->GetSourceModel(LOD).GetMeshDescriptionBulkData()) State += TEXT("\nGeometry=") + Bulk->GetUnversionedIdString();
			for (const auto& Slot : Mesh->GetStaticMaterials()) State += TEXT("\nMaterial=") + GetPathNameSafe(Slot.MaterialInterface);
		}
		return CanonicalState(State);
	}

	void CaptureState(UWorld& World, UDatasmithScene& Scene, const FString& AttemptFolder,
		const TSet<const AActor*>& ExistingActors, FConVerseOptimizedImportResult& Result)
	{
		Result.TrackedObjects.Reset();
		auto Record = [&](UObject* Object)
		{
			if (!Object) return;
			auto& Entry = Result.TrackedObjects.AddDefaulted_GetRef();
			Entry.ObjectPath = FSoftObjectPath(Object);
			Entry.State = ObjectState(*Object);
		};
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (ExistingActors.Contains(*It)) continue;
			Record(*It);
			TInlineComponentArray<USceneComponent*> Components(*It);
			for (USceneComponent* Component : Components) Record(Component);
		}
		for (const auto& Pair : Scene.StaticMeshes)
		{
			UStaticMesh* Mesh = Pair.Value.Get();
			if (Mesh && Mesh->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/"))) Record(Mesh);
		}
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
			if (It->GetOutermost()->GetName().StartsWith(AttemptFolder + TEXT("/"))) Record(*It);
	}

	// A level's first save moves it out of /Temp/. Saving the manifest alongside it lets the engine's
	// path redirection rewrite its soft paths, but not the paths embedded in tracked-state text. No
	// named level can own objects in a /Temp/ world, so those references can only mean the owning world.
	// Like explicit-save rebinding, this requires the session owner to be proven in that world.
	FString RebaseTemporaryWorld(const UConVerseOptimizedImportManifest& Manifest, const FString& State)
	{
		const FString WorldPath = Manifest.WorldPath.ToString();
		if (WorldPath.IsEmpty() || WorldPath.StartsWith(TEXT("/Temp/")) || !State.Contains(TEXT("/Temp/"))) return State;
		const AActor* Owner = Cast<AActor>(Manifest.DatasmithSceneActorPath.ResolveObject());
		if (!Owner || Owner->GetWorld() != Manifest.WorldPath.ResolveObject() || Owner->GetActorGuid() != Manifest.DatasmithSceneActorGuid
			|| !Owner->Tags.Contains(FName(*(TEXT("ConVerseImportSession=") + Manifest.SessionId)))) return State;		static const FRegexPattern TemporaryWorld(TEXT("/Temp/[^\\s|:=]+:PersistentLevel\\."));
		FString Result;
		int32 Copied = 0;
		FRegexMatcher Match(TemporaryWorld, State);
		while (Match.FindNext())
		{
			Result += State.Mid(Copied, Match.GetMatchBeginning() - Copied) + WorldPath + TEXT(":PersistentLevel.");
			Copied = Match.GetMatchEnding();
		}
		return Copied == 0 ? State : Result + State.Mid(Copied);
	}

	bool CheckState(const UConVerseOptimizedImportManifest& Manifest, TArray<FString>& OutDifferences)
	{
		if (Manifest.TrackedStateVersion != 1 || Manifest.TrackedObjects.IsEmpty())
		{
			OutDifferences.Add(TEXT("This older manifest has no complete tracked-state baseline. Explicit rebuild approval is required."));
			return false;
		}
		for (const auto& Record : Manifest.TrackedObjects)
		{
			UObject* Object = Record.ObjectPath.ResolveObject();
			if (!Object && Record.ObjectPath.GetSubPathString().IsEmpty()) Object = Record.ObjectPath.TryLoad();
			const FString Current = Object ? ObjectState(*Object) : FString();
			const FString Committed = CanonicalState(RebaseTemporaryWorld(Manifest, Record.State));
			if (!Object || Current != Committed)
			{
				OutDifferences.Add((Object ? TEXT("Tracked settings or placement changed: ") : TEXT("Tracked object missing: ")) + Record.ObjectPath.ToString());
				TArray<FString> Before, After;
				Committed.ParseIntoArrayLines(Before); Current.ParseIntoArrayLines(After);
				for (int32 Index = 0; Object && Index < FMath::Max(Before.Num(), After.Num()); ++Index)
				{
					const FString A = Before.IsValidIndex(Index) ? Before[Index] : TEXT("<absent>");
					const FString B = After.IsValidIndex(Index) ? After[Index] : TEXT("<absent>");
					if (A != B) OutDifferences.Add(TEXT("  committed: ") + A.Left(4000) + TEXT("\n  current: ") + B.Left(4000));
				}
			}
		}
		return OutDifferences.IsEmpty();
	}
}
