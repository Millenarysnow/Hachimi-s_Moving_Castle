// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleGameplayTypes.h"
#include "CastleBackpackComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCastleBackpackChanged);

/**
 * 经典固定容量背包组件。
 *
 * 背包保存建筑描述而不是建筑 Actor。这样建筑离开场上后不会留下失效的
 * World 指针，放置和合成也都可以由棋盘重新创建合适的具体等级 Actor。
 */
UCLASS(ClassGroup = (Castle), meta = (BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造函数设置 10 个卡牌槽位，并关闭不需要的 Tick。 */
	UCastleBackpackComponent();

	/** 背包容量，策划案当前固定为 10，也允许在测试关卡中调小验证满包分支。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "背包", meta = (ClampMin = "1"))
	int32 Capacity = 10;

	/** 读取当前背包中的卡牌数量。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	int32 GetItemCount() const
	{
		return Items.Num();
	}

	/** 判断背包是否已达到容量上限。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	bool IsFull() const
	{
		return Items.Num() >= Capacity;
	}

	/** 返回背包容量是否足以再加入指定数量的卡牌。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	bool HasFreeSlots(int32 RequiredSlots = 1) const;

	/** 读取指定槽位的卡牌，不会改变背包内容。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	bool TryGetItemAt(int32 Index, FCastleBuildingDescriptor& OutDescriptor) const;

	/** 将完整建筑描述加入背包，成功时返回实际槽位索引。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryAddItem(const FCastleBuildingDescriptor& Descriptor, int32& OutIndex, FText& OutFailureReason);

	/** 道具专用入包接口。建筑栏满时，完整卡牌追加到临时栏队尾。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryAddItemAllowOverflow(const FCastleBuildingDescriptor& Descriptor, bool& bOutWentToTempBar,
		int32& OutIndex, FText& OutFailureReason);

	/** 先验证本批所有卡牌，再按顺序填入两栏。一次发放只通知一次。 */
	bool TryAddItemsAllowOverflow(TConstArrayView<FCastleBuildingDescriptor> Descriptors, FText& OutFailureReason);

	UPROPERTY(BlueprintAssignable, Category = "移动城堡|背包")
	FCastleBackpackChanged OnBackpackChanged;

	/** 临时栏保存完全确定的卡牌，索引 0 为队首。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	TArray<FCastleBuildingDescriptor> GetTempItems() const { return TempItems; }

	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	int32 GetTempItemCount() const { return TempItems.Num(); }

	/** 在指定槽位插入建筑卡牌，供放置失败时恢复原有槽位顺序。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryInsertItemAt(int32 Index, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason);

	/**
	 * 根据建筑 Data Asset、等级、颜色和数字创建一张卡牌并加入背包。
	 * 具体逻辑类和模型始终由资产解析，不在背包里复制第二份配置。
	 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryAddBuilding(UCastleBuildingDefinition* Definition, int32 Level, ECastleTileColor Color, int32 Number, int32& OutIndex, FText& OutFailureReason);

	/** 从指定槽位取出卡牌，暂不补位或广播。事务失败时可以原索引插回。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryRemoveItemAt(int32 Index, FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason);

	/** 已完成的事务调用此入口，按 FIFO 填满空位并只通知一次最终状态。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	int32 CommitAutoRefillFromTempBar();

	/** 不需要回滚的消费同步取牌、补位并通知。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TryConsumeItemAt(int32 Index, FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason);

	/** 调整容量；新容量不能小于已有卡牌数量。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包")
	bool TrySetCapacity(int32 NewCapacity, FText& OutFailureReason);

	/** 返回背包内容副本，供商店和调试界面读取。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	TArray<FCastleBuildingDescriptor> GetItems() const
	{
		return Items;
	}

protected:
	/** 运行时再次检查容量配置，错误配置直接记录日志而不静默修正。 */
	virtual void BeginPlay() override;

	/** 校验卡牌的类、等级、颜色和数字，供追加和插入共用。 */
	bool ValidateDescriptorForStorage(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 背包内的卡牌数据，顺序就是 UI 显示和取出时使用的槽位顺序。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "背包")
	TArray<FCastleBuildingDescriptor> Items;

private:
	/** 房间组提交入口需一次校验并替换主栏和临时栏，避免在中间状态广播。 */
	friend class UCastleItemGrantLibrary;

	UPROPERTY(VisibleInstanceOnly, Category = "背包")
	TArray<FCastleBuildingDescriptor> TempItems;

	/** 只接收已通过整批校验的卡牌，由调用入口统一通知。 */
	void AppendValidatedItem(const FCastleBuildingDescriptor& Descriptor);
};
