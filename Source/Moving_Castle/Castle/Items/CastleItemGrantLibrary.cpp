#include "Items/CastleItemGrantLibrary.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
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

	bool TryFindMostPlacedFunctions(const ACastleBoard* Board, const CastleRoomCards::FDefinitionPool& Pool,
		TArray<TObjectPtr<UCastleBuildingDefinition>>& OutDefinitions, FText& OutReason)
	{
		if (!IsValid(Board))
		{
			OutReason = INVTEXT("按场上数量选择功能塔需要有效棋盘。");
			return false;
		}
		// GetPlacedBuildings 已按 Actor 去重。多格合成塔与单格塔都只占一个计数。
		const TConstArrayView<ECastleBuildingType> Types = CastleRoomCards::GetFunctionTypes();
		TMap<ECastleBuildingType, int32> Counts;
		int32 Most = 0;
		for (const ACastleBuildingBase* Building : Board->GetPlacedBuildings())
		{
			if (!IsValid(Building))
			{
				OutReason = FText::Format(INVTEXT("棋盘 {0} 有无效的在场建筑 {1}。"),
					FText::FromString(Board->GetPathName()), FText::FromString(GetPathNameSafe(Building)));
				return false;
			}
			const ECastleBuildingType Type = Building->GetBuildingType();
			if (Types.Contains(Type)) { Most = FMath::Max(Most, ++Counts.FindOrAdd(Type)); }
			else if (Type != ECastleBuildingType::NormalRoom)
			{
				OutReason = FText::Format(INVTEXT("在场建筑 {0} 的类型无效。"), FText::FromString(Building->GetPathName()));
				return false;
			}
		}
		if (Most == 0)
		{
			OutReason = INVTEXT("场上没有任何功能塔，无法执行功能塔买断。");
			return false;
		}
		// 平局集合按固定类别顺序构建，Actor 或 TMap 的遍历顺序不进入随机语义。
		for (const ECastleBuildingType Type : Types)
		{
			if (Counts.FindRef(Type) != Most) { continue; }
			UCastleBuildingDefinition* Definition = Pool.Find(Type);
			if (!Definition)
			{
				OutReason = FText::Format(INVTEXT("买断规则缺少最多的功能塔类别 {0} 对应的定义。"),
					StaticEnum<ECastleBuildingType>()->GetDisplayNameTextByValue(static_cast<uint8>(Type)));
				return false;
			}
			OutDefinitions.Add(Definition);
		}
		return true;
	}

	bool TryResolveRuleBuildings(const FCastleRoomCardRule& Rule, const CastleRoomCards::FDefinitionPool& Pool,
		const FCastleBuildingDescriptor* First, const ACastleBoard* Board,
		TArray<TObjectPtr<UCastleBuildingDefinition>>& OutDefinitions, FText& OutReason)
	{
		switch (Rule.BuildingSource)
		{
		case ECastleRoomRuleBuilding::Fixed:
			if (UCastleBuildingDefinition* Definition = Pool.Find(Rule.FixedBuildingType))
			{
				OutDefinitions.Add(Definition);
				return true;
			}
			OutReason = FText::Format(INVTEXT("定义池缺少指定建筑类别 {0}。"),
				StaticEnum<ECastleBuildingType>()->GetDisplayNameTextByValue(static_cast<uint8>(Rule.FixedBuildingType)));
			return false;
		case ECastleRoomRuleBuilding::RandomStandard:
			return Pool.TryCollect(true, OutDefinitions, OutReason);
		case ECastleRoomRuleBuilding::RandomFunction:
		case ECastleRoomRuleBuilding::EachFunction:
			return Pool.TryCollect(false, OutDefinitions, OutReason);
		case ECastleRoomRuleBuilding::MainBarFirst:
			if (First) { OutDefinitions.Add(First->Definition); return true; }
			OutReason = INVTEXT("发牌规则引用主栏首卡定义，但主建筑栏为空。");
			return false;
		case ECastleRoomRuleBuilding::MostPlacedFunction:
			return TryFindMostPlacedFunctions(Board, Pool, OutDefinitions, OutReason);
		default:
			OutReason = INVTEXT("没有配置有效的建筑来源。");
			return false;
		}
	}

	bool TryResolveRuleNumbers(const FCastleRoomCardRule& Rule, const int32 Count,
		const FCastleBuildingDescriptor* First, const FCastleBuildingDescriptor* Previous,
		TArray<int32, TInlineAllocator<9>>& OutNumbers, FText& OutReason)
	{
		switch (Rule.NumberSource)
		{
		case ECastleRoomRuleNumber::Fixed:
			OutNumbers.Add(Rule.FixedNumber);
			break;
		case ECastleRoomRuleNumber::RandomChoice:
		case ECastleRoomRuleNumber::Sequence:
			if (Rule.NumberSource == ECastleRoomRuleNumber::Sequence
				&& (Rule.NumberChoices.Num() != Count || Rule.bShareNumberDraw))
			{
				OutReason = INVTEXT("顺序发牌的数字数目必须等于规则张数，且不能共享数字抽样。");
				return false;
			}
			for (const int32 Number : Rule.NumberChoices)
			{
				if (Rule.NumberSource == ECastleRoomRuleNumber::RandomChoice && OutNumbers.Contains(Number))
				{
					OutReason = INVTEXT("随机数字集合不能重复配置相同数字。");
					return false;
				}
				OutNumbers.Add(Number);
			}
			break;
		case ECastleRoomRuleNumber::MainBarFirst:
			if (First) { OutNumbers.Add(First->Number); }
			break;
		case ECastleRoomRuleNumber::PreviousCard:
			if (Previous) { OutNumbers.Add(Previous->Number); }
			break;
		default:
			break;
		}
		if (OutNumbers.IsEmpty() || OutNumbers.ContainsByPredicate([](int32 Number) { return Number < 1 || Number > 9; }))
		{
			OutReason = INVTEXT("没有可用的数字来源；数字必须在 1 至 9，且引用的卡牌必须存在。");
			return false;
		}
		return true;
	}

	bool TryAppendRule(const FCastleRoomCardRule& Rule, const CastleRoomCards::FDefinitionPool& Pool,
		const FCastleBuildingDescriptor* First, const ACastleBoard* Board, FRandomStream& Random,
		TArray<FCastleBuildingDescriptor>& Cards, FText& OutReason)
	{
		const int32 Count = Rule.bCountFromMainBarFirstNumber && First ? First->Number : Rule.Count;
		if (Count < 1 || (Rule.bCountFromMainBarFirstNumber && !First))
		{
			OutReason = INVTEXT("发牌张数必须为正数；按主栏首卡数字发牌时需要有效首卡。");
			return false;
		}
		const FCastleBuildingDescriptor* Previous = Cards.IsEmpty() ? nullptr : &Cards.Last();
		TArray<TObjectPtr<UCastleBuildingDefinition>> Definitions;
		TArray<int32, TInlineAllocator<9>> Numbers;
		if (!TryResolveRuleBuildings(Rule, Pool, First, Board, Definitions, OutReason)
			|| !TryResolveRuleNumbers(Rule, Count, First, Previous, Numbers, OutReason)) { return false; }

		const bool bEachFunction = Rule.BuildingSource == ECastleRoomRuleBuilding::EachFunction;
		const bool bShareBuilding = Rule.bShareBuildingDraw || Rule.BuildingSource == ECastleRoomRuleBuilding::MostPlacedFunction;
		const int32 BuildingCount = bEachFunction ? Definitions.Num() : 1;
		if (static_cast<int64>(Count) * BuildingCount > MAX_int32 - Cards.Num())
		{
			OutReason = INVTEXT("房间组发牌总数超出数组容量。");
			return false;
		}
		const TConstArrayView<ECastleTileColor> Playable = CastleTileColors::GetPlayable();
		TArray<ECastleTileColor, TInlineAllocator<4>> Colors;
		const bool bDifferent = Rule.ColorSource == ECastleRoomRuleColor::DifferentFromPreviousCard;
		switch (Rule.ColorSource)
		{
		case ECastleRoomRuleColor::Fixed:
			if (Playable.Contains(Rule.FixedColor)) { Colors.Add(Rule.FixedColor); }
			break;
		case ECastleRoomRuleColor::RandomPlayable:
			Colors.Append(Playable);
			break;
		case ECastleRoomRuleColor::MainBarFirst:
			if (First) { Colors.Add(First->Color); }
			break;
		case ECastleRoomRuleColor::PreviousCard:
			if (Previous) { Colors.Add(Previous->Color); }
			break;
		case ECastleRoomRuleColor::DifferentFromPreviousCard:
			if (Previous && (!Rule.bShareColorDraw || static_cast<int64>(Count) * BuildingCount == 1))
			{
				Colors.Append(Playable);
			}
			break;
		default:
			break;
		}
		if (Colors.IsEmpty())
		{
			OutReason = INVTEXT("没有可用的颜色来源；请校验玩法色、引用的卡牌及异色共享配置。");
			return false;
		}
		for (UCastleBuildingDefinition* Definition : Definitions)
		{
			// 校验所有可能抽到的建筑与数字。配置错误不得因刚好抽到其他选项而被掩盖。
			FCastleBuildingDescriptor Candidate;
			Candidate.Definition = Definition;
			Candidate.RoomTrait = Rule.RoomTrait;
			Candidate.TraitRemainingRounds = Rule.TraitRemainingRounds;
			for (const int32 Number : Numbers)
			{
				Candidate.Number = Number;
				if (!CastleRoomCards::ValidateTrait(Candidate, OutReason)) { return false; }
			}
		}

		const int32 RuleStart = Cards.Num();
		for (int32 BuildingIndex = 0; BuildingIndex < BuildingCount; ++BuildingIndex)
		{
			for (int32 CardIndex = 0; CardIndex < Count; ++CardIndex)
			{
				// 各维度独立共享首张牌。指定或唯一候选不消费随机流。
				const bool bHaveShared = Cards.Num() > RuleStart;
				UCastleBuildingDefinition* Definition = bEachFunction ? Definitions[BuildingIndex].Get()
					: (bHaveShared && bShareBuilding ? Cards[RuleStart].Definition.Get()
						: Definitions[Definitions.Num() == 1 ? 0 : Random.RandRange(0, Definitions.Num() - 1)].Get());
				if (bDifferent)
				{
					Colors.Reset();
					for (const ECastleTileColor Color : Playable)
					{
						if (Color != Cards.Last().Color) { Colors.Add(Color); }
					}
				}
				const ECastleTileColor Color = bHaveShared && Rule.bShareColorDraw ? Cards[RuleStart].Color
					: Colors[Colors.Num() == 1 ? 0 : Random.RandRange(0, Colors.Num() - 1)];
				const int32 Number = Rule.NumberSource == ECastleRoomRuleNumber::Sequence ? Numbers[CardIndex]
					: (bHaveShared && Rule.bShareNumberDraw ? Cards[RuleStart].Number
						: Numbers[Numbers.Num() == 1 ? 0 : Random.RandRange(0, Numbers.Num() - 1)]);
				FCastleBuildingDescriptor Card;
				if (!Definition->CreateDescriptor(1, Color, Number, Card, OutReason)) { return false; }
				Card.RoomTrait = Rule.RoomTrait;
				Card.TraitRemainingRounds = Rule.TraitRemainingRounds;
				Cards.Add(MoveTemp(Card));
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
	const ACastleBoard* Board, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	FRandomStream& Random, UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	// 发放事务独占一个候选随机流，背包或待领取奖励未提交时保留调用方的原种子。
	OutPendingReward = nullptr;
	FRandomStream CandidateRandom = Random;
	FCastlePreparedRoomGroup Prepared;
	if (!TryPrepareRoomGroup(Group, Backpack, Board, DefinitionPool, CandidateRandom, Prepared, OutFailureReason))
	{
		return false;
	}
	if (!Prepared.PendingReward && !TryCommitPreparedRoomGroup(Prepared, Backpack, OutFailureReason))
	{
		return false;
	}
	OutPendingReward = Prepared.PendingReward;
	Random = CandidateRandom;
	return true;
}

bool UCastleItemGrantLibrary::TryPrepareRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
	const ACastleBoard* Board, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	FRandomStream& Random, FCastlePreparedRoomGroup& OutPrepared, FText& OutFailureReason)
{
	OutPrepared = {};
	if (!IsInGameThread() || !IsValid(Group) || !IsValid(Backpack))
	{
		return RejectGrant(Group, INVTEXT("准备房间组需要游戏线程、有效资产和目标背包。"), OutFailureReason);
	}
	if (!Group->ValidateGrantConfiguration(OutFailureReason)) { return false; }
	const TArray<FCastleBuildingDescriptor>& CurrentMainBar = Backpack->Items;
	if (Backpack->Capacity < 1 || CurrentMainBar.Num() > Backpack->Capacity)
	{
		return RejectGrant(Group, INVTEXT("目标背包的主建筑栏容量无效。"), OutFailureReason);
	}
	const FCastleBuildingDescriptor* First = CurrentMainBar.IsEmpty() ? nullptr : &CurrentMainBar[0];
	const ECastleRoomGroupMainBarAction Action = Group->MainBarAction;
	const bool bConsumeFirst = Action == ECastleRoomGroupMainBarAction::ConsumeFirst;
	if (!First && (bConsumeFirst || Action == ECastleRoomGroupMainBarAction::RecolorAllRandom))
	{
		return RejectGrant(Group, INVTEXT("主建筑栏为空，无法消耗首卡或执行整栏颜色转变。"), OutFailureReason);
	}
	CastleRoomCards::FDefinitionPool Pool;
	if (!Pool.TryInitialize(DefinitionPool, OutFailureReason)) { return RejectGrant(Group, OutFailureReason, OutFailureReason); }

	FRandomStream CandidateRandom = Random;
	FCastlePreparedRoomGroup Candidate;
	Candidate.Group = Group;
	Candidate.TargetBackpack = Backpack;
	if (Group->bChooseInPicker)
	{
		TArray<TObjectPtr<UCastleBuildingDefinition>> PickerDefinitions;
		// 空选项由规格明确表示全池。随机功能房先限定六类，实际抽取推迟到第一次打开弹窗。
		TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> PickerPool = DefinitionPool;
		if (Group->bLockRandomFunctionOnFirstOpen || Group->RoomSpec.AllowedDefinitions.IsEmpty())
		{
			if (!Pool.TryCollect(!Group->bLockRandomFunctionOnFirstOpen, PickerDefinitions, OutFailureReason))
			{
				return RejectGrant(Group, OutFailureReason, OutFailureReason);
			}
			PickerPool = PickerDefinitions;
		}
		FCastleWildcardRoomOptions Options;
		if (!Group->RoomSpec.TryResolveOptions(PickerPool, Options, OutFailureReason))
		{
			return RejectGrant(Group, OutFailureReason, OutFailureReason);
		}
		Candidate.PendingReward = CreateReward(Group->DisplayName, *Backpack, MoveTemp(Options));
		if (Group->bLockRandomFunctionOnFirstOpen)
		{
			Candidate.PendingReward->DefinitionDraw.Emplace(static_cast<int32>(CandidateRandom.GetUnsignedInt()));
		}
	}
	else
	{
		if (First && !Backpack->ValidateDescriptorForStorage(*First, OutFailureReason))
		{
			return RejectGrant(Group, OutFailureReason, OutFailureReason);
		}
		const TArray<FCastleRoomCardRule>& Rules = !First && !Group->EmptyMainBarCardRules.IsEmpty()
			? Group->EmptyMainBarCardRules : Group->CardRules;
		for (int32 Index = 0; Index < Rules.Num(); ++Index)
		{
			if (!TryAppendRule(Rules[Index], Pool, First, Board, CandidateRandom, Candidate.Cards, OutFailureReason))
			{
				const FText Reason = FText::Format(INVTEXT("第 {0} 条发牌规则失败：{1}"), FText::AsNumber(Index + 1), OutFailureReason);
				return RejectGrant(Group, Reason, OutFailureReason);
			}
		}
		if (Action != ECastleRoomGroupMainBarAction::None)
		{
			// 主栏始终从原样快照变换。刚生成的牌和临时栏牌不会参与本次染色。
			FCastlePreparedMainBarChange& Change = Candidate.MainBarChange.Emplace();
			Change.Before = CurrentMainBar;
			Change.After = CurrentMainBar;
			if (bConsumeFirst) { Change.After.RemoveAt(0); }
			else if (Action == ECastleRoomGroupMainBarAction::RecolorMatching)
			{
				for (FCastleBuildingDescriptor& Card : Change.After)
				{
					if (Card.Color == Group->RecolorFrom) { Card.Color = Group->RecolorTo; }
				}
			}
			else
			{
				const TConstArrayView<ECastleTileColor> Colors = CastleTileColors::GetPlayable();
				const ECastleTileColor Color = Colors[CandidateRandom.RandRange(0, Colors.Num() - 1)];
				for (FCastleBuildingDescriptor& Card : Change.After) { Card.Color = Color; }
			}
		}
		const TArray<FCastleBuildingDescriptor>* After = Candidate.MainBarChange ? &Candidate.MainBarChange->After : nullptr;
		if (!Backpack->CanApplyItemBatch(Candidate.Cards, After, bConsumeFirst, OutFailureReason))
		{
			return RejectGrant(Group, OutFailureReason, OutFailureReason);
		}
	}
	OutPrepared = MoveTemp(Candidate);
	Random = CandidateRandom;
	return true;
}

bool UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(const FCastlePreparedRoomGroup& Prepared,
	UCastleBackpackComponent* Backpack, FText& OutFailureReason)
{
	if (!IsInGameThread() || !IsValid(Backpack) || Prepared.TargetBackpack.Get() != Backpack || !IsValid(Prepared.Group)
		|| Prepared.PendingReward || Prepared.bCommitted)
	{
		return RejectGrant(Prepared.Group, INVTEXT("需要尚未提交的直接发牌事务及其原目标背包。"), OutFailureReason);
	}
	const TArray<FCastleBuildingDescriptor>* After = nullptr;
	bool bRefill = false;
	if (Prepared.MainBarChange.IsSet())
	{
		const FCastlePreparedMainBarChange& Change = Prepared.MainBarChange.GetValue();
		const auto& Current = Backpack->Items;
		bool bSame = Current.Num() == Change.Before.Num();
		for (int32 Index = 0; bSame && Index < Current.Num(); ++Index)
		{
			bSame = CastleRoomCards::HaveSameIdentity(Current[Index], Change.Before[Index]);
		}
		if (!bSame)
		{
			return RejectGrant(Prepared.Group, INVTEXT("主建筑栏已变化，必须重新准备本次房间组。"), OutFailureReason);
		}
		After = &Change.After;
		bRefill = Change.After.Num() < Change.Before.Num();
	}
	if (!Backpack->CanApplyItemBatch(Prepared.Cards, After, bRefill, OutFailureReason))
	{
		return RejectGrant(Prepared.Group, OutFailureReason, OutFailureReason);
	}
	Backpack->ApplyValidatedItemBatch(Prepared.Cards, After, bRefill);
	Prepared.bCommitted = true;
	UCastleAudioSubsystem::PlaySFX(Backpack->GetWorld(), ECastleSFXEvent::Reward);
	UE_LOG(LogMoving_Castle, Display, TEXT("已发放房间组 %s（%s）：新增 %d 张建筑卡。"),
		*Prepared.Group->DisplayName.ToString(), *Prepared.Group->GetPathName(), Prepared.Cards.Num());
	Backpack->OnBackpackChanged.Broadcast();
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
	// 调试命令和空岛共用资产奖励契约，界面收到库存通知时即可读取图标。
	if (!Asset->ValidateReward(OutFailureReason)
		|| !Badges->TryAcquireBadge(Asset->Badge, bOutNeedsReplacement, OutFailureReason))
	{
		const FText Reason = OutFailureReason;
		return RejectGrant(Asset, Reason, OutFailureReason);
	}
	return true;
}
