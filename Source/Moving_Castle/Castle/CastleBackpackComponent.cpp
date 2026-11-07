// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBackpackComponent.h"

#include "CastleBuildingBase.h"
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

bool UCastleBackpackComponent::TryAddBuilding(TSubclassOf<ACastleBuildingBase> BuildingClass, ECastleTileColor Color, int32 Number, int32& OutIndex, FText& OutFailureReason)
{
	OutIndex = INDEX_NONE;
	OutFailureReason = FText::GetEmpty();

	if (BuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("没有指定要加入背包的建筑类。"));
		return false;
	}

	const ACastleBuildingBase* DefaultBuilding = BuildingClass->GetDefaultObject<ACastleBuildingBase>();
	if (DefaultBuilding == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑类默认对象无效，无法生成卡牌。"));
		return false;
	}

	FCastleBuildingDescriptor Descriptor;
	Descriptor.BuildingClass = BuildingClass;
	Descriptor.BuildingType = DefaultBuilding->GetBuildingType();
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	Descriptor.Level = DefaultBuilding->GetBuildingLevel();
	return TryAddItem(Descriptor, OutIndex, OutFailureReason);
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
	OutFailureReason = FText::GetEmpty();

	if (!Descriptor.HasBuildingClass())
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡牌没有指定具体建筑类。"));
		return false;
	}

	const ACastleBuildingBase* DefaultBuilding = Descriptor.BuildingClass->GetDefaultObject<ACastleBuildingBase>();
	if (DefaultBuilding == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑类默认对象无效，无法存入背包。"));
		return false;
	}

	if (DefaultBuilding->GetBuildingType() != Descriptor.BuildingType || DefaultBuilding->GetBuildingLevel() != Descriptor.Level)
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡牌的类别或等级与具体建筑类不一致。"));
		return false;
	}

	if (Descriptor.Number < 0 || Descriptor.Number > 9)
	{
		OutFailureReason = FText::FromString(TEXT("建筑数字必须在 0 到 9 之间。"));
		return false;
	}

	if (Descriptor.BuildingType == ECastleBuildingType::Road)
	{
		if (Descriptor.Color != ECastleTileColor::Gray || Descriptor.Number != 0)
		{
			OutFailureReason = FText::FromString(TEXT("道路卡牌必须是灰色 0 号。"));
			return false;
		}
	}
	else if (Descriptor.Color == ECastleTileColor::Gray || Descriptor.Number == 0)
	{
		OutFailureReason = FText::FromString(TEXT("功能建筑卡牌不能使用灰色或 0 号属性。"));
		return false;
	}

	return true;
}

