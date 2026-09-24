#include "ConVerseOptimizedImportManifest.h"

bool UConVerseOptimizedImportManifest::FindInstanceRecord(
	const FString& GroupId,
	const int32 InstanceIndex,
	FConVerseOptimizedImportInstanceRecord& OutRecord) const
{
	OutRecord = FConVerseOptimizedImportInstanceRecord();

	if (GroupId.IsEmpty() || InstanceIndex < 0)
	{
		return false;
	}

	const FConVerseOptimizedImportGroupRecord* Group = Groups.FindByPredicate(
		[&GroupId](const FConVerseOptimizedImportGroupRecord& Candidate)
		{
			return Candidate.GroupId.Equals(GroupId, ESearchCase::CaseSensitive);
		});

	if (!Group
		|| InstanceIndex >= Group->InstanceRecordCount
		|| Group->FirstInstanceRecordIndex < 0)
	{
		return false;
	}

	const int64 AbsoluteIndex = static_cast<int64>(Group->FirstInstanceRecordIndex) + InstanceIndex;
	if (AbsoluteIndex < 0 || AbsoluteIndex >= Instances.Num())
	{
		return false;
	}

	const FConVerseOptimizedImportInstanceRecord& Candidate = Instances[static_cast<int32>(AbsoluteIndex)];
	if (!Candidate.GroupId.Equals(GroupId, ESearchCase::CaseSensitive)
		|| Candidate.InstanceIndex != InstanceIndex)
	{
		return false;
	}

	OutRecord = Candidate;
	return true;
}
