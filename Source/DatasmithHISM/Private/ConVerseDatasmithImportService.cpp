#include "ConVerseDatasmithImportService.h"

#include "ConVerseHISMUtils.h"
#include "ConVerseImportProcessing.h"
#include "ConVerseSourceMetadata.h"
#include "DatasmithAssetUserData.h"
#include "JsonObjectConverter.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "Misc/ScopedEvent.h"
#include "ConVerseOptimizedImportManifest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AutomatedAssetImportData.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DatasmithImportFactory.h"
#include "DatasmithImportOptions.h"
#include "DatasmithScene.h"
#include "DatasmithSceneActor.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithTranslator.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ExternalSource.h"
#include "ExternalSourceModule.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "IDatasmithSceneElements.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "SourceUri.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY(LogConVerseOptimizedImport);

namespace ConVerseDatasmithImport
{
	using UE::DatasmithImporter::FExternalSource;
	using UE::DatasmithImporter::FSourceUri;

	constexpr int32 ContractVersion = 2;
	constexpr double TranslationToleranceCm = 0.1;
	constexpr double RotationToleranceDegrees = 0.1;
	constexpr double ScaleTolerance = 0.001;
	static const TCHAR* ManagedTag = TEXT("ConVerseOptimizedImport");
	static const TCHAR* GroupTagPrefix = TEXT("ConVerseOptimizedGroup=");
	static const TCHAR* SessionTagPrefix = TEXT("ConVerseImportSession=");
	static const TCHAR* PlanTagPrefix = TEXT("ConVerseOptimizedPlan=");

	struct FMetadataValue
	{
		FString Name;
		FString Value;
		int32 Type = 0;
	};

	enum class ESourceIdentityStatus : uint8
	{
		Explicit,
		Ambiguous,
		Missing
	};

	struct FSourceIdentityValue
	{
		FString Key;
		FString Value;
	};

	struct FMaterialOverrideValue
	{
		FString MaterialElementName;
		int32 SlotId = INDEX_NONE;
	};

	struct FEffectiveMaterialValue
	{
		int32 DatasmithSlotId = INDEX_NONE;
		FString MaterialElementName;
	};

	struct FCandidateValue
	{
		FString SourceElementName;
		FString SourceLabel;
		FString HierarchyPath;
		FString ParentHierarchyPath;
		int32 SourceOrdinal = INDEX_NONE;
		FTransform WorldTransform = FTransform::Identity;
		FTransform RelativeTransform = FTransform::Identity;
		FString MeshElementName;
		TArray<FString> Tags;
		TArray<FMetadataValue> Metadata;
		ESourceIdentityStatus IdentityStatus = ESourceIdentityStatus::Missing;
		TArray<FSourceIdentityValue> IdentityCandidates;
	};

	struct FGroupValue
	{
		FString SerializedKey;
		FString GroupId;
		FString ElementName;
		FString Label;
		FString ParentHierarchyPath;
		FString ParentElementName;
		FString MeshElementName;
		FString Layer;
		FTransform GroupWorldTransform = FTransform::Identity;
		TArray<FMaterialOverrideValue> MaterialOverrides;
		TArray<FEffectiveMaterialValue> EffectiveMaterials;
		TArray<FCandidateValue> Candidates;
		bool bIsComponent = false;
		bool bVisible = true;
		bool bCastShadow = true;
		EDatasmithActorMobilityType Mobility = EDatasmithActorMobilityType::Static;
	};

	struct FPlan
	{
		int32 SchemaVersion = ContractVersion;
		FString PlanId;
		FString SourceUri;
		FString SourceFilePath;
		FString SourceFileHash;
		int64 SourceFileSize = 0;
		FDateTime SourceModifiedUtc;
		FString SceneName;
		FString Host;
		FString Vendor;
		FString ProductName;
		FString ProductVersion;
		FString ExporterVersion;
		FString ExporterSdkVersion;
		FConVerseOptimizedImportOptions Options;
		TArray<FString> TextureResolutions;
		int32 TotalMeshActors = 0;
		int32 EligibleLeafActors = 0;
		TMap<EConVerseOptimizedSkipReason, int32> SkipCounts;
		TArray<FGroupValue> Groups;
		TArray<FGroupValue> AllMeshActors;
	};

	struct FGroupBucketKey
	{
		const IDatasmithActorElement* ParentIdentity = nullptr;
		FString SerializedKey;

		bool operator==(const FGroupBucketKey& Other) const
		{
			return ParentIdentity == Other.ParentIdentity && SerializedKey == Other.SerializedKey;
		}

		friend uint32 GetTypeHash(const FGroupBucketKey& Key)
		{
			return HashCombine(PointerHash(Key.ParentIdentity), GetTypeHash(Key.SerializedKey));
		}
	};

	struct FGroupBucket
	{
		FGroupValue Group;
	};

	struct FResolvedGroup
	{
		const FGroupValue* Group = nullptr;
		TSharedPtr<IDatasmithActorElement> Parent;
		TArray<TSharedPtr<IDatasmithMeshActorElement>> Candidates;
	};

	struct FComponentSnapshot
	{
		FString GroupId;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> SourceHISM;
		TWeakObjectPtr<UStaticMesh> Mesh;
		TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
		TWeakObjectPtr<USceneComponent> AttachParent;
		FName AttachSocket;
		TWeakObjectPtr<ULevel> OwnerLevel;
		FTransform RelativeTransform = FTransform::Identity;
		EComponentMobility::Type Mobility = EComponentMobility::Static;
		bool bVisible = true;
		bool bHiddenInGame = false;
		bool bCastShadow = true;
		ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
		FName CollisionProfile;
		bool bGenerateOverlapEvents = false;
		int32 StartCullDistance = 0;
		int32 EndCullDistance = 0;
		TArray<FName> Tags;
		TArray<FTransform> LocalInstances;
	};

	struct FMutationInventory
	{
		TSet<const AActor*> ExistingActors;
		bool bWorldPackageWasDirty = false;
		FString AttemptFolder;
	};

	static FString InstanceTypeName(EConVerseOptimizedInstanceType Type)
	{
		return Type == EConVerseOptimizedInstanceType::HISM ? TEXT("HISM") : TEXT("ISM");
	}

	static FString StitchingTechniqueName(EConVerseStitchingTechnique Technique)
	{
		switch (Technique)
		{
		case EConVerseStitchingTechnique::Sewing:  return TEXT("Sewing");
		case EConVerseStitchingTechnique::Healing: return TEXT("Healing");
		default:                                   return TEXT("None");
		}
	}

	static FString StatusName(EConVerseOptimizedImportStatus Status)
	{
		switch (Status)
		{
		case EConVerseOptimizedImportStatus::InvalidOptions: return TEXT("InvalidOptions");
		case EConVerseOptimizedImportStatus::SourceLoadFailed: return TEXT("SourceLoadFailed");
		case EConVerseOptimizedImportStatus::SourceChangedAfterAnalysis: return TEXT("SourceChangedAfterAnalysis");
		case EConVerseOptimizedImportStatus::AnalysisSucceeded: return TEXT("AnalysisSucceeded");
		case EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups: return TEXT("AnalysisNoEligibleGroups");
		case EConVerseOptimizedImportStatus::AlreadyCurrent: return TEXT("AlreadyCurrent");
		case EConVerseOptimizedImportStatus::Verified: return TEXT("Verified");
		case EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack: return TEXT("ImportedWithFailuresRolledBack");
		case EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision: return TEXT("AwaitingFailedVerificationDecision");
		case EConVerseOptimizedImportStatus::AcceptedWithFailedVerification: return TEXT("AcceptedWithFailedVerification");
		case EConVerseOptimizedImportStatus::CancelledRolledBack: return TEXT("CancelledRolledBack");
		case EConVerseOptimizedImportStatus::FailedRolledBack: return TEXT("FailedRolledBack");
		case EConVerseOptimizedImportStatus::OptimizedReimportBlocked: return TEXT("OptimizedReimportBlocked");
		case EConVerseOptimizedImportStatus::RollbackFailed: return TEXT("RollbackFailed");
		case EConVerseOptimizedImportStatus::ManualEditsDetected: return TEXT("ManualEditsDetected");
		default: return TEXT("Unknown");
		}
	}

	static FString SkipReasonName(EConVerseOptimizedSkipReason Reason)
	{
		switch (Reason)
		{
		case EConVerseOptimizedSkipReason::HasChildren: return TEXT("HasChildren");
		case EConVerseOptimizedSkipReason::MissingMeshReference: return TEXT("MissingMeshReference");
		case EConVerseOptimizedSkipReason::UnresolvedMeshReference: return TEXT("UnresolvedMeshReference");
		case EConVerseOptimizedSkipReason::UnresolvedMaterialBinding: return TEXT("UnresolvedMaterialBinding");
		case EConVerseOptimizedSkipReason::NonFiniteTransform: return TEXT("NonFiniteTransform");
		case EConVerseOptimizedSkipReason::UnsupportedNegativeScale: return TEXT("UnsupportedNegativeScale");
		case EConVerseOptimizedSkipReason::BelowMinimumInstanceCount: return TEXT("BelowMinimumInstanceCount");
		default: return TEXT("Unknown");
		}
	}

	static FString HashUtf8(const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	static bool CancellationRequested(FScopedSlowTask* Progress, const TFunction<bool()>* Cancel)
	{
		return (Progress && Progress->ShouldCancel()) || (Cancel && *Cancel && (*Cancel)());
	}

	static bool HashFile(const FString& FilePath, FString& OutHash, int64& OutSize, FString& OutError,
		FScopedSlowTask* Progress = nullptr, const TFunction<bool()>* Cancel = nullptr,
		FConVerseImportProgress* Work = nullptr, EConVerseImportWorkPhase Phase = EConVerseImportWorkPhase::SourceHash)
	{
		if (Work)
		{
			const auto Outcome = Work->HashFile(FilePath, OutHash, OutSize, Phase);
			if (Outcome != EConVerseImportWorkResult::Completed)
				OutError = Outcome == EConVerseImportWorkResult::Cancelled ? TEXT("Fingerprinting cancelled.") : TEXT("The source file could not be read completely for fingerprinting.");
			return Outcome == EConVerseImportWorkResult::Completed;
		}
		OutSize = IFileManager::Get().FileSize(*FilePath);
		if (OutSize < 0)
		{
			OutError = TEXT("The source file size could not be read.");
			return false;
		}
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
		if (!Reader) { OutError = TEXT("The source file could not be opened for fingerprinting."); return false; }
		FScopedSlowTask Bytes(FMath::Max(1.0f, float(OutSize)), FText::FromString(TEXT("Reading ") + FPaths::GetCleanFilename(FilePath)), Progress != nullptr);
		FMD5 MD5;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		int64 Remaining = OutSize;
		while (Remaining > 0)
		{
			if (CancellationRequested(Progress, Cancel)) { OutError = TEXT("Fingerprinting cancelled."); return false; }
			const int32 Count = int32(FMath::Min<int64>(Remaining, Buffer.Num()));
			Reader->Serialize(Buffer.GetData(), Count);
			if (Reader->IsError()) { OutError = TEXT("Could not read complete source bytes."); return false; }
			MD5.Update(Buffer.GetData(), Count);
			Remaining -= Count;
			Bytes.EnterProgressFrame(float(Count));
		}
		uint8 Digest[16];
		MD5.Final(Digest);
		OutHash = BytesToHex(Digest, 16).ToLower();
		return true;
	}

	static void AppendField(FString& Target, const FString& Name, const FString& Value)
	{
		Target += FString::Printf(TEXT("%d:%s=%d:%s;"), Name.Len(), *Name, Value.Len(), *Value);
	}

	/**
	 * Result of fingerprinting a Datasmith sidecar asset folder.
	 *
	 * A Datasmith source is a primary file plus a "<BaseName>_Assets" folder holding the actual
	 * mesh and texture payloads. An absent sidecar is legitimate - many .udatasmith exports are
	 * self-contained - so absence is reported distinctly from a read failure. Conflating the two
	 * would let a permissions error masquerade as a self-contained source and suppress the very
	 * warning this exists to raise.
	 */
	struct FSidecarFingerprint
	{
		FString Hash;
		int64 TotalSize = 0;
		int32 FileCount = 0;
		bool bExists = false;
	};

	/** Returns the conventional "<BaseName>_Assets" sidecar folder path for a Datasmith source. */
	static FString GetSidecarDirectory(const FString& SourceFilePath)
	{
		return FPaths::Combine(
			FPaths::GetPath(SourceFilePath),
			FPaths::GetBaseFilename(SourceFilePath) + TEXT("_Assets"));
	}

	/**
	 * Folds an entire sidecar folder into one aggregate fingerprint.
	 *
	 * Relative paths are sorted before folding because directory enumeration order is not
	 * guaranteed stable across platforms or even across runs; an unsorted fold would produce a
	 * different hash for identical content and warn on every single import.
	 *
	 * Each entry contributes its relative path, size, and content hash, so a rename, a resize, and
	 * an edit are all detected, as is a file being added or removed.
	 */
	static bool HashDirectory(const FString& DirectoryPath, FSidecarFingerprint& OutFingerprint, FString& OutError,
		FScopedSlowTask* Progress = nullptr, const TFunction<bool()>* Cancel = nullptr, FConVerseImportProgress* Work = nullptr)
	{
		OutFingerprint = FSidecarFingerprint();

		if (!IFileManager::Get().DirectoryExists(*DirectoryPath))
		{
			// Self-contained source. Not an error.
			return true;
		}
		OutFingerprint.bExists = true;

		TArray<FString> RelativePaths;
		IFileManager::Get().IterateDirectoryRecursively(
			*DirectoryPath,
			[&DirectoryPath, &RelativePaths, Progress, Cancel, Work](const TCHAR* VisitedPath, bool bIsDirectory) -> bool
			{
				if (Work ? Work->IsCancelled() : CancellationRequested(Progress, Cancel)) return false;
				if (!bIsDirectory)
				{
					FString Relative = VisitedPath;
					FPaths::MakePathRelativeTo(Relative, *(DirectoryPath / TEXT("")));
					RelativePaths.Add(MoveTemp(Relative));
				}
				return true;
			});

		if (Work ? Work->IsCancelled() : CancellationRequested(Progress, Cancel)) { OutError = TEXT("Supporting-file check cancelled."); return false; }

		// Stable ordering is what makes the aggregate reproducible.
		RelativePaths.Sort();

		FString Aggregate;
		FScopedSlowTask Files(float(FMath::Max(1, RelativePaths.Num())), FText::FromString(TEXT("Checking supporting files")), Progress != nullptr);
		for (const FString& Relative : RelativePaths)
		{
			if (Work ? Work->IsCancelled() : CancellationRequested(Progress, Cancel)) { OutError = TEXT("Supporting-file check cancelled."); return false; }
			Files.EnterProgressFrame(1.0f, FText::FromString(Relative));
			const FString AbsolutePath = FPaths::Combine(DirectoryPath, Relative);

			FString FileHash;
			int64 FileSize = 0;
			FString FileError;
			if (!HashFile(AbsolutePath, FileHash, FileSize, FileError, Progress, Cancel, Work, EConVerseImportWorkPhase::SidecarHash))
			{
				// Surface the offending file:
				// silently skipping it would leave a changed asset invisible to the comparison.
				OutError = FString::Printf(
					TEXT("The sidecar asset '%s' could not be fingerprinted: %s"),
					*Relative,
					*FileError);
				return false;
			}

			AppendField(Aggregate, TEXT("Path"), Relative);
			AppendField(Aggregate, TEXT("Size"), FString::Printf(TEXT("%lld"), FileSize));
			AppendField(Aggregate, TEXT("Hash"), FileHash);

			OutFingerprint.TotalSize += FileSize;
			++OutFingerprint.FileCount;
		}

		OutFingerprint.Hash = HashUtf8(Aggregate);
		return true;
	}

	static FString SerializeTransform(const FTransform& Transform)
	{
		const FVector Translation = Transform.GetTranslation();
		const FQuat Rotation = Transform.GetRotation();
		const FVector Scale = Transform.GetScale3D();
		return FString::Printf(
			TEXT("T(%.17g,%.17g,%.17g)R(%.17g,%.17g,%.17g,%.17g)S(%.17g,%.17g,%.17g)"),
			Translation.X, Translation.Y, Translation.Z,
			Rotation.X, Rotation.Y, Rotation.Z, Rotation.W,
			Scale.X, Scale.Y, Scale.Z);
	}

	static FString MakePathSegment(const FString& Name, int32 SiblingOrdinal)
	{
		return FString::Printf(TEXT("%d:%s[%d]"), Name.Len(), *Name, SiblingOrdinal);
	}

	static bool IsFiniteTransform(const FTransform& Transform)
	{
		const FVector Translation = Transform.GetTranslation();
		const FQuat Rotation = Transform.GetRotation();
		const FVector Scale = Transform.GetScale3D();
		return FMath::IsFinite(Translation.X) && FMath::IsFinite(Translation.Y) && FMath::IsFinite(Translation.Z)
			&& FMath::IsFinite(Rotation.X) && FMath::IsFinite(Rotation.Y) && FMath::IsFinite(Rotation.Z) && FMath::IsFinite(Rotation.W)
			&& FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y) && FMath::IsFinite(Scale.Z);
	}

	static bool TransformsMatch(const FTransform& Expected, const FTransform& Actual)
	{
		if (!IsFiniteTransform(Expected) || !IsFiniteTransform(Actual))
		{
			return false;
		}
		const bool bTranslation = Expected.GetTranslation().Equals(Actual.GetTranslation(), TranslationToleranceCm);
		const bool bScale = Expected.GetScale3D().Equals(Actual.GetScale3D(), ScaleTolerance);
		const bool bDeterminantSign = FMath::Sign(Expected.GetDeterminant()) == FMath::Sign(Actual.GetDeterminant());
		const double AngularDistance = Expected.GetRotation().GetNormalized().AngularDistance(Actual.GetRotation().GetNormalized());
		return bTranslation && bScale && bDeterminantSign
			&& AngularDistance <= FMath::DegreesToRadians(RotationToleranceDegrees);
	}

	static FTransform GetWorldTransform(const IDatasmithActorElement& Actor)
	{
		return FTransform(Actor.GetRotation(), Actor.GetTranslation(), Actor.GetScale());
	}

	static void IncrementSkip(FPlan& Plan, EConVerseOptimizedSkipReason Reason, int32 Amount = 1)
	{
		Plan.SkipCounts.FindOrAdd(Reason) += Amount;
	}

