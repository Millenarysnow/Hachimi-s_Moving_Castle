#pragma once

#include "CoreMinimal.h"
#include "Castle/Input/CastleControlMode.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CastleGuideSubsystem.generated.h"

class ACastlePlayerController;
class UCastleGuideBookWidget;
class UCastleGuideDataAsset;
class UCastleGuidePopupWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FCastleGuideEntryLearned, FName);

/** 加载图文百科，管理首次弹窗、已收录条目和百科面板。 */
UCLASS()
class MOVING_CASTLE_API UCastleGuideSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool ShowEntryPopup(FName EntryId, ACastlePlayerController* Controller, FText& OutReason);
	bool ShowGuideBook(ACastlePlayerController* Controller, FText& OutReason);
	/** 预检施工细则要求的词条和两个正式蓝图，成功后重置本次教程的入册进度。 */
	bool BeginTutorial(ACastlePlayerController& Controller, TConstArrayView<FName> RequiredIds, FText& OutReason);
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
	void BindModalController(ACastlePlayerController& Controller);
	void ReleaseModalController();
	void RefreshModalText(ECastleControlMode Mode);

	UPROPERTY(Transient) TObjectPtr<UCastleGuideDataAsset> GuideData;
	UPROPERTY(Transient) TObjectPtr<UCastleGuidePopupWidget> ActivePopup;
	UPROPERTY(Transient) TObjectPtr<UCastleGuideBookWidget> ActiveBook;
	TWeakObjectPtr<ACastlePlayerController> ActiveController;
	TWeakObjectPtr<ACastlePlayerController> TutorialController;
	FDelegateHandle ControlModeChangedHandle;
	TSet<FName> UnlockedEntryIds;
	FName PendingEntryId;
};
