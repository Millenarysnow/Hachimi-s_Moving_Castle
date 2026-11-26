// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBackpackComponent.h"

#include "CastleBuildingDefinition.h"
#include "Items/CastleItemTypes.h"
#include "Moving_Castle.h"

UCastleBackpackComponent::UCastleBackpackComponent()
{
	// 背包只在明确的放入、取出操作时变化，不需要每帧执行逻辑。
	PrimaryComponentTick.bCanEverTick = false;
	Capacity = 10;
}
void UCastleBackpackComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Capacity < 1)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("背包配置错误：Capacity 必须大于 0，当前值为 %d。"), Capacity);
	}
}

bool UCastleBackpackComponent::HasFreeSlots(int32 RequiredSlots) const
{
	if (RequiredSlots < 0)
	{
		return false;
	}

	return Items.Num() + RequiredSlots <= Capacity;
}

bool UCastleBackpackComponent::TryGetItemAt(int32 Index, FCastleBuildingDescriptor& OutDescriptor) const
{
	if (!Items.IsValidIndex(Index))
	{
		return false;
	}

	OutDescriptor = Items[Index];
	return true;
}

bool UCastleBackpackComponent::TryAddItem(const FCastleBuildingDescriptor& Descriptor, int32& OutIndex, FText& OutFailureReason)
{
	OutIndex = INDEX_NONE;
	OutFailureReason = FText::GetEmpty();

	if (!ValidateDescriptorForStorage(Descriptor, OutFailureReason))
	{
		return false;
	}

	if (Capacity < 1)
	{
		OutFailureReason = FText::FromString(TEXT("背包容量必须大于 0。"));
		return false;
	}

	if (Items.Num() >= Capacity)
	{
		OutFailureReason = FText::FromString(TEXT("背包已满，无法加入建筑卡牌。"));
		UE_LOG(LogMoving_Castle, Warning, TEXT("背包 %s 建筑栏已满，拒绝通过普通入口加入资产 %s。"),
			*GetPathName(), *GetPathNameSafe(Descriptor.Definition.Get()));
		return false;
	}

	OutIndex = Items.Add(Descriptor);
	// 商店随后才提交扣款。保留底层插入语义，避免监听者读到未提交的商品状态。
	return true;
}

bool UCastleBackpackComponent::TryAddItemAllowOverflow(const FCastleBuildingDescriptor& Descriptor,
	bool& bOutWentToTempBar, int32& OutIndex, FText& OutFailureReason)
{
	bOutWentToTempBar = IsFull();
	OutIndex = bOutWentToTempBar ? TempItems.Num() : Items.Num();
	if (!TryAddItemsAllowOverflow(MakeArrayView(&Descriptor, 1), OutFailureReason))
	{
		bOutWentToTempBar = false;
		OutIndex = INDEX_NONE;
		return false;
	}
	return true;
}

bool UCastleBackpackComponent::TryAddItemsAllowOverflow(const TConstArrayView<FCastleBuildingDescriptor> Descriptors,
	FText& OutFailureReason)
{
	if (!CanApplyItemBatch(Descriptors, nullptr, false, OutFailureReason)) { return false; }
	ApplyValidatedItemBatch(Descriptors, nullptr, false);
	OnBackpackChanged.Broadcast();
	return true;
}

