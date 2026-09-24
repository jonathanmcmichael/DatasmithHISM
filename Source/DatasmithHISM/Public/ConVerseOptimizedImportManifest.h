#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"

#include "ConVerseOptimizedImportManifest.generated.h"

/** Output component type requested by an optimized import. */
UENUM(BlueprintType)
enum class EConVerseOptimizedManifestComponentType : uint8
{
	ISM UMETA(DisplayName = "ISM"),
	HISM UMETA(DisplayName = "HISM"),
};

/** Whether a source element supplied one usable BIM identity, conflicting identities, or none. */
UENUM(BlueprintType)
enum class EConVerseOptimizedSourceIdentityStatus : uint8
{
	Explicit,
	Ambiguous,
	Missing,
};

/** Lifecycle state of a persisted optimized-import manifest. */
UENUM(BlueprintType)
enum class EConVerseOptimizedImportCommitState : uint8
{
	Active,
	Superseded,
	RolledBack,
};

/** Final state of the verification run recorded by a manifest. */
UENUM(BlueprintType)
enum class EConVerseOptimizedVerificationState : uint8
{
	NotRun,
	Passed,
	Failed,
};

/** An exact key/value pair copied from Datasmith metadata or used as an identity candidate. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedMetadataPair
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString Key;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString Value;
};

/** Immutable normalized options used to produce this manifest. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedImportOptionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString CanonicalSourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString DestinationContentFolder = TEXT("/Game/DatasmithOptimized");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	EConVerseOptimizedManifestComponentType ComponentType = EConVerseOptimizedManifestComponentType::ISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import", meta = (ClampMin = "2"))
	int32 MinimumInstanceCount = 2;
};

/** An actor created by the committed import session. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedCreatedActorRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FGuid ActorGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath ActorPath;
};

/** An asset and package created by the committed import session. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedCreatedAssetRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath AssetPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FName PackageName;
};

/** Expected and imported material identity for one static-mesh slot. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedMaterialSlotRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString DatasmithMaterialElementName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath ImportedMaterialPath;
};

/** Component settings copied from the source candidates into an optimized group. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedComponentSettings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString Layer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	bool bVisible = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	bool bCastShadow = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	TEnumAsByte<EComponentMobility::Type> Mobility = EComponentMobility::Static;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	bool bIsAComponent = false;
};

/** One optimized output component and its contiguous range of source-instance records. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedImportGroupRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString GroupId;

	/** Complete canonical group-key serialization used when GroupId was generated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SerializedGroupKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath OutputComponentPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FGuid OutputOwnerActorGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath OutputOwnerActorPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	EConVerseOptimizedManifestComponentType RequestedComponentType = EConVerseOptimizedManifestComponentType::ISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString DatasmithMeshElementReference;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FSoftObjectPath ImportedStaticMeshPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	TArray<FConVerseOptimizedMaterialSlotRecord> MaterialSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FConVerseOptimizedComponentSettings ComponentSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString ParentHierarchyPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FTransform GroupWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 ExpectedInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 FirstInstanceRecordIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 InstanceRecordCount = 0;
};

/** Persistent source identity and transform data for one optimized output instance. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedImportInstanceRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString GroupId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 InstanceIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SourceElementName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SourceElementLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SourceHierarchyPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 SourceTraversalOrdinal = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FTransform SourceWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FTransform SourceRelativeTransform = FTransform::Identity;

	/** Source actor tags in their original order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	TArray<FString> SourceActorTags;

	/** Every associated Datasmith metadata field, including unrecognized fields. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	TArray<FConVerseOptimizedMetadataPair> Metadata;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	EConVerseOptimizedSourceIdentityStatus SourceIdentityStatus = EConVerseOptimizedSourceIdentityStatus::Missing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SelectedSourceIdentityKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString SelectedSourceIdentityValue;

	/** All distinct recognized candidates when SourceIdentityStatus is Ambiguous. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	TArray<FConVerseOptimizedMetadataPair> ConflictingIdentityCandidates;
};

/** Summary of the last verification performed for a committed manifest. */
USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVerseOptimizedVerificationSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	EConVerseOptimizedVerificationState State = EConVerseOptimizedVerificationState::NotRun;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FDateTime CompletedUtc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 PlannedGroupCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 VerifiedGroupCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 PlannedInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 VerifiedInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	int32 FailedGroupCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString Summary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import")
	FString FullReportArtifactPath;
};

