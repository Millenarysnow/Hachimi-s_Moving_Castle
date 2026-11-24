#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CastleGuideSubsystem.generated.h"

class ACastlePlayerController;
class UCastleGuideBookWidget;
class UCastleGuideDataAsset;
class UCastleGuidePopupWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FCastleGuideEntryLearned, FName);

/** 載入圖文百科，管理首看彈窗、已收錄條目和任意時刻可開啟的百科面板。 */
UCLASS()
class MOVING_CASTLE_API UCastleGuideSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool ShowEntryPopup(FName EntryId, ACastlePlayerController* Controller, FText& OutReason);
	bool ShowGuideBook(ACastlePlayerController* Controller, FText& OutReason);
	void CloseCurrentModal();
	bool HasOpenModal() const { return ActivePopup != nullptr || ActiveBook != nullptr; }
	/** 退出关卡丢弃待确认的弹窗，保留已解锁条目。 */
	void CancelForController(const ACastlePlayerController& Controller);
	bool IsEntryUnlocked(FName EntryId) const { return UnlockedEntryIds.Contains(EntryId); }
	TArray<FName> GetUnlockedEntryIds() const { return UnlockedEntryIds.Array(); }
	FCastleGuideEntryLearned OnGuideEntryLearned;

private:
	bool EnsureGuideData(FText& OutReason);
	UClass* LoadRequiredWidgetClass(const TCHAR* AssetPath, FText& OutReason) const;
	void CloseEntryPopup();
	void CloseGuideBook();

	UPROPERTY(Transient) TObjectPtr<UCastleGuideDataAsset> GuideData;
	UPROPERTY(Transient) TObjectPtr<UCastleGuidePopupWidget> ActivePopup;
	UPROPERTY(Transient) TObjectPtr<UCastleGuideBookWidget> ActiveBook;
	TWeakObjectPtr<ACastlePlayerController> ActiveController;
	TSet<FName> UnlockedEntryIds;
	FName PendingEntryId;
};
