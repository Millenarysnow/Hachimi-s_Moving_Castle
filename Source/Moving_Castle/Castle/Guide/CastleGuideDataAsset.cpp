#include "CastleGuideDataAsset.h"

#include "Moving_Castle.h"

bool UCastleGuideDataAsset::ValidateEntries(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	TSet<FName> EntryIds;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FCastleGuideEntry& Entry = Entries[Index];
		if (Entry.EntryId.IsNone() || EntryIds.Contains(Entry.EntryId)
			|| Entry.Title.IsEmpty() || Entry.Description.IsEmpty())
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("百科资产 %s 第 %d 项缺少唯一 EntryId、标题或说明（EntryId=%s）。"),
				*GetPathName(), Index, *Entry.EntryId.ToString()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		}
		EntryIds.Add(Entry.EntryId);
	}
	if (Entries.IsEmpty())
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("百科资产 %s 没有任何图文条目。"), *GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}
	return true;
}

const FCastleGuideEntry* UCastleGuideDataAsset::FindEntry(const FName EntryId) const
{
	return Entries.FindByPredicate([EntryId](const FCastleGuideEntry& Entry) { return Entry.EntryId == EntryId; });
}