	static bool IsRecognizedIdentityKey(const FString& Key)
	{
		static const TCHAR* Keys[] = {
			TEXT("UniqueId"), TEXT("ElementId"), TEXT("Element ID"), TEXT("Revit.Element.Id"),
			TEXT("Revit Element ID"), TEXT("IfcGUID"), TEXT("GlobalId"), TEXT("GUID")
		};
		for (const TCHAR* Candidate : Keys)
		{
			if (Key.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static void CaptureActorSourceData(
		const IDatasmithScene& Scene,
		const TSharedPtr<IDatasmithMeshActorElement>& Actor,
		FCandidateValue& Candidate)
	{
		for (int32 TagIndex = 0; TagIndex < Actor->GetTagsCount(); ++TagIndex)
		{
			Candidate.Tags.Add(Actor->GetTag(TagIndex));
		}

		TSharedPtr<IDatasmithElement> ActorElement = Actor;
		const TSharedPtr<IDatasmithMetaDataElement>& Metadata = Scene.GetMetaData(ActorElement);
		TSet<FString> DistinctIdentityValues;
		if (Metadata.IsValid())
		{
			for (int32 PropertyIndex = 0; PropertyIndex < Metadata->GetPropertiesCount(); ++PropertyIndex)
			{
				const TSharedPtr<IDatasmithKeyValueProperty>& Property = Metadata->GetProperty(PropertyIndex);
				if (!Property.IsValid())
				{
					continue;
				}
				FMetadataValue& Value = Candidate.Metadata.AddDefaulted_GetRef();
				Value.Name = Property->GetName();
				Value.Value = Property->GetValue();
				Value.Type = static_cast<int32>(Property->GetPropertyType());
				if (IsRecognizedIdentityKey(Value.Name) && !Value.Value.IsEmpty())
				{
					FSourceIdentityValue& Identity = Candidate.IdentityCandidates.AddDefaulted_GetRef();
					Identity.Key = Value.Name;
					Identity.Value = Value.Value;
					DistinctIdentityValues.Add(Value.Value);
				}
			}
		}
		Candidate.IdentityStatus = DistinctIdentityValues.Num() == 0
			? ESourceIdentityStatus::Missing
			: (DistinctIdentityValues.Num() == 1 ? ESourceIdentityStatus::Explicit : ESourceIdentityStatus::Ambiguous);
	}

	static FString BuildOverrideSignature(
		const IDatasmithMeshActorElement& Actor,
		TArray<FMaterialOverrideValue>* OutOverrides = nullptr)
	{
		FString Signature;
		for (int32 Index = 0; Index < Actor.GetMaterialOverridesCount(); ++Index)
		{
			const TSharedPtr<const IDatasmithMaterialIDElement> Override = Actor.GetMaterialOverride(Index);
			if (!Override.IsValid())
			{
				AppendField(Signature, TEXT("InvalidOverride"), FString::FromInt(Index));
				continue;
			}
			AppendField(Signature, FString::FromInt(Override->GetId()), Override->GetName());
			if (OutOverrides != nullptr)
			{
				FMaterialOverrideValue& Value = OutOverrides->AddDefaulted_GetRef();
				Value.MaterialElementName = Override->GetName();
				Value.SlotId = Override->GetId();
			}
		}
		return Signature;
	}

	static FString BuildOverrideSignature(const TArray<FMaterialOverrideValue>& Overrides)
	{
		FString Signature;
		for (const FMaterialOverrideValue& Override : Overrides)
		{
			AppendField(Signature, FString::FromInt(Override.SlotId), Override.MaterialElementName);
		}
		return Signature;
	}

	static bool ResolveEffectiveMaterials(
		const IDatasmithMeshElement& Mesh,
		const IDatasmithMeshActorElement& Actor,
		const TSet<FString>& SceneMaterialNames,
		TArray<FEffectiveMaterialValue>& OutMaterials)
	{
		OutMaterials.Reset();
		for (int32 SlotIndex = 0; SlotIndex < Mesh.GetMaterialSlotCount(); ++SlotIndex)
		{
			const TSharedPtr<const IDatasmithMaterialIDElement> Slot = Mesh.GetMaterialSlotAt(SlotIndex);
			if (!Slot.IsValid())
			{
				return false;
			}
			FEffectiveMaterialValue& Value = OutMaterials.AddDefaulted_GetRef();
			Value.DatasmithSlotId = Slot->GetId();
			Value.MaterialElementName = Slot->GetName();
			if (Value.MaterialElementName.IsEmpty() || !SceneMaterialNames.Contains(Value.MaterialElementName))
			{
				return false;
			}
		}

		for (int32 OverrideIndex = 0; OverrideIndex < Actor.GetMaterialOverridesCount(); ++OverrideIndex)
		{
			const TSharedPtr<const IDatasmithMaterialIDElement> Override = Actor.GetMaterialOverride(OverrideIndex);
			if (!Override.IsValid() || FString(Override->GetName()).IsEmpty()
				|| !SceneMaterialNames.Contains(Override->GetName()))
			{
				return false;
			}

			if (Override->GetId() < 0)
			{
				for (FEffectiveMaterialValue& Value : OutMaterials)
				{
					Value.MaterialElementName = Override->GetName();
				}
				continue;
			}

			FEffectiveMaterialValue* MatchingSlot = OutMaterials.FindByPredicate(
				[Override](const FEffectiveMaterialValue& Value) { return Value.DatasmithSlotId == Override->GetId(); });
			if (MatchingSlot == nullptr && OutMaterials.IsValidIndex(Override->GetId()))
			{
				MatchingSlot = &OutMaterials[Override->GetId()];
			}
			if (MatchingSlot == nullptr)
			{
				return false;
			}
			MatchingSlot->MaterialElementName = Override->GetName();
		}
		return true;
	}

	static FString BuildSerializedGroupKey(
		const FString& ParentPath,
		const IDatasmithMeshActorElement& Actor)
	{
		FString Key;
		AppendField(Key, TEXT("Parent"), ParentPath);
		AppendField(Key, TEXT("Mesh"), Actor.GetStaticMeshPathName());
		AppendField(Key, TEXT("Overrides"), BuildOverrideSignature(Actor));
		AppendField(Key, TEXT("Layer"), Actor.GetLayer());
		AppendField(Key, TEXT("Component"), Actor.IsAComponent() ? TEXT("1") : TEXT("0"));
		AppendField(Key, TEXT("Visible"), Actor.GetVisibility() ? TEXT("1") : TEXT("0"));
		AppendField(Key, TEXT("CastShadow"), Actor.GetCastShadow() ? TEXT("1") : TEXT("0"));
		AppendField(Key, TEXT("Mobility"), FString::FromInt(static_cast<int32>(Actor.GetMobility())));
		return Key;
	}

	static void GatherPlanCandidates(
		const IDatasmithScene& Scene,
		const TSharedPtr<IDatasmithActorElement>& Actor,
		const FString& ParentPath,
		int32 SiblingOrdinal,
		int32& TraversalOrdinal,
		const TMap<FString, TSharedPtr<IDatasmithMeshElement>>& MeshesByName,
		const TSet<FString>& MaterialNames,
		FPlan& Plan,
		TMap<FGroupBucketKey, FGroupBucket>& Buckets, FConVerseImportProgress& Work)
	{
		if (!Work.Update(EConVerseImportWorkPhase::GroupPlanning, TraversalOrdinal, 0)) return;
		if (!Actor.IsValid())
		{
			return;
		}

		const FString ActorPath = ParentPath.IsEmpty()
			? MakePathSegment(Actor->GetName(), SiblingOrdinal)
			: ParentPath / MakePathSegment(Actor->GetName(), SiblingOrdinal);
		const int32 ActorOrdinal = TraversalOrdinal++;
		const bool bIsMeshActor = Actor->IsA(EDatasmithElementType::StaticMeshActor)
			&& !Actor->IsA(EDatasmithElementType::HierarchicalInstanceStaticMesh);
		if (bIsMeshActor)
		{
			++Plan.TotalMeshActors;
			const TSharedPtr<IDatasmithMeshActorElement> MeshActor = StaticCastSharedPtr<IDatasmithMeshActorElement>(Actor);
			FGroupValue& SourceMesh = Plan.AllMeshActors.AddDefaulted_GetRef();
			SourceMesh.MeshElementName = MeshActor->GetStaticMeshPathName();
			SourceMesh.bVisible = MeshActor->GetVisibility();
			SourceMesh.bCastShadow = MeshActor->GetCastShadow();
			SourceMesh.Mobility = MeshActor->GetMobility();
			SourceMesh.bIsComponent = MeshActor->IsAComponent();
			FCandidateValue& SourceCandidate = SourceMesh.Candidates.AddDefaulted_GetRef();
			SourceCandidate.SourceElementName = MeshActor->GetName();
			SourceCandidate.SourceLabel = MeshActor->GetLabel();
			SourceCandidate.HierarchyPath = ActorPath;
			SourceCandidate.ParentHierarchyPath = ParentPath;
			SourceCandidate.SourceOrdinal = ActorOrdinal;
			SourceCandidate.WorldTransform = GetWorldTransform(*MeshActor);
			SourceCandidate.RelativeTransform = MeshActor->GetRelativeTransform();
			SourceCandidate.MeshElementName = SourceMesh.MeshElementName;
			CaptureActorSourceData(Scene, MeshActor, SourceCandidate);
			if (const auto* Mesh = MeshesByName.Find(SourceMesh.MeshElementName))
				ResolveEffectiveMaterials(**Mesh, *MeshActor, MaterialNames, SourceMesh.EffectiveMaterials);
			if (Actor->GetChildrenCount() > 0 || Plan.Options.Processing.KeepOrdinaryMeshElements.Contains(MeshActor->GetStaticMeshPathName()))
			{
				IncrementSkip(Plan, EConVerseOptimizedSkipReason::HasChildren);
			}
			else
			{
				const FString MeshReference = MeshActor->GetStaticMeshPathName();
				const TSharedPtr<IDatasmithMeshElement>* MeshElement = nullptr;
				EConVerseOptimizedSkipReason FailureReason = EConVerseOptimizedSkipReason::MissingMeshReference;
				bool bEligible = !MeshReference.IsEmpty();
				if (bEligible)
				{
					MeshElement = MeshesByName.Find(MeshReference);
					bEligible = MeshElement != nullptr && MeshElement->IsValid();
					FailureReason = EConVerseOptimizedSkipReason::UnresolvedMeshReference;
				}

				TArray<FEffectiveMaterialValue> EffectiveMaterials;
				if (bEligible && !ResolveEffectiveMaterials(**MeshElement, *MeshActor, MaterialNames, EffectiveMaterials))
				{
					bEligible = false;
					FailureReason = EConVerseOptimizedSkipReason::UnresolvedMaterialBinding;
				}

				const FTransform WorldTransform = GetWorldTransform(*MeshActor);
				const FTransform ParentWorld = MeshActor->GetParentActor().IsValid()
					? GetWorldTransform(*MeshActor->GetParentActor())
					: FTransform::Identity;
				const FTransform LocalTransform = WorldTransform.GetRelativeTransform(ParentWorld);
				if (bEligible && (!IsFiniteTransform(WorldTransform) || !IsFiniteTransform(ParentWorld) || !IsFiniteTransform(LocalTransform)))
				{
					bEligible = false;
					FailureReason = EConVerseOptimizedSkipReason::NonFiniteTransform;
				}
				if (bEligible && LocalTransform.GetDeterminant() < 0.0)
				{
					bEligible = false;
					FailureReason = EConVerseOptimizedSkipReason::UnsupportedNegativeScale;
				}

				if (!bEligible)
				{
					IncrementSkip(Plan, FailureReason);
				}
				else
				{
					++Plan.EligibleLeafActors;
					const FString SerializedKey = BuildSerializedGroupKey(ParentPath, *MeshActor);
					const FGroupBucketKey BucketKey{MeshActor->GetParentActor().Get(), SerializedKey};
					FGroupBucket& Bucket = Buckets.FindOrAdd(BucketKey);
					if (Bucket.Group.SerializedKey.IsEmpty())
					{
						Bucket.Group.SerializedKey = SerializedKey;
						Bucket.Group.ParentHierarchyPath = ParentPath;
						Bucket.Group.ParentElementName = MeshActor->GetParentActor().IsValid()
							? MeshActor->GetParentActor()->GetName() : FString();
						Bucket.Group.MeshElementName = MeshReference;
						Bucket.Group.Layer = MeshActor->GetLayer();
						Bucket.Group.GroupWorldTransform = ParentWorld;
						Bucket.Group.bIsComponent = MeshActor->IsAComponent();
						Bucket.Group.bVisible = MeshActor->GetVisibility();
						Bucket.Group.bCastShadow = MeshActor->GetCastShadow();
						Bucket.Group.Mobility = MeshActor->GetMobility();
						Bucket.Group.EffectiveMaterials = MoveTemp(EffectiveMaterials);
						BuildOverrideSignature(*MeshActor, &Bucket.Group.MaterialOverrides);
					}

					FCandidateValue& Candidate = Bucket.Group.Candidates.AddDefaulted_GetRef();
					Candidate.SourceElementName = MeshActor->GetName();
					Candidate.SourceLabel = MeshActor->GetLabel();
					Candidate.HierarchyPath = ActorPath;
					Candidate.ParentHierarchyPath = ParentPath;
					Candidate.SourceOrdinal = ActorOrdinal;
					Candidate.WorldTransform = WorldTransform;
					Candidate.RelativeTransform = MeshActor->GetRelativeTransform();
					Candidate.MeshElementName = MeshReference;
					CaptureActorSourceData(Scene, MeshActor, Candidate);
				}
			}
		}

		for (int32 ChildIndex = 0; ChildIndex < Actor->GetChildrenCount() && !Work.IsCancelled(); ++ChildIndex)
		{
			GatherPlanCandidates(
				Scene, Actor->GetChild(ChildIndex), ActorPath, ChildIndex, TraversalOrdinal,
				MeshesByName, MaterialNames, Plan, Buckets, Work);
		}
	}

	static FString ComputePlanId(const FPlan& Plan)
	{
		FString Input;
		AppendField(Input, TEXT("ContractVersion"), FString::FromInt(Plan.SchemaVersion));
		AppendField(Input, TEXT("SourceHash"), Plan.SourceFileHash);
		AppendField(Input, TEXT("SourcePath"), Plan.SourceFilePath);
		AppendField(Input, TEXT("Destination"), Plan.Options.DestinationPath);
		AppendField(Input, TEXT("Mode"), InstanceTypeName(Plan.Options.InstanceType));
		AppendField(Input, TEXT("Minimum"), FString::FromInt(Plan.Options.MinimumInstanceCount));
		FConVerseImportProcessingSettings IdentitySettings = Plan.Options.Processing;
		IdentitySettings.ManyLightThreshold = 100;
		IdentitySettings.AppearanceCatalog.Reset();
		if (!IdentitySettings.bApplyApprovedMaterials) IdentitySettings.MaterialMappings.Reset();
		AppendField(Input, TEXT("Processing"), ConVerseImportProcessing::SettingsJson(IdentitySettings));
		FString MappingIdentity, MappingError;
		ConVerseImportProcessing::ValidateMappings(Plan.Options.Processing, MappingIdentity, MappingError);
		AppendField(Input, TEXT("ApprovedMappings"), MappingIdentity);
		// Tessellation must participate in plan identity. It changes generated geometry without
		// changing the source file, so omitting it would make a tessellation-only change hash to
		// the same PlanId, take the AlreadyCurrent branch, and silently discard the new settings.
		AppendField(Input, TEXT("ChordTolerance"), FString::SanitizeFloat(Plan.Options.Tessellation.ChordTolerance));
		AppendField(Input, TEXT("MaxEdgeLength"), FString::SanitizeFloat(Plan.Options.Tessellation.MaxEdgeLength));
		AppendField(Input, TEXT("NormalTolerance"), FString::SanitizeFloat(Plan.Options.Tessellation.NormalTolerance));
		AppendField(Input, TEXT("Stitching"), StitchingTechniqueName(Plan.Options.Tessellation.StitchingTechnique));
		// A texture found in a search folder changes the imported materials, so it participates. The
		// folder list itself does not: plans that resolve nothing keep their existing identity.
		for (const FString& Resolution : Plan.TextureResolutions)
		{
			AppendField(Input, TEXT("TextureResolution"), Resolution);
		}
		for (const FGroupValue& Group : Plan.Groups)
		{
			AppendField(Input, TEXT("Group"), Group.SerializedKey);
			for (const FCandidateValue& Candidate : Group.Candidates)
			{
				AppendField(Input, TEXT("CandidatePath"), Candidate.HierarchyPath);
				AppendField(Input, TEXT("CandidateOrdinal"), FString::FromInt(Candidate.SourceOrdinal));
				AppendField(Input, TEXT("CandidateWorld"), SerializeTransform(Candidate.WorldTransform));
			}
		}
		return HashUtf8(Input);
	}

	static FPlan BuildPlan(
		const TSharedRef<IDatasmithScene>& Scene,
		const FConVerseOptimizedImportOptions& Options,
		const FString& SourceHash,
		int64 SourceSize, const TArray<FString>& TextureResolutions, FConVerseImportProgress& Work)
	{
		FPlan Plan;
		Plan.TextureResolutions = TextureResolutions;
		Plan.TextureResolutions.Sort();
		Plan.SourceFilePath = Options.FilePath;
		Plan.SourceUri = FSourceUri::FromFilePath(Options.FilePath).ToString();
		Plan.SourceFileHash = SourceHash;
		Plan.SourceFileSize = SourceSize;
		Plan.SourceModifiedUtc = IFileManager::Get().GetTimeStamp(*Options.FilePath);
		Plan.SceneName = Scene->GetName();
		Plan.Host = Scene->GetHost();
		Plan.Vendor = Scene->GetVendor();
		Plan.ProductName = Scene->GetProductName();
		Plan.ProductVersion = Scene->GetProductVersion();
		Plan.ExporterVersion = Scene->GetExporterVersion();
		Plan.ExporterSdkVersion = Scene->GetExporterSDKVersion();
		Plan.Options = Options;

		TMap<FString, TSharedPtr<IDatasmithMeshElement>> MeshesByName;
		for (int32 MeshIndex = 0; MeshIndex < Scene->GetMeshesCount(); ++MeshIndex)
		{
			if (Work.IsCancelled()) return Plan;
			const TSharedPtr<IDatasmithMeshElement> Mesh = Scene->GetMesh(MeshIndex);
			if (Mesh.IsValid())
			{
				MeshesByName.Add(Mesh->GetName(), Mesh);
			}
		}

		TSet<FString> MaterialNames;
		for (int32 MaterialIndex = 0; MaterialIndex < Scene->GetMaterialsCount(); ++MaterialIndex)
		{
			if (Work.IsCancelled()) return Plan;
			const TSharedPtr<IDatasmithBaseMaterialElement> Material = Scene->GetMaterial(MaterialIndex);
			if (Material.IsValid())
			{
				MaterialNames.Add(Material->GetName());
			}
		}

		TMap<FGroupBucketKey, FGroupBucket> Buckets;
		int32 TraversalOrdinal = 0;
		for (int32 ActorIndex = 0; ActorIndex < Scene->GetActorsCount(); ++ActorIndex)
		{
			if (Work.IsCancelled()) return Plan;
			GatherPlanCandidates(
				*Scene, Scene->GetActor(ActorIndex), FString(), ActorIndex, TraversalOrdinal,
				MeshesByName, MaterialNames, Plan, Buckets, Work);
		}

		TArray<FGroupValue> CandidateGroups;
		for (TPair<FGroupBucketKey, FGroupBucket>& Pair : Buckets)
		{
			if (Work.IsCancelled()) return Plan;
			FGroupValue Group = MoveTemp(Pair.Value.Group);
			Group.Candidates.Sort([](const FCandidateValue& Left, const FCandidateValue& Right)
			{
				const int32 PathComparison = Left.HierarchyPath.Compare(Right.HierarchyPath, ESearchCase::CaseSensitive);
				return PathComparison == 0 ? Left.SourceOrdinal < Right.SourceOrdinal : PathComparison < 0;
			});
			if (Group.Candidates.Num() < Options.MinimumInstanceCount)
			{
				IncrementSkip(Plan, EConVerseOptimizedSkipReason::BelowMinimumInstanceCount, Group.Candidates.Num());
				continue;
			}
			Group.GroupId = HashUtf8(FString::Printf(TEXT("%d|%s"), ContractVersion, *Group.SerializedKey));
			Group.ElementName = FString::Printf(TEXT("ConVerseOptimized_%s"), *Group.GroupId);
			Group.Label = FString::Printf(TEXT("%s (%d instances)"), *Group.Candidates[0].SourceLabel, Group.Candidates.Num());
			CandidateGroups.Add(MoveTemp(Group));
		}

		CandidateGroups.Sort([](const FGroupValue& Left, const FGroupValue& Right)
		{
			return Left.SerializedKey.Compare(Right.SerializedKey, ESearchCase::CaseSensitive) < 0;
		});
		Plan.Groups = MoveTemp(CandidateGroups);
		if (Work.IsCancelled()) return Plan;
		Plan.PlanId = ComputePlanId(Plan);
		return Plan;
	}

	static bool NormalizeAndValidateOptions(
		const FConVerseOptimizedImportOptions& Input,
		bool bRequireDestination,
		FConVerseOptimizedImportOptions& OutOptions,
		FString& OutError)
	{
		OutOptions = Input;
		OutOptions.Processing.ManyLightThreshold = FMath::Max(1, Input.Processing.ManyLightThreshold);
		if (uint8(Input.Processing.NanitePolicy) > uint8(EConVerseNanitePolicy::PreserveImported))
		{ OutError = TEXT("Unsupported Nanite policy."); return false; }
		auto NormalizeNames = [](TArray<FString>& Names)
		{
			Names.Remove(TEXT("")); Names.Sort();
			for (int32 Index = Names.Num() - 1; Index > 0; --Index) if (Names[Index] == Names[Index - 1]) Names.RemoveAt(Index);
		};
		NormalizeNames(OutOptions.Processing.KeepOrdinaryMeshElements);
		NormalizeNames(OutOptions.Processing.DisableNaniteMeshElements);
		OutOptions.Processing.DisableNaniteMeshElements.Sort();
		FString MappingIdentity;
		if (!ConVerseImportProcessing::ValidateMappings(OutOptions.Processing, MappingIdentity, OutError)) return false;
		if (Input.InstanceType != EConVerseOptimizedInstanceType::ISM
			&& Input.InstanceType != EConVerseOptimizedInstanceType::HISM)
		{
			OutError = TEXT("Output component type must be ISM or HISM.");
			return false;
		}
		if (Input.MinimumInstanceCount < 2)
		{
			OutError = TEXT("Minimum instances must be at least 2.");
			return false;
		}

		OutOptions.FilePath = FPaths::ConvertRelativePathToFull(Input.FilePath.TrimStartAndEnd());
		FPaths::NormalizeFilename(OutOptions.FilePath);
		FPaths::CollapseRelativeDirectories(OutOptions.FilePath);
		if (OutOptions.FilePath.IsEmpty() || !FPaths::FileExists(OutOptions.FilePath))
		{
			OutError = TEXT("Select an existing local Datasmith-supported source file.");
			return false;
		}
		// Deliberately no extension allowlist. The importer supports every format the installed
		// Datasmith translators accept, including CAD, Revit, and IFC. Translator resolution in
		// LoadFreshSource is the authoritative format gate, so a hardcoded list here would only
		// reject files the engine can actually read.

		OutOptions.DestinationPath = Input.DestinationPath.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		while (OutOptions.DestinationPath.Len() > 5 && OutOptions.DestinationPath.EndsWith(TEXT("/")))
		{
			OutOptions.DestinationPath.LeftChopInline(1);
		}
		if (bRequireDestination
			&& (!OutOptions.DestinationPath.StartsWith(TEXT("/Game"))
				|| !FPackageName::IsValidLongPackageName(OutOptions.DestinationPath)))
		{
			OutError = TEXT("Destination must be a valid Unreal content folder under /Game.");
			return false;
		}

		// Clamp here rather than relying on Slate metadata: the commandlet and automation bypass
		// the panel entirely. Bounds mirror FDatasmithTessellationOptions in DatasmithImportOptions.h.
		FConVerseTessellationSettings& Tessellation = OutOptions.Tessellation;
		if (!FMath::IsFinite(Tessellation.ChordTolerance)
			|| !FMath::IsFinite(Tessellation.MaxEdgeLength)
			|| !FMath::IsFinite(Tessellation.NormalTolerance))
		{
			OutError = TEXT("Tessellation values must be finite numbers.");
			return false;
		}
		Tessellation.ChordTolerance = FMath::Max(Tessellation.ChordTolerance, 0.005f);
		Tessellation.NormalTolerance = FMath::Clamp(Tessellation.NormalTolerance, 5.0f, 90.0f);
		// Zero is the sentinel for "no edge-length constraint". Any other value has a floor of 1.0,
		// so a small non-zero entry must not be silently treated as unconstrained.
		Tessellation.MaxEdgeLength = Tessellation.MaxEdgeLength <= 0.0f
			? 0.0f
			: FMath::Max(Tessellation.MaxEdgeLength, 1.0f);
		return true;
	}

	static EDatasmithCADStitchingTechnique ToDatasmithStitching(EConVerseStitchingTechnique Technique)
	{
		switch (Technique)
		{
		case EConVerseStitchingTechnique::Sewing:  return EDatasmithCADStitchingTechnique::StitchingSew;
		case EConVerseStitchingTechnique::Healing: return EDatasmithCADStitchingTechnique::StitchingHeal;
		default:                                   return EDatasmithCADStitchingTechnique::StitchingNone;
		}
	}

	/**
	 * Push tessellation settings into the translator before the scene is loaded.
	 *
	 * Follows the sequence Epic uses in DatasmithBlueprintLibrary.cpp: GetSceneImportOptions,
	 * mutate any UDatasmithCommonTessellationOptions found, then SetSceneImportOptions. This must
	 * run before TryLoad(), because tessellation decides how surfaces become triangles during the
	 * load itself; applying it afterwards would have no effect.
	 *
	 * Returns false when the translator exposes no tessellation options. That is the normal case
	 * for already-tessellated formats such as .udatasmith and is not an error.
	 */
	static bool ApplyTessellationOptions(
		const TSharedPtr<FExternalSource>& Source,
		const FConVerseTessellationSettings& Settings)
	{
		if (!Source.IsValid())
		{
			return false;
		}
		const TSharedPtr<IDatasmithTranslator>& Translator = Source->GetAssetTranslator();
		if (!Translator.IsValid())
		{
			return false;
		}

		TArray<TObjectPtr<UDatasmithOptionsBase>> Options;
		Translator->GetSceneImportOptions(Options);

		bool bApplied = false;
		for (TObjectPtr<UDatasmithOptionsBase>& Option : Options)
		{
			UDatasmithCommonTessellationOptions* TessellationOption =
				Cast<UDatasmithCommonTessellationOptions>(Option);
			if (TessellationOption == nullptr)
			{
				continue;
			}
			// Assign fields individually rather than replacing the struct. The engine struct carries
			// protected members (GeometricTolerance, StitchingTolerance) and a bUseCADKernel flag
			// that the translator sets up; wholesale replacement would discard them.
			TessellationOption->Options.ChordTolerance = Settings.ChordTolerance;
			TessellationOption->Options.MaxEdgeLength = Settings.MaxEdgeLength;
			TessellationOption->Options.NormalTolerance = Settings.NormalTolerance;
			TessellationOption->Options.StitchingTechnique = ToDatasmithStitching(Settings.StitchingTechnique);
			bApplied = true;
		}

		if (bApplied)
		{
			Translator->SetSceneImportOptions(Options);
			UE_LOG(
				LogConVerseOptimizedImport,
				Log,
				TEXT("Applied tessellation options (chord %.4f, max edge %.4f, normal %.2f, stitching %s)."),
				Settings.ChordTolerance,
				Settings.MaxEdgeLength,
				Settings.NormalTolerance,
				*StitchingTechniqueName(Settings.StitchingTechnique));
		}
		return bApplied;
	}

	static TSharedPtr<FExternalSource> LoadFreshSource(
		const FString& FilePath,
		const FConVerseTessellationSettings& Tessellation,
		TSharedPtr<IDatasmithScene>& OutScene,
		bool& bOutTessellationApplied,
		FString& OutError)
	{
		bOutTessellationApplied = false;

		// The UE 5.8 file resolver creates a new FDatasmithFileExternalSource for each call.
		const FSourceUri Uri = FSourceUri::FromFilePath(FilePath);
		TSharedPtr<FExternalSource> Source = IExternalSourceModule::GetOrCreateExternalSource(Uri);
		if (!Source.IsValid())
		{
			// This is the authoritative format check. A missing translator usually means the
			// format's plugin is disabled rather than that the file is malformed.
			OutError = FString::Printf(
				TEXT("No enabled Datasmith translator accepts '.%s' files. Check that the plugin for this format is enabled."),
				*FPaths::GetExtension(FilePath));
			return nullptr;
		}

		// Must precede TryLoad: tessellation governs how the load itself generates triangles.
		bOutTessellationApplied = ApplyTessellationOptions(Source, Tessellation);

		OutScene = Source->TryLoad();
		if (!OutScene.IsValid())
		{
			OutError = TEXT("Datasmith could not parse the selected file. Check the Output Log and sidecar files.");
			return nullptr;
		}
		return Source;
	}

	static void GatherActorsByPath(
		const TSharedPtr<IDatasmithActorElement>& Actor,
		const FString& ParentPath,
		int32 SiblingOrdinal,
		TMap<FString, TSharedPtr<IDatasmithActorElement>>& OutActors,
		TSet<FString>& OutElementNames)
	{
		if (!Actor.IsValid())
		{
			return;
		}
		const FString Path = ParentPath.IsEmpty()
			? MakePathSegment(Actor->GetName(), SiblingOrdinal)
			: ParentPath / MakePathSegment(Actor->GetName(), SiblingOrdinal);
		OutActors.Add(Path, Actor);
		OutElementNames.Add(Actor->GetName());
		for (int32 ChildIndex = 0; ChildIndex < Actor->GetChildrenCount(); ++ChildIndex)
		{
			GatherActorsByPath(Actor->GetChild(ChildIndex), Path, ChildIndex, OutActors, OutElementNames);
		}
	}

	static bool PreflightPlanApplication(
		const TSharedRef<IDatasmithScene>& Scene,
		const FPlan& Plan,
		TArray<FResolvedGroup>& OutGroups,
		FString& OutError)
	{
		TMap<FString, TSharedPtr<IDatasmithActorElement>> ActorsByPath;
		TSet<FString> ElementNames;
		for (int32 ActorIndex = 0; ActorIndex < Scene->GetActorsCount(); ++ActorIndex)
		{
			GatherActorsByPath(Scene->GetActor(ActorIndex), FString(), ActorIndex, ActorsByPath, ElementNames);
		}

		TSet<const IDatasmithActorElement*> ClaimedActors;
		for (const FGroupValue& Group : Plan.Groups)
		{
			if (ElementNames.Contains(Group.ElementName))
			{
				OutError = FString::Printf(TEXT("Generated group name already exists in the source scene: %s"), *Group.ElementName);
				return false;
			}
			FResolvedGroup& Resolved = OutGroups.AddDefaulted_GetRef();
			Resolved.Group = &Group;
			if (!Group.ParentHierarchyPath.IsEmpty())
			{
				const TSharedPtr<IDatasmithActorElement>* Parent = ActorsByPath.Find(Group.ParentHierarchyPath);
				if (Parent == nullptr || !Parent->IsValid() || FString((*Parent)->GetName()) != Group.ParentElementName)
				{
					OutError = FString::Printf(TEXT("Parent hierarchy changed for group %s."), *Group.GroupId);
					return false;
				}
				Resolved.Parent = *Parent;
			}

			for (const FCandidateValue& Candidate : Group.Candidates)
			{
				const TSharedPtr<IDatasmithActorElement>* Actor = ActorsByPath.Find(Candidate.HierarchyPath);
				if (Actor == nullptr || !Actor->IsValid()
					|| !(*Actor)->IsA(EDatasmithElementType::StaticMeshActor)
					|| (*Actor)->IsA(EDatasmithElementType::HierarchicalInstanceStaticMesh))
				{
					OutError = FString::Printf(TEXT("Planned source actor could not be resolved: %s"), *Candidate.HierarchyPath);
					return false;
				}
				const TSharedPtr<IDatasmithMeshActorElement> MeshActor = StaticCastSharedPtr<IDatasmithMeshActorElement>(*Actor);
				if (ClaimedActors.Contains(MeshActor.Get()) || MeshActor->GetChildrenCount() != 0
					|| FString(MeshActor->GetName()) != Candidate.SourceElementName
					|| FString(MeshActor->GetStaticMeshPathName()) != Group.MeshElementName
					|| MeshActor->GetParentActor().Get() != Resolved.Parent.Get()
					|| BuildOverrideSignature(*MeshActor) != BuildOverrideSignature(Group.MaterialOverrides)
					|| FString(MeshActor->GetLayer()) != Group.Layer
					|| MeshActor->IsAComponent() != Group.bIsComponent
					|| MeshActor->GetVisibility() != Group.bVisible
					|| MeshActor->GetCastShadow() != Group.bCastShadow
					|| MeshActor->GetMobility() != Group.Mobility
					|| !TransformsMatch(Candidate.WorldTransform, GetWorldTransform(*MeshActor)))
				{
					OutError = FString::Printf(TEXT("Planned source actor changed before mutation: %s"), *Candidate.HierarchyPath);
					return false;
				}
				const FTransform Local = Candidate.WorldTransform.GetRelativeTransform(Group.GroupWorldTransform);
				if (!IsFiniteTransform(Local) || Local.GetDeterminant() < 0.0)
				{
					OutError = FString::Printf(TEXT("Unsafe instance transform detected during preflight: %s"), *Candidate.HierarchyPath);
					return false;
				}
				ClaimedActors.Add(MeshActor.Get());
				Resolved.Candidates.Add(MeshActor);
			}
		}
		return true;
	}

	static void ApplyResolvedPlan(
		const TSharedRef<IDatasmithScene>& Scene,
		const FPlan& Plan,
		const TArray<FResolvedGroup>& ResolvedGroups,
		const FString& SessionId)
	{
		for (const FResolvedGroup& Resolved : ResolvedGroups)
		{
			const FGroupValue& Group = *Resolved.Group;
			TSharedRef<IDatasmithHierarchicalInstancedStaticMeshActorElement> InstanceActor =
				FDatasmithSceneFactory::CreateHierarchicalInstanceStaticMeshActor(*Group.ElementName);
			InstanceActor->SetLabel(*Group.Label);
			InstanceActor->SetStaticMeshPathName(*Group.MeshElementName);
			InstanceActor->SetLayer(*Group.Layer);
			InstanceActor->SetIsAComponent(Group.bIsComponent);
			InstanceActor->SetVisibility(Group.bVisible);
			InstanceActor->SetCastShadow(Group.bCastShadow);
			InstanceActor->SetMobility(Group.Mobility);
			InstanceActor->SetTranslation(Group.GroupWorldTransform.GetTranslation());
			InstanceActor->SetRotation(Group.GroupWorldTransform.GetRotation());
			InstanceActor->SetScale(Group.GroupWorldTransform.GetScale3D());
			InstanceActor->AddTag(ManagedTag);
			InstanceActor->AddTag(*FString::Printf(TEXT("%s%s"), GroupTagPrefix, *Group.GroupId));
			InstanceActor->AddTag(*FString::Printf(TEXT("%s%s"), SessionTagPrefix, *SessionId));
			InstanceActor->AddTag(*FString::Printf(TEXT("%s%s"), PlanTagPrefix, *Plan.PlanId));
			InstanceActor->AddTag(*FString::Printf(TEXT("ConVerseRequestedMode=%s"), *InstanceTypeName(Plan.Options.InstanceType)));
			InstanceActor->AddTag(*FString::Printf(TEXT("ConVerseExpectedInstances=%d"), Group.Candidates.Num()));
			for (const FMaterialOverrideValue& Override : Group.MaterialOverrides)
			{
				InstanceActor->AddMaterialOverride(*Override.MaterialElementName, Override.SlotId);
			}
			InstanceActor->ReserveSpaceForInstances(Group.Candidates.Num());
			for (const FCandidateValue& Candidate : Group.Candidates)
			{
				InstanceActor->AddInstance(Candidate.WorldTransform.GetRelativeTransform(Group.GroupWorldTransform));
			}
			if (Resolved.Parent.IsValid())
			{
				Resolved.Parent->AddChild(InstanceActor, EDatasmithActorAttachmentRule::KeepWorldTransform);
			}
			else
			{
				Scene->AddActor(InstanceActor);
			}
		}

		// Removal is delayed until all replacement elements and instances exist.
		for (const FResolvedGroup& Resolved : ResolvedGroups)
		{
			for (const TSharedPtr<IDatasmithMeshActorElement>& Candidate : Resolved.Candidates)
			{
				if (Resolved.Parent.IsValid())
				{
					Resolved.Parent->RemoveChild(Candidate);
				}
				else
				{
					Scene->RemoveActor(Candidate, EDatasmithActorRemovalRule::RemoveChildren);
				}
			}
		}
	}

	static bool HasTag(const UActorComponent& Component, const FString& Tag)
	{
		return Component.ComponentHasTag(FName(*Tag));
	}

	static FString GetTagValue(const UActorComponent& Component, const TCHAR* Prefix)
	{
		for (const FName& Tag : Component.ComponentTags)
		{
			const FString Value = Tag.ToString();
			if (Value.StartsWith(Prefix))
			{
				return Value.RightChop(FCString::Strlen(Prefix));
			}
		}
		return FString();
	}

	static void GatherSessionComponents(
		UWorld& World,
		const FString& SessionId,
		TMap<FString, TArray<UInstancedStaticMeshComponent*>>& OutComponents)
	{
		const FString SessionTag = FString::Printf(TEXT("%s%s"), SessionTagPrefix, *SessionId);
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			TInlineComponentArray<UInstancedStaticMeshComponent*> Components(*It);
			for (UInstancedStaticMeshComponent* Component : Components)
			{
				if (IsValid(Component) && HasTag(*Component, ManagedTag) && HasTag(*Component, SessionTag))
				{
					OutComponents.FindOrAdd(GetTagValue(*Component, GroupTagPrefix)).Add(Component);
				}
			}
		}
	}

	static int32 ParseDatasmithSlotId(const FString& SlotName)
	{
		if (SlotName.IsEmpty())
		{
			return INDEX_NONE;
		}
		for (const TCHAR Character : SlotName)
		{
			if (!FChar::IsDigit(Character))
			{
				return INDEX_NONE;
			}
		}
		int32 SlotId = INDEX_NONE;
		LexFromString(SlotId, *SlotName);
		return SlotId;
	}

	/**
	 * Recovers the Datasmith material slot id behind every imported static mesh slot.
	 * FDatasmithStaticMaterialTemplate::Apply overwrites both MaterialSlotName and
	 * ImportedMaterialSlotName with the imported material asset's name, so the numeric slot id only
	 * survives on the LOD0 mesh description polygon groups. Parsing the FStaticMaterial names alone
	 * resolves every slot of a multi-material mesh to id 0, which compares slot 1 against slot 0's
	 * material and fails verification on meshes that are in fact correct.
	 */
	static void BuildImportedSlotIds(const UStaticMesh& Mesh, TArray<int32>& OutSlotIds)
	{
		const int32 SlotCount = Mesh.GetStaticMaterials().Num();
		OutSlotIds.Reset();
		OutSlotIds.Init(INDEX_NONE, SlotCount);

		const FMeshDescription* MeshDescription = Mesh.GetMeshDescription(0);
		if (MeshDescription != nullptr && MeshDescription->PolygonGroups().Num() == SlotCount)
		{
			const FStaticMeshConstAttributes Attributes(*MeshDescription);
			const TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			if (SlotNames.IsValid())
			{
				int32 SlotIndex = 0;
				for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
				{
					OutSlotIds[SlotIndex++] = ParseDatasmithSlotId(SlotNames[PolygonGroupID].ToString());
				}
			}
		}

		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			if (OutSlotIds[SlotIndex] == INDEX_NONE)
			{
				OutSlotIds[SlotIndex] = ParseDatasmithSlotId(
					Mesh.GetStaticMaterials()[SlotIndex].ImportedMaterialSlotName.ToString());
			}
		}
	}

	static const FEffectiveMaterialValue* FindEffectiveMaterial(
		const FGroupValue& Group,
		int32 SlotIndex,
		int32 DatasmithSlotId)
	{
		const FEffectiveMaterialValue* Effective = DatasmithSlotId != INDEX_NONE
			? Group.EffectiveMaterials.FindByPredicate(
				[DatasmithSlotId](const FEffectiveMaterialValue& Value)
				{
					return Value.DatasmithSlotId == DatasmithSlotId;
				})
			: nullptr;
		if (Effective == nullptr && Group.EffectiveMaterials.IsValidIndex(SlotIndex))
		{
			Effective = &Group.EffectiveMaterials[SlotIndex];
		}
		return Effective;
	}

	static bool ResolveExpectedAssets(
		const FGroupValue& Group,
		UDatasmithScene& ImportedScene,
		UStaticMesh*& OutMesh,
		TArray<UMaterialInterface*>& OutMaterials,
		FString& OutError)
	{
		const TSoftObjectPtr<UStaticMesh>* MeshAsset = ImportedScene.StaticMeshes.Find(FName(*Group.MeshElementName));
		OutMesh = MeshAsset != nullptr ? MeshAsset->LoadSynchronous() : nullptr;
		if (OutMesh == nullptr)
		{
			OutError = FString::Printf(TEXT("Datasmith scene map has no imported mesh for element '%s'."), *Group.MeshElementName);
			return false;
		}
		if (OutMesh->GetStaticMaterials().Num() != Group.EffectiveMaterials.Num())
		{
			OutError = FString::Printf(TEXT("Expected %d mesh material slots, imported mesh has %d."),
				Group.EffectiveMaterials.Num(), OutMesh->GetStaticMaterials().Num());
			return false;
		}

		OutMaterials.SetNum(OutMesh->GetStaticMaterials().Num());
		TArray<int32> SlotIds;
		BuildImportedSlotIds(*OutMesh, SlotIds);
		for (int32 SlotIndex = 0; SlotIndex < OutMesh->GetStaticMaterials().Num(); ++SlotIndex)
		{
			const FEffectiveMaterialValue* Expected = FindEffectiveMaterial(Group, SlotIndex, SlotIds[SlotIndex]);
			if (Expected == nullptr)
			{
				OutError = FString::Printf(TEXT("Could not map imported material slot %d to the Datasmith mesh."), SlotIndex);
				return false;
			}
			const TSoftObjectPtr<UMaterialInterface>* MaterialAsset =
				ImportedScene.Materials.Find(FName(*Expected->MaterialElementName));
			OutMaterials[SlotIndex] = MaterialAsset != nullptr ? MaterialAsset->LoadSynchronous() : nullptr;
			if (OutMaterials[SlotIndex] == nullptr)
			{
				OutError = FString::Printf(TEXT("Datasmith scene map has no imported material for element '%s'."),
					*Expected->MaterialElementName);
				return false;
			}
		}
		return true;
	}

	static FComponentSnapshot CaptureSnapshot(UHierarchicalInstancedStaticMeshComponent& Component, const FString& GroupId)
	{
		FComponentSnapshot Snapshot;
		Snapshot.GroupId = GroupId;
		Snapshot.SourceHISM = &Component;
		Snapshot.Mesh = Component.GetStaticMesh();
		Snapshot.AttachParent = Component.GetAttachParent();
		Snapshot.AttachSocket = Component.GetAttachSocketName();
		Snapshot.OwnerLevel = Component.GetOwner() != nullptr ? Component.GetOwner()->GetLevel() : nullptr;
		Snapshot.RelativeTransform = Component.GetRelativeTransform();
		Snapshot.Mobility = Component.Mobility;
		Snapshot.bVisible = Component.IsVisible();
		Snapshot.bHiddenInGame = Component.bHiddenInGame;
		Snapshot.bCastShadow = Component.CastShadow;
		Snapshot.CollisionEnabled = Component.GetCollisionEnabled();
		Snapshot.CollisionProfile = Component.GetCollisionProfileName();
		Snapshot.bGenerateOverlapEvents = Component.GetGenerateOverlapEvents();
		Component.GetCullDistances(Snapshot.StartCullDistance, Snapshot.EndCullDistance);
		Snapshot.Tags = Component.ComponentTags;
		for (int32 MaterialIndex = 0; MaterialIndex < Component.GetNumMaterials(); ++MaterialIndex)
		{
			Snapshot.Materials.Add(Component.GetMaterial(MaterialIndex));
		}
		for (int32 InstanceIndex = 0; InstanceIndex < Component.GetInstanceCount(); ++InstanceIndex)
		{
			FTransform Transform;
			if (Component.GetInstanceTransform(InstanceIndex, Transform, false))
			{
				Snapshot.LocalInstances.Add(Transform);
			}
		}
		return Snapshot;
	}

	static bool MatchesSnapshot(
		const UInstancedStaticMeshComponent& Component,
		const FComponentSnapshot& Snapshot,
		FString& OutError)
	{
		if (Component.GetStaticMesh() != Snapshot.Mesh.Get())
		{
			OutError = TEXT("mesh differs from the imported HISM precursor");
			return false;
		}
		if (Component.GetAttachParent() != Snapshot.AttachParent.Get()
			|| Component.GetAttachSocketName() != Snapshot.AttachSocket
			|| !TransformsMatch(Component.GetRelativeTransform(), Snapshot.RelativeTransform)
			|| Component.GetOwner() == nullptr || Component.GetOwner()->GetLevel() != Snapshot.OwnerLevel.Get())
		{
			OutError = TEXT("attachment, relative transform, or owner level differs from the imported precursor");
			return false;
		}
		if (Component.Mobility != Snapshot.Mobility || Component.IsVisible() != Snapshot.bVisible
			|| Component.bHiddenInGame != Snapshot.bHiddenInGame || Component.CastShadow != Snapshot.bCastShadow
			|| Component.GetCollisionEnabled() != Snapshot.CollisionEnabled
			|| Component.GetCollisionProfileName() != Snapshot.CollisionProfile
			|| Component.GetGenerateOverlapEvents() != Snapshot.bGenerateOverlapEvents)
		{
			OutError = TEXT("shared component settings differ from the imported precursor");
			return false;
		}
		int32 StartCull = 0;
		int32 EndCull = 0;
		Component.GetCullDistances(StartCull, EndCull);
		if (StartCull != Snapshot.StartCullDistance || EndCull != Snapshot.EndCullDistance
			|| Component.ComponentTags != Snapshot.Tags)
		{
			OutError = TEXT("cull distances or required tags differ from the imported precursor");
			return false;
		}
		if (Component.GetNumMaterials() != Snapshot.Materials.Num())
		{
			OutError = TEXT("material slot count differs from the imported precursor");
			return false;
		}
		for (int32 MaterialIndex = 0; MaterialIndex < Snapshot.Materials.Num(); ++MaterialIndex)
		{
			if (Component.GetMaterial(MaterialIndex) != Snapshot.Materials[MaterialIndex].Get())
			{
				OutError = FString::Printf(TEXT("material differs at slot %d"), MaterialIndex);
				return false;
			}
		}
		if (Component.GetInstanceCount() != Snapshot.LocalInstances.Num())
		{
			OutError = TEXT("instance count differs from the imported precursor");
			return false;
		}
		for (int32 InstanceIndex = 0; InstanceIndex < Snapshot.LocalInstances.Num(); ++InstanceIndex)
		{
			FTransform Actual;
			if (!Component.GetInstanceTransform(InstanceIndex, Actual, false)
				|| !TransformsMatch(Snapshot.LocalInstances[InstanceIndex], Actual))
			{
				OutError = FString::Printf(TEXT("local instance transform differs at index %d"), InstanceIndex);
				return false;
			}
		}
		return true;
	}

	static bool ConvertHISMsToISMsTwoStage(
		const TArray<FComponentSnapshot>& Snapshots,
		FString& OutError)
	{
		struct FReplacement
		{
			UHierarchicalInstancedStaticMeshComponent* Source = nullptr;
			UInstancedStaticMeshComponent* Target = nullptr;
			const FComponentSnapshot* Snapshot = nullptr;
		};
		TArray<FReplacement> Replacements;

		auto DestroyTargets = [&Replacements]()
		{
			for (FReplacement& Replacement : Replacements)
			{
				if (IsValid(Replacement.Target))
				{
					Replacement.Target->DestroyComponent();
				}
			}
		};

		for (const FComponentSnapshot& Snapshot : Snapshots)
		{
			UHierarchicalInstancedStaticMeshComponent* Source = Snapshot.SourceHISM.Get();
			AActor* Owner = IsValid(Source) ? Source->GetOwner() : nullptr;
			if (!IsValid(Source) || !IsValid(Owner))
			{
				OutError = FString::Printf(TEXT("Imported HISM for group %s is no longer valid."), *Snapshot.GroupId);
				DestroyTargets();
				return false;
			}

			Owner->Modify();
			const FName Name = MakeUniqueObjectName(
				Owner, UInstancedStaticMeshComponent::StaticClass(),
				FName(*FString::Printf(TEXT("%s_ISM"), *Source->GetName())));
			UInstancedStaticMeshComponent* Target = NewObject<UInstancedStaticMeshComponent>(Owner, Name, RF_Transactional);
			FReplacement& Replacement = Replacements.AddDefaulted_GetRef();
			Replacement.Source = Source;
			Replacement.Target = Target;
			Replacement.Snapshot = &Snapshot;

			Owner->AddInstanceComponent(Target);
			Target->SetupAttachment(Source->GetAttachParent(), Source->GetAttachSocketName());
			Target->SetRelativeTransform(Source->GetRelativeTransform());
			Target->SetMobility(Source->Mobility);
			UStaticMesh* Mesh = Source->GetStaticMesh();
			Target->SetStaticMesh(Mesh);
			Target->SetVisibility(Source->IsVisible(), true);
			Target->SetHiddenInGame(Source->bHiddenInGame, true);
			Target->SetCastShadow(Source->CastShadow);
			Target->SetCollisionProfileName(Source->GetCollisionProfileName());
			Target->SetCollisionEnabled(Source->GetCollisionEnabled());
			Target->SetGenerateOverlapEvents(Source->GetGenerateOverlapEvents());
			Target->SetCullDistances(Snapshot.StartCullDistance, Snapshot.EndCullDistance);
			Target->ComponentTags = Source->ComponentTags;
			for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
			{
				Target->SetMaterial(MaterialIndex, Source->GetMaterial(MaterialIndex));
			}
			Target->PreAllocateInstancesMemory(Source->GetInstanceCount());
			for (const FTransform& LocalTransform : Snapshot.LocalInstances)
			{
				Target->AddInstance(LocalTransform);
			}
			Target->RegisterComponent();

			FString Mismatch;
			if (!MatchesSnapshot(*Target, Snapshot, Mismatch))
			{
				OutError = FString::Printf(TEXT("ISM replacement preflight failed for group %s: %s."),
					*Snapshot.GroupId, *Mismatch);
				DestroyTargets();
				return false;
			}
		}

		// No source HISM is destroyed until every target has been built and validated.
		for (FReplacement& Replacement : Replacements)
		{
			AActor* Owner = Replacement.Source->GetOwner();
			if (Owner->GetRootComponent() == Replacement.Source)
			{
				Owner->SetRootComponent(Replacement.Target);
			}
			TArray<USceneComponent*> Children;
			Replacement.Source->GetChildrenComponents(false, Children);
			for (USceneComponent* Child : Children)
			{
				Child->AttachToComponent(Replacement.Target, FAttachmentTransformRules::KeepWorldTransform);
			}
			Replacement.Source->DestroyComponent();
			Owner->MarkPackageDirty();
		}
		return true;
	}

	static void AttachRuntimeSource(UStaticMeshComponent& Component, const FCandidateValue& Candidate,
		const FString& Document, int32 InstanceIndex)
	{
		AActor* Owner = Component.GetOwner();
		auto* Data = Owner->FindComponentByClass<UConVerseSourceMetadata>();
		if (!Data)
		{
			Data = NewObject<UConVerseSourceMetadata>(Owner, NAME_None, RF_Transactional);
			Owner->AddInstanceComponent(Data);
			Data->RegisterComponent();
		}
		auto& Record = Data->Records.AddDefaulted_GetRef();
		Record.ComponentName = Component.GetFName();
		Record.InstanceIndex = InstanceIndex;
		Record.SourceElement = Candidate.SourceElementName;
		Record.SourceLabel = Candidate.SourceLabel;
		Record.SourceMesh = Candidate.MeshElementName;
		Record.SourceDocument = Document;
		for (const auto& Pair : Candidate.Metadata) Record.Metadata.Add(Pair.Name, Pair.Value);
	}

	static bool VerifySourceAccounting(UWorld& World, UDatasmithScene& ImportedScene, const FPlan& Plan,
		const FMutationInventory& Inventory, const FString& SessionId, FConVerseOptimizedImportResult& Result)
	{
		TMap<FString, TArray<UStaticMeshComponent*>> ByElement;
		TSet<UStaticMeshComponent*> OrdinaryComponents;
		TMap<FString, TArray<UInstancedStaticMeshComponent*>> Groups;
		GatherSessionComponents(World, SessionId, Groups);
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (Inventory.ExistingActors.Contains(*It)) continue;
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (UStaticMeshComponent* Component : Components)
			{
				if (Cast<UInstancedStaticMeshComponent>(Component)) continue;
				OrdinaryComponents.Add(Component);
				const FString Id = UDatasmithAssetUserData::GetDatasmithUserDataValueForKey(Component,
					FName(UDatasmithAssetUserData::UniqueIdMetaDataKey));
				if (!Id.IsEmpty()) ByElement.FindOrAdd(Id).AddUnique(Component);
			}
			if (auto* SceneActor = Cast<ADatasmithSceneActor>(*It))
			{
				for (const auto& Pair : SceneActor->RelatedActors)
				{
					AActor* Actor = Pair.Value.Get();
					if (!Actor) continue;
					if (auto* Mesh = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
						if (!Cast<UInstancedStaticMeshComponent>(Mesh)) ByElement.FindOrAdd(Pair.Key.ToString()).AddUnique(Mesh);
				}
			}
		}
		TSet<FString> Converted;
		TMap<FString, TPair<FSoftObjectPath, int32>> InstanceOutputs;
		const FString Document = HashUtf8(Plan.SourceFilePath);
		for (const auto& Group : Plan.Groups)
		{
			const auto* Outputs = Groups.Find(Group.GroupId);
			for (int32 Index = 0; Index < Group.Candidates.Num(); ++Index)
			{
				const auto& Candidate = Group.Candidates[Index];
				Converted.Add(Candidate.SourceElementName);
				if (Outputs && Outputs->Num() == 1)
				{
					AttachRuntimeSource(*(*Outputs)[0], Candidate, Document, Index);
					InstanceOutputs.Add(Candidate.SourceElementName, {FSoftObjectPath((*Outputs)[0]), Index});
				}
			}
		}
		bool bPassed = true;
		TSet<UStaticMeshComponent*> Accounted;
		Result.InspectionRows.Reset();
		for (const auto& Source : Plan.AllMeshActors)
		{
			const auto& Candidate = Source.Candidates[0];
			auto& Row = Result.InspectionRows.AddDefaulted_GetRef();
			Row.SourceElement = Candidate.SourceElementName;
			for (const auto& Identity : Candidate.IdentityCandidates)
				Row.SourceIdentity += (Row.SourceIdentity.IsEmpty() ? FString() : TEXT("; ")) + Identity.Key + TEXT("=") + Identity.Value;
			Row.Label = Candidate.SourceLabel;
			Row.MeshElement = Candidate.MeshElementName;
			if (Converted.Contains(Candidate.SourceElementName))
			{
				Row.Outcome = TEXT("Instance");
				if (const auto* Output = InstanceOutputs.Find(Candidate.SourceElementName))
				{
					Row.ComponentPath = Output->Key;
					Row.InstanceIndex = Output->Value;
				}
				if (ByElement.Contains(Candidate.SourceElementName)) { bPassed = false; Row.Outcome = TEXT("FAIL: duplicate ordinary geometry for converted source"); }
				continue;
			}
			const auto* Outputs = ByElement.Find(Candidate.SourceElementName);
			FString Error;
			UStaticMesh* Mesh = nullptr;
			TArray<UMaterialInterface*> Materials;
			if (!Outputs || Outputs->Num() != 1 || !ResolveExpectedAssets(Source, ImportedScene, Mesh, Materials, Error))
			{
				Row.Outcome = TEXT("FAIL: ordinary source has missing, duplicate, or unresolved output. ") + Error;
				bPassed = false;
				continue;
			}
			UStaticMeshComponent* Component = (*Outputs)[0];
			Row.ComponentPath = FSoftObjectPath(Component);
			bool bMatches = !Accounted.Contains(Component) && Component->GetStaticMesh() == Mesh
				&& TransformsMatch(Component->GetComponentTransform(), Candidate.WorldTransform)
				&& Component->IsVisible() == Source.bVisible && Component->CastShadow == Source.bCastShadow
				&& Component->GetNumMaterials() == Materials.Num();
			for (int32 Slot = 0; Slot < Materials.Num(); ++Slot) bMatches &= Component->GetMaterial(Slot) == Materials[Slot];
			Accounted.Add(Component);
			Row.Outcome = bMatches ? TEXT("Ordinary: verified") : TEXT("FAIL: ordinary mesh, placement, visibility, or materials differ");
			if (bMatches) ++Result.VerifiedOrdinaryMeshes;
			else bPassed = false;
			AttachRuntimeSource(*Component, Candidate, Document, INDEX_NONE);
		}
		if (Accounted.Num() != OrdinaryComponents.Num())
		{
			bPassed = false;
			Result.Diagnostics.Add(FString::Printf(TEXT("Unaccounted ordinary mesh components: actual=%d accounted=%d"), OrdinaryComponents.Num(), Accounted.Num()));
		}
		for (const auto& Row : Result.InspectionRows)
			if (Row.Outcome.StartsWith(TEXT("FAIL"))) Result.Diagnostics.Add(Row.SourceElement + TEXT(": ") + Row.Outcome);
		return bPassed;
	}

	static bool VerifySession(
		UWorld& World,
		UDatasmithScene& ImportedScene,
		const FPlan& Plan,
		const FString& SessionId,
		const TArray<FComponentSnapshot>& Snapshots,
		FConVerseOptimizedImportResult& Result)
	{
		TMap<FString, const FComponentSnapshot*> SnapshotsByGroup;
		for (const FComponentSnapshot& Snapshot : Snapshots)
		{
			SnapshotsByGroup.Add(Snapshot.GroupId, &Snapshot);
		}

		TMap<FString, TArray<UInstancedStaticMeshComponent*>> ComponentsByGroup;
		GatherSessionComponents(World, SessionId, ComponentsByGroup);
		bool bAllPassed = true;
		int32 SessionInstanceTotal = 0;
		for (const FGroupValue& Group : Plan.Groups)
		{
			FConVerseOptimizedGroupVerification& Verification = Result.GroupVerification.AddDefaulted_GetRef();
			Verification.GroupId = Group.GroupId;
			Verification.ExpectedInstances = Group.Candidates.Num();
			const TArray<UInstancedStaticMeshComponent*>* Components = ComponentsByGroup.Find(Group.GroupId);
			if (Components == nullptr || Components->Num() != 1)
			{
				Verification.Details = FString::Printf(TEXT("expected one session component, found %d"), Components == nullptr ? 0 : Components->Num());
				bAllPassed = false;
				continue;
			}

			UInstancedStaticMeshComponent* Component = (*Components)[0];
			Verification.ComponentPath = Component->GetPathName();
			Verification.ComponentClass = Component->GetClass()->GetName();
			Verification.ActualInstances = Component->GetInstanceCount();
			SessionInstanceTotal += Verification.ActualInstances;
			FString Failure;
			const UClass* ExpectedClass = Plan.Options.InstanceType == EConVerseOptimizedInstanceType::HISM
				? UHierarchicalInstancedStaticMeshComponent::StaticClass()
				: UInstancedStaticMeshComponent::StaticClass();
			if (Component->GetClass() != ExpectedClass)
			{
				Failure = FString::Printf(TEXT("expected exact %s class, found %s"), *ExpectedClass->GetName(), *Component->GetClass()->GetName());
			}
			else if (Component->GetInstanceCount() != Group.Candidates.Num())
			{
				Failure = FString::Printf(TEXT("expected %d instances, found %d"), Group.Candidates.Num(), Component->GetInstanceCount());
			}

			UStaticMesh* ExpectedMesh = nullptr;
			TArray<UMaterialInterface*> ExpectedMaterials;
			if (Failure.IsEmpty() && !ResolveExpectedAssets(Group, ImportedScene, ExpectedMesh, ExpectedMaterials, Failure))
			{
			}
			else if (Failure.IsEmpty() && Component->GetStaticMesh() != ExpectedMesh)
			{
				Failure = TEXT("component mesh pointer does not match UDatasmithScene::StaticMeshes");
			}
			else if (Failure.IsEmpty() && Component->GetNumMaterials() != ExpectedMaterials.Num())
			{
				Failure = FString::Printf(TEXT("expected %d effective material slots, found %d"), ExpectedMaterials.Num(), Component->GetNumMaterials());
			}
			if (Failure.IsEmpty())
			{
				for (int32 SlotIndex = 0; SlotIndex < ExpectedMaterials.Num(); ++SlotIndex)
				{
					if (Component->GetMaterial(SlotIndex) != ExpectedMaterials[SlotIndex])
					{
						Failure = FString::Printf(TEXT("material pointer mismatch at slot %d"), SlotIndex);
						break;
					}
				}
			}
			if (Failure.IsEmpty())
			{
				const FComponentSnapshot* const* Snapshot = SnapshotsByGroup.Find(Group.GroupId);
				if (Snapshot == nullptr || !MatchesSnapshot(*Component, **Snapshot, Failure))
				{
					if (Snapshot == nullptr)
					{
						Failure = TEXT("missing imported HISM precursor snapshot");
					}
				}
			}
			if (Failure.IsEmpty())
			{
				for (int32 InstanceIndex = 0; InstanceIndex < Group.Candidates.Num(); ++InstanceIndex)
				{
					FTransform ActualWorld;
					if (!Component->GetInstanceTransform(InstanceIndex, ActualWorld, true)
						|| !TransformsMatch(Group.Candidates[InstanceIndex].WorldTransform, ActualWorld))
					{
						Failure = FString::Printf(TEXT("ordered world transform mismatch at instance %d"), InstanceIndex);
						break;
					}
				}
			}

			Verification.bPassed = Failure.IsEmpty();
			Verification.Details = Verification.bPassed ? TEXT("type, mesh, materials, count, order, transforms, placement, and settings passed") : Failure;
			if (Verification.bPassed)
			{
				++Result.VerifiedGroupCount;
				Result.VerifiedInstanceCount += Verification.ActualInstances;
			}
			else
			{
				bAllPassed = false;
			}
		}

		for (const TPair<FString, TArray<UInstancedStaticMeshComponent*>>& Pair : ComponentsByGroup)
		{
			if (Pair.Key.IsEmpty() || !Plan.Groups.ContainsByPredicate(
				[&Pair](const FGroupValue& Group) { return Group.GroupId == Pair.Key; }))
			{
				FConVerseOptimizedGroupVerification& Unexpected = Result.GroupVerification.AddDefaulted_GetRef();
				Unexpected.GroupId = Pair.Key.IsEmpty() ? TEXT("<missing>") : Pair.Key;
				Unexpected.Details = FString::Printf(TEXT("unexpected current-session group with %d component(s)"), Pair.Value.Num());
				bAllPassed = false;
			}
		}
		if (ComponentsByGroup.Num() != Plan.Groups.Num() || SessionInstanceTotal != Result.PlannedInstanceCount)
		{
			bAllPassed = false;
		}
		return bAllPassed;
	}

	static EConVerseOptimizedSourceIdentityStatus ToManifestIdentityStatus(ESourceIdentityStatus Status)
	{
		switch (Status)
		{
		case ESourceIdentityStatus::Explicit:
			return EConVerseOptimizedSourceIdentityStatus::Explicit;
		case ESourceIdentityStatus::Ambiguous:
			return EConVerseOptimizedSourceIdentityStatus::Ambiguous;
		default:
			return EConVerseOptimizedSourceIdentityStatus::Missing;
		}
	}

	static bool CommitManifestAndOwnership(
		UWorld& World,
		UDatasmithScene& ImportedScene,
		const FPlan& Plan,
		const FMutationInventory& Inventory,
		const UConVerseOptimizedImportManifest* PreviousManifest,
		FConVerseOptimizedImportResult& Result,
		FString& OutError)
	{
		TMap<FString, TArray<UInstancedStaticMeshComponent*>> ComponentsByGroup;
		GatherSessionComponents(World, Result.SessionId, ComponentsByGroup);
		if (ComponentsByGroup.Num() != Plan.Groups.Num())
		{
			OutError = TEXT("The verified component inventory changed before the manifest was committed.");
			return false;
		}

		ImportedScene.Modify();
		ImportedScene.RemoveUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass());
		UConVerseOptimizedImportManifest* Manifest = NewObject<UConVerseOptimizedImportManifest>(
			&ImportedScene,
			TEXT("ConVerseOptimizedImportManifest"),
			RF_Transactional);
		if (Manifest == nullptr)
		{
			OutError = TEXT("Could not allocate the optimized import manifest.");
			return false;
		}

		Manifest->ManifestSchemaVersion = ContractVersion;
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DatasmithHISM")))
		{
			Manifest->PluginVersion = Plugin->GetDescriptor().VersionName;
		}
		Manifest->EngineVersion = FEngineVersion::Current().ToString();
		Manifest->ManifestId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		Manifest->SourceUri = Plan.SourceUri;
		Manifest->CanonicalSourceFilePath = Plan.SourceFilePath;
		Manifest->SourceFileHash = Plan.SourceFileHash;
		Manifest->SourceFileSize = Plan.SourceFileSize;
		Manifest->SourceModifiedUtc = Plan.SourceModifiedUtc;
		// Sourced from the result rather than the plan: the sidecar fingerprint is deliberately not
		// part of plan identity, so it is carried alongside it.
		Manifest->SidecarHash = Result.SidecarHash;
		Manifest->SidecarTotalSize = Result.SidecarTotalSize;
		Manifest->SidecarFileCount = Result.SidecarFileCount;
		Manifest->SceneName = Plan.SceneName;
		Manifest->Host = Plan.Host;
		Manifest->Vendor = Plan.Vendor;
		Manifest->ProductName = Plan.ProductName;
		Manifest->ProductVersion = Plan.ProductVersion;
		Manifest->ExporterVersion = Plan.ExporterVersion;
		Manifest->ExporterSdkVersion = Plan.ExporterSdkVersion;
		Manifest->Options.CanonicalSourceFilePath = Plan.SourceFilePath;
		Manifest->Options.DestinationContentFolder = Plan.Options.DestinationPath;
		Manifest->Options.ComponentType = Plan.Options.InstanceType == EConVerseOptimizedInstanceType::HISM
			? EConVerseOptimizedManifestComponentType::HISM
			: EConVerseOptimizedManifestComponentType::ISM;
		Manifest->Options.MinimumInstanceCount = Plan.Options.MinimumInstanceCount;
		Manifest->ProcessingSettingsJson = Result.ProcessingSettingsJson;
		Manifest->Appearances = Result.Appearances;
		Manifest->SourceInventoryVersion = 1;
		Manifest->SourceLightDescriptions = Result.SourceLightDescriptions;
		Manifest->MaterialDecisions = Result.MaterialDecisions;
		Manifest->TrackedObjects = Result.TrackedObjects;
		Manifest->TrackedStateVersion = 1;
		Manifest->ImportedElements = Result.InspectionRows;
		Manifest->Diagnostics = Result.Diagnostics;
		Manifest->PlanId = Plan.PlanId;
		Manifest->SessionId = Result.SessionId;
		if (PreviousManifest != nullptr)
		{
			Manifest->PreviousManifestId = PreviousManifest->ManifestId;
			Manifest->PreviousManifestObjectPath = PreviousManifest->ManifestObjectPath;
			Manifest->PreviousSessionId = PreviousManifest->SessionId;
		}
		Manifest->ImportedDatasmithSceneAssetPath = FSoftObjectPath(&ImportedScene);
		Manifest->DestinationContentFolder = Plan.Options.DestinationPath;
		Manifest->WorldPath = FSoftObjectPath(&World);
		Manifest->LevelPath = World.GetCurrentLevel() != nullptr
			? FSoftObjectPath(World.GetCurrentLevel())
			: FSoftObjectPath();
		// Commit is reachable from the accept-with-failures path as well as the verified path, so the
		// recorded state has to come from the result. Hardcoding Passed here would let a degraded
		// session claim it verified, which is exactly what supersede consults later.
		int32 FailedGroupCount = 0;
		for (const FConVerseOptimizedGroupVerification& GroupVerification : Result.GroupVerification)
		{
			if (!GroupVerification.bPassed)
			{
				++FailedGroupCount;
			}
		}

		Manifest->Verification.State = Result.bVerificationSucceeded
			? EConVerseOptimizedVerificationState::Passed
			: EConVerseOptimizedVerificationState::Failed;
		Manifest->Verification.CompletedUtc = FDateTime::UtcNow();
		Manifest->Verification.PlannedGroupCount = Result.PlannedGroupCount;
		Manifest->Verification.VerifiedGroupCount = Result.VerifiedGroupCount;
		Manifest->Verification.PlannedInstanceCount = Result.PlannedInstanceCount;
		Manifest->Verification.VerifiedInstanceCount = Result.VerifiedInstanceCount;
		Manifest->Verification.FailedGroupCount = FailedGroupCount;
		Manifest->Verification.Summary = Result.Summary;
		Manifest->Verification.FullReportArtifactPath = Result.SavedReportPath;

		ADatasmithSceneActor* SceneActor = nullptr;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Inventory.ExistingActors.Contains(Actor))
			{
				FConVerseOptimizedCreatedActorRecord& ActorRecord = Manifest->CreatedActors.AddDefaulted_GetRef();
				ActorRecord.ActorGuid = Actor->GetActorGuid();
				ActorRecord.ActorPath = FSoftObjectPath(Actor);
			}

