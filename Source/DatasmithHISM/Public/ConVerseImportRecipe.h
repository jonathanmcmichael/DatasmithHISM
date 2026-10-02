#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"
#include "Materials/MaterialInterface.h"
#include "ConVerseImportRecipe.generated.h"

UENUM(BlueprintType)
enum class EConVerseNanitePolicy : uint8
{
	AllSupportedMeshes,
	// Amendment 10: covers converted output built as either ISM or HISM groups; the enum name is
	// unchanged to avoid a breaking rename, but the display name reflects actual coverage.
	ConvertedISMOnly UMETA(DisplayName = "Converted ISM/HISM Groups Only"),
	PreserveImported
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseImportProcessingSettings
{
	GENERATED_BODY()

	/**
	 * Amendment 16: imports default to PreserveImported, so Datasmith's single mesh build is the only
	 * one. Enable Nanite afterwards with the separate Apply Nanite step. AllSupportedMeshes and
	 * ConvertedISMOnly still work here as an inline option, at the cost of rebuilding each changed
	 * mesh inside the import.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes")
	EConVerseNanitePolicy NanitePolicy = EConVerseNanitePolicy::PreserveImported;

	/**
	 * Most meshes this import may enable Nanite on; 0 means unlimited. UE 5.8.3 has a fixed, fatal
	 * Nanite root-page pool, so the default stays below it. When the policy would exceed the budget,
	 * the meshes used most often keep Nanite and the rest are left ordinary (Amendment 15). Ignored
	 * by PreserveImported. Kept second in this struct on purpose: plan identity strips this field's
	 * line from the settings JSON, so it must not be the last field.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes", meta=(ClampMin="0"))
	int32 MaxNaniteMeshes = 16384;

	/** Exact Datasmith mesh element names, not display labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes")
	TArray<FString> KeepOrdinaryMeshElements;

	/** Disables Nanite on the owned mesh asset and therefore all its uses in this import. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes")
	TArray<FString> DisableNaniteMeshElements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lighting", meta=(ClampMin="1"))
	int32 ManyLightThreshold = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials")
	bool bApplyApprovedMaterials = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials", meta=(RequiredAssetDataTags="RowStructure=/Script/DatasmithHISM.ConVerseMaterialMappingRow"))
	TSoftObjectPtr<UDataTable> MaterialMappings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials", meta=(RequiredAssetDataTags="RowStructure=/Script/DatasmithHISM.ConVerseAppearanceCatalogRow"))
	TSoftObjectPtr<UDataTable> AppearanceCatalog;
};

/** Create in the Content Browser to share settings across projects or team members. */
UCLASS(BlueprintType)
class DATASMITHHISM_API UConVerseImportRecipe : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Import")
	FConVerseImportProcessingSettings Processing;

	/**
	 * Searched in order, by file name, for textures the export references but did not copy beside it,
	 * such as Autodesk's shared material library (Textures/1, 2 and 3 are 256, 512 and 1024 px).
	 * A match is imported from its folder; source and library files are never modified.
	 * Kept outside Processing so existing plan identities are unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Textures", meta=(DisplayName="Texture Search Folders"))
	TArray<FDirectoryPath> TextureSearchFolders;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseAppearanceCatalogRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString CatalogId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString LibraryVersion;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString AppearanceName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	TArray<FString> Aliases;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString SourceIdentity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString ReferenceFingerprint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog")
	FString Evidence;
};

/** Approval is bound to an exact source appearance fingerprint, never a fuzzy name match. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseMaterialMappingRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mapping")
	FString CatalogId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mapping")
	FString SourceFingerprint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mapping")
	TSoftObjectPtr<UMaterialInterface> Replacement;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mapping")
	bool bApproved = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mapping")
	int32 Revision = 1;
};

/** Exact observed output state, used for drift detection rather than source equivalence. */
USTRUCT()
struct DATASMITHHISM_API FConVerseTrackedObjectState
{
	GENERATED_BODY()
	UPROPERTY()
	FSoftObjectPath ObjectPath;
	UPROPERTY()
	FString State;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseImportInspectionRow
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FString SourceElement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FString Label;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FString MeshElement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FString Outcome;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FString SourceIdentity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	FSoftObjectPath ComponentPath;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Import")
	int32 InstanceIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseAppearanceReviewRow
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	FString SourceElement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	FString Name;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	FString Fingerprint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	FString Evidence;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	TArray<FString> CatalogCandidates;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	int32 ReferencedSlots = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	int32 AffectedSourceElements = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Appearance")
	FSoftObjectPath ImportedMaterial;
};
