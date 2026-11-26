#include "Items/CastleItemTypes.h"

#include "CastleBuildingDefinition.h"
#include "Moving_Castle.h"

#define LOCTEXT_NAMESPACE "CastleItemTypes"

TConstArrayView<ECastleBuildingType> CastleRoomCards::GetFunctionTypes()
{
	static constexpr ECastleBuildingType Types[] = {
		ECastleBuildingType::ArcherTower, ECastleBuildingType::CannonTower, ECastleBuildingType::Farm,
		ECastleBuildingType::ShieldTower, ECastleBuildingType::BeeHive, ECastleBuildingType::BalloonTower
	};
	return MakeArrayView(Types);
}

bool CastleRoomCards::FDefinitionPool::TryInitialize(
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> Definitions, FText& OutReason)
{
	for (UCastleBuildingDefinition* Definition : Definitions)
	{
		if (!IsValid(Definition))
		{
			OutReason = LOCTEXT("MissingDefinition", "建筑定义池含有空引用或已失效的建筑定义。");
			return false;
		}
		const ECastleBuildingType Type = Definition->BuildingType;
		if ((Type != ECastleBuildingType::NormalRoom && !GetFunctionTypes().Contains(Type)) || ByType.Contains(Type))
		{
			OutReason = FText::Format(LOCTEXT("AmbiguousDefinition", "建筑定义 {0} 的类别 {1} 非标准房间，或该类别存在多个定义。"),
				FText::FromString(Definition->GetPathName()), FText::AsNumber(static_cast<uint8>(Type)));
			return false;
		}
		if (!Definition->ValidateConfiguration(OutReason))
		{
			OutReason = FText::Format(LOCTEXT("InvalidDefinition", "建筑定义 {0} 校验失败：{1}"),
				FText::FromString(Definition->GetPathName()), OutReason);
			return false;
		}
		ByType.Add(Type, Definition);
	}
	return true;
}

UCastleBuildingDefinition* CastleRoomCards::FDefinitionPool::Find(const ECastleBuildingType Type) const
{
	return ByType.FindRef(Type);
}

bool CastleRoomCards::FDefinitionPool::TryCollect(const bool bIncludeNormalRoom,
	TArray<TObjectPtr<UCastleBuildingDefinition>>& OutDefinitions, FText& OutReason) const
{
	OutDefinitions.Reset();
	TArray<ECastleBuildingType, TInlineAllocator<7>> Types;
	if (bIncludeNormalRoom) { Types.Add(ECastleBuildingType::NormalRoom); }
	Types.Append(GetFunctionTypes());
	for (const ECastleBuildingType Type : Types)
	{
		UCastleBuildingDefinition* Definition = Find(Type);
		if (!Definition)
		{
			OutReason = FText::Format(LOCTEXT("IncompleteDrawPool", "抽取池缺少建筑类别 {0} 的定义。"),
				StaticEnum<ECastleBuildingType>()->GetDisplayNameTextByValue(static_cast<uint8>(Type)));
			return false;
		}
		OutDefinitions.Add(Definition);
	}
	return true;
}

bool CastleRoomCards::ValidateTrait(const FCastleBuildingDescriptor& Card, FText& OutReason)
{
	// 带特质的卡随回收保留原字段，因而所有存入入口及 Actor 初始化都使用相同的约束。
	switch (Card.RoomTrait)
	{
	case ECastleRoomTrait::None:
		if (Card.TraitRemainingRounds == 0) { return true; }
		break;
	case ECastleRoomTrait::Highway:
	case ECastleRoomTrait::SkyIslandPortal:
		if (Card.GetBuildingType() == ECastleBuildingType::NormalRoom && Card.Level == 1 && Card.TraitRemainingRounds == 0)
		{
			return true;
		}
		break;
	case ECastleRoomTrait::SelfOrbiting:
		if (Card.GetBuildingType() == ECastleBuildingType::CannonTower && Card.Level == 1 && Card.Number == 6
			&& Card.TraitRemainingRounds >= 1 && Card.TraitRemainingRounds <= 6)
		{
			return true;
		}
		break;
	default:
		break;
	}
	OutReason = FText::Format(LOCTEXT("InvalidRoomTrait", "建筑定义 {0} 的牌面不支持特质 {1} 或剩余轮数 {2}。"),
		FText::FromString(GetPathNameSafe(Card.Definition.Get())), FText::AsNumber(static_cast<uint8>(Card.RoomTrait)),
		FText::AsNumber(Card.TraitRemainingRounds));
	return false;
}

