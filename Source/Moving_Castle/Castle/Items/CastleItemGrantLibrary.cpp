#include "Items/CastleItemGrantLibrary.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Moving_Castle.h"

namespace
{
	bool RejectGrant(const UObject* Asset, const FText& Reason, FText& OutFailureReason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("道具资产 %s 发放失败：%s"), *GetPathNameSafe(Asset), *Reason.ToString());
		return false;
	}

	/** 收集指定模板真正允许的定义，同类锁定的模板要求能识别唯一的具体定义。 */
	bool PrepareGroupDefinitions(const ECastleRoomGroupType Type, FCastleWildcardRoomSpec& Spec,
		const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> Pool, FText& OutFailureReason)
	{
		TOptional<ECastleBuildingType> RequiredType;
		switch (Type)
		{
		case ECastleRoomGroupType::ThreeNumberFourArchers: RequiredType = ECastleBuildingType::ArcherTower; break;
		case ECastleRoomGroupType::ThreeConsecutiveFarms: RequiredType = ECastleBuildingType::Farm; break;
		case ECastleRoomGroupType::CustomNormalRoom:
		case ECastleRoomGroupType::CustomColorNormalRoom: RequiredType = ECastleBuildingType::NormalRoom; break;
		case ECastleRoomGroupType::NPCSpeedRoom: RequiredType = ECastleBuildingType::SpeedRoom; break;
		default: break;
		}
		const bool bFunctionTowerOnly = Type == ECastleRoomGroupType::RandomFunctionCustomRoom;
		if (!RequiredType.IsSet() && !bFunctionTowerOnly)
		{
			return true;
		}

		const bool bExplicit = !Spec.AllowedDefinitions.IsEmpty();
		const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> Source = bExplicit
			? MakeArrayView(static_cast<const FCastleWildcardRoomSpec&>(Spec).AllowedDefinitions) : Pool;
		TArray<TObjectPtr<UCastleBuildingDefinition>> Matching;
		for (UCastleBuildingDefinition* Definition : Source)
		{
			if (!IsValid(Definition))
			{
				OutFailureReason = INVTEXT("建筑定义池中存在失效资产。");
				return false;
			}
			const ECastleBuildingType BuildingType = Definition->BuildingType;
			const bool bMatches = RequiredType.IsSet() ? BuildingType == RequiredType.GetValue()
				: BuildingType > ECastleBuildingType::NormalRoom && BuildingType <= ECastleBuildingType::BalloonTower;
			if (bMatches)
			{
				Matching.Add(Definition);
			}
			else if (bExplicit)
			{
				OutFailureReason = FText::Format(INVTEXT("显式建筑定义 {0} 的类型不符合该房间组。"),
					FText::FromString(Definition->GetPathName()));
				return false;
			}
		}

		if (Matching.IsEmpty() || (RequiredType.IsSet() && Matching.Num() != 1))
		{
			OutFailureReason = INVTEXT("建筑定义池缺少所需建筑，或固定建筑类型匹配多个定义，请显式配置唯一的定义。");
			return false;
		}
		Spec.AllowedDefinitions = MoveTemp(Matching);
		return true;
	}

	bool PrepareGroupNumbers(const ECastleRoomGroupType Type, FCastleWildcardRoomSpec& Spec, FText& OutFailureReason)
	{
		if (Type == ECastleRoomGroupType::ThreeNumberFourArchers)
		{
			if (!Spec.AllowedNumbers.IsEmpty() && (Spec.AllowedNumbers.Num() != 1 || Spec.AllowedNumbers[0] != 4))
			{
				OutFailureReason = INVTEXT("三张数字 4 箭塔的数字只能配置为 4。");
				return false;
			}
			Spec.AllowedNumbers = {4};
		}
		else if (Type == ECastleRoomGroupType::HighNumberRoom)
		{
			if (Spec.AllowedNumbers.IsEmpty())
			{
				Spec.AllowedNumbers = {7, 8, 9};
			}
			for (const int32 Number : Spec.AllowedNumbers)
			{
				if (Number < 7 || Number > 9)
				{
					OutFailureReason = INVTEXT("大数字房间只能配置 7 至 9。");
					return false;
				}
			}
		}
		return true;
	}
}