bool UCastleBackpackComponent::CanApplyItemBatch(const TConstArrayView<FCastleBuildingDescriptor> Descriptors,
	const TArray<FCastleBuildingDescriptor>* MainBarAfter, const bool bRefill, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || Capacity < 1 || Items.Num() > Capacity
		|| (MainBarAfter ? MainBarAfter->Num() > Capacity : Descriptors.IsEmpty()))
	{
		OutFailureReason = INVTEXT("建筑卡牌必须在游戏线程发放，且背包容量有效、卡牌批次非空。");
		UE_LOG(LogMoving_Castle, Error, TEXT("背包 %s 发放失败：容量 %d，批次张数 %d。%s"),
			*GetPathName(), Capacity, Descriptors.Num(), *OutFailureReason.ToString());
		return false;
	}
	const auto ValidateBatch = [this, &OutFailureReason](const TConstArrayView<FCastleBuildingDescriptor> Batch)
	{
		for (const FCastleBuildingDescriptor& Descriptor : Batch)
		{
			if (ValidateDescriptorForStorage(Descriptor, OutFailureReason)) { continue; }
			if (Descriptor.Level == 1)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("背包 %s 拒绝整批建筑卡牌：资产 %s，%s"),
					*GetPathName(), *GetPathNameSafe(Descriptor.Definition.Get()), *OutFailureReason.ToString());
			}
			return false;
		}
		return true;
	};
	if (MainBarAfter && !ValidateBatch(*MainBarAfter)) { return false; }
	if (bRefill)
	{
		const int32 FreeSlots = Capacity - (MainBarAfter ? MainBarAfter->Num() : Items.Num());
		const int32 RefillCount = FMath::Min(FreeSlots, TempItems.Num());
		if (!ValidateBatch(MakeArrayView(TempItems.GetData(), RefillCount))) { return false; }
	}
	return ValidateBatch(Descriptors);
}

void UCastleBackpackComponent::ApplyValidatedItemBatch(const TConstArrayView<FCastleBuildingDescriptor> Descriptors,
	const TArray<FCastleBuildingDescriptor>* MainBarAfter, const bool bRefill)
{
	// 本段没有可失败回调。主栏变换、补位与临时栏追加结束后由事务入口统一广播。
	if (MainBarAfter) { Items = *MainBarAfter; }
	if (bRefill) { RefillFromTempBar(); }
	const int32 PreviousTempCount = TempItems.Num();
	for (const FCastleBuildingDescriptor& Descriptor : Descriptors)
	{
		AppendValidatedItem(Descriptor);
	}
	if (TempItems.Num() > PreviousTempCount)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("背包 %s 有 %d 张新卡进入临时建筑栏，当前共 %d 张。"),
			*GetPathName(), TempItems.Num() - PreviousTempCount, TempItems.Num());
	}
}

void UCastleBackpackComponent::AppendValidatedItem(const FCastleBuildingDescriptor& Descriptor)
{
	TArray<FCastleBuildingDescriptor>& Destination = IsFull() ? TempItems : Items;
	Destination.Add(Descriptor);
}

bool UCastleBackpackComponent::TryInsertItemAt(int32 Index, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (!ValidateDescriptorForStorage(Descriptor, OutFailureReason))
	{
		return false;
	}

	if (Capacity < 1)
	{
		OutFailureReason = FText::FromString(TEXT("背包容量必须大于 0。"));
		return false;
	}

	if (Items.Num() >= Capacity)
	{
		OutFailureReason = FText::FromString(TEXT("背包已满，无法插入建筑卡牌。"));
		return false;
	}

	if (Index < 0 || Index > Items.Num())
	{
		OutFailureReason = FText::FromString(TEXT("要插入的背包槽位索引无效。"));
		return false;
	}

	Items.Insert(Descriptor, Index);
	return true;
}

bool UCastleBackpackComponent::TryAddBuilding(UCastleBuildingDefinition* Definition, int32 Level, ECastleTileColor Color, int32 Number, int32& OutIndex, FText& OutFailureReason)
{
	OutIndex = INDEX_NONE;
	if (Level != 1)
	{
		// 在定义资产创建描述前拒绝等级，连超出资产等级表的值也留下同一错误。
		FCastleBuildingDescriptor Rejected;
		Rejected.Definition = Definition;
		Rejected.Level = Level;
		return ValidateDescriptorForStorage(Rejected, OutFailureReason);
	}
	if (!IsValid(Definition))
	{
		OutFailureReason = FText::FromString(TEXT("没有指定要加入背包的建筑 Data Asset。"));
		return false;
	}
	// 工厂只生成携带同一资产引用的描述，存入时仍经过背包容量与属性校验。
	FCastleBuildingDescriptor Descriptor;
	return Definition->CreateDescriptor(Level, Color, Number, Descriptor, OutFailureReason)
		&& TryAddItem(Descriptor, OutIndex, OutFailureReason);
}

