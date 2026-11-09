// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBackpackComponent.h"

#include "CastleBuildingDefinition.h"
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

	if (Capacity < 1)
	{
		OutFailureReason = FText::FromString(TEXT("背包容量必须大于 0。"));
		return false;
	}

	if (Items.Num() >= Capacity)
	{
		OutFailureReason = FText::FromString(TEXT("背包已满，无法加入建筑卡牌。"));
		return false;
	}

	if (!ValidateDescriptorForStorage(Descriptor, OutFailureReason))
	{
		return false;
	}

	OutIndex = Items.Add(Descriptor);
	return true;
}

bool UCastleBackpackComponent::TryInsertItemAt(int32 Index, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

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

	if (!ValidateDescriptorForStorage(Descriptor, OutFailureReason))
	{
		return false;
	}

	Items.Insert(Descriptor, Index);
	return true;
}

bool UCastleBackpackComponent::TryAddBuilding(UCastleBuildingDefinition* Definition, int32 Level, ECastleTileColor Color, int32 Number, int32& OutIndex, FText& OutFailureReason)
{
	OutIndex = INDEX_NONE;
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
	// 与放置、预览和定价共用描述校验，不按类名重新推断建筑身份。
	return Descriptor.Validate(OutFailureReason);
}
