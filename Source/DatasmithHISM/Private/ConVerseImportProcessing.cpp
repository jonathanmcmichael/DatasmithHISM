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
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

	FString IdentitySettingsJson(const FConVerseImportProcessingSettings& Settings)
	{
		// Amendment 15: MaxNaniteMeshes is added to plan identity separately, and only when it binds, so
		// the field's mere existence must not change the hash of every existing import. Removing its whole
		// line leaves every other byte of the pre-existing settings JSON untouched.
		FString Json = SettingsJson(Settings);
		const int32 Key = Json.Find(TEXT("\"maxNaniteMeshes\""), ESearchCase::CaseSensitive);
		if (Key != INDEX_NONE)
		{
			const int32 LineStart = Json.Left(Key).Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1;
			int32 LineEnd = Json.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Key);
			LineEnd = LineEnd == INDEX_NONE ? Json.Len() : LineEnd + 1;
			Json.RemoveAt(LineStart, LineEnd - LineStart);
		}
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

	enum class EAppearanceFingerprintResult : uint8
	{
		Completed,
		Cancelled,
		Missing,
		Failed
	};

	static EAppearanceFingerprintResult TextureFingerprint(
		const FString& File, FConVerseImportProgress* Progress, FString& OutFingerprint)
	{
		OutFingerprint.Reset();
		if (!FPaths::FileExists(File)) return EAppearanceFingerprintResult::Missing;
		if (!Progress)
		{
			const FMD5Hash HashValue = FMD5Hash::HashFile(*File);
			if (!HashValue.IsValid()) return EAppearanceFingerprintResult::Failed;
			OutFingerprint = LexToString(HashValue);
			return EAppearanceFingerprintResult::Completed;
		}
		FString Content;
		int64 Size = 0;
		const EConVerseImportWorkResult Outcome = Progress->HashFile(
			File, Content, Size, EConVerseImportWorkPhase::TextureHash);
		if (Outcome == EConVerseImportWorkResult::Cancelled) return EAppearanceFingerprintResult::Cancelled;
		if (Outcome == EConVerseImportWorkResult::Failed) return EAppearanceFingerprintResult::Failed;
		OutFingerprint = MoveTemp(Content);
		return EAppearanceFingerprintResult::Completed;
	}

	static EAppearanceFingerprintResult AppearanceFingerprint(const TSharedRef<IDatasmithScene>& Scene,
		IDatasmithBaseMaterialElement& Material, const FString& SourceFile,
		FString& OutFingerprint, FConVerseImportProgress* Progress = nullptr)
	{
		OutFingerprint.Reset();
		// Revit exports material instances. Their typed properties include UV transforms and texture references.
		// Display names are deliberately excluded so a rename does not approve a changed appearance.
		if (!Material.IsA(EDatasmithElementType::MaterialInstance))
		{
			FString Evidence = LexToString(Material.CalculateElementHash(true));
			// Conservatively bind non-Revit graphs to every scene texture until graph-specific traversal is available.
			for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
			{
				const auto& Texture = Scene->GetTexture(Index);
				if (!Texture.IsValid()) return EAppearanceFingerprintResult::Failed;
				FString File = Texture->GetFile();
				if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SourceFile), File);
				FString Content;
				const EAppearanceFingerprintResult TextureResult = TextureFingerprint(File, Progress, Content);
				if (TextureResult != EAppearanceFingerprintResult::Completed) return TextureResult;
				Evidence += Content + LexToString(Texture->CalculateElementHash(true));
			}
			OutFingerprint = Hash(Evidence);
			return EAppearanceFingerprintResult::Completed;
		}
		auto& Instance = static_cast<IDatasmithMaterialInstanceElement&>(Material);
		TArray<FString> Properties;
		Properties.Add(FString::Printf(TEXT("Type=%d|Quality=%d|Parent=%s"), int32(Instance.GetMaterialType()),
			int32(Instance.GetQuality()), Instance.GetCustomMaterialPathName()));
		for (int32 Index = 0; Index < Instance.GetPropertiesCount(); ++Index)
		{
			if (Progress && Progress->IsCancelled()) return EAppearanceFingerprintResult::Cancelled;
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
						FString Content;
						const EAppearanceFingerprintResult TextureResult = TextureFingerprint(File, Progress, Content);
						if (TextureResult != EAppearanceFingerprintResult::Completed) return TextureResult;
						Value = Content + TEXT("|") + LexToString(Texture->CalculateElementHash(true));
						bResolved = true;
						break;
					}
				}
				if (!bResolved && !Value.IsEmpty()) return EAppearanceFingerprintResult::Failed;
			}
			Properties.Add(FString::Printf(TEXT("%s|%d|%d:%s"), Property->GetName(), int32(Property->GetPropertyType()), Value.Len(), *Value));
		}
		Properties.Sort();
		OutFingerprint = Hash(FString::Join(Properties, TEXT("\n")));
		return EAppearanceFingerprintResult::Completed;
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
			const EAppearanceFingerprintResult FingerprintResult = AppearanceFingerprint(
				Scene, *Material, Options.FilePath, Row.Fingerprint, &Progress);
			if (FingerprintResult == EAppearanceFingerprintResult::Cancelled)
				return EConVerseImportWorkResult::Cancelled;
			if (FingerprintResult == EAppearanceFingerprintResult::Failed)
				return EConVerseImportWorkResult::Failed;
			// Dependency validation already proved every missing texture was explicitly accepted.
			// Only a genuinely missing referenced file may omit its appearance fingerprint; an
			// unrelated read/hash failure above remains fatal even when another texture is missing.
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

	// A 0-byte dependency file is exactly as unusable as an absent one: FPaths::FileExists is true for
	// an empty file, so a truncated/zero-length export (observed for a Revit IES sidecar) would
	// otherwise sail through preflight and produce content with a silently missing dependency.
	// IFileManager::FileSize returns -1 when the path does not exist at all, so both cases collapse here.
	static bool IsDependencyFileUnusable(const FString& Path)
	{
		return IFileManager::Get().FileSize(*Path) <= 0;
	}

	// Appended to a missing-dependency entry when the file exists but is 0 bytes, so the report
	// distinguishes "never there" from "present but empty" without changing the exact-string
	// acceptance matching in ValidateDependencies: both entry shapes are still compared verbatim.
	static const TCHAR* const EmptyDependencyFileSuffix = TEXT(" (empty file)");

	// Amendment 18: Datasmith creates an IES profile by handing the texture factory the file's extension
	// (FDatasmithTextureImporter::CreateIESTexture), and the factory only builds a light profile from
	// "ies". An IES file with no such extension is present and non-empty yet imports to nothing, which
	// is how a Revit export's extensionless "Generic" profile silently left 67 lights without one.
	static const TCHAR* const UnimportableIesSuffix = TEXT(" (IES file without a .ies extension)");

	// Exporters sometimes reference library textures (for example Autodesk's shared material images)
	// without copying them beside the source. Only the in-memory element is repointed; nothing is copied.
	EConVerseImportWorkResult ResolveMissingTextures(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress)
	{
		Result.ResolvedTextures.Reset();
		Result.ResolvedTextureIdentities.Reset();
		OutError.Reset();
		TArray<FString> ResolvedTextures;
		TArray<FString> ResolvedTextureIdentities;
		TMap<FString, FString> ByName;
		constexpr int32 MaxIndexedTextureFiles = 250000;
		constexpr int32 MaxVisitedTextureDirectories = 100000;
		bool bIndexed = false;
		for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
		{
			if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
			const TSharedPtr<IDatasmithTextureElement> Texture = Scene->GetTexture(Index);
			if (!Texture.IsValid()) continue;
			FString Path(Texture->GetFile());
			if (Path.IsEmpty() || Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/"))) continue;
			if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Options.FilePath), Path);
			// A 0-byte file gets the same search-folder opportunity as a genuinely absent one.
			if (!IsDependencyFileUnusable(Path)) continue;
			if (!bIndexed)
			{
				// First folder wins, then sorted path order, so the choice is deterministic.
				for (const FString& Folder : Options.TextureSearchFolders)
				{
					if (Folder.IsEmpty() || !FPaths::DirectoryExists(Folder)) continue;
					TArray<FString> Files;
					TArray<FString> PendingDirectories{FPaths::ConvertRelativePathToFull(Folder)};
					TSet<FString> VisitedDirectories;
					int64 Visited = 0;
					while (!PendingDirectories.IsEmpty())
					{
						PendingDirectories.Sort([](const FString& Left, const FString& Right) { return Left > Right; });
						FString Directory = PendingDirectories.Pop(EAllowShrinking::No);
						FPaths::NormalizeDirectoryName(Directory);
						Directory = FPaths::ConvertRelativePathToFull(Directory);
						FString DirectoryKey = Directory;
#if PLATFORM_WINDOWS
						DirectoryKey.ToLowerInline();
#endif
						if (VisitedDirectories.Contains(DirectoryKey)) continue;
						if (VisitedDirectories.Num() >= MaxVisitedTextureDirectories)
						{
							OutError = FString::Printf(
								TEXT("Texture search folder '%s' exceeded the safety limit of %d directories."),
								*Folder, MaxVisitedTextureDirectories);
							return EConVerseImportWorkResult::Failed;
						}
						VisitedDirectories.Add(DirectoryKey);
						if (!Progress.Update(EConVerseImportWorkPhase::TextureSearch, Visited++, 0))
							return EConVerseImportWorkResult::Cancelled;
						TArray<FString> Names;
						IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*")), true, false);
						Names.Sort();
						for (const FString& Name : Names)
						{
							if (Files.Num() >= MaxIndexedTextureFiles)
							{
								OutError = FString::Printf(
									TEXT("Texture search folder '%s' exceeded the safety limit of %d files."),
									*Folder, MaxIndexedTextureFiles);
								return EConVerseImportWorkResult::Failed;
							}
							Files.Add(Directory / Name);
							if (!Progress.Update(EConVerseImportWorkPhase::TextureSearch, Visited++, 0))
								return EConVerseImportWorkResult::Cancelled;
						}
						TArray<FString> Directories;
						IFileManager::Get().FindFiles(Directories, *(Directory / TEXT("*")), false, true);
						Directories.Sort();
						for (const FString& Name : Directories) PendingDirectories.Add(Directory / Name);
					}
					Files.Sort();
					for (const FString& File : Files)
						if (!ByName.Contains(FPaths::GetCleanFilename(File).ToLower())) ByName.Add(FPaths::GetCleanFilename(File).ToLower(), File);
				}
				bIndexed = true;
			}
			if (const FString* Found = ByName.Find(FPaths::GetCleanFilename(Path).ToLower()))
			{
				FString ContentHash;
				int64 ContentSize = 0;
				const EConVerseImportWorkResult HashResult = Progress.HashFile(
					*Found, ContentHash, ContentSize, EConVerseImportWorkPhase::TextureHash);
				if (HashResult != EConVerseImportWorkResult::Completed)
				{
					if (HashResult == EConVerseImportWorkResult::Failed)
						OutError = TEXT("A resolved texture-library file could not be fingerprinted: ") + *Found;
					return HashResult;
				}
				FMD5Hash ParsedHash;
				LexFromString(ParsedHash, *ContentHash);
				Texture->SetFile(**Found);
				Texture->SetFileHash(ParsedHash);
				ResolvedTextures.Add(FString(Texture->GetName()) + TEXT(": ") + Path + TEXT(" -> ") + *Found);
				FString CanonicalReferenced = FPaths::ConvertRelativePathToFull(Path);
				FString CanonicalResolved = FPaths::ConvertRelativePathToFull(*Found);
				FPaths::NormalizeFilename(CanonicalReferenced);
				FPaths::NormalizeFilename(CanonicalResolved);
#if PLATFORM_WINDOWS
				// Windows paths are case-insensitive. Enumeration may preserve the caller's alias casing,
				// which must not manufacture a different output identity for the same selected file.
				CanonicalReferenced.ToLowerInline();
				CanonicalResolved.ToLowerInline();
#endif
				auto AddIdentityField = [](FString& Target, const FString& Name, const FString& Value)
				{
					Target += FString::Printf(TEXT("%d:%s=%d:%s;"), Name.Len(), *Name, Value.Len(), *Value);
				};
				FString Identity;
				AddIdentityField(Identity, TEXT("Element"), Texture->GetName());
				AddIdentityField(Identity, TEXT("ReferencedPath"), CanonicalReferenced);
				AddIdentityField(Identity, TEXT("ResolvedPath"), CanonicalResolved);
				AddIdentityField(Identity, TEXT("Size"), FString::Printf(TEXT("%lld"), ContentSize));
				AddIdentityField(Identity, TEXT("Hash"), ContentHash);
				ResolvedTextureIdentities.Add(MoveTemp(Identity));
			}
		}
		Result.ResolvedTextures = MoveTemp(ResolvedTextures);
		Result.ResolvedTextureIdentities = MoveTemp(ResolvedTextureIdentities);
		for (const FString& Resolved : Result.ResolvedTextures)
			Result.Diagnostics.Add(TEXT("Missing texture found in search folder: ") + Resolved);
		return EConVerseImportWorkResult::Completed;
	}

	EConVerseImportWorkResult ValidateDependencies(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress)
	{
		Result.MissingTextures.Reset();
		Result.UnavailableIesProfiles.Reset();
		Result.MissingMeshFiles.Reset();
		auto Check = [&](const TCHAR* File, const TCHAR* Element, TArray<FString>& Missing, bool bIesProfile = false)
		{
			FString Path(File);
			if (Path.IsEmpty() || Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/"))) return;
			if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Options.FilePath), Path);
			const int64 Size = IFileManager::Get().FileSize(*Path);
			if (Size < 0) Missing.Add(FString(Element) + TEXT(": ") + Path);
			// Present but 0 bytes: still unusable, but named distinctly from "absent" in the report.
			else if (Size == 0) Missing.Add(FString(Element) + TEXT(": ") + Path + EmptyDependencyFileSuffix);
			// Present and non-empty, but Datasmith cannot turn it into an IES profile.
			else if (bIesProfile && !FPaths::GetExtension(Path).Equals(TEXT("ies"), ESearchCase::IgnoreCase))
				Missing.Add(FString(Element) + TEXT(": ") + Path + UnimportableIesSuffix);
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
			if (Scene->GetTexture(Index).IsValid())
			{
				// Amendment 18: an IES profile that is absent, empty or unimportable is a warning, not a blocker.
				const bool bIes = Scene->GetTexture(Index)->GetTextureMode() == EDatasmithTextureMode::Ies;
				Check(Scene->GetTexture(Index)->GetFile(), Scene->GetTexture(Index)->GetName(),
					bIes ? Result.UnavailableIesProfiles : Result.MissingTextures, bIes);
			}
			if (!Progress.Update(EConVerseImportWorkPhase::Dependencies, ++Completed, Total)) return EConVerseImportWorkResult::Cancelled;
		}
		for (const FString& Profile : Result.UnavailableIesProfiles)
			Result.Diagnostics.Add(TEXT("WARN IES profile unavailable, the light imports without it: ") + Profile);
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

	// Resolves a light's GetIesTexturePathName() to the absolute source file it names, the same way
	// FDatasmithImporterUtils::FindAsset resolves it at import time: as the name of a scene texture
	// element. Returns an empty string when the name does not match any scene texture, which fails
	// closed - an unresolved reference is never treated as accepted.
	static FString ResolveIesTextureFilePath(const TSharedRef<IDatasmithScene>& Source, const FString& IesTexturePathName, const FString& SourceFilePath)
	{
		if (IesTexturePathName.IsEmpty()) return FString();
		for (int32 Index = 0; Index < Source->GetTexturesCount(); ++Index)
		{
			const auto& Texture = Source->GetTexture(Index);
			if (!Texture.IsValid() || FString(Texture->GetName()) != IesTexturePathName) continue;
			FString Path(Texture->GetFile());
			if (Path.IsEmpty()) return FString();
			if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SourceFilePath), Path);
			return Path;
		}
		return FString();
	}

	bool VerifyLights(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const TSet<const AActor*>& ExistingActors, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, int32& OutFailedLightCount, int32& OutTotalLightCount)
	{
		OutFailedLightCount = 0;
		OutTotalLightCount = 0;
		if (const auto* ImportData = Cast<UDatasmithSceneImportData>(ImportedScene.AssetImportData))
		{
			if (!ImportData->BaseOptions.bIncludeLight)
			{
				Result.Diagnostics.Add(TEXT("Light import was disabled in Datasmith options; source lights are explicitly excluded."));
				return true;
			}
		}

		// Amendments 9 and 18: a light whose IES profile file is absent, empty or not importable does not
		// fail verification over that one field; it is reported as a warning. No acceptance is needed.
		// Entries are exact "<Element>: <Path>[ suffix]" strings as ValidateDependencies records them, so
		// the path is recovered by stripping the known prefix and suffixes rather than re-deriving
		// unavailability here.
		TSet<FString> AcceptedMissingTexturePaths;
		for (const FString& Entry : Result.UnavailableIesProfiles)
		{
			const int32 SeparatorIndex = Entry.Find(TEXT(": "));
			if (SeparatorIndex == INDEX_NONE) continue;
			FString Path = Entry.Mid(SeparatorIndex + 2);
			Path.RemoveFromEnd(EmptyDependencyFileSuffix);
			Path.RemoveFromEnd(UnimportableIesSuffix);
			AcceptedMissingTexturePaths.Add(Path);
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
				++OutTotalLightCount;
				const auto Light = StaticCastSharedPtr<IDatasmithLightActorElement>(Actor);
				ULightComponent* const* Found = ByElement.Find(Actor->GetName());
				ULightComponent* Component = Found ? *Found : nullptr;

				TArray<FString> Fragments;
				bool bIESMissingAccepted = false;
				if (!Component)
				{
					Fragments.Add(TEXT("component missing"));
				}
				else
				{
					const double ExpectedIntensity = Light->GetIntensity();
					const bool bIntensityOk = FMath::IsFinite(ExpectedIntensity) && ExpectedIntensity >= 0.0
						&& FMath::IsNearlyEqual(double(Component->Intensity), ExpectedIntensity, FMath::Max(0.001, FMath::Abs(ExpectedIntensity) * 0.00001));
					if (!bIntensityOk)
						Fragments.Add(FString::Printf(TEXT("intensity expected=%.9g actual=%.9g"), ExpectedIntensity, double(Component->Intensity)));

					const bool bExpectedVisible = Light->IsEnabled();
					if (Component->GetVisibleFlag() != bExpectedVisible)
						Fragments.Add(FString::Printf(TEXT("visibility expected=%d actual=%d"), bExpectedVisible ? 1 : 0, Component->GetVisibleFlag() ? 1 : 0));

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
						if (Component->GetLightUnits() != Expected)
							Fragments.Add(FString::Printf(TEXT("units expected=%d actual=%d"), int32(Expected), int32(Component->GetLightUnits())));
					}

					if (Light->GetUseIes())
					{
						if (!Component->IESTexture)
						{
							const FString IesPath = ResolveIesTextureFilePath(Source, Light->GetIesTexturePathName(), Result.SourceFilePath);
							if (!IesPath.IsEmpty() && AcceptedMissingTexturePaths.Contains(IesPath))
								bIESMissingAccepted = true;
							else
								Fragments.Add(TEXT("IES texture expected=present actual=missing"));
						}
						if (Component->bUseIESBrightness != Light->GetUseIesBrightness())
							Fragments.Add(FString::Printf(TEXT("bUseIESBrightness expected=%d actual=%d"), Light->GetUseIesBrightness() ? 1 : 0, Component->bUseIESBrightness ? 1 : 0));
						if (!FMath::IsNearlyEqual(double(Component->IESBrightnessScale), Light->GetIesBrightnessScale(), 0.0001))
							Fragments.Add(FString::Printf(TEXT("IES brightness scale expected=%.9g actual=%.9g"), Light->GetIesBrightnessScale(), double(Component->IESBrightnessScale)));
					}
				}

				const bool bMatch = Fragments.IsEmpty();
				auto& Inspection = Result.InspectionRows.AddDefaulted_GetRef();
				Inspection.SourceElement = Actor->GetName(); Inspection.Label = Actor->GetLabel();
				if (!bMatch)
					Inspection.Outcome = TEXT("FAIL: light settings differ from source: ") + FString::Join(Fragments, TEXT("; "));
				else if (bIESMissingAccepted)
					Inspection.Outcome = TEXT("Light: source values preserved; IES profile unavailable (warning)");
				else
					Inspection.Outcome = TEXT("Light: source values preserved");
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
				const FString FragmentSuffix = bMatch ? FString() : (TEXT(" | ") + FString::Join(Fragments, TEXT("; ")));
				Result.Diagnostics.Add(FString::Printf(TEXT("%s light %s | source intensity=%.9g | source IES=%d | applied=%s%s"),
					bMatch ? TEXT("PASS") : TEXT("FAIL"), Actor->GetName(), Light->GetIntensity(), Light->GetUseIes(), *GetPathNameSafe(Component),
					*FragmentSuffix));
				if (bIESMissingAccepted)
					Result.Diagnostics.Add(FString::Printf(TEXT("WARN light %s | IES profile unavailable (warning)"), Actor->GetName()));
				if (!bMatch) ++OutFailedLightCount;
				bPassed &= bMatch;
			}
			for (int32 Index = 0; Index < Actor->GetChildrenCount(); ++Index) Visit(Actor->GetChild(Index));
		};
		for (int32 Index = 0; Index < Source->GetActorsCount(); ++Index) Visit(Source->GetActor(Index));
		return bPassed;
	}

	EConVerseImportWorkResult ApplyMaterials(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const FString& AttemptFolder, const FConVerseImportProcessingSettings& Settings,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress)
	{
		for (auto& Appearance : Result.Appearances)
			if (const auto* Material = ImportedScene.Materials.Find(FName(*Appearance.SourceElement))) Appearance.ImportedMaterial = Material->ToSoftObjectPath();
		if (!Settings.bApplyApprovedMaterials) return EConVerseImportWorkResult::Completed;
		FString Identity;
		if (!ValidateMappings(Settings, Identity, OutError)) return EConVerseImportWorkResult::Failed;
		UDataTable* Table = Settings.MaterialMappings.Get();
		TMap<UMaterialInterface*, UMaterialInterface*> Replacements;
		for (int32 Index = 0; Index < Source->GetMaterialsCount(); ++Index)
		{
			if (Progress.IsCancelled()) return EConVerseImportWorkResult::Cancelled;
			const auto& Material = Source->GetMaterial(Index);
			if (!Material.IsValid()) continue;
			FString Fingerprint;
			const EAppearanceFingerprintResult FingerprintResult = AppearanceFingerprint(
				Source, *Material, Result.SourceFilePath, Fingerprint, &Progress);
			if (FingerprintResult == EAppearanceFingerprintResult::Missing)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("Approved material lookup skipped for %s because its explicitly accepted texture dependency is missing; the imported material is retained."),
					Material->GetName()));
				continue;
			}
			if (FingerprintResult != EAppearanceFingerprintResult::Completed)
			{
				if (FingerprintResult != EAppearanceFingerprintResult::Cancelled)
					OutError = TEXT("An imported material appearance texture could not be fingerprinted.");
				return FingerprintResult == EAppearanceFingerprintResult::Cancelled
					? EConVerseImportWorkResult::Cancelled : EConVerseImportWorkResult::Failed;
			}
			for (const auto& Pair : Table->GetRowMap())
			{
				const auto& Row = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
				if (!Row.bApproved || Fingerprint.IsEmpty() || Row.SourceFingerprint != Fingerprint) continue;
				auto* Imported = ImportedScene.Materials.Find(FName(Material->GetName()));
				UMaterialInterface* Original = Imported ? Imported->LoadSynchronous() : nullptr;
				UMaterialInterface* Replacement = Row.Replacement.Get();
				if (!Original || !Replacement) { OutError = TEXT("An approved material mapping could not resolve its imported source or target."); return EConVerseImportWorkResult::Failed; }
				if (UMaterialInterface** Existing = Replacements.Find(Original); Existing && *Existing != Replacement)
				{ OutError = TEXT("Different approved targets resolve to one shared imported material. Resolve this ambiguity before replacement."); return EConVerseImportWorkResult::Failed; }
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
		return Progress.IsCancelled() ? EConVerseImportWorkResult::Cancelled : EConVerseImportWorkResult::Completed;
	}

	bool ProcessMeshes(UWorld& World, UDatasmithScene& Scene, const FString& AttemptFolder,
		const FConVerseOptimizedImportOptions& Options, FConVerseOptimizedImportResult& Result, FString& OutError,
		const TFunction<void(const FString&, bool)>& WriteProgress)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_MeshPolicyAndCompilation);
		const double ProcessingStarted = FPlatformTime::Seconds();
		ON_SCOPE_EXIT { Result.MeshProcessingSeconds += FPlatformTime::Seconds() - ProcessingStarted; };
		UE_LOG(LogConVerseOptimizedImport, Display, TEXT("Processing imported meshes and waiting for compilation."));
		TSet<UStaticMesh*> Converted;
		TSet<UStaticMesh*> Incompatible;
		// Placements per mesh: one per ordinary component, one per instance of an instanced component.
		TMap<UStaticMesh*, int32> Uses;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_MeshPolicyWorldScan);
			UE_LOG(LogConVerseOptimizedImport, Display, TEXT("Import step started: Mesh policy world scan"));
			const double ScanStarted = FPlatformTime::Seconds();
			ON_SCOPE_EXIT { Result.StepSeconds.Emplace(TEXT("Mesh policy world scan"), FPlatformTime::Seconds() - ScanStarted); };
			for (TActorIterator<AActor> It(&World); It; ++It)
			{
				TInlineComponentArray<UStaticMeshComponent*> Components(*It);
				for (UStaticMeshComponent* Component : Components)
				{
					// Amendment 10: EConVerseNanitePolicy::ConvertedISMOnly must cover HISM groups too, since
					// UHierarchicalInstancedStaticMeshComponent is a subclass of UInstancedStaticMeshComponent.
					// An exact-class check here silently excluded every HISM group from Nanite.
					if (Component->IsA<UInstancedStaticMeshComponent>()) Converted.Add(Component->GetStaticMesh());
					if (UStaticMesh* UsedMesh = Component->GetStaticMesh())
					{
						const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
						Uses.FindOrAdd(UsedMesh) += Instanced ? FMath::Max(1, Instanced->GetInstanceCount()) : 1;
					}
					for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
					{
						UMaterialInterface* Material = Component->GetMaterial(Slot);
						if (Material && (Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked))
							Incompatible.Add(Component->GetStaticMesh());
					}
				}
			}
		}
		TSet<UStaticMesh*> Disabled;
		for (const auto& Pair : Scene.StaticMeshes)
			if (Options.Processing.DisableNaniteMeshElements.Contains(Pair.Key.ToString())) Disabled.Add(Pair.Value.LoadSynchronous());
		// Amendment 15: UE 5.8.3's Nanite root-page pool is fixed and exhausting it is fatal, so the policy
		// may enable at most MaxNaniteMeshes meshes. Eligibility mirrors the per-mesh decision below; the
		// most-used meshes keep Nanite, ties break by element name so the choice is reproducible.
		TSet<UStaticMesh*> OverBudget;
		const int32 Budget = Options.Processing.MaxNaniteMeshes;
		const TSet<FString> OnlyElements(Options.NaniteOnlyMeshElements);
		if (Budget > 0 && Options.Processing.NanitePolicy != EConVerseNanitePolicy::PreserveImported)
		{
			struct FRanked { UStaticMesh* Mesh; FString Element; int32 Uses; };
			TArray<FRanked> Eligible;
			TSet<UStaticMesh*> Considered;
			for (const auto& Pair : Scene.StaticMeshes)
			{
				UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
				if (!Mesh || Considered.Contains(Mesh)) continue;
				Considered.Add(Mesh);
				const bool bWouldEnable = Options.Processing.NanitePolicy == EConVerseNanitePolicy::AllSupportedMeshes
					|| Converted.Contains(Mesh);
				const bool bSelected = OnlyElements.IsEmpty() || OnlyElements.Contains(Pair.Key.ToString());
				bool bCompatible = !Disabled.Contains(Mesh) && !Incompatible.Contains(Mesh);
				for (const auto& Slot : Mesh->GetStaticMaterials())
					if (Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() != BLEND_Opaque && Slot.MaterialInterface->GetBlendMode() != BLEND_Masked)
						bCompatible = false;
				if (bWouldEnable && bSelected && bCompatible) Eligible.Add({ Mesh, Pair.Key.ToString(), Uses.FindRef(Mesh) });
			}
			if (Eligible.Num() > Budget)
			{
				Eligible.Sort([](const FRanked& Left, const FRanked& Right)
				{
					return Left.Uses != Right.Uses ? Left.Uses > Right.Uses : Left.Element < Right.Element;
				});
				for (int32 Index = Budget; Index < Eligible.Num(); ++Index) OverBudget.Add(Eligible[Index].Mesh);
				Result.Diagnostics.Add(FString::Printf(
					TEXT("Nanite budget: %d meshes were eligible but the budget is %d, so the %d least-used meshes were left without Nanite."),
					Eligible.Num(), Budget, OverBudget.Num()));
			}
		}
		FScopedSlowTask Progress(Scene.StaticMeshes.Num(), NSLOCTEXT("ConVerseHISM", "MeshProcessing", "Processing imported meshes..."));
		TArray<UStaticMesh*> Meshes;
		TSet<UStaticMesh*> Seen;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_MeshPolicyAssets);
			UE_LOG(LogConVerseOptimizedImport, Display, TEXT("Import step started: Mesh policy assets"));
			const double AssetsStarted = FPlatformTime::Seconds();
			ON_SCOPE_EXIT { Result.StepSeconds.Emplace(TEXT("Mesh policy assets"), FPlatformTime::Seconds() - AssetsStarted); };
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
				// An explicit selection (Apply Nanite step): everything outside it ends up Nanite-off.
				if (!OnlyElements.IsEmpty() && !OnlyElements.Contains(Pair.Key.ToString())) { bEnable = false; bDisable = true; }
				for (const auto& Slot : Mesh->GetStaticMaterials())
					if (Slot.MaterialInterface && (Slot.MaterialInterface->GetBlendMode() != BLEND_Opaque && Slot.MaterialInterface->GetBlendMode() != BLEND_Masked))
						Incompatible.Add(Mesh);
				if (Incompatible.Contains(Mesh) && bEnable)
				{
					bEnable = false;
					bDisable = true;
					++Result.NaniteSkippedMeshes;
					Result.Diagnostics.Add(TEXT("Nanite excluded due to incompatible effective material: ") + Pair.Key.ToString());
				}
				if (bEnable && OverBudget.Contains(Mesh))
				{
					bEnable = false;
					bDisable = true;
					++Result.NaniteBudgetSkippedMeshes;
				}
				if (bDisable) bEnable = false;
				if ((bEnable || bDisable) && Mesh->GetNaniteSettings().bEnabled != bEnable)
				{
					const TCHAR* PolicyName = Options.Processing.NanitePolicy == EConVerseNanitePolicy::AllSupportedMeshes
						? TEXT("AllSupportedMeshes")
						: Options.Processing.NanitePolicy == EConVerseNanitePolicy::ConvertedISMOnly
							? TEXT("ConvertedISMOnly") : TEXT("PreserveImported");
					const FString ChangeDetails = FString::Printf(
						TEXT("source_element=\"%s\" mesh_asset=\"%s\" policy=%s previous_enabled=%s requested_enabled=%s"),
						*Pair.Key.ToString(), *Mesh->GetPathName(), PolicyName,
						Mesh->GetNaniteSettings().bEnabled ? TEXT("true") : TEXT("false"),
						bEnable ? TEXT("true") : TEXT("false"));
					WriteProgress(FString::Printf(TEXT("Nanite setting change started %s"), *ChangeDetails), true);
					double PostEditChangeSeconds = 0.0;
					Mesh->Modify();
					Mesh->GetNaniteSettings().bEnabled = bEnable;
					const double PostEditChangeStartedAt = FPlatformTime::Seconds();
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_NanitePostEditChange);
						Mesh->PostEditChange();
					}
					PostEditChangeSeconds = FPlatformTime::Seconds() - PostEditChangeStartedAt;
					WriteProgress(FString::Printf(
						TEXT("Nanite setting change returned %s post_edit_change_seconds=%.6f"),
						*ChangeDetails, PostEditChangeSeconds), false);
					++Result.NaniteRebuiltMeshes;
				}
				Meshes.Add(Mesh);
			}
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConVerse_MeshCompilationWait);
			UE_LOG(LogConVerseOptimizedImport, Display, TEXT("Import step started: Mesh compilation wait"));
			const double BuildStarted = FPlatformTime::Seconds();
			FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
			Result.MeshBuildSeconds += FPlatformTime::Seconds() - BuildStarted;
		}
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
