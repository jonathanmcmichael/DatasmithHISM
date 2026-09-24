#include "ConVerseOptimizedReimportHandler.h"

#include "ConVerseOptimizedImportManifest.h"
#include "DatasmithScene.h"
#include "Factories/Factory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Misc/App.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ConVerseOptimizedReimportHandler"

DEFINE_LOG_CATEGORY_STATIC(LogConVerseOptimizedReimport, Log, All);

namespace
{
	struct FConVerseOptimizedOwnership
	{
		FString ManifestId;
		FString SessionId;
		FString SourceFilePath;
	};

	IInterface_AssetUserData* GetAssetUserDataInterface(UObject* Object)
	{
		return IsValid(Object) ? Cast<IInterface_AssetUserData>(Object) : nullptr;
	}

	bool FindActiveOwnership(UObject* Object, FConVerseOptimizedOwnership& OutOwnership)
	{
		IInterface_AssetUserData* AssetUserData = GetAssetUserDataInterface(Object);
		if (AssetUserData == nullptr)
		{
			return false;
		}

		if (Object->IsA<UDatasmithScene>())
		{
			const UConVerseOptimizedImportManifest* Manifest = Cast<UConVerseOptimizedImportManifest>(
				AssetUserData->GetAssetUserDataOfClass(UConVerseOptimizedImportManifest::StaticClass()));

			if (Manifest != nullptr && Manifest->CommitState == EConVerseOptimizedImportCommitState::Active)
			{
				OutOwnership.ManifestId = Manifest->ManifestId;
				OutOwnership.SessionId = Manifest->SessionId;
				OutOwnership.SourceFilePath = Manifest->CanonicalSourceFilePath;
				return true;
			}
		}

		const UConVerseOptimizedAssetMarker* Marker = Cast<UConVerseOptimizedAssetMarker>(
			AssetUserData->GetAssetUserDataOfClass(UConVerseOptimizedAssetMarker::StaticClass()));

		if (Marker != nullptr && Marker->CommitState == EConVerseOptimizedImportCommitState::Active)
		{
			OutOwnership.ManifestId = Marker->ManifestId;
			OutOwnership.SessionId = Marker->SessionId;
			OutOwnership.SourceFilePath = Marker->SourceFilePath;
			return true;
		}

		return false;
	}
}

bool FConVerseOptimizedReimportHandler::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	FConVerseOptimizedOwnership Ownership;
	if (!FindActiveOwnership(Obj, Ownership))
	{
		return false;
	}

	if (!Ownership.SourceFilePath.IsEmpty())
	{
		OutFilenames.AddUnique(Ownership.SourceFilePath);
	}

	return true;
}

void FConVerseOptimizedReimportHandler::SetReimportPaths(
	UObject* Obj,
	const TArray<FString>& NewReimportPaths)
{
	// The optimized manifest is the source of truth. Changing its source path
	// through Unreal's ordinary reimport UI would bypass planning and verification.
	UE_LOG(
		LogConVerseOptimizedReimport,
		Verbose,
		TEXT("Ignored an ordinary reimport path update for optimized asset '%s' (%d proposed path(s))."),
		Obj != nullptr ? *Obj->GetPathName() : TEXT("<null>"),
		NewReimportPaths.Num());
}

EReimportResult::Type FConVerseOptimizedReimportHandler::Reimport(UObject* Obj)
{
	FConVerseOptimizedOwnership Ownership;
	if (!FindActiveOwnership(Obj, Ownership))
	{
		return EReimportResult::Failed;
	}

	const FText Message = LOCTEXT(
		"OrdinaryReimportBlocked",
		"Reimport is managed by Optimized Datasmith Import. Use Tools > Optimized Datasmith Import.");

	const bool bUnattended =
		IsAutomatedReimport()
		|| IsRunningCommandlet()
		|| FApp::IsUnattended()
		|| GIsAutomationTesting
		|| GIsRunningUnattendedScript;

	UE_LOG(
		LogConVerseOptimizedReimport,
		Warning,
		TEXT("Blocked ordinary reimport for optimized asset '%s' (manifest '%s', session '%s'). %s"),
		Obj != nullptr ? *Obj->GetPathName() : TEXT("<null>"),
		*Ownership.ManifestId,
		*Ownership.SessionId,
		*Message.ToString());

	if (!bUnattended)
	{
		FNotificationInfo NotificationInfo(Message);
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 8.0f;
		NotificationInfo.FadeOutDuration = 0.5f;

		if (TSharedPtr<SNotificationItem> Notification =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	return EReimportResult::Cancelled;
}

int32 FConVerseOptimizedReimportHandler::GetPriority() const
{
	// Stay above both the default legacy priority and UE 5.8's Interchange
	// reimport handler so ownership is checked before either import path runs.
	return UFactory::GetDefaultImportPriority() + 100;
}

#undef LOCTEXT_NAMESPACE