UCastlePendingRoomReward* UCastleItemGrantLibrary::CreateReward(FText DisplayName, UCastleBackpackComponent& Backpack,
	FCastleWildcardRoomOptions&& Options)
{
	UCastlePendingRoomReward* Reward = NewObject<UCastlePendingRoomReward>(&Backpack);
	Reward->DisplayName = MoveTemp(DisplayName);
	Reward->Backpack = &Backpack;
	Reward->Definitions = MoveTemp(Options.Definitions);
	Reward->Colors = MoveTemp(Options.Colors);
	Reward->Numbers = MoveTemp(Options.Numbers);
	return Reward;
}

bool UCastleItemGrantLibrary::TryCreateWildcardReward(FText DisplayName, const FCastleWildcardRoomSpec& Spec,
	UCastleBackpackComponent* Backpack, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	OutPendingReward = nullptr;
	if (!IsInGameThread() || !IsValid(Backpack) || DisplayName.IsEmpty())
	{
		return RejectGrant(Backpack, INVTEXT("创建万能卡奖励需要在游戏线程提供有效背包与奖励名称。"), OutFailureReason);
	}
	FCastleWildcardRoomOptions Options;
	if (!Spec.TryResolveOptions(DefinitionPool, Options, OutFailureReason))
	{
		return false;
	}
	OutPendingReward = CreateReward(MoveTemp(DisplayName), *Backpack, MoveTemp(Options));
	return true;
}

bool UCastleItemGrantLibrary::TryGrantRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FRandomStream& Random,
	UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	TArray<FCastleBuildingDescriptor> Cards;
	FRandomStream CandidateRandom = Random;
	if (!TryPrepareRoomGroup(Group, Backpack, DefinitionPool, CandidateRandom, Cards, OutPendingReward, OutFailureReason))
	{
		return false;
	}
	if (!OutPendingReward)
	{
		FText Reason;
		if (!Backpack->TryAddItemsAllowOverflow(Cards, Reason)) { return RejectGrant(Group, Reason, OutFailureReason); }
		UE_LOG(LogMoving_Castle, Display, TEXT("房间组 %s 已发放 %d 张确定卡牌。"), *Group->GetPathName(), Cards.Num());
	}
	Random = CandidateRandom;
	return true;
}