/**
 * Persistent provenance and verification data for an optimized Datasmith import.
 *
 * This object is attached to the imported UDatasmithScene through IInterface_AssetUserData.
 * It stores object locations as soft paths so no imported object is kept alive by the manifest.
 */
UCLASS(BlueprintType)
class DATASMITHHISM_API UConVerseOptimizedImportManifest : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	int32 ManifestSchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FString PluginVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FString EngineVersion;

	/** Stable manifest identifier assigned when the record is constructed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FString ManifestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	FString SourceUri;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	FString CanonicalSourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	FString SourceFileHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	int64 SourceFileSize = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	FDateTime SourceModifiedUtc;

	/**
	 * Aggregate fingerprint of the source's "<BaseName>_Assets" sidecar folder at commit time.
	 *
	 * A Datasmith source is a primary file plus this sidecar folder, and for CAD, Revit, and IFC
	 * the geometry genuinely lives in the sidecar. Recording it lets a reimport detect assets that
	 * changed while the primary file stayed byte-identical.
	 *
	 * Empty means "not recorded" - either a self-contained source or a manifest written before
	 * this field existed. It must never be read as "changed", or every pre-existing session would
	 * warn spuriously on its first reimport.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	FString SidecarHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	int64 SidecarTotalSize = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Source")
	int32 SidecarFileCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString SceneName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString Host;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString Vendor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString ProductName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString ProductVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString ExporterVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Scene")
	FString ExporterSdkVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FConVerseOptimizedImportOptionSnapshot Options;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FString PlanId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Header")
	FString SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FSoftObjectPath ImportedDatasmithSceneAssetPath;

	/** Soft path to this manifest subobject after it has been attached and saved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FSoftObjectPath ManifestObjectPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FString DestinationContentFolder;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FSoftObjectPath WorldPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FSoftObjectPath LevelPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FGuid DatasmithSceneActorGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	FSoftObjectPath DatasmithSceneActorPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	TArray<FConVerseOptimizedCreatedActorRecord> CreatedActors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	TArray<FConVerseOptimizedCreatedAssetRecord> CreatedAssets;

	/** Complete package inventory, including created packages that do not have a standalone asset record. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Output")
	TArray<FName> CreatedPackageNames;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|History")
	FString PreviousManifestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|History")
	FSoftObjectPath PreviousManifestObjectPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|History")
	FString PreviousSessionId;

	/** Defaults to RolledBack so a newly allocated, incomplete manifest is never treated as active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|History")
	EConVerseOptimizedImportCommitState CommitState = EConVerseOptimizedImportCommitState::RolledBack;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Groups")
	TArray<FConVerseOptimizedImportGroupRecord> Groups;

	/** Group ranges index this array; records for each group must remain contiguous. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Instances")
	TArray<FConVerseOptimizedImportInstanceRecord> Instances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Verification")
	FConVerseOptimizedVerificationSummary Verification;

	/** Finds the persistent source record aligned with an output GroupId and instance index. */
	UFUNCTION(BlueprintPure, Category = "DatasmithHISM|Optimized Import")
	bool FindInstanceRecord(
		const FString& GroupId,
		int32 InstanceIndex,
		FConVerseOptimizedImportInstanceRecord& OutRecord) const;
};

/**
 * Lightweight ownership marker attached to each asset created by an optimized import.
 * Consumers can identify the owning manifest directly from the asset without loading or
 * searching every Datasmith scene asset. The marker records ownership only; it does not
 * implement reimport behavior.
 */
UCLASS(BlueprintType)
class DATASMITHHISM_API UConVerseOptimizedAssetMarker : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Ownership")
	FString ManifestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Ownership")
	FString SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Ownership")
	FString SourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Ownership")
	FSoftObjectPath DatasmithSceneAssetPath;

	/** Defaults to RolledBack until the owning import commits successfully. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Optimized Import|Ownership")
	EConVerseOptimizedImportCommitState CommitState = EConVerseOptimizedImportCommitState::RolledBack;
};
