#include "ConVerseSourceMetadata.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, DatasmithHISMRuntime)

UConVerseSourceMetadata::UConVerseSourceMetadata()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UConVerseSourceMetadata::FindSource(const UActorComponent* MeshComponent, int32 InstanceIndex, FConVerseSourceRecord& OutRecord)
{
	OutRecord = FConVerseSourceRecord();
	if (!IsValid(MeshComponent) || !MeshComponent->GetOwner()) return false;
	const auto* Data = MeshComponent->GetOwner()->FindComponentByClass<UConVerseSourceMetadata>();
	if (!Data) return false;
	const auto* Found = Data->Records.FindByPredicate([&](const FConVerseSourceRecord& Record)
	{
		return Record.ComponentName == MeshComponent->GetFName() && Record.InstanceIndex == InstanceIndex;
	});
	if (!Found) return false;
	OutRecord = *Found;
	return true;
}
