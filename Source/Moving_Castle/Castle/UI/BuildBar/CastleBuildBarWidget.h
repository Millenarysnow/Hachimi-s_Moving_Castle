// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "CastleBuildBarWidget.generated.h"

class ACastlePatrolRoute;
class ACastlePlayerController;
class UCastleBackpackComponent;
class UCastleBuildBarSlotWidget;
class UCastleDirectorSubsystem;
class UCastleDialogueSubsystem;
class UCastleShopComponent;
class UTextBlock;
enum class ECastleDirectorPhase : uint8;

/** 建造栏的原生数据绑定。WBP 管理排版，本类读取现有背包和快捷键选中项，不写入玩法状态。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleBuildBarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 接入当前关卡的背包并做首次同步；输入穿透仍由建造栏 WBP 的根可见性保证。 */
	virtual void NativeConstruct() override;

	/** 仅在牌组、容量或选择变化时更新显示，不重建槽位树。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 离开视口时解除对导演与路径编辑入口的显隐事件订阅。 */
	virtual void NativeDestruct() override;

private:
	/** 绑定固定的 10 个键位（第 10 格为数字键 0），交由 WBP 编译检查名称及类型。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot01;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot02;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot03;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot04;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot05;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot06;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot07;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot08;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot09;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleBuildBarSlotWidget> Slot10;

	/** 显示当前背包使用量与选中建筑名（编号徽章仍表示槽位，不用于描述建筑）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BuildBarStatus;

	/** 库存和选中仍分别归棋盘和控制器所有，UI 只持有本次显示期间的引用。 */
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;
	TWeakObjectPtr<ACastlePlayerController> Controller;
	TWeakObjectPtr<UCastleDirectorSubsystem> BoundDirector;
	TWeakObjectPtr<ACastlePatrolRoute> BoundPatrolRoute;
	TWeakObjectPtr<UCastleShopComponent> BoundShop;
	TWeakObjectPtr<UCastleDialogueSubsystem> BoundDialogue;
	FDelegateHandle DirectorPhaseHandle;
	FDelegateHandle PatrolEditingHandle;
	FDelegateHandle DialogueActivityHandle;

	/** 对比所有会影响渲染的字段，避免切换选择时刷新模型。 */
	UPROPERTY(Transient)
	TArray<FCastleBuildingDescriptor> DisplayedCards;
	int32 DisplayedCapacity = INDEX_NONE;
	int32 DisplayedSelection = INDEX_NONE;

	/** 只有完整验证并成功同步后才继续显示；失败不留下误导性的旧栏。 */
	bool bReady = false;

	/** 校验设计器中的槽位、状态文案以及根 WBP 上的静态美术资源。 */
	bool ValidatePresentation() const;

	/** 从项目原有库存读取并同步快照；调用方在失败时停止显示。 */
	bool SyncBackpack();

	/** 按键位顺序返回设计器中的槽位绑定，共用同一份顺序验证和同步。 */
	TStaticArray<UCastleBuildBarSlotWidget*, 10> GetSlots() const;

	/** 绑定导演和路径编辑状态，确保建造栏只在建造页面显示（路径编辑页与战斗页隐藏）。 */
	void EnsureModeBindings();
	bool ShouldDisplayInCurrentScreen() const;
	void RefreshScreenVisibility();
	void HandleDirectorPhaseChanged(ECastleDirectorPhase NewPhase);
	void HandlePatrolEditingChanged(bool bIsEditing);
	void HandleBuildBarSlotClicked(int32 SlotIndex);

	UFUNCTION()
	void HandleShopChanged();
};