			if (ADatasmithSceneActor* Candidate = Cast<ADatasmithSceneActor>(Actor);
				Candidate != nullptr && Candidate->Scene == &ImportedScene)
			{
				SceneActor = Candidate;
			}
		}

		if (SceneActor == nullptr)
		{
			OutError = TEXT("The verified import has no Datasmith Scene Actor to own the optimized output.");
			return false;
		}
		Manifest->DatasmithSceneActorGuid = SceneActor->GetActorGuid();
		Manifest->DatasmithSceneActorPath = FSoftObjectPath(SceneActor);

		for (const FGroupValue& Group : Plan.Groups)
		{
			const TArray<UInstancedStaticMeshComponent*>* Components = ComponentsByGroup.Find(Group.GroupId);
			if (Components == nullptr || Components->Num() != 1)
			{
				OutError = FString::Printf(TEXT("Group %s no longer has exactly one verified component."), *Group.GroupId);
				return false;
			}

			UInstancedStaticMeshComponent* Component = (*Components)[0];
			UStaticMesh* ImportedMesh = nullptr;
			TArray<UMaterialInterface*> ImportedMaterials;
			if (!ResolveExpectedAssets(Group, ImportedScene, ImportedMesh, ImportedMaterials, OutError))
			{
				return false;
			}

			FConVerseOptimizedImportGroupRecord& GroupRecord = Manifest->Groups.AddDefaulted_GetRef();
			GroupRecord.GroupId = Group.GroupId;
			GroupRecord.SerializedGroupKey = Group.SerializedKey;
			GroupRecord.OutputComponentPath = FSoftObjectPath(Component);
			GroupRecord.OutputOwnerActorGuid = Component->GetOwner()->GetActorGuid();
			GroupRecord.OutputOwnerActorPath = FSoftObjectPath(Component->GetOwner());
			GroupRecord.RequestedComponentType = Manifest->Options.ComponentType;
			GroupRecord.DatasmithMeshElementReference = Group.MeshElementName;
			GroupRecord.ImportedStaticMeshPath = FSoftObjectPath(ImportedMesh);
			GroupRecord.ComponentSettings.Layer = Group.Layer;
			GroupRecord.ComponentSettings.bVisible = Component->IsVisible();
			GroupRecord.ComponentSettings.bCastShadow = Component->CastShadow;
			GroupRecord.ComponentSettings.Mobility = Component->Mobility;
			GroupRecord.ComponentSettings.bIsAComponent = Group.bIsComponent;
			GroupRecord.ParentHierarchyPath = Group.ParentHierarchyPath;
			GroupRecord.GroupWorldTransform = Group.GroupWorldTransform;
			GroupRecord.ExpectedInstanceCount = Group.Candidates.Num();
			GroupRecord.FirstInstanceRecordIndex = Manifest->Instances.Num();
			GroupRecord.InstanceRecordCount = Group.Candidates.Num();

			TArray<int32> SlotIds;
			BuildImportedSlotIds(*ImportedMesh, SlotIds);
			for (int32 SlotIndex = 0; SlotIndex < ImportedMaterials.Num(); ++SlotIndex)
			{
				const FEffectiveMaterialValue* Effective = FindEffectiveMaterial(
					Group, SlotIndex, SlotIds.IsValidIndex(SlotIndex) ? SlotIds[SlotIndex] : INDEX_NONE);
				FConVerseOptimizedMaterialSlotRecord& SlotRecord = GroupRecord.MaterialSlots.AddDefaulted_GetRef();
				SlotRecord.SlotIndex = SlotIndex;
				SlotRecord.DatasmithMaterialElementName = Effective != nullptr ? Effective->MaterialElementName : FString();
				SlotRecord.ImportedMaterialPath = FSoftObjectPath(ImportedMaterials[SlotIndex]);
			}

			for (int32 InstanceIndex = 0; InstanceIndex < Group.Candidates.Num(); ++InstanceIndex)
			{
				const FCandidateValue& Candidate = Group.Candidates[InstanceIndex];
				FConVerseOptimizedImportInstanceRecord& InstanceRecord = Manifest->Instances.AddDefaulted_GetRef();
				InstanceRecord.GroupId = Group.GroupId;
				InstanceRecord.InstanceIndex = InstanceIndex;
				InstanceRecord.SourceElementName = Candidate.SourceElementName;
				InstanceRecord.SourceElementLabel = Candidate.SourceLabel;
				InstanceRecord.SourceHierarchyPath = Candidate.HierarchyPath;
				InstanceRecord.SourceTraversalOrdinal = Candidate.SourceOrdinal;
				InstanceRecord.SourceWorldTransform = Candidate.WorldTransform;
				InstanceRecord.SourceRelativeTransform = Candidate.RelativeTransform;
				InstanceRecord.SourceActorTags = Candidate.Tags;
				InstanceRecord.SourceIdentityStatus = ToManifestIdentityStatus(Candidate.IdentityStatus);
				for (const FMetadataValue& Metadata : Candidate.Metadata)
				{
					FConVerseOptimizedMetadataPair& Pair = InstanceRecord.Metadata.AddDefaulted_GetRef();
					Pair.Key = Metadata.Name;
					Pair.Value = Metadata.Value;
				}
				if (Candidate.IdentityStatus == ESourceIdentityStatus::Explicit && !Candidate.IdentityCandidates.IsEmpty())
				{
					InstanceRecord.SelectedSourceIdentityKey = Candidate.IdentityCandidates[0].Key;
					InstanceRecord.SelectedSourceIdentityValue = Candidate.IdentityCandidates[0].Value;
				}
				else if (Candidate.IdentityStatus == ESourceIdentityStatus::Ambiguous)
				{
					for (const FSourceIdentityValue& Identity : Candidate.IdentityCandidates)
					{
						FConVerseOptimizedMetadataPair& Pair = InstanceRecord.ConflictingIdentityCandidates.AddDefaulted_GetRef();
						Pair.Key = Identity.Key;
						Pair.Value = Identity.Value;
					}
				}
			}
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> AttemptAssetData;
		Registry.GetAssetsByPath(FName(*Inventory.AttemptFolder), AttemptAssetData, true, false);
		TSet<UObject*> OwnedAssets;
		OwnedAssets.Add(&ImportedScene);
		for (const FAssetData& AssetData : AttemptAssetData)
		{
			if (UObject* Asset = AssetData.GetAsset())
			{
				OwnedAssets.Add(Asset);
				FConVerseOptimizedCreatedAssetRecord& AssetRecord = Manifest->CreatedAssets.AddDefaulted_GetRef();
				AssetRecord.AssetPath = FSoftObjectPath(Asset);
				AssetRecord.PackageName = Asset->GetOutermost()->GetFName();
				Manifest->CreatedPackageNames.AddUnique(AssetRecord.PackageName);
			}
		}

		ImportedScene.AddAssetUserData(Manifest);
		Manifest->ManifestObjectPath = FSoftObjectPath(Manifest);
		Result.ManifestAssetPath = Manifest->ManifestObjectPath;

		TArray<UConVerseOptimizedAssetMarker*> Markers;
		for (UObject* Asset : OwnedAssets)
		{
			IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(Asset);
			if (AssetUserData == nullptr)
			{
				continue;
			}
			Asset->Modify();
			AssetUserData->RemoveUserDataOfClass(UConVerseOptimizedAssetMarker::StaticClass());
			UConVerseOptimizedAssetMarker* Marker = NewObject<UConVerseOptimizedAssetMarker>(
				Asset,
				NAME_None,
				RF_Transactional);
			Marker->ManifestId = Manifest->ManifestId;
			Marker->SessionId = Result.SessionId;
			Marker->SourceFilePath = Plan.SourceFilePath;
			Marker->DatasmithSceneAssetPath = FSoftObjectPath(&ImportedScene);
			AssetUserData->AddAssetUserData(Marker);
			Markers.Add(Marker);
			Asset->MarkPackageDirty();
		}

		// Remove the ordinary scene synchronization link only after the manifest and
		// ownership markers are complete. Optimized reimport is then the sole update path.
		SceneActor->Modify();
		SceneActor->Tags.AddUnique(FName(ManagedTag));
		SceneActor->Tags.AddUnique(FName(*FString::Printf(TEXT("%s%s"), SessionTagPrefix, *Result.SessionId)));
		SceneActor->Tags.AddUnique(FName(*FString::Printf(TEXT("%s%s"), PlanTagPrefix, *Plan.PlanId)));
		SceneActor->Scene = nullptr;
		SceneActor->MarkPackageDirty();

		Manifest->CommitState = EConVerseOptimizedImportCommitState::Active;
		for (UConVerseOptimizedAssetMarker* Marker : Markers)
		{
			Marker->CommitState = EConVerseOptimizedImportCommitState::Active;
		}
		ImportedScene.MarkPackageDirty();
		return true;
	}

	static UConVerseOptimizedImportManifest* FindActiveManifest(
		const FString& SourceFilePath,
		const FString& DestinationPath,
		FString* OutUnreadableManifestError = nullptr)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*DestinationPath), Assets, true, false);
		UConVerseOptimizedImportManifest* FoundManifest = nullptr;
		for (const FAssetData& AssetData : Assets)
		{
			if (AssetData.AssetClassPath != UDatasmithScene::StaticClass()->GetClassPathName()) continue;
			UDatasmithScene* Scene = Cast<UDatasmithScene>(AssetData.GetAsset());
			if (Scene == nullptr)
			{
				continue;
			}
			UConVerseOptimizedImportManifest* Manifest = Cast<UConVerseOptimizedImportManifest>(
				Scene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()));
			if (Manifest == nullptr)
			{
				continue;
			}
			if (Manifest->CommitState != EConVerseOptimizedImportCommitState::Active
				|| !Manifest->CanonicalSourceFilePath.Equals(SourceFilePath, ESearchCase::IgnoreCase)
				|| !Manifest->DestinationContentFolder.Equals(DestinationPath, ESearchCase::CaseSensitive))
			{
				continue;
			}
			// Fail closed on an unrecognized schema. Treating a newer manifest as active would let this
			// build supersede a session it cannot fully interpret, and supersede destroys actors.
			// Returning nullptr is NOT safe here: the caller would treat the destination as unowned and
			// import a duplicate session alongside the newer one. The refusal must be reported instead.
			if (Manifest->ManifestSchemaVersion > ContractVersion)
			{
				UE_LOG(LogConVerseOptimizedImport, Warning,
					TEXT("ConVerseHISM: manifest '%s' has schema version %d; this build supports up to version %d. ")
					TEXT("Reimport is blocked rather than risking destructive supersede of a newer session."),
					*Manifest->GetName(), Manifest->ManifestSchemaVersion, ContractVersion);
				if (OutUnreadableManifestError != nullptr)
				{
					*OutUnreadableManifestError = FString::Printf(
						TEXT("The existing optimized session at %s was written with manifest schema version %d, ")
						TEXT("but this build supports up to version %d. Update the plugin to reimport this destination."),
						*DestinationPath, Manifest->ManifestSchemaVersion, ContractVersion);
				}
				return nullptr;
			}
			if (FoundManifest)
			{
				if (OutUnreadableManifestError) *OutUnreadableManifestError = TEXT("Multiple active manifests claim this source and destination. Resolve ownership before importing.");
				return nullptr;
			}
			FoundManifest = Manifest;
		}
		return FoundManifest;
	}

	/** Resolves the prior session's world actors. Destruction is irreversible, so this runs before any mutation. */
	static bool PreflightSupersedeSession(
		UWorld& World,
		const UConVerseOptimizedImportManifest& PreviousManifest,
		TArray<AActor*>& OutActors,
		FString& OutError)
	{
		OutActors.Reset();
		const FSoftObjectPath WorldPath(&World);
		if (PreviousManifest.WorldPath.IsValid() && PreviousManifest.WorldPath != WorldPath)
		{
			OutError = FString::Printf(
				TEXT("The active optimized import owns actors in %s, but the current editor world is %s. ")
				TEXT("Open the owning level before reimporting."),
				*PreviousManifest.WorldPath.ToString(), *WorldPath.ToString());
			return false;
		}

		TMap<FGuid, AActor*> ActorsByGuid;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			ActorsByGuid.Add(It->GetActorGuid(), *It);
		}

		FString Missing;
		for (const FConVerseOptimizedCreatedActorRecord& Record : PreviousManifest.CreatedActors)
		{
			AActor* Actor = ActorsByGuid.FindRef(Record.ActorGuid);
			if (Actor == nullptr)
			{
				Actor = Cast<AActor>(Record.ActorPath.ResolveObject());
			}
			if (!IsValid(Actor))
			{
				// A user already removed this actor by hand. Record it, but do not fail the reimport.
				Missing += FString::Printf(TEXT("\n  already missing: %s"), *Record.ActorPath.ToString());
				continue;
			}
			OutActors.AddUnique(Actor);
		}

		if (!Missing.IsEmpty())
		{
			UE_LOG(LogConVerseOptimizedImport, Warning,
				TEXT("Optimized reimport preflight found prior-session actors that no longer exist:%s"), *Missing);
		}
		return true;
	}

	/** Destroys the prior session's actors deepest-child-first and reports anything left behind. */
	static bool RemoveSupersededSession(
		UWorld& World,
		TArray<AActor*>& Actors,
		int32& OutRemovedCount,
		FString& OutDetails)
	{
		OutRemovedCount = 0;
		Actors.Sort([](const AActor& Left, const AActor& Right)
		{
			const auto Depth = [](const AActor& Actor)
			{
				int32 Value = 0;
				for (const AActor* Parent = Actor.GetAttachParentActor(); Parent != nullptr; Parent = Parent->GetAttachParentActor())
				{
					++Value;
				}
				return Value;
			};
			return Depth(Left) > Depth(Right);
		});

		for (AActor* Actor : Actors)
		{
			if (IsValid(Actor) && World.DestroyActor(Actor, true, true))
			{
				++OutRemovedCount;
			}
		}

		int32 Remaining = 0;
		for (AActor* Actor : Actors)
		{
			if (IsValid(Actor))
			{
				++Remaining;
				OutDetails += FString::Printf(TEXT("\n  prior-session actor was not removed: %s"), *Actor->GetPathName());
			}
		}
		return Remaining == 0;
	}

	/** Re-states the prior manifest and its asset ownership markers so they are no longer treated as active. */
	static void MarkManifestSuperseded(UConVerseOptimizedImportManifest& PreviousManifest)
	{
		PreviousManifest.Modify();
		PreviousManifest.CommitState = EConVerseOptimizedImportCommitState::Superseded;

		for (const FConVerseOptimizedCreatedAssetRecord& Record : PreviousManifest.CreatedAssets)
		{
			UObject* Asset = Record.AssetPath.ResolveObject();
			IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(Asset);
			if (Asset == nullptr || AssetUserData == nullptr)
			{
				continue;
			}
			UConVerseOptimizedAssetMarker* Marker = Cast<UConVerseOptimizedAssetMarker>(
				AssetUserData->GetAssetUserDataOfClass(UConVerseOptimizedAssetMarker::StaticClass()));
			if (Marker == nullptr || Marker->ManifestId != PreviousManifest.ManifestId)
			{
				continue;
			}
			Asset->Modify();
			Marker->CommitState = EConVerseOptimizedImportCommitState::Superseded;
			Asset->MarkPackageDirty();
		}

		if (UObject* Outer = PreviousManifest.GetOuter())
		{
			Outer->MarkPackageDirty();
		}
	}

	/**
	 * Re-verifies an already-committed active session against its own manifest. Read-only: it resolves
	 * nothing new and mutates neither the world nor any package. Used by the AlreadyCurrent path so an
	 * unchanged source still reports drift in committed output.
	 */
	static bool ReverifyCommittedSession(
		const UConVerseOptimizedImportManifest& Manifest,
		FConVerseOptimizedImportResult& Result)
	{
		bool bAllPassed = true;
		int32 VerifiedInstanceTotal = 0;

		for (const FConVerseOptimizedImportGroupRecord& Group : Manifest.Groups)
		{
			FConVerseOptimizedGroupVerification& Verification = Result.GroupVerification.AddDefaulted_GetRef();
			Verification.GroupId = Group.GroupId;
			Verification.ExpectedInstances = Group.ExpectedInstanceCount;
			Verification.ComponentPath = Group.OutputComponentPath.ToString();

			UInstancedStaticMeshComponent* Component =
				Cast<UInstancedStaticMeshComponent>(Group.OutputComponentPath.ResolveObject());
			if (!IsValid(Component))
			{
				Verification.Details = TEXT("the committed output component no longer resolves");
				bAllPassed = false;
				continue;
			}

			Verification.ComponentClass = Component->GetClass()->GetName();
			Verification.ActualInstances = Component->GetInstanceCount();

			const UClass* ExpectedClass = Group.RequestedComponentType == EConVerseOptimizedManifestComponentType::HISM
				? UHierarchicalInstancedStaticMeshComponent::StaticClass()
				: UInstancedStaticMeshComponent::StaticClass();

			FString Failure;
			if (Component->GetClass() != ExpectedClass)
			{
				Failure = FString::Printf(TEXT("expected exact %s class, found %s"),
					*ExpectedClass->GetName(), *Component->GetClass()->GetName());
			}
			else if (Component->GetInstanceCount() != Group.ExpectedInstanceCount)
			{
				Failure = FString::Printf(TEXT("expected %d instances, found %d"),
					Group.ExpectedInstanceCount, Component->GetInstanceCount());
			}
			else if (FSoftObjectPath(Component->GetStaticMesh()) != Group.ImportedStaticMeshPath)
			{
				Failure = TEXT("component mesh no longer matches the manifest record");
			}
			else
			{
				for (const FConVerseOptimizedMaterialSlotRecord& Slot : Group.MaterialSlots)
				{
					if (Slot.SlotIndex < 0
						|| Slot.SlotIndex >= Component->GetNumMaterials()
						|| FSoftObjectPath(Component->GetMaterial(Slot.SlotIndex)) != Slot.ImportedMaterialPath)
					{
						Failure = FString::Printf(TEXT("material mismatch at slot %d"), Slot.SlotIndex);
						break;
					}
				}
			}

			Verification.bPassed = Failure.IsEmpty();
			if (Verification.bPassed)
			{
				++Result.VerifiedGroupCount;
				VerifiedInstanceTotal += Verification.ActualInstances;
			}
			else
			{
				Verification.Details = Failure;
				bAllPassed = false;
			}
		}

		for (const FConVerseOptimizedCreatedActorRecord& Record : Manifest.CreatedActors)
		{
			if (!IsValid(Cast<AActor>(Record.ActorPath.ResolveObject())))
			{
				FConVerseOptimizedGroupVerification& Verification = Result.GroupVerification.AddDefaulted_GetRef();
				Verification.GroupId = TEXT("<created actor>");
				Verification.ComponentPath = Record.ActorPath.ToString();
				Verification.Details = TEXT("a committed actor from this session no longer exists");
				bAllPassed = false;
			}
		}

		Result.VerifiedInstanceCount = VerifiedInstanceTotal;
		TArray<FString> Differences;
		if (!ConVerseImportProcessing::CheckState(Manifest, Differences))
		{
			bAllPassed = false;
			Result.Diagnostics.Append(Differences);
		}
		return bAllPassed;
	}

	static FMutationInventory CaptureMutationInventory(UWorld& World, const FString& AttemptFolder)
	{
		FMutationInventory Inventory;
		Inventory.AttemptFolder = AttemptFolder;
		Inventory.bWorldPackageWasDirty = World.GetOutermost()->IsDirty();
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			Inventory.ExistingActors.Add(*It);
		}
		return Inventory;
	}

	/**
	 * Unwinds an attempt back to the captured inventory.
	 *
	 * bObstructForTesting is a test-only seam: it skips destroying the actors this attempt created
	 * so the verification sweep below genuinely finds them still present and reports failure on its
	 * own terms. The return value is never faked; the RollbackFailed degradation is reached because
	 * the rollback really did leave objects behind.
	 */
	static bool RollBackAttempt(
		UWorld& World,
		const FMutationInventory& Inventory,
		const FString& SessionId,
		FConVerseOptimizedImportResult& Result,
		FString& OutDetails,
		bool bObstructForTesting = false)
	{
		Result.bRollbackAttempted = true;
		const FString SessionTag = FString::Printf(TEXT("%s%s"), SessionTagPrefix, *SessionId);
		TArray<AActor*> NewActors;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			AActor* Actor = *It;
			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Component : Components)
			{
				if (IsValid(Component) && HasTag(*Component, SessionTag))
				{
					Component->DestroyComponent();
				}
			}
			if (!Inventory.ExistingActors.Contains(Actor))
			{
				NewActors.Add(Actor);
			}
		}
		NewActors.Sort([](const AActor& Left, const AActor& Right)
		{
			const auto Depth = [](const AActor& Actor)
			{
				int32 Value = 0;
				for (const AActor* Parent = Actor.GetAttachParentActor(); Parent != nullptr; Parent = Parent->GetAttachParentActor())
				{
					++Value;
				}
				return Value;
			};
			return Depth(Left) > Depth(Right);
		});
		for (AActor* Actor : NewActors)
		{
			if (IsValid(Actor) && !bObstructForTesting)
			{
				World.DestroyActor(Actor, true, true);
			}
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> AttemptAssets;
		Registry.GetAssetsByPath(FName(*Inventory.AttemptFolder), AttemptAssets, true, false);
		TArray<UObject*> AssetsToDelete;
		for (const FAssetData& AssetData : AttemptAssets)
		{
			if (UObject* Asset = AssetData.GetAsset())
			{
				AssetsToDelete.Add(Asset);
			}
		}
		Result.CreatedObjectCount = NewActors.Num() + AttemptAssets.Num();
		// When obstructing, the created actors are still alive and still reference these assets.
		// Force-deleting them anyway would null out live references, which is a worse state than
		// the one under test, so the obstruction leaves the whole attempt intact.
		if (!AssetsToDelete.IsEmpty() && !bObstructForTesting)
		{
			ObjectTools::ForceDeleteObjects(AssetsToDelete, false);
		}

		TArray<FAssetData> RemainingAssets;
		Registry.GetAssetsByPath(FName(*Inventory.AttemptFolder), RemainingAssets, true, false);
		int32 RemainingActors = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (!Inventory.ExistingActors.Contains(*It))
			{
				++RemainingActors;
				OutDetails += FString::Printf(TEXT("\n  remaining actor: %s"), *It->GetPathName());
			}
		}
		for (const FAssetData& Asset : RemainingAssets)
		{
			OutDetails += FString::Printf(TEXT("\n  remaining asset: %s"), *Asset.GetObjectPathString());
		}
		Result.RemainingObjectCount = RemainingActors + RemainingAssets.Num();
		if (!Inventory.bWorldPackageWasDirty && Result.RemainingObjectCount == 0)
		{
			World.GetOutermost()->SetDirtyFlag(false);
		}
		Result.bRollbackSucceeded = Result.RemainingObjectCount == 0;
		if (Result.bRollbackSucceeded)
		{
			Result.LastCompletedStage = EConVerseOptimizedImportStage::RolledBack;
		}
		return Result.bRollbackSucceeded;
	}

	static void PopulatePlanResult(const FPlan& Plan, FConVerseOptimizedImportResult& Result)
	{
		Result.PlanId = Plan.PlanId;
		TMap<FString, int32> AppearanceIndices;
		for (int32 Index = 0; Index < Result.Appearances.Num(); ++Index) AppearanceIndices.Add(Result.Appearances[Index].SourceElement, Index);
		for (const auto& Source : Plan.AllMeshActors)
		{
			TSet<FString> Used;
			for (const auto& Slot : Source.EffectiveMaterials) Used.Add(Slot.MaterialElementName);
			for (const auto& Name : Used) if (const int32* Index = AppearanceIndices.Find(Name)) ++Result.Appearances[*Index].AffectedSourceElements;
		}
		TSet<FString> ConvertedElements;
		for (const auto& Group : Plan.Groups)
			for (const auto& Candidate : Group.Candidates) ConvertedElements.Add(Candidate.SourceElementName);
		for (const auto& Source : Plan.AllMeshActors)
		{
			const auto& Candidate = Source.Candidates[0];
			auto& Row = Result.InspectionRows.AddDefaulted_GetRef();
			Row.SourceElement = Candidate.SourceElementName;
			for (const auto& Identity : Candidate.IdentityCandidates)
				Row.SourceIdentity += (Row.SourceIdentity.IsEmpty() ? FString() : TEXT("; ")) + Identity.Key + TEXT("=") + Identity.Value;
			Row.Label = Candidate.SourceLabel;
			Row.MeshElement = Candidate.MeshElementName;
			Row.Outcome = ConvertedElements.Contains(Candidate.SourceElementName) ? TEXT("Planned instance")
				: Plan.Options.Processing.KeepOrdinaryMeshElements.Contains(Candidate.MeshElementName) ? TEXT("Ordinary: explicit exception")
				: TEXT("Ordinary: not eligible or below threshold");
		}
		Result.SourceFileHash = Plan.SourceFileHash;
		Result.PlannedGroupCount = Plan.Groups.Num();
		Result.TotalSourceMeshActors = Plan.TotalMeshActors;
		Result.EligibleSourceActors = Plan.EligibleLeafActors;
		for (const FGroupValue& Group : Plan.Groups)
		{
			Result.PlannedInstanceCount += Group.Candidates.Num();
		}
		for (const TPair<EConVerseOptimizedSkipReason, int32>& Pair : Plan.SkipCounts)
		{
			FConVerseOptimizedSkipCount& Count = Result.SkipCounts.AddDefaulted_GetRef();
			Count.Reason = Pair.Key;
			Count.Count = Pair.Value;
			Result.SkippedActorCount += Pair.Value;
			if (Pair.Key == EConVerseOptimizedSkipReason::BelowMinimumInstanceCount)
			{
				Result.BelowThresholdActorCount = Pair.Value;
			}
		}
		Result.SkipCounts.Sort([](const FConVerseOptimizedSkipCount& Left, const FConVerseOptimizedSkipCount& Right)
		{
			return static_cast<uint8>(Left.Reason) < static_cast<uint8>(Right.Reason);
		});
	}

	static FString BuildReport(const FPlan* Plan, const FConVerseOptimizedImportResult& Result)
	{
		FString Report = FString::Printf(
			TEXT("Optimized Datasmith Import\n\nStatus: %s\nSummary: %s\nPlan: %s\nSession: %s\nSource MD5: %s\n"),
			*StatusName(Result.Status), *Result.Summary, *Result.PlanId,
			Result.SessionId.IsEmpty() ? TEXT("<analysis only>") : *Result.SessionId,
			*Result.SourceFileHash);
		if (Result.bWasReimport)
		{
			Report += FString::Printf(
				TEXT("Reimport: yes\nPrevious manifest: %s\nPrevious session: %s\nPrior-session actors removed: %d\n"),
				*Result.PreviousManifestId, *Result.PreviousSessionId, Result.RemovedPreviousActorCount);
		}
		Report += FString::Printf(TEXT("Duration: %.3f seconds\nMesh policy processing (including builds): %.3f seconds\nMesh compilation wait: %.3f seconds\nProcess peak physical memory (process lifetime): %llu bytes\n"), Result.DurationSeconds, Result.MeshProcessingSeconds, Result.MeshBuildSeconds, Result.ProcessPeakPhysicalBytes);
		Report += FString::Printf(
			TEXT("Source mesh actors: %d\nEligible leaf actors: %d\nPlanned groups: %d\nPlanned instances: %d\nVerified groups: %d\nVerified instances: %d\n"),
			Result.TotalSourceMeshActors, Result.EligibleSourceActors, Result.PlannedGroupCount,
			Result.PlannedInstanceCount, Result.VerifiedGroupCount, Result.VerifiedInstanceCount);
		for (const FConVerseOptimizedSkipCount& Count : Result.SkipCounts)
		{
			Report += FString::Printf(TEXT("Skipped %s: %d\n"), *SkipReasonName(Count.Reason), Count.Count);
		}
		Report += FString::Printf(TEXT("Source mesh assets: %d\nSource lights: %d\nEnabled local lights: %d\nUnitless lights: %d\nIES lights: %d\nNanite meshes enabled: %d\nNanite exclusions: %d\nVerified ordinary meshes: %d\n"),
			Result.SourceMeshAssetCount, Result.SourceLightCount, Result.EnabledLocalLights, Result.UnitlessLights, Result.IESLights, Result.NaniteEnabledMeshes, Result.NaniteSkippedMeshes, Result.VerifiedOrdinaryMeshes);
		for (const FString& Diagnostic : Result.Diagnostics) Report += Diagnostic + TEXT("\n");
		if (Plan != nullptr)
		{
			Report +=  FString::Printf(TEXT("Source: %s\nDestination: %s\nOutput: %s\n"),
				*Plan->SourceFilePath, *Plan->Options.DestinationPath, *InstanceTypeName(Plan->Options.InstanceType));
		}
		for (const FConVerseOptimizedGroupVerification& Group : Result.GroupVerification)
		{
			Report += FString::Printf(TEXT("%s [%s] expected=%d actual=%d class=%s component=%s: %s\n"),
				Group.bPassed ? TEXT("PASS") : TEXT("FAIL"), *Group.GroupId,
				Group.ExpectedInstances, Group.ActualInstances, *Group.ComponentClass,
				*Group.ComponentPath, *Group.Details);
		}
		if (Result.bRollbackAttempted)
		{
			Report += FString::Printf(TEXT("Rollback attempted: yes\nRollback succeeded: %s\nCreated objects: %d\nRemaining objects: %d\n"),
				Result.bRollbackSucceeded ? TEXT("yes") : TEXT("no"), Result.CreatedObjectCount, Result.RemainingObjectCount);
		}
		return Report;
	}

	static FString JsonEscape(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Value;
	}

	/**
	 * True for outcomes where the user got what they asked for. Everything else is a failure the
	 * import log and the Output Log must make visible.
	 *
	 * AlreadyCurrent is a success only when its re-verification passed: an AlreadyCurrent that found
	 * drift means the committed output no longer matches the manifest, which is exactly the silent
	 * failure this log exists to surface.
	 *
	 * AcceptedWithFailedVerification is NOT a success, even though the user chose it and the output
	 * was kept. The log records whether the checks passed, not whether the outcome was intended.
	 * AwaitingFailedVerificationDecision is likewise not a success: it is not even an outcome yet.
	 */
	static bool IsSuccessOutcome(const FConVerseOptimizedImportResult& Result)
	{
		switch (Result.Status)
		{
		case EConVerseOptimizedImportStatus::Verified:
		case EConVerseOptimizedImportStatus::AnalysisSucceeded:
			return true;
		case EConVerseOptimizedImportStatus::AlreadyCurrent:
			return Result.bVerificationSucceeded && !Result.bSidecarChanged;
		default:
			return false;
		}
	}

	static const TCHAR* SeverityName(const FConVerseOptimizedImportResult& Result)
	{
		if (Result.Status == EConVerseOptimizedImportStatus::RollbackFailed)
		{
			return TEXT("Error");
		}
		return IsSuccessOutcome(Result) ? TEXT("Info") : TEXT("Warning");
	}

	static const TCHAR* OperationName(EConVerseOptimizedImportOperation Operation)
	{
		switch (Operation)
		{
		case EConVerseOptimizedImportOperation::Analyze: return TEXT("Analyze");
		case EConVerseOptimizedImportOperation::ResolveFailedVerification: return TEXT("ResolveFailedVerification");
		default: return TEXT("ImportAndVerify");
		}
	}

	/** RFC 4180: wrap in quotes and double any embedded quote. Summaries contain commas and newlines. */
	static FString CsvField(const FString& Input)
	{
		FString Value = Input;
		Value.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Value);
	}

	static FString ImportLogPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/ImportLog.csv");
	}

	/**
	 * Appends one row per attempt to the import log.
	 *
	 * Deliberately best-effort and non-fatal: an import that actually succeeded must never be
	 * reported as failed because a log file was read-only or locked by a spreadsheet. A single
	 * warning is emitted and the outcome is left untouched.
	 */
	static void AppendImportLogRow(const FConVerseOptimizedImportResult& Result, const FDateTime& TimestampUtc)
	{
		const FString ImportLogFile = ImportLogPath();
		const bool bNeedsHeader = !FPaths::FileExists(ImportLogFile);

		FString Row;
		if (bNeedsHeader)
		{
			Row += TEXT("TimestampUtc,Operation,Status,Severity,Source,Destination,PlanId,SessionId,")
				TEXT("PlannedGroups,VerifiedGroups,PlannedInstances,VerifiedInstances,Summary,ReportPath\n");
		}
		Row += FString::Printf(
			TEXT("%s,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d,%d,%s,%s\n"),
			*CsvField(TimestampUtc.ToIso8601()),
			*CsvField(OperationName(Result.Operation)),
			*CsvField(StatusName(Result.Status)),
			*CsvField(SeverityName(Result)),
			*CsvField(Result.SourceFilePath),
			*CsvField(Result.DestinationPath),
			*CsvField(Result.PlanId),
			*CsvField(Result.SessionId),
			Result.PlannedGroupCount, Result.VerifiedGroupCount,
			Result.PlannedInstanceCount, Result.VerifiedInstanceCount,
			*CsvField(Result.Summary),
			*CsvField(Result.SavedReportPath));

		if (!FFileHelper::SaveStringToFile(
			Row, *ImportLogFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), EFileWrite::FILEWRITE_Append))
		{
			UE_LOG(LogConVerseOptimizedImport, Warning,
				TEXT("Could not append to the DatasmithHISM import log at %s. The import outcome itself is unaffected."),
				*ImportLogFile);
		}
	}

	static void SaveAndLogReport(FConVerseOptimizedImportResult& Result, bool bAppendToImportLog)
	{
		const FDateTime TimestampUtc = FDateTime::UtcNow();

		// Sessionless attempts previously fell back to the PlanId alone, so a repeatedly failing
		// import overwrote its own evidence every run. PlanId is deterministic for identical
		// options, so the collision was guaranteed rather than incidental.
		//
		// A timestamp alone is not sufficient: two attempts easily land in the same second, and
		// even millisecond resolution collides under automation. The process-lifetime counter makes
		// the name unique regardless of clock resolution; the timestamp remains for readability.
		static TAtomic<uint32> SessionlessReportCounter(0);
		const FString ReportId = Result.SessionId.IsEmpty()
			? FString::Printf(TEXT("%s-%03d_%u_%s"),
				*TimestampUtc.ToString(TEXT("%Y%m%d-%H%M%S")),
				TimestampUtc.GetMillisecond(),
				SessionlessReportCounter.IncrementExchange(),
				Result.PlanId.IsEmpty() ? *FGuid::NewGuid().ToString(EGuidFormats::Digits) : *Result.PlanId)
			: Result.SessionId;
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("DatasmithHISM/ImportReports");
		IFileManager::Get().MakeDirectory(*Directory, true);
		Result.SavedReportPath = Directory / (ReportId + TEXT(".json"));
		const FString Json = FString::Printf(
			TEXT("{\n  \"status\": \"%s\",\n  \"planId\": \"%s\",\n  \"sessionId\": \"%s\",\n  \"summary\": \"%s\",\n  \"report\": \"%s\"\n}\n"),
			*StatusName(Result.Status), *JsonEscape(Result.PlanId), *JsonEscape(Result.SessionId),
			*JsonEscape(Result.Summary), *JsonEscape(Result.Report));
		if (!FFileHelper::SaveStringToFile(Json, *Result.SavedReportPath))
		{
			Result.SavedReportPath.Reset();
		}

		// Severity is the point: a failure must be visible when the Output Log is filtered to
		// warnings and errors, and RollbackFailed must stand out from ordinary failures.
		if (Result.Status == EConVerseOptimizedImportStatus::RollbackFailed)
		{
			UE_LOG(LogConVerseOptimizedImport, Error, TEXT("%s"), *Result.Report);
		}
		else if (!IsSuccessOutcome(Result))
		{
			UE_LOG(LogConVerseOptimizedImport, Warning, TEXT("%s"), *Result.Report);
		}
		else
		{
			UE_LOG(LogConVerseOptimizedImport, Display, TEXT("%s"), *Result.Report);
		}

		if (bAppendToImportLog)
		{
			AppendImportLogRow(Result, TimestampUtc);
		}
	}

	/**
	 * Progress frames in ImportAndVerify: hash, load, plan, preflight, transform, import, convert,
	 * verify, commit. Kept in sync with the EnterProgressFrame calls below.
	 */
	static constexpr float ImportProgressStepCount = 9.0f;

	static void EnterStage(FScopedSlowTask& Progress, float Work, const FText& Label)
	{
		UE_LOG(LogConVerseOptimizedImport, Display, TEXT("%s"), *Label.ToString());
		Progress.EnterProgressFrame(Work, Label);
	}


	/**
	 * Contract: cancellation is checked at defined checkpoints. Returning true means the caller must
	 * stop before performing any further mutation. Nothing has been mutated at the checkpoints that
	 * use this helper, so no rollback is required; later checkpoints route through RollBackAttempt.
	 */
	static bool WasCancelled(FScopedSlowTask& SlowTask, FConVerseOptimizedImportResult& Result, const TFunction<bool()>* Cancel = nullptr)
	{
		if (!SlowTask.ShouldCancel() && !(Cancel && *Cancel && (*Cancel)()))
		{
			return false;
		}
		Result.Status = EConVerseOptimizedImportStatus::CancelledRolledBack;
		Result.Summary = TEXT("The optimized import was cancelled before any changes were made.");
		return true;
	}


	/**
	 * An import that failed verification and is waiting for the user to accept or discard it.
	 *
	 * Held in memory only, for the lifetime of the editor process. A parked attempt owns real
	 * assets and actors, so it is deliberately NOT persisted: resuming a half-finished mutation
	 * across an editor restart, against a world that may have changed underneath it, is far more
	 * dangerous than losing the ability to accept. If the editor closes with an attempt pending,
	 * the objects remain in the level as ordinary unmanaged actors with no manifest claiming them,
	 * which is the same state a user would get from a plain Datasmith import.
	 *
	 * Raw pointers are safe here only because resolution revalidates every one of them before use.
	 */
	struct FPendingFailedVerification
	{
		FPlan Plan;
		FMutationInventory Inventory;
		FConVerseOptimizedImportResult Result;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDatasmithScene> ImportedScene;
		TWeakObjectPtr<UConVerseOptimizedImportManifest> PreviousManifest;
		FString FailureDetails;
	};

	static TMap<FString, TSharedPtr<FPendingFailedVerification>>& PendingFailedVerifications()
	{
		static TMap<FString, TSharedPtr<FPendingFailedVerification>> Pending;
		return Pending;
	}

	/**
	 * Builds and saves the report for a result.
	 *
	 * bTerminal distinguishes the final outcome of an attempt from an intermediate checkpoint. The
	 * Verified checkpoint calls this before commit so a report exists if commit later fails, then
	 * calls it again on the real exit — which would otherwise log the same attempt twice. The import
	 * log must have exactly one row per attempt, so only terminal calls append.
	 */
	static void FinishResult(const FPlan* Plan, FConVerseOptimizedImportResult& Result, bool bTerminal = true)
	{
		Result.DurationSeconds = FPlatformTime::Seconds() - Result.StartedAtSeconds;
		Result.ProcessPeakPhysicalBytes = FPlatformMemory::GetStats().PeakUsedPhysical;
		Result.Report = BuildReport(Plan, Result);
		SaveAndLogReport(Result, bTerminal);
		if (!FConVerseDatasmithImportService::WriteAttemptCheckpoint(Result, bTerminal))
			UE_LOG(LogConVerseOptimizedImport, Warning, TEXT("Could not update attempt recovery checkpoint for %s."), *Result.SessionId);
	}
	static bool FinishPreMutationCancellation(FConVerseImportProgress& Work, FConVerseOptimizedImportResult& Result)
	{
		if (!Work.IsCancelled()) return false;
		Result.Status = EConVerseOptimizedImportStatus::CancelledRolledBack;
		Result.Summary = TEXT("Analysis cancelled before mutation; no assets or actors were created.");
		Result.PlanId.Reset();
		Result.PlannedGroupCount = Result.PlannedInstanceCount = 0;
		Result.TotalSourceMeshActors = Result.EligibleSourceActors = Result.BelowThresholdActorCount = Result.SkippedActorCount = 0;
		Result.SourceLightCount = Result.SourceMeshAssetCount = Result.EnabledLocalLights = Result.UnitlessLights = Result.IESLights = 0;
		Result.InspectionRows.Reset(); Result.Appearances.Reset(); Result.SkipCounts.Reset();
		Result.SourceLightDescriptions.Reset(); Result.MaterialDecisions.Reset(); Result.Diagnostics.Reset();
		if (Result.LastCompletedStage == EConVerseOptimizedImportStage::PlanBuilt)
			Result.LastCompletedStage = EConVerseOptimizedImportStage::SourceLoaded;
		FinishResult(nullptr, Result);
		return true;
	}

}

