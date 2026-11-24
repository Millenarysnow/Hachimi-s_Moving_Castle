#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Castle/Guide/CastleGuideDataAsset.h"
#include "CastleGuideWidgets.generated.h"

class UButton;
class UCastleGuideSubsystem;
class UImage;
class UTextBlock;

/** 首次机制说明弹窗，关闭后由子系统解锁并收录该词条。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleGuidePopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool HasRequiredBindings() const;
	bool Configure(UCastleGuideSubsystem* InSubsystem, const FCastleGuideEntry& Entry);
	void RefreshDescription();

protected:
	virtual void NativeOnInitialized() override;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> Illustration;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EntryTitle;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EntryDescription;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;

private:
	UFUNCTION() void HandleCloseClicked();
	UPROPERTY(Transient) TObjectPtr<UCastleGuideSubsystem> GuideSubsystem;
	FName EntryId;
	FText DescriptionTemplate;
};

/** 左下角问号打开的图文百科；导航和分页状态由 C++ 管理。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleGuideBookWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool HasRequiredBindings() const;
	bool Configure(UCastleGuideSubsystem* InSubsystem, const TArray<FCastleGuideEntry>& Entries);
	void RefreshEntries();

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> Illustration;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EntryTitle;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EntryDescription;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PageStatus;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EmptyMessage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PreviousButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> NextButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;

private:
	UFUNCTION() void HandlePreviousClicked();
	UFUNCTION() void HandleNextClicked();
	UFUNCTION() void HandleCloseClicked();
	void DisplayCurrentEntry();

	UPROPERTY(Transient) TObjectPtr<UCastleGuideSubsystem> GuideSubsystem;
	UPROPERTY(Transient) TArray<FCastleGuideEntry> Entries;
	int32 CurrentEntryIndex = INDEX_NONE;
};
