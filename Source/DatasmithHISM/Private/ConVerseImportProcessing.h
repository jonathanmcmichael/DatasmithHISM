#pragma once

#include "ConVerseDatasmithImportService.h"
#include "ConVerseImportProgress.h"

class IDatasmithScene;
class UDatasmithScene;
class UConVerseOptimizedImportManifest;
class UWorld;
class UObject;

namespace ConVerseImportProcessing
{
	bool ApproveAppearance(const FConVerseAppearanceReviewRow& Appearance, const FString& CatalogId, UMaterialInterface* Target,
		const FConVerseImportProcessingSettings& Settings, FString& OutError);
	FString SettingsJson(const FConVerseImportProcessingSettings& Settings);
	bool ValidateMappings(const FConVerseImportProcessingSettings& Settings, FString& OutIdentity, FString& OutError);
	EConVerseImportWorkResult AnalyzeScene(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FConVerseImportProgress& Progress);
	EConVerseImportWorkResult ResolveMissingTextures(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress);
	EConVerseImportWorkResult ValidateDependencies(const TSharedRef<IDatasmithScene>& Scene, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress);
	bool VerifyLights(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const TSet<const AActor*>& ExistingActors, const FConVerseOptimizedImportOptions& Options,
		FConVerseOptimizedImportResult& Result, int32& OutFailedLightCount, int32& OutTotalLightCount);
	bool ProcessMeshes(UWorld& World, UDatasmithScene& Scene, const FString& AttemptFolder,
		const FConVerseOptimizedImportOptions& Options, FConVerseOptimizedImportResult& Result, FString& OutError);
	EConVerseImportWorkResult ApplyMaterials(UWorld& World, UDatasmithScene& ImportedScene, const TSharedRef<IDatasmithScene>& Source,
		const FString& AttemptFolder, const FConVerseImportProcessingSettings& Settings,
		FConVerseOptimizedImportResult& Result, FString& OutError, FConVerseImportProgress& Progress);
	FString ObjectState(UObject& Object);
	void CaptureState(UWorld& World, UDatasmithScene& Scene, const FString& AttemptFolder,
		const TSet<const AActor*>& ExistingActors, FConVerseOptimizedImportResult& Result);
	FString RebaseTemporaryWorld(const UConVerseOptimizedImportManifest& Manifest, const FString& State);
	bool CheckState(const UConVerseOptimizedImportManifest& Manifest, TArray<FString>& OutDifferences);
}