TArray<FString> ConVerseDefaultTextureSearchFolders()
{
	FString Common = FPlatformMisc::GetEnvironmentVariable(TEXT("CommonProgramW6432"));
	if (Common.IsEmpty()) Common = FPlatformMisc::GetEnvironmentVariable(TEXT("CommonProgramFiles"));
	if (Common.IsEmpty()) return {};
	const FString Root = FPaths::Combine(Common.Replace(TEXT("\\"), TEXT("/")), TEXT("Autodesk Shared/Materials/Textures"));
	return { Root / TEXT("1/Mats"), Root / TEXT("2/Mats"), Root / TEXT("3/Mats") };
}

FConVerseOptimizedImportResult FConVerseDatasmithImportService::Analyze(const FConVerseOptimizedImportOptions& Options)
{
	using namespace ConVerseDatasmithImport;
	FConVerseOptimizedImportResult Result;
	Result.Operation = EConVerseOptimizedImportOperation::Analyze;
	// Raw input first so an InvalidOptions row still records what was asked for; overwritten with
	// the normalized values once normalization succeeds.
	Result.SourceFilePath = Options.FilePath;
	Result.DestinationPath = Options.DestinationPath;
	FConVerseOptimizedImportOptions Normalized;
	FString Error;
	if (!NormalizeAndValidateOptions(Options, false, Normalized, Error))
	{
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.SourceFilePath = Normalized.FilePath;
	Result.DestinationPath = Normalized.DestinationPath;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::OptionsValidated;

	FScopedSlowTask SlowTask(5.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeProgress", "Analyze Datasmith source"));
	if (!Normalized.bAutomated) SlowTask.MakeDialog(true);
	FConVerseImportProgress Work(Normalized, SlowTask);
	const auto Cancelled = [&]() { return FinishPreMutationCancellation(Work, Result); };
	EnterStage(SlowTask, 1.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeRead", "Reading source file..."));
	if (Cancelled()) return Result;
	FString SourceHash;
	int64 SourceSize = 0;
	if (!HashFile(Normalized.FilePath, SourceHash, SourceSize, Error, &SlowTask, &Normalized.CancelRequested, &Work))
	{
		if (Cancelled()) return Result;
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.LastCompletedStage = EConVerseOptimizedImportStage::SourceHashed;
	Result.SourceFileHash = SourceHash;

	// Fingerprint the sidecar folder as well. This is reported, not folded into PlanId.
	EnterStage(SlowTask, 1.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeAssets", "Checking supporting assets..."));
	if (Cancelled()) return Result;
	FSidecarFingerprint Sidecar;
	if (!HashDirectory(GetSidecarDirectory(Normalized.FilePath), Sidecar, Error, &SlowTask, &Normalized.CancelRequested, &Work))
	{
		if (Cancelled()) return Result;
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.SidecarHash = Sidecar.Hash;
	Result.SidecarTotalSize = Sidecar.TotalSize;
	Result.SidecarFileCount = Sidecar.FileCount;
	Result.bSidecarExists = Sidecar.bExists;

	EnterStage(SlowTask, 1.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeTranslate", "Loading/translating source (translator progress may be unavailable)..."));
	if (Cancelled()) return Result;
	if (!Work.Update(EConVerseImportWorkPhase::Translation, 0, 1)) { Cancelled(); return Result; }
	TSharedPtr<IDatasmithScene> Scene;
	const TSharedPtr<FExternalSource> Source = LoadFreshSource(
		Normalized.FilePath,
		Normalized.Tessellation,
		Scene,
		Result.bTessellationApplied,
		Error);
	Work.Update(EConVerseImportWorkPhase::Translation, 1, 1);
	if (Cancelled()) return Result;
	if (!Source.IsValid() || !Scene.IsValid())
	{
		if (Cancelled()) return Result;
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	ConVerseImportProcessing::ResolveMissingTextures(Scene.ToSharedRef(), Normalized, Result);
	ConVerseImportProcessing::AnalyzeScene(Scene.ToSharedRef(), Normalized, Result, Work);
	if (Cancelled()) return Result;
	const auto Dependencies = ConVerseImportProcessing::ValidateDependencies(Scene.ToSharedRef(), Normalized, Result, Error, Work);
	if (Cancelled()) return Result;
	if (Dependencies == EConVerseImportWorkResult::Failed)
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.bSourceLoaded = true;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::SourceLoaded;
	if (Cancelled()) return Result;
	EnterStage(SlowTask, 1.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeGroups", "Analyzing meshes and groups..."));
	const FPlan Plan = BuildPlan(Scene.ToSharedRef(), Normalized, SourceHash, SourceSize, Result.ResolvedTextures, Work);
	if (Cancelled()) return Result;

	PopulatePlanResult(Plan, Result);
	if (Cancelled()) return Result;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::PlanBuilt;
	Result.Status = Plan.Groups.IsEmpty()
		? EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups
		: EConVerseOptimizedImportStatus::AnalysisSucceeded;
	Result.Summary = Plan.Groups.IsEmpty()
		? TEXT("Analysis completed, but no safe group meets the current threshold.")
		: FString::Printf(TEXT("Analysis planned %d groups and %d instances."), Result.PlannedGroupCount, Result.PlannedInstanceCount);
	FString OwnershipError;
	if (const auto* Previous = FindActiveManifest(Normalized.FilePath, Normalized.DestinationPath, &OwnershipError))
	{
		TSet<FString> OldGroups, NewGroups;
		for (const auto& Group : Previous->Groups) OldGroups.Add(Group.GroupId);
		for (const auto& Group : Plan.Groups) NewGroups.Add(Group.GroupId);
		Result.Diagnostics.Add(FString::Printf(TEXT("Rebuild preview: %d groups added, %d removed, %d retained; %d ordinary source mesh elements planned."),
			NewGroups.Difference(OldGroups).Num(), OldGroups.Difference(NewGroups).Num(), OldGroups.Intersect(NewGroups).Num(), Plan.TotalMeshActors - Result.PlannedInstanceCount));
		TArray<FString> Differences;
		ConVerseImportProcessing::CheckState(*Previous, Differences);
		Result.Diagnostics.Append(Differences);
		if (Previous->SourceInventoryVersion != 1)
			Result.Diagnostics.Add(TEXT("Per-light/material change preview is unverified for this older manifest; an explicit rebuild will record the source inventory."));
		else
		{
			auto Preview = [&](const TCHAR* Kind, const TMap<FString, FString>& Before, const TMap<FString, FString>& After)
			{
				TSet<FString> UniqueNames;
				for (const auto& Pair : Before) UniqueNames.Add(Pair.Key);
				for (const auto& Pair : After) UniqueNames.Add(Pair.Key);
				TArray<FString> Names = UniqueNames.Array();
				Names.Sort();
				for (const FString& Name : Names)
				{
					const FString* Old = Before.Find(Name); const FString* New = After.Find(Name);
					if (!Old || !New || *Old != *New)
						Result.Diagnostics.Add(FString::Printf(TEXT("%s change %s\n  before: %s\n  after: %s"), Kind, *Name, Old ? **Old : TEXT("<absent>"), New ? **New : TEXT("<removed>")));
				}
			};
			Preview(TEXT("Material"), Previous->MaterialDecisions, Result.MaterialDecisions);
			Preview(TEXT("Light"), Previous->SourceLightDescriptions, Result.SourceLightDescriptions);
		}
	}
	if (!OwnershipError.IsEmpty()) Result.Diagnostics.Add(OwnershipError);
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> VersionAssets;
	Registry.GetAssetsByPath(FName(*Normalized.DestinationPath), VersionAssets, true, false);
	for (const auto& Asset : VersionAssets)
	{
		if (Asset.AssetClassPath != UDatasmithScene::StaticClass()->GetClassPathName()) continue;
		auto* VersionScene = Cast<UDatasmithScene>(Asset.GetAsset());
		const auto* Version = VersionScene ? Cast<UConVerseOptimizedImportManifest>(VersionScene->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass())) : nullptr;
		if (!Version || Version->ManifestSchemaVersion > ContractVersion || Version->CommitState != EConVerseOptimizedImportCommitState::Superseded
			|| !Version->CanonicalSourceFilePath.Equals(Normalized.FilePath, ESearchCase::IgnoreCase)) continue;
		int64 Bytes = 0;
		for (const FName& Package : Version->CreatedPackageNames)
			for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".ubulk"), TEXT(".uexp"), TEXT(".uptnl")})
				Bytes += FMath::Max(int64(0), IFileManager::Get().FileSize(*FPackageName::LongPackageNameToFilename(Package.ToString(), Extension)));
		Result.Diagnostics.Add(FString::Printf(TEXT("Retained superseded session %s: %d packages, %lld on-disk package/bulk bytes (excludes shared DDC; unsaved data has no disk size)."),
			*Version->SessionId, Version->CreatedPackageNames.Num(), Bytes));
	}

	EnterStage(SlowTask, 1.0f, NSLOCTEXT("ConVerseHISM", "AnalyzeReport", "Preparing analysis report..."));
	Work.Update(EConVerseImportWorkPhase::Report, 0, 1);
	if (Cancelled()) return Result;
	FinishResult(&Plan, Result);
	return Result;
}

FConVerseOptimizedImportResult FConVerseDatasmithImportService::ImportAndVerify(const FConVerseOptimizedImportOptions& Options)
{
	using namespace ConVerseDatasmithImport;
	FConVerseOptimizedImportResult Result;
	Result.Operation = EConVerseOptimizedImportOperation::ImportAndVerify;
	Result.SourceFilePath = Options.FilePath;
	Result.DestinationPath = Options.DestinationPath;
	FConVerseOptimizedImportOptions Normalized;
	FString Error;
	if (!NormalizeAndValidateOptions(Options, true, Normalized, Error))
	{
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.SourceFilePath = Normalized.FilePath;
	Result.DestinationPath = Normalized.DestinationPath;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::OptionsValidated;
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = TEXT("No editor world is open.");
		FinishResult(nullptr, Result);
		return Result;
	}

	// Contract: show stage and progress, and allow cancellation, for this long-running operation.
	// Cancellation is only honored before the point of no return; once the manifest is committed the
	// session is owned and must be superseded rather than abandoned.
	FScopedSlowTask SlowTask(
		ImportProgressStepCount,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportProgress", "Optimized Datasmith Import"));
	if (!Normalized.bAutomated)
	{
		SlowTask.MakeDialog(true);
	}
	FConVerseImportProgress Work(Normalized, SlowTask);
	const auto Cancelled = [&]() { return FinishPreMutationCancellation(Work, Result); };
	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportHashing", "Hashing the source file..."));

	FString InitialHash;
	int64 InitialSize = 0;
	if (!HashFile(Normalized.FilePath, InitialHash, InitialSize, Error, &SlowTask, &Normalized.CancelRequested, &Work))
	{
		if (Cancelled()) return Result;
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.LastCompletedStage = EConVerseOptimizedImportStage::SourceHashed;
	Result.SourceFileHash = InitialHash;

	// Fingerprinted here, ahead of the manifest lookup, so the AlreadyCurrent branch can compare it.
	FSidecarFingerprint Sidecar;
	if (!HashDirectory(GetSidecarDirectory(Normalized.FilePath), Sidecar, Error, &SlowTask, &Normalized.CancelRequested, &Work))
	{
		if (Cancelled()) return Result;
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.SidecarHash = Sidecar.Hash;
	Result.SidecarTotalSize = Sidecar.TotalSize;
	Result.SidecarFileCount = Sidecar.FileCount;
	Result.bSidecarExists = Sidecar.bExists;

	if (Cancelled()) return Result;

	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportLoading", "Loading the Datasmith source..."));
	if (!Work.Update(EConVerseImportWorkPhase::Translation, 0, 1)) { Cancelled(); return Result; }
	TSharedPtr<IDatasmithScene> Scene;
	TSharedPtr<FExternalSource> Source = LoadFreshSource(
		Normalized.FilePath,
		Normalized.Tessellation,
		Scene,
		Result.bTessellationApplied,
		Error);
	Work.Update(EConVerseImportWorkPhase::Translation, 1, 1);
	if (Cancelled()) return Result;
	if (!Source.IsValid() || !Scene.IsValid())
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	ConVerseImportProcessing::ResolveMissingTextures(Scene.ToSharedRef(), Normalized, Result);
	const auto Dependencies = ConVerseImportProcessing::ValidateDependencies(Scene.ToSharedRef(), Normalized, Result, Error, Work);
	if (Cancelled()) return Result;
	if (Dependencies == EConVerseImportWorkResult::Failed)
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(nullptr, Result);
		return Result;
	}
	Result.bSourceLoaded = true;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::SourceLoaded;
	if (Cancelled()) return Result;

	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportPlanning", "Traversing the scene and planning groups..."));
	const FPlan Plan = BuildPlan(Scene.ToSharedRef(), Normalized, InitialHash, InitialSize, Result.ResolvedTextures, Work);
	if (Cancelled()) return Result;
	ConVerseImportProcessing::AnalyzeScene(Scene.ToSharedRef(), Normalized, Result, Work);
	if (Cancelled()) return Result;
	PopulatePlanResult(Plan, Result);
	Result.LastCompletedStage = EConVerseOptimizedImportStage::PlanBuilt;
	if (Cancelled()) return Result;
	if (Plan.Groups.IsEmpty() && Plan.TotalMeshActors == 0 && Result.SourceLightCount == 0)
	{
		Result.Status = EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups;
		Result.Summary = TEXT("Import stopped before mutation because the plan contains no eligible groups.");
		FinishResult(&Plan, Result);
		return Result;
	}

	// An active manifest for this exact source and destination turns this call into an optimized reimport.
	// The Datasmith import below can trigger garbage collection, so root the manifest for the whole transaction.
	FString UnreadableManifestError;
	TStrongObjectPtr<UConVerseOptimizedImportManifest> PreviousManifestGuard(FindActiveManifest(
		Normalized.FilePath,
		Normalized.DestinationPath,
		&UnreadableManifestError));
	if (!UnreadableManifestError.IsEmpty())
	{
		// The destination is owned by a session this build cannot interpret. Importing anyway would
		// leave two active sessions in the same destination, so refuse without mutating anything.
		Result.Status = EConVerseOptimizedImportStatus::OptimizedReimportBlocked;
		Result.Summary = UnreadableManifestError;
		FinishResult(&Plan, Result);
		return Result;
	}
	UConVerseOptimizedImportManifest* PreviousManifest = PreviousManifestGuard.Get();
	if (PreviousManifest != nullptr && PreviousManifest->Verification.bAcceptedWithFailedVerification)
	{
		// The destination is owned by a session that was accepted despite failing verification.
		// Superseding it would destroy actors based on records verification could not confirm, so
		// the only safe move is to refuse and let the user remove it deliberately.
		Result.Status = EConVerseOptimizedImportStatus::OptimizedReimportBlocked;
		Result.bWasReimport = true;
		Result.PreviousManifestId = PreviousManifest->ManifestId;
		Result.PreviousSessionId = PreviousManifest->SessionId;
		Result.Summary = FString::Printf(
			TEXT("This destination is owned by session %s, which was accepted despite failing verification. ")
			TEXT("Optimized reimport is refused because that session's records cannot be trusted to ")
			TEXT("identify what to remove. Delete its output manually, then import again.%s"),
			*PreviousManifest->SessionId, *PreviousManifest->Verification.AcceptedFailureDetails);
		FinishResult(&Plan, Result);
		return Result;
	}
	if (PreviousManifest != nullptr)
	{
		Result.bWasReimport = true;
		Result.PreviousManifestId = PreviousManifest->ManifestId;
		Result.PreviousSessionId = PreviousManifest->SessionId;
		if (PreviousManifest->PlanId == Plan.PlanId && !Normalized.bRebuildFromSource)
		{
			// Contract: an unchanged source runs verification only. This is read-only and never mutates the world.
			Result.Status = EConVerseOptimizedImportStatus::AlreadyCurrent;
			Result.bVerificationSucceeded = ReverifyCommittedSession(*PreviousManifest, Result);
			Result.InspectionRows = PreviousManifest->ImportedElements;
			Result.ManifestAssetPath = FSoftObjectPath(PreviousManifest);
			Result.ImportAssetPath = FSoftObjectPath(PreviousManifest->GetOuter());
			Result.LastCompletedStage = EConVerseOptimizedImportStage::Verified;

			// The PlanId matched, so the primary file is unchanged. The sidecar may not be: for CAD,
			// Revit, and IFC the geometry lives there, and re-exporting can rewrite it while leaving
			// the primary file byte-identical. An empty recorded hash means the manifest predates
			// sidecar tracking, which is "unknown", not "changed".
			Result.bSidecarChanged =
				!PreviousManifest->SidecarHash.IsEmpty()
				&& PreviousManifest->SidecarHash != Result.SidecarHash;
			Result.SidecarFileCountAtCommit = PreviousManifest->SidecarFileCount;

			Result.Summary = Result.bVerificationSucceeded
				? FString::Printf(
					TEXT("The active optimized import (manifest %s) already matches this source. ")
					TEXT("Re-verified %d groups and %d instances. No session was created and the world was not modified."),
					*PreviousManifest->ManifestId, Result.VerifiedGroupCount, Result.VerifiedInstanceCount)
				: FString::Printf(
					TEXT("The active optimized import (manifest %s) already matches this source, but re-verification ")
					TEXT("found drift in the committed output. Only %d of %d groups passed. The world was not modified."),
					*PreviousManifest->ManifestId, Result.VerifiedGroupCount, PreviousManifest->Groups.Num());

			if (Result.bSidecarChanged)
			{
				// Deliberately a warning, not an automatic reimport: superseding is destructive, so
				// the decision to rebuild belongs to the user.
				UE_LOG(LogConVerseOptimizedImport, Warning,
					TEXT("Sidecar assets changed for '%s' while the primary file did not. ")
					TEXT("The committed output may be stale. Recorded %d files / %lld bytes, found %d files / %lld bytes."),
					*Normalized.FilePath,
					PreviousManifest->SidecarFileCount, PreviousManifest->SidecarTotalSize,
					Result.SidecarFileCount, Result.SidecarTotalSize);

				Result.Summary += FString::Printf(
					TEXT(" WARNING: the source's sidecar assets changed (%d files / %lld bytes recorded, ")
					TEXT("%d files / %lld bytes now) even though the primary file did not. ")
					TEXT("The committed geometry may be out of date. Force a reimport to pick up the new assets."),
					PreviousManifest->SidecarFileCount, PreviousManifest->SidecarTotalSize,
					Result.SidecarFileCount, Result.SidecarTotalSize);
			}

			FinishResult(&Plan, Result);
			return Result;
		}
	}

	if (PreviousManifest && !Normalized.bReplaceManualEdits)
	{
		TArray<FString> Differences;
		if (!ConVerseImportProcessing::CheckState(*PreviousManifest, Differences))
		{
			Result.Status = EConVerseOptimizedImportStatus::ManualEditsDetected;
			Result.Diagnostics.Append(Differences);
			Result.Summary = TEXT("Rebuild would replace tracked manual edits or an unrecorded legacy baseline. Review the differences and explicitly replace or cancel.");
			FinishResult(&Plan, Result);
			return Result;
		}
	}

	if (!Normalized.bAutomated && Result.EnabledLocalLights >= Normalized.Processing.ManyLightThreshold)
	{
		const FText Warning = FText::Format(NSLOCTEXT("ConVerseHISM", "ManyLightsBeforeImport", "This source contains {0} enabled local lights. Review MegaLights in Project Settings > Rendering for this scene. The importer will preserve the exported light values and leave rendering settings unchanged.\n\nContinue importing?"), FText::AsNumber(Result.EnabledLocalLights));
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Warning) == EAppReturnType::Cancel)
		{
			Result.Status = EConVerseOptimizedImportStatus::CancelledRolledBack;
			Result.Summary = TEXT("Import cancelled before creating any output.");
			FinishResult(&Plan, Result);
			return Result;
		}
	}

	FString PreMutationHash;
	int64 PreMutationSize = 0;
	if (!HashFile(Normalized.FilePath, PreMutationHash, PreMutationSize, Error))
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceLoadFailed;
		Result.Summary = Error;
		FinishResult(&Plan, Result);
		return Result;
	}
	if (PreMutationHash != InitialHash || PreMutationSize != InitialSize)
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceChangedAfterAnalysis;
		Result.Summary = TEXT("The source file changed while the import plan was being built. No mutation occurred.");
		FinishResult(&Plan, Result);
		return Result;
	}

	TArray<FResolvedGroup> ResolvedGroups;
	if (!PreflightPlanApplication(Scene.ToSharedRef(), Plan, ResolvedGroups, Error))
	{
		Result.Status = EConVerseOptimizedImportStatus::SourceChangedAfterAnalysis;
		Result.Summary = Error;
		FinishResult(&Plan, Result);
		return Result;
	}
	Result.LastCompletedStage = EConVerseOptimizedImportStage::MutationPreflight;
	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportPreflight", "Preflighting destination and world changes..."));
	Result.SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	const FString AssetName = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Normalized.FilePath));
	const FString ConventionalObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Normalized.DestinationPath, *AssetName, *AssetName);
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FSoftObjectPath ConventionalPath(ConventionalObjectPath);
	const bool bConventionalPathIsOwned = PreviousManifest != nullptr
		&& PreviousManifest->ImportedDatasmithSceneAssetPath == ConventionalPath;
	if (!bConventionalPathIsOwned && Registry.GetAssetByObjectPath(ConventionalPath).IsValid())
	{
		Result.Status = EConVerseOptimizedImportStatus::OptimizedReimportBlocked;
		Result.Summary = FString::Printf(TEXT("An unrelated or unowned asset already occupies %s."), *ConventionalObjectPath);
		FinishResult(&Plan, Result);
		return Result;
	}

	const FString AttemptFolder = FString::Printf(TEXT("%s/%s_ConVerse_%s"),
		*Normalized.DestinationPath, *AssetName, *Result.SessionId.Left(12));
	TArray<FAssetData> ExistingAttemptAssets;
	Registry.GetAssetsByPath(FName(*AttemptFolder), ExistingAttemptAssets, true, false);
	if (!ExistingAttemptAssets.IsEmpty())
	{
		Result.Status = EConVerseOptimizedImportStatus::OptimizedReimportBlocked;
		Result.Summary = TEXT("The unique attempt destination unexpectedly contains assets.");
		FinishResult(&Plan, Result);
		return Result;
	}
	// Final cancellation checkpoint before the first mutation. Past this point the operation owns
	// partial state, so cancellation must unwind through RollBackAttempt instead of returning early.
	if (WasCancelled(SlowTask, Result, &Normalized.CancelRequested))
	{
		FinishResult(&Plan, Result);
		return Result;
	}
	Result.AttemptFolder = AttemptFolder;
	if (!WriteAttemptCheckpoint(Result, false))
	{
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = TEXT("Cannot write the attempt recovery checkpoint. Import stopped before mutation.");
		FinishResult(&Plan, Result);
		return Result;
	}
	const FMutationInventory Inventory = CaptureMutationInventory(*World, AttemptFolder);

	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportTransform", "Rewriting the scene into instanced groups..."));
	ApplyResolvedPlan(Scene.ToSharedRef(), Plan, ResolvedGroups, Result.SessionId);
	Result.LastCompletedStage = EConVerseOptimizedImportStage::SceneTransformed;
	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportDatasmith", "Running the Datasmith import..."));
	const FString PackageName = AttemptFolder / AssetName;
	TStrongObjectPtr<UPackage> Package(CreatePackage(*PackageName));
	TStrongObjectPtr<UDatasmithImportFactory> Factory(NewObject<UDatasmithImportFactory>());
	Factory->ResetState();
	TStrongObjectPtr<UAutomatedAssetImportData> AutomatedImportData;
	if (Normalized.bAutomated)
	{
		AutomatedImportData.Reset(NewObject<UAutomatedAssetImportData>());
		AutomatedImportData->GroupName = TEXT("DatasmithHISM Optimized Import");
		AutomatedImportData->Filenames = { Normalized.FilePath };
		AutomatedImportData->DestinationPath = AttemptFolder;
		AutomatedImportData->FactoryName = UDatasmithImportFactory::StaticClass()->GetPathName();
		AutomatedImportData->Factory = Factory.Get();
		AutomatedImportData->bReplaceExisting = false;
		Factory->SetAutomatedAssetImportData(AutomatedImportData.Get());
	}
	bool bCancelled = false;
	UObject* ImportedObject = Factory->CreateFromExternalSource(
		Factory->ResolveSupportedClass(), Package.Get(), FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional, Source.ToSharedRef(), nullptr, GWarn, bCancelled);
	UDatasmithScene* ImportedScene = Cast<UDatasmithScene>(ImportedObject);
	if (ImportedScene == nullptr || bCancelled)
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack
			? (bCancelled ? EConVerseOptimizedImportStatus::CancelledRolledBack : EConVerseOptimizedImportStatus::FailedRolledBack)
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = (bCancelled ? TEXT("Datasmith import was cancelled.") : TEXT("Datasmith import failed.")) + RollbackDetails;
		FinishResult(&Plan, Result);
		return Result;
	}

	Result.bImportSucceeded = true;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::DatasmithImported;
	WriteAttemptCheckpoint(Result, false);
	Result.ImportAssetPath = FSoftObjectPath(ImportedScene);
	FAssetRegistryModule::AssetCreated(ImportedScene);
	ImportedScene->MarkPackageDirty();

	// From here the attempt owns real assets and actors, so cancellation unwinds through rollback.
	if (SlowTask.ShouldCancel())
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::CancelledRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = TEXT("The optimized import was cancelled after the Datasmith import.") + RollbackDetails;
		FinishResult(&Plan, Result);
		return Result;
	}

	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportConvert", "Converting instanced components..."));
	TMap<FString, TArray<UInstancedStaticMeshComponent*>> ImportedComponents;
	GatherSessionComponents(*World, Result.SessionId, ImportedComponents);
	TArray<FComponentSnapshot> Snapshots;
	bool bInventoryValid = ImportedComponents.Num() == Plan.Groups.Num();
	if (bInventoryValid) bInventoryValid = ConVerseImportProcessing::ApplyMaterials(*World, *ImportedScene, Scene.ToSharedRef(),
		AttemptFolder, Normalized.Processing, Result, Error);
	for (const FGroupValue& Group : Plan.Groups)
	{
		if (!bInventoryValid) break;
		const TArray<UInstancedStaticMeshComponent*>* Components = ImportedComponents.Find(Group.GroupId);
		if (Components == nullptr || Components->Num() != 1
			|| (*Components)[0]->GetClass() != UHierarchicalInstancedStaticMeshComponent::StaticClass())
		{
			bInventoryValid = false;
			break;
		}
		Snapshots.Add(CaptureSnapshot(
			*CastChecked<UHierarchicalInstancedStaticMeshComponent>((*Components)[0]), Group.GroupId));
	}
	if (!bInventoryValid)
	{
		Error = TEXT("The imported HISM precursor inventory did not match the immutable plan.");
	}
	else if (Normalized.InstanceType == EConVerseOptimizedInstanceType::ISM
		&& !ConvertHISMsToISMsTwoStage(Snapshots, Error))
	{
		bInventoryValid = false;
	}
	// Test-only seam. Injected here because the attempt now owns imported assets and world actors,
	// which is exactly the state RollBackAttempt exists to unwind.
	else if (Normalized.FailureInjection == EConVerseOptimizedImportFailureInjection::AfterDatasmithImport
		|| Normalized.FailureInjection == EConVerseOptimizedImportFailureInjection::ObstructRollback)
	{
		bInventoryValid = false;
		Error = TEXT("Injected failure after the Datasmith import (test-only failure injection).");
	}
	if (bInventoryValid) bInventoryValid = ConVerseImportProcessing::ProcessMeshes(*World, *ImportedScene, AttemptFolder, Normalized, Result, Error);
	if (!bInventoryValid)
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails,
			Normalized.FailureInjection == EConVerseOptimizedImportFailureInjection::ObstructRollback);
		const bool bProcessingCancelled = SlowTask.ShouldCancel() || Error.StartsWith(TEXT("Mesh processing cancelled"));
		Result.Status = !bRolledBack ? EConVerseOptimizedImportStatus::RollbackFailed
			: (bProcessingCancelled ? EConVerseOptimizedImportStatus::CancelledRolledBack : EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack);
		Result.Summary = Error + RollbackDetails;
		FinishResult(&Plan, Result);
		return Result;
	}
	Result.LastCompletedStage = EConVerseOptimizedImportStage::ComponentsConverted;
	ConVerseImportProcessing::CaptureState(*World, *ImportedScene, AttemptFolder, Inventory.ExistingActors, Result);
	WriteAttemptCheckpoint(Result, false);

	if (Normalized.FailureInjection == EConVerseOptimizedImportFailureInjection::CorruptBeforeVerification)
	{
		// Test-only seam. Deliberately damages real output so the verification below fails on its
		// own terms rather than being told to. Nothing about the failure is simulated.
		TMap<FString, TArray<UInstancedStaticMeshComponent*>> CorruptTargets;
		GatherSessionComponents(*World, Result.SessionId, CorruptTargets);
		for (TPair<FString, TArray<UInstancedStaticMeshComponent*>>& Pair : CorruptTargets)
		{
			if (!Pair.Value.IsEmpty() && IsValid(Pair.Value[0]) && Pair.Value[0]->GetInstanceCount() > 0)
			{
				Pair.Value[0]->RemoveInstance(Pair.Value[0]->GetInstanceCount() - 1);
				break;
			}
		}
	}

	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportVerify", "Verifying the imported session..."));
	Result.bVerificationSucceeded = VerifySession(
		*World, *ImportedScene, Plan, Result.SessionId, Snapshots, Result);
	Result.bVerificationSucceeded &= VerifySourceAccounting(*World, *ImportedScene, Plan, Inventory, Result.SessionId, Result);
	Result.bVerificationSucceeded &= ConVerseImportProcessing::VerifyLights(*World, *ImportedScene, Scene.ToSharedRef(), Inventory.ExistingActors, Result);
	if (!Result.bVerificationSucceeded)
	{
		FString FailedGroupSummary;
		for (const FConVerseOptimizedGroupVerification& Verification : Result.GroupVerification)
		{
			if (!Verification.bPassed)
			{
				FailedGroupSummary += FString::Printf(TEXT("\n  %s: %s"), *Verification.GroupId, *Verification.Details);
			}
		}

		if (Normalized.bDeferRollbackOnVerificationFailure)
		{
			// Park the attempt instead of unwinding it. Nothing is committed and nothing is
			// destroyed: the objects this attempt created stay exactly as verification found them
			// until AcceptFailedVerification or DiscardFailedVerification resolves the decision.
			TSharedPtr<FPendingFailedVerification> Pending = MakeShared<FPendingFailedVerification>();
			Pending->Plan = Plan;
			Pending->Inventory = Inventory;
			Pending->World = World;
			Pending->ImportedScene = ImportedScene;
			Pending->PreviousManifest = PreviousManifest;
			Pending->FailureDetails = FailedGroupSummary;

			Result.Status = EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision;
			Result.Summary = FString::Printf(
				TEXT("Import completed, but %d of %d groups failed verification. ")
				TEXT("Nothing has been committed or rolled back yet; accept or discard this result.%s"),
				Result.PlannedGroupCount - Result.VerifiedGroupCount, Result.PlannedGroupCount,
				*FailedGroupSummary);
			Pending->Result = Result;
			PendingFailedVerifications().Add(Result.SessionId, Pending);
			FinishResult(&Plan, Result);
			return Result;
		}

		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = TEXT("Import completed, but one or more required verification checks failed.") + RollbackDetails;
		FinishResult(&Plan, Result);
		return Result;
	}

	Result.Status = EConVerseOptimizedImportStatus::Verified;
	Result.LastCompletedStage = EConVerseOptimizedImportStage::Verified;
	Result.Summary = FString::Printf(TEXT("Verified, unsaved: %d groups, %d ordered instances, and %d ordinary mesh elements. Light checks are listed in the report."),
		Result.VerifiedGroupCount, Result.VerifiedInstanceCount, Result.VerifiedOrdinaryMeshes);
	// Intermediate checkpoint: commit has not run yet, so this is not the attempt's final outcome
	// and must not produce an import log row. The exits below each finish the attempt for real.
	FinishResult(&Plan, Result, /*bTerminal=*/false);

	if (SlowTask.ShouldCancel() || (Normalized.CancelRequested && Normalized.CancelRequested()))
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack ? EConVerseOptimizedImportStatus::CancelledRolledBack : EConVerseOptimizedImportStatus::RollbackFailed;
		Result.bVerificationSucceeded = false;
		Result.Summary = TEXT("Import cancelled before commit.") + RollbackDetails;
		FinishResult(&Plan, Result);
		return Result;
	}

	// Point of no return: the session is verified and is about to take ownership, so cancellation is
	// no longer offered. Superseding is atomic from the user's perspective.
	EnterStage(SlowTask, 1.0f,
		NSLOCTEXT("ConVerseHISM", "OptimizedImportCommit", "Committing the manifest and superseding the previous session..."));

	// Resolve the prior session before any destruction so a failure here still leaves the old output intact.
	TArray<AActor*> SupersededActors;
	if (PreviousManifest != nullptr
		&& !PreflightSupersedeSession(*World, *PreviousManifest, SupersededActors, Error))
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.bVerificationSucceeded = false;
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = FString::Printf(
			TEXT("Verification passed, but the previous optimized session could not be superseded: %s%s"),
			*Error, *RollbackDetails);
		FinishResult(&Plan, Result);
		return Result;
	}

	const bool bCommitSucceeded =
		Normalized.FailureInjection != EConVerseOptimizedImportFailureInjection::BeforeManifestCommit
		&& CommitManifestAndOwnership(*World, *ImportedScene, Plan, Inventory, PreviousManifest, Result, Error);
	if (Normalized.FailureInjection == EConVerseOptimizedImportFailureInjection::BeforeManifestCommit)
	{
		// Test-only seam. Short-circuited before CommitManifestAndOwnership runs, so nothing is
		// committed and this is the latest checkpoint that still rolls back.
		Error = TEXT("Injected failure before the manifest commit (test-only failure injection).");
	}
	if (!bCommitSucceeded)
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
		Result.bVerificationSucceeded = false;
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = FString::Printf(TEXT("Verification passed, but manifest commit failed: %s%s"), *Error, *RollbackDetails);
		FinishResult(&Plan, Result);
		return Result;
	}

	if (PreviousManifest != nullptr)
	{
		FString SupersedeDetails;
		if (!RemoveSupersededSession(*World, SupersededActors, Result.RemovedPreviousActorCount, SupersedeDetails))
		{
			// Replacing the old session failed. Remove the new attempt and leave the old output active.
			FString RollbackDetails;
			const bool bRolledBack = RollBackAttempt(*World, Inventory, Result.SessionId, Result, RollbackDetails);
			Result.bVerificationSucceeded = false;
			Result.Status = bRolledBack
				? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
				: EConVerseOptimizedImportStatus::RollbackFailed;
			Result.Summary = FString::Printf(
				TEXT("Verification passed, but the previous optimized session could not be removed. ")
				TEXT("The previous import remains active.%s%s"),
				*SupersedeDetails, *RollbackDetails);
			FinishResult(&Plan, Result);
			return Result;
		}

		MarkManifestSuperseded(*PreviousManifest);
		Result.Summary += FString::Printf(
			TEXT(" Optimized reimport superseded manifest %s and removed %d prior-session actors. ")
			TEXT("Superseded asset packages were retained."),
			*Result.PreviousManifestId, Result.RemovedPreviousActorCount);
	}

	Result.Summary += TEXT(" Source identity manifest committed and ordinary Datasmith reimport was guarded.");
	FinishResult(&Plan, Result);
	return Result;
}