bool CastleRoomCards::HaveSameIdentity(const FCastleBuildingDescriptor& A, const FCastleBuildingDescriptor& B)
{
	return A.Definition == B.Definition && A.Color == B.Color && A.Number == B.Number && A.Level == B.Level
		&& A.RoomTrait == B.RoomTrait && A.TraitRemainingRounds == B.TraitRemainingRounds;
}

bool FCastleWildcardRoomSpec::TryResolveOptions(
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FCastleWildcardRoomOptions& OutOptions,
	FText& OutFailureReason) const
{
	OutOptions = {};
	OutFailureReason = FText::GetEmpty();
	auto Fail = [&OutFailureReason](const FText& Reason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡选项配置无效：%s"), *Reason.ToString());
		return false;
	};
	if (DefaultMinNumber < 1 || DefaultMaxNumber > 9 || DefaultMinNumber > DefaultMaxNumber)
	{
		return Fail(LOCTEXT("WildcardDefaultRange", "默认数字范围必须满足 1 ≤ 最小值 ≤ 最大值 ≤ 9。"));
	}
	if (DefinitionPool.IsEmpty())
	{
		return Fail(LOCTEXT("WildcardEmptyPool", "没有为万能卡传入建筑定义池。"));
	}

	FCastleWildcardRoomOptions Candidate;
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> Definitions = AllowedDefinitions.IsEmpty()
		? DefinitionPool : MakeArrayView(AllowedDefinitions);
	for (UCastleBuildingDefinition* Definition : Definitions)
	{
		if (!IsValid(Definition) || !DefinitionPool.Contains(Definition) || Candidate.Definitions.Contains(Definition))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidDefinition", "定义 {0} 无效、不在本次定义池中或重复配置。"),
				FText::FromString(GetPathNameSafe(Definition))));
		}
		FText Reason;
		if (!Definition->ValidateConfiguration(Reason))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidDefinitionConfig", "定义 {0} 校验失败：{1}"),
				FText::FromString(Definition->GetPathName()), Reason));
		}
		Candidate.Definitions.Add(Definition);
	}

	// 默认选项顺序来自共享玩法色集合，显式配置的顺序由房间组资产决定。
	const TConstArrayView<ECastleTileColor> LegalColors = CastleTileColors::GetPlayable();
	const TConstArrayView<ECastleTileColor> Colors = AllowedColors.IsEmpty() ? LegalColors : MakeArrayView(AllowedColors);
	for (const ECastleTileColor Color : Colors)
	{
		if (!LegalColors.Contains(Color) || Candidate.Colors.Contains(Color))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidColor", "色值 {0} 非正式颜色或重复配置。"),
				FText::AsNumber(static_cast<int32>(Color))));
		}
		Candidate.Colors.Add(Color);
	}

	if (AllowedNumbers.IsEmpty())
	{
		for (int32 Number = DefaultMinNumber; Number <= DefaultMaxNumber; ++Number)
		{
			Candidate.Numbers.Add(Number);
		}
	}
	else
	{
		for (const int32 Number : AllowedNumbers)
		{
			if (Number < 1 || Number > 9 || Candidate.Numbers.Contains(Number))
			{
				return Fail(FText::Format(LOCTEXT("WildcardInvalidNumber", "数字 {0} 超出 1 至 9 的范围或重复配置。"),
					FText::AsNumber(Number)));
			}
			Candidate.Numbers.Add(Number);
		}
	}

	OutOptions = MoveTemp(Candidate);
	return true;
}

bool FCastleWildcardRoomOptions::TryMakeDescriptor(UCastleBuildingDefinition* Definition, const ECastleTileColor Color,
	const int32 Number, FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason) const
{
	OutDescriptor = {};
	if (!IsValid(Definition) || !Definitions.Contains(Definition) || !Colors.Contains(Color) || !Numbers.Contains(Number))
	{
		OutFailureReason = LOCTEXT("WildcardInvalidSelection", "请先选定本张万能卡允许的建筑、颜色和数字。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("万能卡确认失败：定义=%s，色值=%d，数字=%d。%s"),
			*GetPathNameSafe(Definition), static_cast<int32>(Color), Number, *OutFailureReason.ToString());
		return false;
	}
	if (!Definition->CreateDescriptor(1, Color, Number, OutDescriptor, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡无法生成建筑定义 %s 的一级描述符：%s"),
			*Definition->GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