bool UCastleBackpackComponent::TryRemoveItemAt(int32 Index, FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason)
{
	OutDescriptor = FCastleBuildingDescriptor();
	OutFailureReason = FText::GetEmpty();

	if (!Items.IsValidIndex(Index))
	{
		OutFailureReason = FText::FromString(TEXT("要取出的背包槽位无效。"));
		return false;
	}

	OutDescriptor = Items[Index];
	Items.RemoveAt(Index);
	return true;
}

int32 UCastleBackpackComponent::CommitAutoRefillFromTempBar()
{
	if (!IsInGameThread() || Capacity < 1 || Items.Num() > Capacity)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("背包 %s 不能提交补位：提交必须在游戏线程，容量 %d 必须有效且不小于建筑栏张数 %d。"),
			*GetPathName(), Capacity, Items.Num());
		return 0;
	}

	const int32 Count = RefillFromTempBar();
	// 调用方已完成本次移除事务，即使临时栏为空也要发布主栏的最终变化。
	OnBackpackChanged.Broadcast();
	return Count;
}

int32 UCastleBackpackComponent::RefillFromTempBar()
{
	const int32 Count = FMath::Min(Capacity - Items.Num(), TempItems.Num());
	if (Count > 0)
	{
		// 主栏末尾按临时栏原顺序追加，一次删除已转移的前缀，保留其余卡牌的 FIFO 顺序。
		Items.Append(TempItems.GetData(), Count);
		TempItems.RemoveAt(0, Count, EAllowShrinking::No);
		UE_LOG(LogMoving_Castle, Display, TEXT("背包 %s 自动补入 %d 张建筑卡牌，临时栏剩余 %d 张。"),
			*GetPathName(), Count, TempItems.Num());
	}
	return Count;
}

bool UCastleBackpackComponent::TryConsumeItemAt(const int32 Index, FCastleBuildingDescriptor& OutDescriptor,
	FText& OutFailureReason)
{
	if (!IsInGameThread() || Capacity < 1 || Items.Num() > Capacity)
	{
		OutDescriptor = {};
		OutFailureReason = INVTEXT("消费建筑卡牌需要在游戏线程操作，且背包容量有效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("背包 %s 消费槽位 %d 失败：%s"),
			*GetPathName(), Index, *OutFailureReason.ToString());
		return false;
	}
	if (!TryRemoveItemAt(Index, OutDescriptor, OutFailureReason))
	{
		return false;
	}
	CommitAutoRefillFromTempBar();
	return true;
}

bool UCastleBackpackComponent::TrySetCapacity(int32 NewCapacity, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (NewCapacity < 1)
	{
		OutFailureReason = FText::FromString(TEXT("背包容量必须大于 0。"));
		return false;
	}

	if (NewCapacity < Items.Num())
	{
		OutFailureReason = FText::FromString(TEXT("新容量小于当前卡牌数量，不能丢弃已有卡牌。"));
		return false;
	}

	Capacity = NewCapacity;
	return true;
}

bool UCastleBackpackComponent::ValidateDescriptorForStorage(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	// 主栏、临时栏和指定索引写入共用等级约束，防止固定朝向的多格卡进入建造栏。
	if (Descriptor.Level != 1)
	{
		OutFailureReason = INVTEXT("建筑栏仅允许一级单格建筑卡。");
		UE_LOG(LogMoving_Castle, Error, TEXT("背包 %s 拒绝存入资产 %s（Lv%d）：%s"),
			*GetPathName(), *GetPathNameSafe(Descriptor.Definition.Get()), Descriptor.Level, *OutFailureReason.ToString());
		return false;
	}
	return Descriptor.Validate(OutFailureReason) && CastleRoomCards::ValidateTrait(Descriptor, OutFailureReason);
}