bool UCastleItemGrantLibrary::TryPrepareRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FRandomStream& Random,
	TArray<FCastleBuildingDescriptor>& OutCards, UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	OutCards.Reset();
	OutPendingReward = nullptr;
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || !IsValid(Group) || !IsValid(Backpack) || Group->DisplayName.IsEmpty()
		|| Group->GroupType > ECastleRoomGroupType::NPCSpeedRoom)
	{
		return RejectGrant(Group, INVTEXT("房间组需要有效模板、名称和目标背包，且只能在游戏线程发放。"), OutFailureReason);
	}
	const ECastleRoomGroupType Type = Group->GroupType;
	if (Group->bChooseInPicker && Type != ECastleRoomGroupType::HighNumberRoom && Type != ECastleRoomGroupType::NPCSpeedRoom)
	{
		return RejectGrant(Group, INVTEXT("弹窗发放开关只适用于大数字房间和加速房间模板。"), OutFailureReason);
	}

	FCastleWildcardRoomSpec Spec = Group->RoomSpec;
	FCastleWildcardRoomOptions Options;
	FText Reason;
	if (!PrepareGroupDefinitions(Type, Spec, DefinitionPool, Reason) || !PrepareGroupNumbers(Type, Spec, Reason)
		|| !Spec.TryResolveOptions(DefinitionPool, Options, Reason))
	{
		return RejectGrant(Group, Reason, OutFailureReason);
	}

	FRandomStream CandidateRandom = Random;
	if (Type == ECastleRoomGroupType::CustomColorNormalRoom)
	{
		// 颜色留给玩家，数字从配置允许的范围预选；单值表示资产明确锁定数字。
		Options.Numbers = {Options.Numbers[CandidateRandom.RandRange(0, Options.Numbers.Num() - 1)]};
	}
	const bool bRandomFunction = Type == ECastleRoomGroupType::RandomFunctionCustomRoom;
	const bool bPending = Type == ECastleRoomGroupType::CustomNormalRoom || Type == ECastleRoomGroupType::CustomColorNormalRoom
		|| bRandomFunction || Type == ECastleRoomGroupType::FullyCustomRoom || Group->bChooseInPicker;
	if (bPending)
	{
		UCastlePendingRoomReward* Reward = CreateReward(Group->DisplayName, *Backpack, MoveTemp(Options));
		if (bRandomFunction)
		{
			// 分配此奖励独有的随机流，真正抽定义由点击获取触发，避免其他奖励操作改变待领牌的结果。
			Reward->DefinitionDraw.Emplace(static_cast<int32>(CandidateRandom.GetUnsignedInt()));
		}
		Random = CandidateRandom;
		OutPendingReward = Reward;
		UE_LOG(LogMoving_Castle, Display, TEXT("房间组 %s 已生成待领取奖励。"), *Group->GetPathName());
		return true;
	}

	// 三连农田从合法起点抽一次，三张全部共用同色。显式数字子集也必须能提供完整的三连号。
	const bool bFarmTrio = Type == ECastleRoomGroupType::ThreeConsecutiveFarms;
	TArray<int32> FarmStarts;
	if (bFarmTrio)
	{
		for (const int32 Number : Options.Numbers)
		{
			if (Options.Numbers.Contains(Number + 1) && Options.Numbers.Contains(Number + 2))
			{
				FarmStarts.Add(Number);
			}
		}
		if (FarmStarts.IsEmpty())
		{
			return RejectGrant(Group, INVTEXT("农田组的数字配置必须包含至少一段连续三号。"), OutFailureReason);
		}
	}
	const int32 CardCount = bFarmTrio || Type == ECastleRoomGroupType::ThreeNumberFourArchers ? 3 : 1;
	const int32 FarmStart = bFarmTrio ? FarmStarts[CandidateRandom.RandRange(0, FarmStarts.Num() - 1)] : 0;
	const ECastleTileColor FarmColor = bFarmTrio
		? Options.Colors[CandidateRandom.RandRange(0, Options.Colors.Num() - 1)] : ECastleTileColor::Gray;
	TArray<FCastleBuildingDescriptor> Cards;
	for (int32 Index = 0; Index < CardCount; ++Index)
	{
		UCastleBuildingDefinition* Definition = Options.Definitions[CandidateRandom.RandRange(0, Options.Definitions.Num() - 1)];
		const ECastleTileColor Color = bFarmTrio ? FarmColor : Options.Colors[CandidateRandom.RandRange(0, Options.Colors.Num() - 1)];
		const int32 Number = bFarmTrio ? FarmStart + Index : Options.Numbers[CandidateRandom.RandRange(0, Options.Numbers.Num() - 1)];
		FCastleBuildingDescriptor Card;
		if (!Options.TryMakeDescriptor(Definition, Color, Number, Card, Reason))
		{
			return RejectGrant(Group, Reason, OutFailureReason);
		}
		Cards.Add(Card);
	}
	OutCards = MoveTemp(Cards);
	Random = CandidateRandom;
	return true;
}

bool UCastleItemGrantLibrary::TryGrantBadge(const UCastleBadgeDataAsset* Asset, UCastleBadgeSubsystem* Badges,
	bool& bOutNeedsReplacement, FText& OutFailureReason)
{
	bOutNeedsReplacement = false;
	if (!IsValid(Asset) || !IsValid(Badges))
	{
		return RejectGrant(Asset, INVTEXT("需要有效的徽章资产和 GameInstance 徽章子系统。"), OutFailureReason);
	}
	if (!Badges->TryAcquireBadge(Asset->Badge, bOutNeedsReplacement, OutFailureReason))
	{
		const FText Reason = OutFailureReason;
		return RejectGrant(Asset, Reason, OutFailureReason);
	}
	return true;
}