bool FConVerseDatasmithImportService::HasActiveOptimizedImport(const FConVerseOptimizedImportOptions& Options)
{
	using namespace ConVerseDatasmithImport;
	FConVerseOptimizedImportOptions Normalized;
	FString Error;
	if (!NormalizeAndValidateOptions(Options, true, Normalized, Error))
	{
		return false;
	}
	return FindActiveManifest(Normalized.FilePath, Normalized.DestinationPath) != nullptr;
}

FString FConVerseDatasmithImportService::GetImportLogPath()
{
	return FPaths::ConvertRelativePathToFull(ConVerseDatasmithImport::ImportLogPath());
}

bool FConVerseDatasmithImportService::HasPendingFailedVerification(const FString& SessionId)
{
	return ConVerseDatasmithImport::PendingFailedVerifications().Contains(SessionId);
}

FConVerseOptimizedImportResult FConVerseDatasmithImportService::AcceptFailedVerification(const FString& SessionId)
{
	using namespace ConVerseDatasmithImport;

	TSharedPtr<FPendingFailedVerification> Pending;
	if (!PendingFailedVerifications().RemoveAndCopyValue(SessionId, Pending) || !Pending.IsValid())
	{
		FConVerseOptimizedImportResult Result;
		Result.Operation = EConVerseOptimizedImportOperation::ResolveFailedVerification;
		Result.SessionId = SessionId;
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = TEXT("No import is awaiting a verification decision for this session. ")
			TEXT("Pending attempts do not survive an editor restart.");
		FinishResult(nullptr, Result);
		return Result;
	}

	FConVerseOptimizedImportResult Result = Pending->Result;
	Result.Operation = EConVerseOptimizedImportOperation::ResolveFailedVerification;
	UWorld* World = Pending->World.Get();
	UDatasmithScene* ImportedScene = Pending->ImportedScene.Get();

	// The world can change between the failure and the decision. Committing a manifest whose
	// referents have gone is worse than not committing at all, so a stale attempt is discarded.
	if (!IsValid(World) || !IsValid(ImportedScene))
	{
		Result.Status = EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = TEXT("The imported scene or world was destroyed before the result could be accepted. ")
			TEXT("Objects created by this attempt may remain in the level and are not owned by any manifest.");
		FinishResult(&Pending->Plan, Result);
		return Result;
	}

	FString Error;
	TArray<AActor*> SupersededActors;
	UConVerseOptimizedImportManifest* PreviousManifest = Pending->PreviousManifest.Get();
	if (PreviousManifest != nullptr
		&& !PreflightSupersedeSession(*World, *PreviousManifest, SupersededActors, Error))
	{
		// The previous session cannot be safely replaced, so fall back to the behavior that would
		// have happened automatically rather than leaving two competing sessions in the level.
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Pending->Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = FString::Printf(
			TEXT("The result could not be accepted because the previous optimized session could not be superseded: %s%s"),
			*Error, *RollbackDetails);
		FinishResult(&Pending->Plan, Result);
		return Result;
	}

	if (!CommitManifestAndOwnership(
		*World, *ImportedScene, Pending->Plan, Pending->Inventory, PreviousManifest, Result, Error))
	{
		FString RollbackDetails;
		const bool bRolledBack = RollBackAttempt(*World, Pending->Inventory, Result.SessionId, Result, RollbackDetails);
		Result.Status = bRolledBack
			? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
			: EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = FString::Printf(TEXT("The result could not be accepted: manifest commit failed: %s%s"),
			*Error, *RollbackDetails);
		FinishResult(&Pending->Plan, Result);
		return Result;
	}

	// Quarantine the committed manifest. It describes output that verification could not confirm,
	// so it must never be used as the basis for superseding anything later.
	if (UConVerseOptimizedImportManifest* Committed =
		Cast<UConVerseOptimizedImportManifest>(ImportedScene->GetAssetUserDataOfClass(
			UConVerseOptimizedImportManifest::StaticClass())))
	{
		Committed->Modify();
		Committed->Verification.bAcceptedWithFailedVerification = true;
		Committed->Verification.AcceptedFailureDetails = Pending->FailureDetails;
		ImportedScene->MarkPackageDirty();
	}

	if (PreviousManifest != nullptr)
	{
		FString SupersedeDetails;
		if (RemoveSupersededSession(*World, SupersededActors, Result.RemovedPreviousActorCount, SupersedeDetails))
		{
			MarkManifestSuperseded(*PreviousManifest);
		}
	}

	// bVerificationSucceeded stays false. Accepting changes what happens to the output, never the
	// record of whether it passed.
	Result.Status = EConVerseOptimizedImportStatus::AcceptedWithFailedVerification;
	Result.Summary = FString::Printf(
		TEXT("Accepted despite failed verification: %d of %d groups verified. ")
		TEXT("This session is committed but degraded, and optimized reimport is refused against it.%s"),
		Result.VerifiedGroupCount, Result.PlannedGroupCount, *Pending->FailureDetails);
	FinishResult(&Pending->Plan, Result);
	return Result;
}

FConVerseOptimizedImportResult FConVerseDatasmithImportService::DiscardFailedVerification(const FString& SessionId)
{
	using namespace ConVerseDatasmithImport;

	TSharedPtr<FPendingFailedVerification> Pending;
	if (!PendingFailedVerifications().RemoveAndCopyValue(SessionId, Pending) || !Pending.IsValid())
	{
		FConVerseOptimizedImportResult Result;
		Result.Operation = EConVerseOptimizedImportOperation::ResolveFailedVerification;
		Result.SessionId = SessionId;
		Result.Status = EConVerseOptimizedImportStatus::InvalidOptions;
		Result.Summary = TEXT("No import is awaiting a verification decision for this session. ")
			TEXT("Pending attempts do not survive an editor restart.");
		FinishResult(nullptr, Result);
		return Result;
	}

	FConVerseOptimizedImportResult Result = Pending->Result;
	Result.Operation = EConVerseOptimizedImportOperation::ResolveFailedVerification;
	UWorld* World = Pending->World.Get();
	if (!IsValid(World))
	{
		Result.Status = EConVerseOptimizedImportStatus::RollbackFailed;
		Result.Summary = TEXT("The world was destroyed before the result could be discarded. ")
			TEXT("Objects created by this attempt may remain and are not owned by any manifest.");
		FinishResult(&Pending->Plan, Result);
		return Result;
	}

	FString RollbackDetails;
	const bool bRolledBack = RollBackAttempt(*World, Pending->Inventory, Result.SessionId, Result, RollbackDetails);
	Result.Status = bRolledBack
		? EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack
		: EConVerseOptimizedImportStatus::RollbackFailed;
	Result.Summary = TEXT("Discarded after failed verification.") + RollbackDetails;
	FinishResult(&Pending->Plan, Result);
	return Result;
}
